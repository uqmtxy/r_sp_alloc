#include "idle_reclaimer.hpp"
#include "platform_memory.hpp"
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_session_options_config_keys.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

using namespace utilization;

namespace {

constexpr size_t kInputCount = 1 * 3 * 640 * 640;
constexpr int kInputVariants = 8;

using Reference = std::vector<std::vector<std::vector<float>>>;

Reference read_reference(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed opening reference file: " + path);
    Reference result(kInputVariants);
    for (auto& variant : result) {
        uint64_t outputs = 0;
        file.read(reinterpret_cast<char*>(&outputs), sizeof(outputs));
        if (!file || outputs == 0 || outputs > 16) throw std::runtime_error("invalid reference output count");
        variant.resize(static_cast<size_t>(outputs));
        for (auto& tensor : variant) {
            uint64_t count = 0;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!file || count == 0 || count > 100000000) throw std::runtime_error("invalid reference tensor length");
            tensor.resize(static_cast<size_t>(count));
            file.read(reinterpret_cast<char*>(tensor.data()),
                      static_cast<std::streamsize>(count * sizeof(float)));
            if (!file) throw std::runtime_error("truncated reference");
        }
    }
    return result;
}

void verify(const std::vector<Ort::Value>& output,
            const std::vector<std::vector<float>>& reference,
            double& max_error) {
    if (output.size() != reference.size()) throw std::runtime_error("output count mismatch");
    for (size_t t = 0; t < output.size(); ++t) {
        auto info = output[t].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            info.GetElementCount() != reference[t].size()) {
            throw std::runtime_error("output type/length mismatch");
        }
        const float* values = output[t].GetTensorData<float>();
        for (size_t i = 0; i < reference[t].size(); ++i) {
            double expected = reference[t][i];
            double observed = values[i];
            double error = std::abs(observed - expected);
            if (!std::isfinite(observed) || !std::isfinite(expected) ||
                error > 1e-5 + 1e-4 * std::abs(expected)) {
                throw std::runtime_error("output verification failed at tensor " + std::to_string(t) +
                                         " element " + std::to_string(i));
            }
            max_error = std::max(max_error, error);
        }
    }
}

class ControllerAllocator : public OrtAllocator {
public:
    enum class Mode { Pool, DirectMalloc, DirectJemalloc };

private:
    OrtMemoryInfo* info_ = nullptr;
    std::exception_ptr failure_;
    Mode mode_;
    int (*je_posix_memalign_)(void**, size_t, size_t) = nullptr;
    void (*je_dallocx_)(void*, int) = nullptr;

    static void* jemalloc_handle() {
        static void* h = dlopen("/opt/homebrew/opt/jemalloc/lib/libjemalloc.dylib", RTLD_LAZY | RTLD_LOCAL);
        if (!h) {
            throw std::runtime_error(std::string("failed to dlopen libjemalloc.dylib: ") +
                                     (dlerror() ? dlerror() : "unknown"));
        }
        return h;
    }

public:
    std::unique_ptr<IdleReclamationController> ctrl;

