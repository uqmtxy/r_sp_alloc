#define _GNU_SOURCE
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>

#include "onnxruntime_cxx_api.h"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

using namespace factory_cv;

struct PreciseLatencyReport {
    double p50_ms = 0.0;
    double p90_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double p999_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double mean_ms = 0.0;
    double stddev_ms = 0.0;

    static PreciseLatencyReport compute(std::vector<double>& latencies) {
        if (latencies.empty()) return {};
        std::sort(latencies.begin(), latencies.end());
        size_t n = latencies.size();

        PreciseLatencyReport r;
        r.min_ms = latencies.front();
        r.max_ms = latencies.back();
        r.p50_ms = latencies[n * 50 / 100];
        r.p90_ms = latencies[n * 90 / 100];
        r.p95_ms = latencies[n * 95 / 100];
        r.p99_ms = latencies[n * 99 / 100];
        r.p999_ms = latencies[std::min(n - 1, n * 999 / 1000)];

        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        r.mean_ms = sum / static_cast<double>(n);

        double sq_sum = 0.0;
        for (double v : latencies) {
            sq_sum += (v - r.mean_ms) * (v - r.mean_ms);
        }
        r.stddev_ms = std::sqrt(sq_sum / static_cast<double>(n));

        return r;
    }
};

bool pin_thread_to_core(int core_id) {
    if (core_id < 0) return true;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
}

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_frames = 500;
    size_t warmup_frames = 50;
    int pin_core = -1;
    int trial_idx = 0;
    bool output_json = false;
    std::string allocator_name = "default";

    static struct option long_options[] = {
        {"model", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"pin-core", required_argument, 0, 'c'},
        {"trial", required_argument, 0, 't'},
        {"name", required_argument, 0, 'n'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:w:c:t:n:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'c': pin_core = std::stoi(optarg); break;
            case 't': trial_idx = std::stoi(optarg); break;
            case 'n': allocator_name = optarg; break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    // 1. Noise Control: Pin thread to dedicated CPU core
    if (pin_core >= 0) {
        if (!pin_thread_to_core(pin_core)) {
            std::cerr << "Warning: Could not pin thread to CPU core " << pin_core << std::endl;
        }
    }

    // 2. Initialize ONNX Runtime Session
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "precise_yolo26");
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetInterOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    Ort::Session session(env, model_path.c_str(), session_options);

    Ort::AllocatorWithDefaultOptions allocator;
    auto in_name = session.GetInputNameAllocated(0, allocator);
    auto out_name = session.GetOutputNameAllocated(0, allocator);
    const char* in_names[] = { in_name.get() };
    const char* out_names[] = { out_name.get() };

    // Input Tensor: [1, 3, 640, 640]
    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    std::vector<float> input_tensor_values(input_elements);
    for (size_t i = 0; i < input_elements; ++i) {
        input_tensor_values[i] = static_cast<float>((i + trial_idx * 31) % 256) / 255.0f;
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_tensor_values.data(),
        input_tensor_values.size(),
        input_shape.data(),
        input_shape.size()
    );

    // Boot memory snapshot (model loaded)
    MemorySnapshot boot_snap = TelemetryCollector::sample();

    // 3. WARMUP PHASE (pre-fault execution provider buffers & stabilize free-lists)
    for (size_t w = 0; w < warmup_frames; ++w) {
        auto out = session.Run(Ort::RunOptions{nullptr}, in_names, &input_tensor, 1, out_names, 1);
        (void)out;
    }

    // Warmup memory snapshot
    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    // 4. MEASURED STEADY-STATE INFERENCE LOOP
    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::steady_clock::now();

    for (size_t f = 0; f < num_frames; ++f) {
        auto t0 = std::chrono::steady_clock::now();

        auto out = session.Run(Ort::RunOptions{nullptr}, in_names, &input_tensor, 1, out_names, 1);

        float* data = out[0].GetTensorMutableData<float>();
        if (data) {
            volatile float touch = data[0];
            (void)touch;
        }

        auto t1 = std::chrono::steady_clock::now();
        double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencies_ms.push_back(lat);
    }

    auto bench_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();
    double fps = static_cast<double>(num_frames) / total_sec;

    // 5. POST-INFERENCE TELEMETRY
    MemorySnapshot final_snap = TelemetryCollector::sample();
    PreciseLatencyReport lrep = PreciseLatencyReport::compute(latencies_ms);

    double boot_rss_mb = TelemetryCollector::to_mb(boot_snap.rss_kb);
    double warmup_rss_mb = TelemetryCollector::to_mb(warmup_snap.rss_kb);
    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double private_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);

    // Delta RSS during steady state = Peak RSS - Warmup RSS
    double steady_state_delta_mb = peak_rss_mb - warmup_rss_mb;
    long steady_state_minor_faults = final_snap.minor_faults - warmup_snap.minor_faults;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"trial\": " << trial_idx << ",\n"
                  << "  \"pinned_core\": " << pin_core << ",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"warmup_frames\": " << warmup_frames << ",\n"
                  << "  \"achieved_fps\": " << fps << ",\n"
                  << "  \"boot_rss_mb\": " << boot_rss_mb << ",\n"
                  << "  \"warmup_rss_mb\": " << warmup_rss_mb << ",\n"
                  << "  \"peak_rss_mb\": " << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << private_dirty_mb << ",\n"
                  << "  \"steady_state_delta_mb\": " << steady_state_delta_mb << ",\n"
                  << "  \"steady_state_minor_faults\": " << steady_state_minor_faults << ",\n"
                  << "  \"latency\": {\n"
                  << "    \"mean_ms\": " << lrep.mean_ms << ",\n"
                  << "    \"stddev_ms\": " << lrep.stddev_ms << ",\n"
                  << "    \"min_ms\": " << lrep.min_ms << ",\n"
                  << "    \"p50_ms\": " << lrep.p50_ms << ",\n"
                  << "    \"p90_ms\": " << lrep.p90_ms << ",\n"
                  << "    \"p95_ms\": " << lrep.p95_ms << ",\n"
                  << "    \"p99_ms\": " << lrep.p99_ms << ",\n"
                  << "    \"p999_ms\": " << lrep.p999_ms << ",\n"
                  << "    \"max_ms\": " << lrep.max_ms << "\n"
                  << "  }\n"
                  << "}" << std::endl;
    } else {
        std::cout << "\n=======================================================\n"
                  << " PRECISE YOLO26n BENCHMARK: " << allocator_name << " (Trial " << trial_idx << ")\n"
                  << "=======================================================\n"
                  << " Pinned Core        : " << pin_core << "\n"
                  << " Steady-State Frames: " << num_frames << " (Warmup: " << warmup_frames << ")\n"
                  << " Achieved Throughput: " << std::fixed << std::setprecision(2) << fps << " FPS\n"
                  << " Latency (Mean±Std) : " << lrep.mean_ms << " ± " << lrep.stddev_ms << " ms\n"
                  << " Latency (p50 / p99): " << lrep.p50_ms << " ms / " << lrep.p99_ms << " ms\n"
                  << " Boot RSS           : " << boot_rss_mb << " MB\n"
                  << " Warmup RSS         : " << warmup_rss_mb << " MB\n"
                  << " Peak RSS (VmHWM)   : " << peak_rss_mb << " MB\n"
                  << " Settled RSS (VmRSS): " << settled_rss_mb << " MB\n"
                  << " Private Dirty RAM  : " << private_dirty_mb << " MB\n"
                  << " Steady-State Delta : " << steady_state_delta_mb << " MB\n"
                  << " Steady-State Faults: " << steady_state_minor_faults << "\n"
                  << "=======================================================\n" << std::endl;
    }

    return 0;
}
