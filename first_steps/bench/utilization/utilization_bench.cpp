#include "page_reuse_resource.hpp"
#include "onnxruntime_cxx_api.h"
#include "onnxruntime_session_options_config_keys.h"
#include "../../tools/adversarial/adversarial_heap.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <sys/resource.h>
#include <sched.h>
#include <malloc.h>
#include <atomic>
#include <thread>

using namespace utilization;
constexpr size_t input_count = 3 * 640 * 640;
constexpr int input_variants = 8;

struct Snapshot {
    size_t rss = 0, pss = 0, private_bytes = 0, hwm = 0;
};
Snapshot snapshot() {
    Snapshot s;
    std::ifstream file("/proc/self/smaps_rollup");
    std::string line;
    while (std::getline(file, line)) {
        size_t kb;
        if (sscanf(line.c_str(), "Rss: %zu kB", &kb) == 1) s.rss = kb * 1024;
        if (sscanf(line.c_str(), "Pss: %zu kB", &kb) == 1) s.pss = kb * 1024;
        if (sscanf(line.c_str(), "Private_Clean: %zu kB", &kb) == 1 ||
            sscanf(line.c_str(), "Private_Dirty: %zu kB", &kb) == 1) s.private_bytes += kb * 1024;
    }
    std::ifstream status("/proc/self/status");
    while (std::getline(status, line)) {
        size_t kb;
        if (sscanf(line.c_str(), "VmHWM: %zu kB", &kb) == 1) s.hwm = kb * 1024;
    }
    if (!s.rss || !s.hwm) throw std::runtime_error("cannot read process residency");
    return s;
}
void print_snapshot(const Snapshot& s) {
    std::cout << "{\"rss_bytes\":" << s.rss << ",\"private_bytes\":" << s.private_bytes
              << ",\"pss_bytes\":" << s.pss << ",\"hwm_bytes\":" << s.hwm << "}";
}
void print_pages(const PageAccounting& s) {
    std::cout << "{\"reserved_bytes\":" << s.reserved << ",\"resident_bytes\":" << s.resident
              << ",\"live_requested_bytes\":" << s.live_requested << ",\"live_rounded_bytes\":" << s.live_rounded
              << ",\"resident_payload_bytes\":" << s.resident_payload << ",\"resident_body_bytes\":" << s.resident_body
              << ",\"resident_head_bytes\":" << s.resident_head << ",\"resident_tail_bytes\":" << s.resident_tail
              << ",\"resident_sub_bytes\":" << s.resident_sub << ",\"resident_waste_bytes\":" << s.waste()
              << ",\"payload_utilization\":" << (s.resident ? double(s.resident_payload) / s.resident : 0) << "}";
}