    ControllerAllocator(Mode mode,
                        bool auto_enabled,
                        size_t budget_bytes,
                        int64_t idle_ms,
                        int64_t poll_ms)
        : OrtAllocator{}, mode_(mode) {
        version = ORT_API_VERSION;
        Alloc = allocate_cb;
        Reserve = allocate_cb;
        Free = free_cb;
        Info = info_cb;
        Ort::ThrowOnError(Ort::GetApi().CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &info_));
        if (mode_ == Mode::Pool) {
            ctrl = std::make_unique<IdleReclamationController>(
                auto_enabled, budget_bytes, idle_ms, poll_ms, /*trim_system=*/false);
        } else if (mode_ == Mode::DirectJemalloc) {
            void* h = jemalloc_handle();
            je_posix_memalign_ = reinterpret_cast<int (*)(void**, size_t, size_t)>(dlsym(h, "posix_memalign"));
            je_dallocx_ = reinterpret_cast<void (*)(void*, int)>(dlsym(h, "dallocx"));
            if (!je_posix_memalign_ || !je_dallocx_) {
                throw std::runtime_error("missing posix_memalign/dallocx symbols in libjemalloc.dylib");
            }
        }
    }

    ~ControllerAllocator() {
        ctrl.reset();
        if (info_) Ort::GetApi().ReleaseMemoryInfo(info_);
    }

    void check() {
        if (failure_) std::rethrow_exception(failure_);
    }

    static void* ORT_API_CALL allocate_cb(OrtAllocator* a, size_t n) {
        auto& self = *static_cast<ControllerAllocator*>(a);
        try {
            if (self.mode_ == Mode::Pool) {
                return self.ctrl->allocate(n);
            }
            void* p = nullptr;
            size_t req = std::max<size_t>(n, 1);
            if (self.mode_ == Mode::DirectJemalloc) {
                if (self.je_posix_memalign_(&p, 64, req) != 0) throw std::bad_alloc();
                return p;
            }
            if (posix_memalign(&p, 64, req) != 0) throw std::bad_alloc();
            return p;
        } catch (...) {
            self.failure_ = std::current_exception();
            return nullptr;
        }
    }

    static void ORT_API_CALL free_cb(OrtAllocator* a, void* p) {
        if (!p) return;
        auto& self = *static_cast<ControllerAllocator*>(a);
        try {
            if (self.mode_ == Mode::Pool) {
                self.ctrl->deallocate(p);
            } else if (self.mode_ == Mode::DirectJemalloc) {
                self.je_dallocx_(p, 0);
            } else {
                std::free(p);
            }
        } catch (...) {
            self.failure_ = std::current_exception();
        }
    }

    static const OrtMemoryInfo* ORT_API_CALL info_cb(const OrtAllocator* a) {
        return static_cast<const ControllerAllocator*>(a)->info_;
    }
};

class CameraStream {
public:
    std::unique_ptr<ControllerAllocator> allocator;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> output_strings;
    std::vector<const char*> output_names;
    std::string input_name;
    std::vector<float> input;
    Ort::Value tensor{nullptr};

    CameraStream(Ort::Env& env,
                 const std::string& model,
                 const std::string& policy,
                 size_t stream_budget_bytes,
                 int64_t idle_ms,
                 int64_t poll_ms)
        : input(kInputCount) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        bool registered = false;
        const OrtMemoryInfo* reg_info = nullptr;
        if (policy != "bfc") {
            ControllerAllocator::Mode mode = ControllerAllocator::Mode::Pool;
            if (policy == "direct_malloc") mode = ControllerAllocator::Mode::DirectMalloc;
            else if (policy == "direct_jemalloc") mode = ControllerAllocator::Mode::DirectJemalloc;
            bool auto_enabled = (policy == "pool_auto_0" || policy == "pool_auto_warm" || policy == "pool_hybrid");
            allocator = std::make_unique<ControllerAllocator>(
                mode, auto_enabled, stream_budget_bytes, idle_ms, poll_ms);
            options.DisableCpuMemArena();
            options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
            Ort::ThrowOnError(Ort::GetApi().RegisterAllocator(env, allocator.get()));
            reg_info = allocator->Info(allocator.get());
            registered = true;
        }
        try {
            session = std::make_unique<Ort::Session>(env, model.c_str(), options);
        } catch (...) {
            if (registered) {
                auto st = Ort::GetApi().UnregisterAllocator(env, reg_info);
                if (st) Ort::GetApi().ReleaseStatus(st);
            }
            throw;
        }
        if (registered) Ort::ThrowOnError(Ort::GetApi().UnregisterAllocator(env, reg_info));
        if (allocator) allocator->check();

