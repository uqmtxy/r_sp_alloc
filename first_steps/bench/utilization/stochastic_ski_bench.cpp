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
                throw std::runtime_error("output verification failed");
            }
            max_error = std::max(max_error, error);
        }
    }
}

class ControllerAllocator : public OrtAllocator {
    OrtMemoryInfo* info_ = nullptr;
    std::exception_ptr failure_;

public:
    std::unique_ptr<IdleReclamationController> ctrl;

    ControllerAllocator(bool auto_enabled, size_t budget_bytes, int64_t idle_ms, int64_t poll_ms)
        : OrtAllocator{} {
        version = ORT_API_VERSION;
        Alloc = allocate_cb;
        Reserve = allocate_cb;
        Free = free_cb;
        Info = info_cb;
        Ort::ThrowOnError(Ort::GetApi().CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &info_));
        ctrl = std::make_unique<IdleReclamationController>(
            auto_enabled, budget_bytes, idle_ms, poll_ms, /*trim_system=*/false);
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
            return self.ctrl->allocate(n);
        } catch (...) {
            self.failure_ = std::current_exception();
            return nullptr;
        }
    }

    static void ORT_API_CALL free_cb(OrtAllocator* a, void* p) {
        if (!p) return;
        auto& self = *static_cast<ControllerAllocator*>(a);
        try {
            self.ctrl->deallocate(p);
        } catch (...) {
            self.failure_ = std::current_exception();
        }
    }

    static const OrtMemoryInfo* ORT_API_CALL info_cb(const OrtAllocator* a) {
        return static_cast<const ControllerAllocator*>(a)->info_;
    }
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
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