struct Request { size_t size; };
class ObservedAllocator : public OrtAllocator {
    OrtMemoryInfo* info_ = nullptr;
    std::map<void*, Request> live_;
    std::exception_ptr failure_;
public:
    std::unique_ptr<PageReuseResource> pool;
    size_t requested = 0, peak_requested = 0, calls = 0, peak_count = 0;
    std::string mode;
    explicit ObservedAllocator(std::string name) : mode(std::move(name)) {
        version = ORT_API_VERSION;
        Alloc = allocate_callback; Reserve = allocate_callback; Free = free_callback; Info = info_callback;
        Ort::ThrowOnError(Ort::GetApi().CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &info_));
        if (mode.rfind("pool_", 0) == 0)
            pool = std::make_unique<PageReuseResource>(mode == "pool_eager" || mode == "pool_page", mode == "pool_page" ? 4096 : 64);

    }
    ~ObservedAllocator() { if (info_) Ort::GetApi().ReleaseMemoryInfo(info_); }
    void check() { if (failure_) std::rethrow_exception(failure_); }
    void boundary() { if (pool && (mode == "pool_boundary" || mode == "pool_page")) pool->purge(); }
    void after_load() {
        if (pool && mode != "pool_retain") pool->purge();
    }
    static void* ORT_API_CALL allocate_callback(OrtAllocator* a, size_t n) {
        auto& self = *static_cast<ObservedAllocator*>(a);
        try {
            void* p = nullptr;
            if (self.pool) p = self.pool->allocate(n);
            else if (posix_memalign(&p, 64, std::max<size_t>(n, 1)) != 0) throw std::bad_alloc();
            if (!p) throw std::bad_alloc();
            if (!self.live_.emplace(p, Request{n}).second) throw std::runtime_error("allocator reused outstanding pointer");
            self.requested += n;
            self.peak_requested = std::max(self.peak_requested, self.requested);
            self.peak_count = std::max(self.peak_count, self.live_.size());
            ++self.calls;
            return p;
        } catch (...) { self.failure_ = std::current_exception(); return nullptr; }
    }
    static void ORT_API_CALL free_callback(OrtAllocator* a, void* p) {
        if (!p) return;
        auto& self = *static_cast<ObservedAllocator*>(a);
        try {
            auto it = self.live_.find(p);
            if (it == self.live_.end()) throw std::runtime_error("unknown ORT free");
            self.requested -= it->second.size;
            self.live_.erase(it);
            if (self.pool) self.pool->deallocate(p);
            else free(p);
        } catch (...) { self.failure_ = std::current_exception(); }
    }
    static const OrtMemoryInfo* ORT_API_CALL info_callback(const OrtAllocator* a) {
        return static_cast<const ObservedAllocator*>(a)->info_;
    }
};

class Engine {
public:
    std::unique_ptr<ObservedAllocator> allocator;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> output_strings;
    std::vector<const char*> output_names;
    std::string input_name;
    std::vector<float> input;
    Ort::Value tensor{nullptr};
    std::string mode;
    Engine(Ort::Env& env, const std::string& model, std::string name, bool pattern)
        : input(input_count), mode(std::move(name)) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (!pattern) options.DisableMemPattern();
        bool registered = false;
        Ort::MemoryInfo arena_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        if (mode != "bfc") {
            allocator = std::make_unique<ObservedAllocator>(mode);
            options.DisableCpuMemArena();
            options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
            Ort::ThrowOnError(Ort::GetApi().RegisterAllocator(env, allocator.get()));
            registered = true;
        }
        const OrtMemoryInfo* registered_info = allocator ? allocator->Info(allocator.get()) : static_cast<const OrtMemoryInfo*>(arena_info);
        try { session = std::make_unique<Ort::Session>(env, model.c_str(), options); }
        catch (...) {
            if (registered) {
                auto status = Ort::GetApi().UnregisterAllocator(env, registered_info);
                if (status) Ort::GetApi().ReleaseStatus(status);
            }
            throw;
        }
        // ORT owns CreateAndRegisterAllocator's arena; its environment must retain
        // ownership until the session is destroyed. Custom allocators are owned here.
        if (registered && allocator) Ort::ThrowOnError(Ort::GetApi().UnregisterAllocator(env, registered_info));
        if (allocator) { allocator->check(); allocator->after_load(); }
        Ort::AllocatorWithDefaultOptions names;
        input_name = session->GetInputNameAllocated(0, names).get();
        for (size_t i = 0; i < session->GetOutputCount(); ++i) output_strings.emplace_back(session->GetOutputNameAllocated(i, names).get());
        for (const auto& s : output_strings) output_names.push_back(s.c_str());
        std::vector<int64_t> shape{1, 3, 640, 640};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
    }
    std::vector<Ort::Value> run(int seed) {
        for (size_t i = 0; i < input.size(); ++i) input[i] = float((i * (2 * seed + 1) + seed * 37) % 256) / 255.0f;
        const char* name = input_name.c_str();
        auto outputs = session->Run(Ort::RunOptions{nullptr}, &name, &tensor, 1, output_names.data(), output_names.size());
        if (allocator) allocator->check();
        return outputs;
    }
    void boundary() { if (allocator) { allocator->check(); allocator->boundary(); } }
};