        Ort::AllocatorWithDefaultOptions names;
        input_name = session->GetInputNameAllocated(0, names).get();
        for (size_t i = 0; i < session->GetOutputCount(); ++i)
            output_strings.emplace_back(session->GetOutputNameAllocated(i, names).get());
        for (const auto& s : output_strings) output_names.push_back(s.c_str());

        std::vector<int64_t> shape{1, 3, 640, 640};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
    }

    std::vector<Ort::Value> run(int seed) {
        if (allocator && allocator->ctrl) allocator->ctrl->mark_frame_start();
        for (size_t i = 0; i < kInputCount; ++i)
            input[i] = float((i * (2 * seed + 1) + seed * 37) % 256) / 255.0f;
        const char* name = input_name.c_str();
        std::vector<Ort::Value> outputs;
        try {
            outputs = session->Run(Ort::RunOptions{nullptr}, &name, &tensor, 1,
                                   output_names.data(), output_names.size());
        } catch (...) {
            if (allocator && allocator->ctrl) allocator->ctrl->mark_frame_end();
            throw;
        }
        if (allocator && allocator->ctrl) allocator->ctrl->mark_frame_end();
        if (allocator) allocator->check();
        return outputs;
    }
};

struct MemSample {
    double t_sec;
    size_t phys_bytes;
    size_t rss_bytes;
    size_t minor_faults;
};

size_t get_minor_faults() {
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return static_cast<size_t>(ru.ru_minflt);
    }
    return 0;
}

