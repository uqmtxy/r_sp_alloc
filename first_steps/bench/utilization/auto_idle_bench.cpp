#include "idle_reclaimer.hpp"
#include "onnxruntime_cxx_api.h"
#include "onnxruntime_session_options_config_keys.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

using namespace utilization;

namespace {

constexpr size_t kInputCount = 3 * 640 * 640;
constexpr int kInputVariants = 8;

using Reference = std::vector<std::vector<std::vector<float>>>;

Reference read_reference(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open reference file: " + path);
    Reference result(kInputVariants);
    for (auto& variant : result) {
        uint64_t outputs = 0;
        file.read(reinterpret_cast<char*>(&outputs), sizeof(outputs));
        if (!file || outputs == 0 || outputs > 16) throw std::runtime_error("invalid reference output count");
        variant.resize(outputs);
        for (auto& tensor : variant) {
            uint64_t count = 0;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!file || count == 0 || count > 100000000) throw std::runtime_error("invalid reference tensor length");
            tensor.resize(count);
            file.read(reinterpret_cast<char*>(tensor.data()), count * sizeof(float));
            if (!file) throw std::runtime_error("truncated reference");
        }
    }
    return result;
}

void write_reference(const std::string& path, const Reference& ref) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write reference file: " + path);
    for (const auto& variant : ref) {
        uint64_t outputs = variant.size();
        file.write(reinterpret_cast<const char*>(&outputs), sizeof(outputs));
        for (const auto& tensor : variant) {
            uint64_t count = tensor.size();
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            file.write(reinterpret_cast<const char*>(tensor.data()), count * sizeof(float));
        }
    }
    if (!file) throw std::runtime_error("failed writing reference file");
}

void verify(const std::vector<Ort::Value>& output, const std::vector<std::vector<float>>& reference, double& max_error) {
    if (output.size() != reference.size()) throw std::runtime_error("output count mismatch");
    for (size_t t = 0; t < output.size(); ++t) {
        auto info = output[t].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            info.GetElementCount() != reference[t].size()) {
            throw std::runtime_error("output type/length mismatch");
        }
        const float* values = output[t].GetTensorData<float>();
        for (size_t i = 0; i < reference[t].size(); ++i) {
            double expected = reference[t][i], observed = values[i];
            double error = std::abs(observed - expected);
            if (!std::isfinite(observed) || !std::isfinite(expected) || error > 1e-5 + 1e-4 * std::abs(expected)) {
                throw std::runtime_error("full output comparison failed at tensor " + std::to_string(t) +
                                         " element " + std::to_string(i));
            }
            max_error = std::max(max_error, error);
        }
    }
}

class ControllerAllocator : public OrtAllocator {
    OrtMemoryInfo* info_ = nullptr;
    std::mutex mu_;
    std::map<void*, size_t> live_;
    std::exception_ptr failure_;
    bool direct_mode_ = false;

public:
    std::unique_ptr<IdleReclamationController> ctrl;

    ControllerAllocator(std::string policy, size_t budget_bytes, int64_t idle_ms, int64_t poll_ms)
        : OrtAllocator{} {
        version = ORT_API_VERSION;
        Alloc = allocate_cb;
        Reserve = allocate_cb;
        Free = free_cb;
        Info = info_cb;
        Ort::ThrowOnError(Ort::GetApi().CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &info_));
        if (policy == "direct_trim") {
            direct_mode_ = true;
        } else {
            ctrl = std::make_unique<IdleReclamationController>(
                policy == "pool_auto", budget_bytes, idle_ms, poll_ms, /*trim_system=*/true);
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
            void* p = nullptr;
            size_t req = std::max<size_t>(n, 1);
            if (self.direct_mode_) {
                if (posix_memalign(&p, 64, req) != 0) throw std::bad_alloc();
            } else {
                p = self.ctrl->allocate(n);
            }
            if (!p) throw std::bad_alloc();
            {
                std::lock_guard<std::mutex> lk(self.mu_);
                if (!self.live_.emplace(p, n).second) throw std::runtime_error("duplicate outstanding ptr");
            }
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
            {
                std::lock_guard<std::mutex> lk(self.mu_);
                auto it = self.live_.find(p);
                if (it == self.live_.end()) throw std::runtime_error("unknown/double ORT free");
                self.live_.erase(it);
            }
            if (self.direct_mode_) {
                free(p);
            } else {
                self.ctrl->deallocate(p);
            }
        } catch (...) {
            self.failure_ = std::current_exception();
        }
    }

    static const OrtMemoryInfo* ORT_API_CALL info_cb(const OrtAllocator* a) {
        return static_cast<const ControllerAllocator*>(a)->info_;
    }
};