using Reference = std::vector<std::vector<std::vector<float>>>;
Reference read_reference(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    Reference result(input_variants);
    for (auto& variant : result) {
        uint64_t outputs = 0; file.read(reinterpret_cast<char*>(&outputs), sizeof(outputs));
        if (!file || outputs == 0 || outputs > 16) throw std::runtime_error("invalid reference output count");
        variant.resize(outputs);
        for (auto& tensor : variant) {
            uint64_t count = 0; file.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!file || count == 0 || count > 100000000) throw std::runtime_error("invalid reference tensor length");
            tensor.resize(count); file.read(reinterpret_cast<char*>(tensor.data()), count * sizeof(float));
            if (!file) throw std::runtime_error("truncated reference");
        }
    }
    return result;
}
void verify(const std::vector<Ort::Value>& output, const std::vector<std::vector<float>>& reference, double& max_error) {
    if (output.size() != reference.size()) throw std::runtime_error("output count mismatch");
    for (size_t t = 0; t < output.size(); ++t) {
        auto info = output[t].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetElementCount() != reference[t].size())
            throw std::runtime_error("output type/length mismatch");
        const float* values = output[t].GetTensorData<float>();
        for (size_t i = 0; i < reference[t].size(); ++i) {
            double expected = reference[t][i], observed = values[i];
            double error = std::abs(observed - expected);
            if (!std::isfinite(observed) || !std::isfinite(expected) || error > 1e-5 + 1e-4 * std::abs(expected))
                throw std::runtime_error("full output comparison failed at tensor " + std::to_string(t) + " element " + std::to_string(i));
            max_error = std::max(max_error, error);
        }
    }
}