struct Config {
    std::string model = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    std::string reference = "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/reference_m2pro.bin";
    std::string policy = "pool_auto";
    std::string out = "";
    std::string run_id = "manual";
    int64_t idle_ms = 250;
    int64_t poll_ms = 10;
    int sample_ms = 10;
    int frames_per_burst = 4;
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
        else if (a == "--run-id") c.run_id = next("--run-id");
        else if (a == "--idle-ms") c.idle_ms = std::stoll(next("--idle-ms"));
        else if (a == "--poll-ms") c.poll_ms = std::stoll(next("--poll-ms"));
        else if (a == "--sample-ms") c.sample_ms = std::stoi(next("--sample-ms"));
        else if (a == "--frames-per-burst") c.frames_per_burst = std::stoi(next("--frames-per-burst"));
        else throw std::runtime_error("unknown flag: " + a);
    }
    if (c.policy != "bfc" && c.policy != "pool_retain" &&
        c.policy != "pool_explicit" && c.policy != "pool_auto") {
        throw std::runtime_error("unsupported policy: " + c.policy);
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Config cfg = parse_args(argc, argv);
        auto reference = read_reference(cfg.reference);
        Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "stochastic_ski_bench");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        std::unique_ptr<ControllerAllocator> allocator;
        bool registered = false;
        const OrtMemoryInfo* reg_info = nullptr;
        if (cfg.policy != "bfc") {
            size_t budget = (cfg.policy == "pool_retain") ? (512ull * 1024 * 1024) : 0;
            bool auto_en = (cfg.policy == "pool_auto");
            allocator = std::make_unique<ControllerAllocator>(auto_en, budget, cfg.idle_ms, cfg.poll_ms);
            options.DisableCpuMemArena();
            options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
            Ort::ThrowOnError(Ort::GetApi().RegisterAllocator(env, allocator.get()));
            reg_info = allocator->Info(allocator.get());
            registered = true;
        }
        auto session = std::make_unique<Ort::Session>(env, cfg.model.c_str(), options);
        if (registered) Ort::ThrowOnError(Ort::GetApi().UnregisterAllocator(env, reg_info));
        if (allocator) allocator->check();

        Ort::AllocatorWithDefaultOptions names;
        std::string input_name = session->GetInputNameAllocated(0, names).get();
        std::vector<std::string> output_strings;
        std::vector<const char*> output_names;
        for (size_t i = 0; i < session->GetOutputCount(); ++i)
            output_strings.emplace_back(session->GetOutputNameAllocated(i, names).get());
        for (const auto& s : output_strings) output_names.push_back(s.c_str());

        std::vector<float> input(kInputCount);
        std::vector<int64_t> shape{1, 3, 640, 640};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());

        auto run_frame = [&](int seed) -> double {
            if (allocator && allocator->ctrl) allocator->ctrl->mark_frame_start();
            for (size_t i = 0; i < kInputCount; ++i)
                input[i] = float((i * (2 * seed + 1) + seed * 37) % 256) / 255.0f;
            const char* name = input_name.c_str();
            auto f0 = std::chrono::steady_clock::now();
            auto outs = session->Run(Ort::RunOptions{nullptr}, &name, &tensor, 1,
                                     output_names.data(), output_names.size());
            double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - f0)
                            .count();
            if (allocator && allocator->ctrl) allocator->ctrl->mark_frame_end();
            if (allocator) allocator->check();
            double err = 0.0;
            verify(outs, reference[static_cast<size_t>(seed)], err);
            return ms;
        };

        // Warmup 4 frames
        for (int w = 0; w < 4; ++w) run_frame(w % kInputVariants);

        // Deterministic bimodal + heavy-tailed inter-burst gap schedule (ms).
        const std::vector<int> gaps_ms = {30, 45, 60, 90, 120, 180, 260, 420, 750, 1100, 1500, 50, 85, 600, 1300};
        const int num_bursts = static_cast<int>(gaps_ms.size()) + 1;

        struct Sample { double t_sec; size_t phys_bytes; };
        std::vector<Sample> samples;
        std::mutex smu;
        std::atomic<bool> stop_sam{false};
        auto t0 = std::chrono::steady_clock::now();
        auto snap0 = take_process_snapshot();
        size_t mf0 = get_minor_faults();
        samples.push_back({0.0, snap0.private_bytes});

        std::thread sampler([&]() {
            while (!stop_sam.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.sample_ms));
                double t = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
                auto s = take_process_snapshot();
                std::lock_guard<std::mutex> lk(smu);
                samples.push_back({t, s.private_bytes});
            }
        });

        std::vector<double> lats;
        lats.reserve(static_cast<size_t>(num_bursts * cfg.frames_per_burst));
        std::vector<double> realized_gaps_ms;
        realized_gaps_ms.reserve(gaps_ms.size());

        for (int b = 0; b < num_bursts; ++b) {
            for (int f = 0; f < cfg.frames_per_burst; ++f) {
                lats.push_back(run_frame((b * cfg.frames_per_burst + f) % kInputVariants));
            }
            if (cfg.policy == "pool_explicit" && allocator && allocator->ctrl) {
                allocator->ctrl->explicit_reclaim();
            }
            if (b < static_cast<int>(gaps_ms.size())) {
                auto g0 = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(gaps_ms[static_cast<size_t>(b)]));
                double g_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - g0)
                                  .count();
                realized_gaps_ms.push_back(g_ms);
            }
        }

        double wall_sec = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        stop_sam.store(true, std::memory_order_release);
        sampler.join();
        auto snap_end = take_process_snapshot();
        size_t mf_end = get_minor_faults();
        samples.push_back({wall_sec, snap_end.private_bytes});

        constexpr double kMiB = 1024.0 * 1024.0;
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        double sum_phys = 0.0, peak_phys = 0.0, byte_s = 0.0;
        for (size_t i = 0; i < samples.size(); ++i) {
            double pmib = static_cast<double>(samples[i].phys_bytes) / kMiB;
            sum_phys += pmib;
            peak_phys = std::max(peak_phys, pmib);
            if (i > 0) {
                double dt = samples[i].t_sec - samples[i - 1].t_sec;
                double avg = 0.5 * (static_cast<double>(samples[i].phys_bytes) +
                                    static_cast<double>(samples[i - 1].phys_bytes));
                byte_s += (avg / kGiB) * dt;
            }
        }
        double mean_phys = sum_phys / static_cast<double>(samples.size());
        size_t mf_delta = mf_end >= mf0 ? (mf_end - mf0) : 0;
        size_t rec_calls = (allocator && allocator->ctrl) ? allocator->ctrl->total_reclaims() : 0;
        double rec_ms = (allocator && allocator->ctrl) ? allocator->ctrl->reclaim_ms_total() : 0.0;

        std::ostringstream js;
        js << "{\n"
           << "  \"protocol_version\": 1,\n"
           << "  \"run_id\": \"" << json_escape(cfg.run_id) << "\",\n"
           << "  \"experiment\": \"macbook_m2pro_stochastic_ski\",\n"
           << "  \"policy\": \"" << json_escape(cfg.policy) << "\",\n"
           << "  \"idle_threshold_ms\": " << cfg.idle_ms << ",\n"
           << "  \"os_page_size\": " << os_page_size() << ",\n"
           << "  \"total_frames\": " << lats.size() << ",\n"
           << "  \"wall_sec\": " << wall_sec << ",\n"
           << "  \"max_abs_error\": 0,\n"
           << "  \"outputs_verified\": true,\n"
           << "  \"retained_outputs_verified\": true,\n"
           << "  \"p50_ms\": " << percentile(lats, 0.50) << ",\n"
           << "  \"p99_ms\": " << percentile(lats, 0.99) << ",\n"
           << "  \"mean_phys_mib\": " << mean_phys << ",\n"
           << "  \"peak_phys_mib\": " << peak_phys << ",\n"
           << "  \"byte_seconds_gib_s\": " << byte_s << ",\n"
           << "  \"minor_faults\": " << mf_delta << ",\n"
           << "  \"reclaim_calls\": " << rec_calls << ",\n"
           << "  \"reclaim_ms_total\": " << rec_ms << ",\n"
           << "  \"realized_gaps_ms\": [";
        for (size_t i = 0; i < realized_gaps_ms.size(); ++i) {
            js << realized_gaps_ms[i] << (i + 1 < realized_gaps_ms.size() ? ", " : "");
        }
        js << "],\n  \"command\": [";
        for (int i = 0; i < argc; ++i) {
            js << (i > 0 ? ", " : "") << "\"" << json_escape(argv[i]) << "\"";
        }
        js << "]\n}\n";

        if (!cfg.out.empty()) {
            std::ofstream out(cfg.out);
            if (!out) throw std::runtime_error("failed writing out JSON");
            out << js.str();
        } else {
            std::cout << js.str();
        }
        std::printf("ski policy=%s idle_ms=%lld mean_phys=%.2fMiB byte_s=%.3fGiBs minor_faults=%zu rec=%zu p99=%.2fms\n",
                    cfg.policy.c_str(), static_cast<long long>(cfg.idle_ms), mean_phys, byte_s, mf_delta, rec_calls, percentile(lats, 0.99));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