class Engine {
public:
    std::unique_ptr<ControllerAllocator> allocator;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> output_strings;
    std::vector<const char*> output_names;
    std::string input_name;
    std::vector<float> input;
    Ort::Value tensor{nullptr};

    Engine(Ort::Env& env, const std::string& model, const std::string& policy,
           size_t budget_bytes, int64_t idle_ms, int64_t poll_ms)
        : input(kInputCount) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        bool registered = false;
        const OrtMemoryInfo* reg_info = nullptr;
        if (policy != "bfc") {
            allocator = std::make_unique<ControllerAllocator>(policy, budget_bytes, idle_ms, poll_ms);
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
        for (size_t i = 0; i < kInputCount; ++i)
            input[i] = float((i * (2 * seed + 1) + seed * 37) % 256) / 255.0f;
        const char* name = input_name.c_str();
        auto outputs = session->Run(Ort::RunOptions{nullptr}, &name, &tensor, 1,
                                    output_names.data(), output_names.size());
        if (allocator) allocator->check();
        return outputs;
    }
};

struct PauseEvent {
    int frame_idx;
    int pause_ms;
    bool is_long_idle;
};

std::vector<PauseEvent> build_schedule(const std::string& regime, int frames, int burst, int pause_ms) {
    std::vector<PauseEvent> events;
    if (regime == "continuous") return events;
    if (regime == "burst") {
        for (int i = burst - 1; i < frames; i += burst) {
            events.push_back({i, pause_ms, true});
        }
        return events;
    }
    if (regime == "mixed") {
        // Irregular burst sizes alternating short 80 ms jitter (< idle_ms) and 1000 ms true idle (> idle_ms).
        const int sizes[] = {6, 12, 8, 15, 9, 10, 7, 13, 10, 10, 10, 10};
        int cursor = 0;
        for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]) && cursor < frames; ++k) {
            cursor += sizes[k];
            if (cursor <= frames) {
                bool long_idle = (k % 2 == 1);
                events.push_back({cursor - 1, long_idle ? pause_ms : 80, long_idle});
            }
        }
        return events;
    }
    throw std::runtime_error("unknown regime: " + regime);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto arg = [&](const std::string& k, const std::string& d) {
            for (int i = 1; i + 1 < argc; ++i)
                if (argv[i] == k) return std::string(argv[i + 1]);
            return d;
        };

        std::string model = arg("--model", "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx");
        std::string write_ref_path = arg("--write-reference", "");
        std::string ref_path = arg("--reference", "first_steps/bench/utilization/results/recheck/reference.bin");

        if (!write_ref_path.empty()) {
            Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "write_ref");
            Engine e(env, model, "bfc", 0, 250, 20);
            Reference generated(kInputVariants);
            for (int s = 0; s < kInputVariants; ++s) {
                auto outs = e.run(s);
                generated[s].resize(outs.size());
                for (size_t t = 0; t < outs.size(); ++t) {
                    auto info = outs[t].GetTensorTypeAndShapeInfo();
                    const float* ptr = outs[t].GetTensorData<float>();
                    generated[s][t].assign(ptr, ptr + info.GetElementCount());
                }
            }
            write_reference(write_ref_path, generated);
            double cross_diff = -1.0;
            std::string cloudlab_ref = arg("--compare-cloudlab", "");
            if (!cloudlab_ref.empty()) {
                auto cl = read_reference(cloudlab_ref);
                cross_diff = 0.0;
                for (int s = 0; s < kInputVariants; ++s) {
                    for (size_t t = 0; t < generated[s].size(); ++t) {
                        for (size_t i = 0; i < generated[s][t].size(); ++i) {
                            cross_diff = std::max(cross_diff, double(std::abs(generated[s][t][i] - cl[s][t][i])));
                        }
                    }
                }
            }
            std::cout << "{\"wrote_reference\":\"" << write_ref_path << "\",\"cross_arch_max_abs_diff\":" << cross_diff << "}\n";
            return 0;
        }

        std::string policy = arg("--policy", "bfc");
        if (policy != "bfc" && policy != "pool_explicit" && policy != "pool_auto" && policy != "direct_trim")
            throw std::runtime_error("unknown policy: " + policy);
        std::string regime = arg("--regime", "burst");
        int frames = std::stoi(arg("--frames", "120"));
        int warmup = std::stoi(arg("--warmup", "10"));
        int burst = std::stoi(arg("--burst", "10"));
        int pause_ms = std::stoi(arg("--pause-ms", "1000"));
        int budget_mib = std::stoi(arg("--budget-mib", "24"));
        int idle_ms = std::stoi(arg("--idle-ms", "250"));
        int poll_ms = std::stoi(arg("--poll-ms", "20"));
        if (frames < 1 || warmup < 0 || burst < 1 || pause_ms < 0 || budget_mib < 0 || idle_ms < 1 || poll_ms < 1)
            throw std::runtime_error("invalid protocol argument");

        bool core_pinned = configure_thread_affinity_or_qos(2);
        auto refs = read_reference(ref_path);
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "auto_idle");
        Engine e(env, model, policy, size_t(budget_mib) << 20, idle_ms, poll_ms);

        auto schedule = build_schedule(regime, frames, burst, pause_ms);
        std::map<int, PauseEvent> pause_at;
        for (const auto& ev : schedule) pause_at[ev.frame_idx] = ev;

        double max_error = 0.0;
        for (int i = 0; i < warmup; ++i) {
            if (e.allocator && e.allocator->ctrl) e.allocator->ctrl->mark_frame_start();
            auto o = e.run(i % kInputVariants);
            verify(o, refs[i % kInputVariants], max_error);
            o.clear();
            if (e.allocator && e.allocator->ctrl) e.allocator->ctrl->mark_frame_end();
        }
        // Reset cleanly before measurement window if explicit or auto policy
        if (e.allocator && e.allocator->ctrl) {
            if (policy == "pool_auto") std::this_thread::sleep_for(std::chrono::milliseconds(idle_ms + 80));
            else if (policy == "pool_explicit") e.allocator->ctrl->explicit_reclaim();
        } else if (policy == "direct_trim") {
            trim_system_allocator();
        }

        const size_t base_reclaims = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->total_reclaims() : 0;
        const double base_reclaim_ms = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->reclaim_ms_total() : 0.0;

        std::vector<double> latency;
        std::vector<double> resume_long;
        std::vector<double> resume_jitter;
        double byte_seconds = 0.0;
        size_t idle_private = 0;
        size_t direct_reclaims = 0;
        double direct_reclaim_ms = 0.0;
        size_t false_reclaims = 0;
        double pool_idle_util = 0.0;

        std::vector<Ort::Value> held;
        int held_seed = 0;
        bool prev_was_long_pause = false;
        bool prev_was_jitter = false;

        rusage before{};
        getrusage(RUSAGE_SELF, &before);

        for (int i = 0; i < frames; ++i) {
            size_t pre_auto = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->auto_reclaims() : 0;
            if (e.allocator && e.allocator->ctrl) e.allocator->ctrl->mark_frame_start();

            auto t0 = std::chrono::steady_clock::now();
            auto o = e.run(i % kInputVariants);
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

            verify(o, refs[i % kInputVariants], max_error);
            if (!held.empty()) verify(held, refs[held_seed], max_error);

            auto t1 = std::chrono::steady_clock::now();
            if (i % 11 == 5) held.clear();
            if (i % 11 == 0) {
                held = std::move(o);
                held_seed = i % kInputVariants;
            }
            o.clear();
            if (policy == "direct_trim") {
                auto tr0 = std::chrono::steady_clock::now();
                trim_system_allocator();
                direct_reclaim_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tr0).count();
                ++direct_reclaims;
            }
            sec += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            if (e.allocator && e.allocator->ctrl) e.allocator->ctrl->mark_frame_end();

            size_t post_auto = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->auto_reclaims() : 0;
            if (post_auto > pre_auto) false_reclaims += (post_auto - pre_auto);

            latency.push_back(sec);
            if (prev_was_long_pause) {
                resume_long.push_back(sec * 1000.0);
                prev_was_long_pause = false;
            }
            if (prev_was_jitter) {
                resume_jitter.push_back(sec * 1000.0);
                prev_was_jitter = false;
            }

            auto s_active = take_process_snapshot();
            byte_seconds += s_active.private_bytes * sec;

            auto it = pause_at.find(i);
            if (it != pause_at.end()) {
                const PauseEvent& ev = it->second;
                size_t before_pause_auto = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->auto_reclaims() : 0;
                if (policy == "pool_explicit" && e.allocator && e.allocator->ctrl) {
                    e.allocator->ctrl->explicit_reclaim();
                }
                auto s_pre_sleep = take_process_snapshot();
                std::this_thread::sleep_for(std::chrono::milliseconds(ev.pause_ms));
                auto s_end_sleep = take_process_snapshot();

                size_t after_pause_auto = (e.allocator && e.allocator->ctrl) ? e.allocator->ctrl->auto_reclaims() : 0;
                if (!ev.is_long_idle && after_pause_auto > before_pause_auto) {
                    false_reclaims += (after_pause_auto - before_pause_auto);
                }

                double sleep_s = ev.pause_ms / 1000.0;
                byte_seconds += 0.5 * (double(s_pre_sleep.private_bytes) + double(s_end_sleep.private_bytes)) * sleep_s;
                if (ev.is_long_idle) {
                    idle_private = s_end_sleep.private_bytes;
                    if (e.allocator && e.allocator->ctrl) {
                        auto pg = e.allocator->ctrl->sample_pages();
                        pool_idle_util = pg.resident ? double(pg.resident_payload) / double(pg.resident) : 0.0;
                    }
                    prev_was_long_pause = true;
                } else {
                    prev_was_jitter = true;
                }
            }
        }

        if (!held.empty()) verify(held, refs[held_seed], max_error);
        rusage after{};
        getrusage(RUSAGE_SELF, &after);
        long faults = after.ru_minflt - before.ru_minflt;

        if (idle_private == 0) {
            idle_private = take_process_snapshot().private_bytes;
            if (e.allocator && e.allocator->ctrl) {
                auto pg = e.allocator->ctrl->sample_pages();
                pool_idle_util = pg.resident ? double(pg.resident_payload) / double(pg.resident) : 0.0;
            }
        }

        const size_t total_reclaims = (e.allocator && e.allocator->ctrl)
                                          ? (e.allocator->ctrl->total_reclaims() - base_reclaims)
                                          : direct_reclaims;
        const double total_reclaim_ms = (e.allocator && e.allocator->ctrl)
                                            ? (e.allocator->ctrl->reclaim_ms_total() - base_reclaim_ms)
                                            : direct_reclaim_ms;

        double total_sec = std::accumulate(latency.begin(), latency.end(), 0.0);
        std::sort(latency.begin(), latency.end());
        std::sort(resume_long.begin(), resume_long.end());
        std::sort(resume_jitter.begin(), resume_jitter.end());

        double p99 = latency[std::min(latency.size() - 1, size_t(std::ceil(latency.size() * 0.99) - 1))] * 1000.0;
        double res_med = resume_long.empty() ? 0.0 : resume_long[resume_long.size() / 2];
        double jit_med = resume_jitter.empty() ? 0.0 : resume_jitter[resume_jitter.size() / 2];

        std::cout << "{"
                  << "\"policy\":\"" << policy << "\","
                  << "\"regime\":\"" << regime << "\","
                  << "\"budget_mib\":" << budget_mib << ","
                  << "\"idle_ms\":" << idle_ms << ","
                  << "\"poll_ms\":" << poll_ms << ","
                  << "\"frames\":" << frames << ","
                  << "\"warmup\":" << warmup << ","
                  << "\"os_page_size\":" << os_page_size() << ","
                  << "\"core_pinned\":" << (core_pinned ? "true" : "false") << ","
                  << "\"outputs_verified\":true,"
                  << "\"retained_outputs_verified\":true,"
                  << "\"max_abs_error\":" << max_error << ","
                  << "\"fps\":" << (frames / total_sec) << ","
                  << "\"p99_ms\":" << p99 << ","
                  << "\"resume_median_ms\":" << res_med << ","
                  << "\"jitter_resume_median_ms\":" << jit_med << ","
                  << "\"pause_reclaim_ms_total\":" << total_reclaim_ms << ","
                  << "\"reclaim_calls\":" << total_reclaims << ","
                  << "\"false_reclaims\":" << false_reclaims << ","
                  << "\"minor_faults\":" << faults << ","
                  << "\"idle_private_bytes\":" << idle_private << ","
                  << "\"estimated_private_byte_seconds\":" << byte_seconds << ","
                  << "\"pool_payload_utilization\":" << pool_idle_util
                  << "}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