int run_cameras(Ort::Env& env, const std::string& model, const std::string& mode,
                const std::string& reference_path, int cameras, int frames,
                int warmup, bool pattern, bool trim, int adv, double occupancy) {
    if (cameras < 2 || cameras > 8 || mode == "reference")
        throw std::runtime_error("unsupported camera configuration");
    auto reference = read_reference(reference_path);
    std::vector<std::unique_ptr<Engine>> engines;
    for (int i = 0; i < cameras; ++i) engines.push_back(std::make_unique<Engine>(env, model, mode, pattern));
    if (trim) malloc_trim(0);
    std::atomic<int> ready{0}, finished{0};
    std::atomic<bool> start{false}, release{false}, failed{false};
    std::vector<std::string> errors(cameras);
    std::vector<double> max_error(cameras);
    std::vector<PageAccounting> pages(cameras);
    std::vector<std::thread> workers;
    for (int c = 0; c < cameras; ++c) workers.emplace_back([&, c] {
        bool signaled = false;
        try {
            cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(c, &mask);
            if (sched_setaffinity(0, sizeof(mask), &mask)) throw std::runtime_error("camera affinity failed");
            auto& e = *engines[c];
            for (int i = 0; i < warmup; ++i) {
                { int seed = (i + c) % input_variants; auto outputs = e.run(seed); verify(outputs, reference[seed], max_error[c]); }
                e.boundary(); if (trim) malloc_trim(0);
            }
            signaled = true; ++ready;
            while (!start.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::vector<Ort::Value> held; int held_seed = 0;
            for (int i = 0; i < frames; ++i) {
                if (i % 11 == 5) held.clear();
                { int seed = (i + c) % input_variants; auto outputs = e.run(seed);
                  verify(outputs, reference[seed], max_error[c]);
                  if (!held.empty()) verify(held, reference[held_seed], max_error[c]);
                  if (i % 11 == 0) { held = std::move(outputs); held_seed = seed; } }
                e.boundary(); if (trim) malloc_trim(0);
            }
            if (!held.empty()) verify(held, reference[held_seed], max_error[c]);
            held.clear(); e.boundary(); if (trim) malloc_trim(0);
        } catch (const std::exception& e) { errors[c] = e.what(); failed.store(true); }
        if (!signaled) ++ready;
        ++finished;
        // Sessions, worker thread caches, weights, and arenas remain alive until
        // the coordinator has sampled the idle footprint.
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (ready.load() < cameras) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Snapshot warm = snapshot();
    { std::ofstream reset("/proc/self/clear_refs"); reset << "5\n"; }
    auto begun = std::chrono::steady_clock::now(); start.store(true);
    while (finished.load() < cameras) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Snapshot idle = snapshot();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
    if (!failed.load()) {
        for (int c = 0; c < cameras; ++c)
            if (engines[c]->allocator && engines[c]->allocator->pool) pages[c] = engines[c]->allocator->pool->sample();
    }
    release.store(true);
    for (auto& worker : workers) worker.join();
    if (failed.load()) {
        std::string message;
        for (const auto& error : errors) if (!error.empty()) message += error + "; ";
        throw std::runtime_error(message);
    }
    std::cout << "{\"mode\":\"" << mode << "\",\"cameras\":" << cameras << ",\"frames_per_camera\":" << frames
              << ",\"warmup\":" << warmup << ",\"adv\":" << adv << ",\"occupancy\":" << occupancy
              << ",\"memory_pattern\":" << (pattern ? "true" : "false") << ",\"glibc_trim\":" << (trim ? "true" : "false")
              << ",\"outputs_verified\":true,\"retained_outputs_verified\":true,\"max_abs_error\":" << *std::max_element(max_error.begin(), max_error.end())
              << ",\"startup_peak_rss_bytes\":" << warm.hwm << ",\"active_peak_rss_bytes\":" << idle.hwm
              << ",\"fps_diagnostic\":" << cameras * frames / seconds << ",\"live_session_idle\":";
    print_snapshot(idle);
    std::cout << ",\"camera_pages_idle\":[";
    for (int c = 0; c < cameras; ++c) { if (c) std::cout << ','; print_pages(pages[c]); }
    std::cout << "]}" << std::endl;
    return 0;
}

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; i += 2) { if (i + 1 >= argc) throw std::runtime_error("arguments require values"); args[argv[i]] = argv[i+1]; }
        auto arg = [&](std::string key, std::string fallback) { return args.count(key) ? args.at(key) : fallback; };
        std::string mode = arg("--mode", "pool_boundary");
        std::set<std::string> modes{"reference", "bfc", "direct", "pool_retain", "pool_boundary", "pool_initial_trim", "pool_eager", "pool_page"};
        if (!modes.count(mode)) throw std::runtime_error("unknown allocator mode");
        int frames = std::stoi(arg("--frames", "100")), warmup = std::stoi(arg("--warmup", "20"));
        int adv = std::stoi(arg("--adv", "0"));
        if (frames < 1 || warmup < 2 || adv < 0) throw std::runtime_error("invalid frame/warmup/adv count");
        double occupancy = std::stod(arg("--occupancy", adv == 1 ? "0.33" : adv == 10 ? "0.8" : "0.66"));
        if (occupancy < 0 || occupancy > 1) throw std::runtime_error("invalid occupancy");
        cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(std::stoi(arg("--core", "2")), &mask);
        if (sched_setaffinity(0, sizeof(mask), &mask)) throw std::runtime_error("affinity failed");
        // Adaptation: explicit supplied/profiled peak and existing size histogram.
        adversarial::AdversarialPreconditioner::precondition(adv, occupancy, std::stoul(arg("--n-peak", "20000")));
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "utilization");
        std::string reference_path = arg("--reference", "/tmp/r_sp_utilization_reference.bin");
        int cameras = std::stoi(arg("--cameras", "1"));
        if (cameras != 1) return run_cameras(env,
            arg("--model", "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"),
            mode, reference_path, cameras, frames, warmup,
            arg("--pattern", "1") == "1", arg("--trim", "0") == "1", adv, occupancy);
        Engine engine(env, arg("--model", "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"), mode == "reference" ? "bfc" : mode, arg("--pattern", "1") == "1");
        if (mode == "reference") {
            std::ofstream file(reference_path, std::ios::binary);
            for (int seed = 0; seed < input_variants; ++seed) {
                auto outputs = engine.run(seed);
                uint64_t n = outputs.size(); file.write(reinterpret_cast<const char*>(&n), sizeof(n));
                for (auto& output : outputs) {
                    uint64_t count = output.GetTensorTypeAndShapeInfo().GetElementCount();
                    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
                    file.write(reinterpret_cast<const char*>(output.GetTensorData<float>()), count * sizeof(float));
                }
            }
            if (!file) throw std::runtime_error("cannot save references");
            std::cout << "{\"reference_variants\":" << input_variants << "}\n";
            return 0;
        }
        auto reference = read_reference(reference_path);
        bool trim = arg("--trim", "0") == "1";
        if (trim) malloc_trim(0);
        double error = 0;
        for (int i = 0; i < warmup; ++i) {
            { auto outputs = engine.run(i % input_variants); verify(outputs, reference[i % input_variants], error); }
            engine.boundary();
            if (trim) malloc_trim(0);
        }
        Snapshot warm = snapshot();
        PageAccounting pages_warm, pages_end;
        if (engine.allocator && engine.allocator->pool) pages_warm = engine.allocator->pool->sample();
        { std::ofstream reset("/proc/self/clear_refs"); reset << "5\n"; if (!reset) throw std::runtime_error("cannot reset RSS high-water mark"); }
        std::vector<Ort::Value> held;
        int held_seed = 0;
        size_t max_private = 0, max_rss = 0;
        auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) {
            if (i % 11 == 5) held.clear();
            {
                int seed = i % input_variants;
                auto outputs = engine.run(seed); verify(outputs, reference[seed], error);
                if (!held.empty()) verify(held, reference[held_seed], error);
                if (i % 11 == 0) { held = std::move(outputs); held_seed = seed; }
                Snapshot active = snapshot(); max_private = std::max(max_private, active.private_bytes); max_rss = std::max(max_rss, active.rss);
            }
            engine.boundary();
            if (trim) malloc_trim(0);
        }
        if (!held.empty()) verify(held, reference[held_seed], error);
        held.clear(); engine.boundary();
        if (trim) malloc_trim(0);
        Snapshot idle = snapshot();
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (engine.allocator && engine.allocator->pool) pages_end = engine.allocator->pool->sample();
        std::cout << "{\"mode\":\"" << mode << "\",\"frames\":" << frames << ",\"warmup\":" << warmup
                  << ",\"adv\":" << adv << ",\"occupancy\":" << occupancy << ",\"memory_pattern\":" << arg("--pattern", "1")
                  << ",\"glibc_trim\":" << (trim ? "true" : "false")
                  << ",\"outputs_verified\":true,\"retained_outputs_verified\":true,\"max_abs_error\":" << error
                  << ",\"fps_diagnostic\":" << frames / seconds << ",\"startup_peak_rss_bytes\":" << warm.hwm
                  << ",\"active_peak_rss_bytes\":" << idle.hwm << ",\"active_sampled_max_private_bytes\":" << max_private
                  << ",\"active_sampled_max_rss_bytes\":" << max_rss << ",\"live_session_idle\":"; print_snapshot(idle);
        std::cout << ",\"warmup_idle\":"; print_snapshot(warm);
        if (engine.allocator) {
            engine.allocator->check();
            std::cout << ",\"allocator_calls\":" << engine.allocator->calls << ",\"peak_live_requests\":" << engine.allocator->peak_count
                      << ",\"peak_requested_bytes\":" << engine.allocator->peak_requested << ",\"idle_requested_bytes\":" << engine.allocator->requested;
            if (engine.allocator->pool) {
                std::cout << ",\"pages_warmup\":"; print_pages(pages_warm);
                std::cout << ",\"pages_idle\":"; print_pages(pages_end);
                std::cout << ",\"advice_calls\":" << engine.allocator->pool->advice_calls;
            }
        }
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "utilization_bench: " << e.what() << "\n"; return 1; }
}