double percentile(std::vector<double> vals, double q) {
    if (vals.empty()) return 0.0;
    std::sort(vals.begin(), vals.end());
    double idx = q * static_cast<double>(vals.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    double frac = idx - static_cast<double>(lo);
    return vals[lo] * (1.0 - frac) + vals[hi] * frac;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

struct Config {
    std::string model = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    std::string reference = "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/reference_m2pro.bin";
    std::string policy = "pool_coop";
    std::string out = "";
    std::string save_trace = "";
    std::string run_id = "manual";
    int streams = 4;
    int concurrent = 1;
    int frames_per_burst = 8;
    int cycles = 2;
    int burst_gap_ms = 350;
    int idle_ms = 200;
    int poll_ms = 20;
    int sample_ms = 25;
    double global_budget_mib = 24.0;
    double warm_budget_mib = 24.0;
    bool shed_warmup = false;
};

Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + flag);
            return argv[++i];
        };
        if (a == "--model") c.model = next("--model");
        else if (a == "--reference") c.reference = next("--reference");
        else if (a == "--policy") c.policy = next("--policy");
        else if (a == "--out") c.out = next("--out");
        else if (a == "--save-trace") c.save_trace = next("--save-trace");
        else if (a == "--run-id") c.run_id = next("--run-id");
        else if (a == "--streams") c.streams = std::stoi(next("--streams"));
        else if (a == "--concurrent") c.concurrent = std::stoi(next("--concurrent"));
        else if (a == "--frames-per-burst") c.frames_per_burst = std::stoi(next("--frames-per-burst"));
        else if (a == "--cycles") c.cycles = std::stoi(next("--cycles"));
        else if (a == "--burst-gap-ms") c.burst_gap_ms = std::stoi(next("--burst-gap-ms"));
        else if (a == "--idle-ms") c.idle_ms = std::stoi(next("--idle-ms"));
        else if (a == "--poll-ms") c.poll_ms = std::stoi(next("--poll-ms"));
        else if (a == "--sample-ms") c.sample_ms = std::stoi(next("--sample-ms"));
        else if (a == "--global-budget-mib") c.global_budget_mib = std::stod(next("--global-budget-mib"));
        else if (a == "--warm-budget-mib") c.warm_budget_mib = std::stod(next("--warm-budget-mib"));
        else if (a == "--shed-warmup") c.shed_warmup = true;
        else throw std::runtime_error("unknown flag: " + a);
    }
    if (c.streams <= 0 || c.concurrent <= 0 || c.streams % c.concurrent != 0) {
        throw std::runtime_error("streams must be > 0 and divisible by concurrent");
    }
    if (c.policy != "bfc" && c.policy != "direct_malloc" && c.policy != "direct_jemalloc" &&
        c.policy != "pool_retain" && c.policy != "pool_auto_0" &&
        c.policy != "pool_auto_warm" && c.policy != "pool_coop" && c.policy != "pool_hybrid") {
        throw std::runtime_error("unsupported policy: " + c.policy);
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Config cfg = parse_args(argc, argv);
        std::ostringstream cmd;
        for (int i = 0; i < argc; ++i) {
            if (i > 0) cmd << ' ';
            cmd << argv[i];
        }

        auto reference = read_reference(cfg.reference);
        Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "multicam_staggered_bench");

        size_t stream_budget = 0;
        if (cfg.policy == "pool_retain") {
            stream_budget = 512ull * 1024 * 1024;
        } else if (cfg.policy == "pool_auto_warm") {
            stream_budget = static_cast<size_t>(cfg.warm_budget_mib * 1024.0 * 1024.0);
        } else {
            stream_budget = 0;
        }

        std::unique_ptr<GlobalBudgetArbiter> arbiter;
        if (cfg.policy == "pool_coop" || cfg.policy == "pool_hybrid") {
            arbiter = std::make_unique<GlobalBudgetArbiter>(
                static_cast<size_t>(cfg.global_budget_mib * 1024.0 * 1024.0));
        }

        std::vector<std::unique_ptr<CameraStream>> streams;
        streams.reserve(static_cast<size_t>(cfg.streams));
        for (int s = 0; s < cfg.streams; ++s) {
            auto st = std::make_unique<CameraStream>(
                env, cfg.model, cfg.policy, stream_budget, cfg.idle_ms, cfg.poll_ms);
            if (arbiter && st->allocator && st->allocator->ctrl) {
                arbiter->register_controller(st->allocator->ctrl.get());
            }
            streams.push_back(std::move(st));
        }

        double max_abs_error = 0.0;
        // Warmup 1 frame per stream with seed=0 to verify against reference.
        for (int s = 0; s < cfg.streams; ++s) {
            auto out = streams[static_cast<size_t>(s)]->run(0);
            verify(out, reference[0], max_abs_error);
        }
        if (cfg.shed_warmup) {
            if (arbiter) {
                arbiter->enforce_budget();
            } else if (cfg.policy == "pool_auto_0") {
                for (const auto& st : streams) {
                    if (st->allocator && st->allocator->ctrl) {
                        st->allocator->ctrl->pressure_reclaim(0);
                    }
                }
            }
        }

        // Background 25 ms sampler.
        std::vector<MemSample> samples;
        std::mutex samples_mu;
        std::atomic<bool> stop_sampling{false};
        auto t_start = std::chrono::steady_clock::now();
        auto snap0 = take_process_snapshot();
        size_t minflt0 = get_minor_faults();
        {
            std::lock_guard<std::mutex> lk(samples_mu);
            samples.push_back({0.0, snap0.private_bytes, snap0.rss, minflt0});
        }
        std::thread sampler([&]() {
            while (!stop_sampling.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.sample_ms));
                double t = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - t_start)
                               .count();
                auto snap = take_process_snapshot();
                size_t mf = get_minor_faults();
                std::lock_guard<std::mutex> lk(samples_mu);
                samples.push_back({t, snap.private_bytes, snap.rss, mf});
            }
        });

        std::vector<double> frame_lat_ms;
        frame_lat_ms.reserve(static_cast<size_t>(cfg.cycles * cfg.streams * cfg.frames_per_burst));
        std::mutex lat_mu;
        size_t arbiter_reclaimed_bytes = 0;

        int waves = cfg.streams / cfg.concurrent;
        for (int cyc = 0; cyc < cfg.cycles; ++cyc) {
            for (int w = 0; w < waves; ++w) {
                int s_begin = w * cfg.concurrent;
                int s_end = s_begin + cfg.concurrent;

                // Protect active wave streams before cooperative shedding.
                if (arbiter) {
                    for (int s = s_begin; s < s_end; ++s) {
                        streams[static_cast<size_t>(s)]->allocator->ctrl->mark_frame_start();
                    }
                    arbiter_reclaimed_bytes += arbiter->notify_burst_start();
                }

                std::vector<std::thread> workers;
                workers.reserve(static_cast<size_t>(cfg.concurrent));
                std::exception_ptr worker_err;
                std::mutex err_mu;

                for (int s = s_begin; s < s_end; ++s) {
                    workers.emplace_back([&, s]() {
                        try {
                            std::vector<double> local_lats;
                            local_lats.reserve(static_cast<size_t>(cfg.frames_per_burst));
                            double local_err = 0.0;
                            for (int f = 0; f < cfg.frames_per_burst; ++f) {
                                int seed = (cyc * waves + w + f) % kInputVariants;
                                auto f0 = std::chrono::steady_clock::now();
                                auto out = streams[static_cast<size_t>(s)]->run(seed);
                                double ms = std::chrono::duration<double, std::milli>(
                                                std::chrono::steady_clock::now() - f0)
                                                .count();
                                verify(out, reference[static_cast<size_t>(seed)], local_err);
                                local_lats.push_back(ms);
                            }
                            std::lock_guard<std::mutex> lk(lat_mu);
                            max_abs_error = std::max(max_abs_error, local_err);
                            frame_lat_ms.insert(frame_lat_ms.end(), local_lats.begin(), local_lats.end());
                        } catch (...) {
                            std::lock_guard<std::mutex> lk(err_mu);
                            if (!worker_err) worker_err = std::current_exception();
                        }
                    });
                }
                for (auto& th : workers) th.join();
                if (arbiter) {
                    for (int s = s_begin; s < s_end; ++s) {
                        streams[static_cast<size_t>(s)]->allocator->ctrl->mark_frame_end();
                    }
                }
                if (worker_err) std::rethrow_exception(worker_err);

                if (cfg.burst_gap_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.burst_gap_ms));
                }
            }
        }

        double wall_sec = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t_start)
                              .count();
        stop_sampling.store(std::memory_order_release);
        sampler.join();
        auto snap_end = take_process_snapshot();
        size_t minflt_end = get_minor_faults();
        {
            std::lock_guard<std::mutex> lk(samples_mu);
            samples.push_back({wall_sec, snap_end.private_bytes, snap_end.rss, minflt_end});
        }

        constexpr double kMiB = 1024.0 * 1024.0;
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        if (!cfg.save_trace.empty()) {
            std::ostringstream tr;
            tr << "{\n  \"policy\": \"" << json_escape(cfg.policy) << "\",\n"
               << "  \"streams\": " << cfg.streams << ",\n"
               << "  \"concurrent\": " << cfg.concurrent << ",\n"
               << "  \"burst_gap_ms\": " << cfg.burst_gap_ms << ",\n"
               << "  \"samples\": [\n";
            for (size_t i = 0; i < samples.size(); ++i) {
                tr << "    {\"t_sec\": " << samples[i].t_sec
                   << ", \"phys_mib\": " << (static_cast<double>(samples[i].phys_bytes) / kMiB)
                   << ", \"rss_mib\": " << (static_cast<double>(samples[i].rss_bytes) / kMiB)
                   << "}" << (i + 1 < samples.size() ? "," : "") << "\n";
            }
            tr << "  ]\n}\n";
            std::ofstream tf(cfg.save_trace);
            if (!tf) throw std::runtime_error("failed writing trace JSON: " + cfg.save_trace);
            tf << tr.str();
        }
        double sum_phys = 0.0, peak_phys = 0.0, min_phys = 1e18;
        double sum_rss = 0.0, peak_rss = 0.0;
        double byte_seconds_gib_s = 0.0;
        std::vector<double> phys_vals;
        phys_vals.reserve(samples.size());

        for (size_t i = 0; i < samples.size(); ++i) {
            double pmib = static_cast<double>(samples[i].phys_bytes) / kMiB;
            double rmib = static_cast<double>(samples[i].rss_bytes) / kMiB;
            phys_vals.push_back(pmib);
            sum_phys += pmib;
            sum_rss += rmib;
            peak_phys = std::max(peak_phys, pmib);
            min_phys = std::min(min_phys, pmib);
            peak_rss = std::max(peak_rss, rmib);
            if (i > 0) {
                double dt = samples[i].t_sec - samples[i - 1].t_sec;
                double avg_bytes = 0.5 * (static_cast<double>(samples[i].phys_bytes) +
                                          static_cast<double>(samples[i - 1].phys_bytes));
                byte_seconds_gib_s += (avg_bytes / kGiB) * dt;
            }
        }

        double mean_phys = samples.empty() ? 0.0 : sum_phys / static_cast<double>(samples.size());
        double p95_phys = percentile(phys_vals, 0.95);
        double final_phys = samples.empty() ? 0.0 : static_cast<double>(samples.back().phys_bytes) / kMiB;
        double mean_rss = samples.empty() ? 0.0 : sum_rss / static_cast<double>(samples.size());
        double final_rss = samples.empty() ? 0.0 : static_cast<double>(samples.back().rss_bytes) / kMiB;
        size_t minor_faults_delta =
            minflt_end >= minflt0 ? (minflt_end - minflt0) : 0;

        double lat_mean = frame_lat_ms.empty()
                              ? 0.0
                              : std::accumulate(frame_lat_ms.begin(), frame_lat_ms.end(), 0.0) /
                                    static_cast<double>(frame_lat_ms.size());
        double lat_p50 = percentile(frame_lat_ms, 0.50);
        double lat_p95 = percentile(frame_lat_ms, 0.95);
        double lat_p99 = percentile(frame_lat_ms, 0.99);
        double lat_max = frame_lat_ms.empty()
                             ? 0.0
                             : *std::max_element(frame_lat_ms.begin(), frame_lat_ms.end());
        double fps = wall_sec > 0.0 ? static_cast<double>(frame_lat_ms.size()) / wall_sec : 0.0;

        size_t auto_reclaims_total = 0;
        size_t pressure_reclaims_total = 0;
        double reclaim_ms_total = 0.0;
        double resident_mib = 0.0;
        double resident_body_mib = 0.0;
        double live_req_mib = 0.0;
        for (const auto& st : streams) {
            if (st->allocator && st->allocator->ctrl) {
                auto_reclaims_total += st->allocator->ctrl->auto_reclaims();
                pressure_reclaims_total += st->allocator->ctrl->pressure_reclaims();
                reclaim_ms_total += st->allocator->ctrl->reclaim_ms_total();
                auto acc = st->allocator->ctrl->sample_pages();
                resident_mib += static_cast<double>(acc.resident) / kMiB;
                resident_body_mib += static_cast<double>(acc.resident_body) / kMiB;
                live_req_mib += static_cast<double>(acc.live_requested) / kMiB;
            }
        }

        std::ostringstream js;
        js << "{\n";
        js << "  \"protocol_version\": 1,\n";
        js << "  \"run_id\": \"" << json_escape(cfg.run_id) << "\",\n";
        js << "  \"experiment\": \"macbook_m2pro_multicam\",\n";
        js << "  \"policy\": \"" << json_escape(cfg.policy) << "\",\n";
        js << "  \"streams\": " << cfg.streams << ",\n";
        js << "  \"concurrent\": " << cfg.concurrent << ",\n";
        js << "  \"frames_per_burst\": " << cfg.frames_per_burst << ",\n";
        js << "  \"cycles\": " << cfg.cycles << ",\n";
        js << "  \"total_frames\": " << frame_lat_ms.size() << ",\n";
        js << "  \"burst_gap_ms\": " << cfg.burst_gap_ms << ",\n";
        js << "  \"idle_threshold_ms\": " << cfg.idle_ms << ",\n";
        js << "  \"global_budget_mib\": " << cfg.global_budget_mib << ",\n";
        js << "  \"warm_budget_mib\": " << cfg.warm_budget_mib << ",\n";
        js << "  \"os_page_size\": " << os_page_size() << ",\n";
        js << "  \"wall_sec\": " << wall_sec << ",\n";
        js << "  \"throughput_fps\": " << fps << ",\n";
        js << "  \"max_abs_error\": " << max_abs_error << ",\n";
        js << "  \"outputs_verified\": true,\n";
        js << "  \"retained_outputs_verified\": true,\n";
        js << "  \"latency_ms\": {\n";
        js << "    \"mean\": " << lat_mean << ",\n";
        js << "    \"p50\": " << lat_p50 << ",\n";
        js << "    \"p95\": " << lat_p95 << ",\n";
        js << "    \"p99\": " << lat_p99 << ",\n";
        js << "    \"max\": " << lat_max << "\n";
        js << "  },\n";
        js << "  \"phys_footprint_mib\": {\n";
        js << "    \"mean\": " << mean_phys << ",\n";
        js << "    \"peak\": " << peak_phys << ",\n";
        js << "    \"min\": " << min_phys << ",\n";
        js << "    \"p95\": " << p95_phys << ",\n";
        js << "    \"final\": " << final_phys << ",\n";
        js << "    \"byte_seconds_gib_s\": " << byte_seconds_gib_s << "\n";
        js << "  },\n";
        js << "  \"rss_mib\": {\n";
        js << "    \"mean\": " << mean_rss << ",\n";
        js << "    \"peak\": " << peak_rss << ",\n";
        js << "    \"final\": " << final_rss << "\n";
        js << "  },\n";
        js << "  \"minor_faults_total\": " << minor_faults_delta << ",\n";
        js << "  \"auto_reclaims_total\": " << auto_reclaims_total << ",\n";
        js << "  \"pressure_reclaims_total\": " << pressure_reclaims_total << ",\n";
        js << "  \"reclaim_ms_total\": " << reclaim_ms_total << ",\n";
        js << "  \"arbiter_reclaimed_bytes\": " << arbiter_reclaimed_bytes << ",\n";
        js << "  \"page_accounting\": {\n";
        js << "    \"resident_mib\": " << resident_mib << ",\n";
        js << "    \"resident_body_mib\": " << resident_body_mib << ",\n";
        js << "    \"live_requested_mib\": " << live_req_mib << "\n";
        js << "  },\n";
        js << "  \"samples_count\": " << samples.size() << ",\n";
        js << "  \"command\": [";
        for (int i = 0; i < argc; ++i) {
            if (i > 0) js << ", ";
            js << "\"" << json_escape(argv[i]) << "\"";
        }
        js << "]\n";
        js << "}\n";

        if (!cfg.out.empty()) {
            std::ofstream out(cfg.out);
            if (!out) throw std::runtime_error("failed writing output JSON: " + cfg.out);
            out << js.str();
        } else {
            std::cout << js.str();
        }
        std::printf(
            "multicam policy=%s N=%d K=%d mean_phys=%.2fMiB peak_phys=%.2fMiB p99=%.2fms minor_faults=%zu auto_rec=%zu press_rec=%zu max_err=%.3g\n",
            cfg.policy.c_str(), cfg.streams, cfg.concurrent, mean_phys, peak_phys,
            lat_p99, minor_faults_delta, auto_reclaims_total, pressure_reclaims_total, max_abs_error);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
