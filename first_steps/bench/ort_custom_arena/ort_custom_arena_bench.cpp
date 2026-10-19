#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <algorithm>
#include <pthread.h>
#include <sched.h>
#include <fstream>

#include "onnxruntime_cxx_api.h"
#include "onnxruntime_session_options_config_keys.h"
#include "ort_double_buffered_arena.hpp"
#include "../../tools/adversarial/adversarial_heap.hpp"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

using namespace factory_cv;
using namespace adversarial;

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_frames = 200;
    size_t warmup_frames = 15;
    bool output_json = false;
    std::string allocator_name = "default";
    std::string mode = "default"; // "default" or "ort_custom_arena"
    int pinned_cpu_core = 0;

    int adv_level = 0; // 0=Adv0 (Clean), 1=Adv1, 3=Adv3, 10=Adv10
    double multiplier = -1.0;
    double occupancy = 0.66;

    static struct option long_options[] = {
        {"model", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"name", required_argument, 0, 'n'},
        {"mode", required_argument, 0, 'o'},
        {"core", required_argument, 0, 'c'},
        {"adv", required_argument, 0, 'a'},
        {"multiplier", required_argument, 0, 'u'},
        {"occupancy", required_argument, 0, 'p'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:w:n:o:c:a:u:p:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'n': allocator_name = optarg; break;
            case 'o': mode = optarg; break;
            case 'c': pinned_cpu_core = std::stoi(optarg); break;
            case 'a': adv_level = std::stoi(optarg); break;
            case 'u': multiplier = std::stod(optarg); break;
            case 'p': occupancy = std::stod(optarg); break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    if (multiplier < 0.0) {
        multiplier = static_cast<double>(adv_level);
    }

    // 0. CPU Core Pinning
    if (pinned_cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(pinned_cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    // 1. ISMM '24 ADVERSARIAL HEAP PRECONDITIONING
    AdversarialPreconditioner::precondition(multiplier, occupancy);

    const auto& api = Ort::GetApi();
    OrtEnv* c_env = nullptr;
    auto status = api.CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ort_arena_bench", &c_env);
    if (status) {
        std::cerr << "CreateEnv failed: " << api.GetErrorMessage(status) << std::endl;
        return 1;
    }

    std::unique_ptr<OrtDoubleBufferedArena> custom_arena;
    if (mode == "ort_custom_arena") {
        custom_arena = std::make_unique<OrtDoubleBufferedArena>(64, 128);
        status = api.RegisterAllocator(c_env, custom_arena.get());
        if (status) {
            std::cerr << "RegisterAllocator failed: " << api.GetErrorMessage(status) << std::endl;
            api.ReleaseStatus(status);
            api.ReleaseEnv(c_env);
            return 1;
        }
    }

    Ort::Env env(c_env);
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetInterOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (mode == "ort_custom_arena") {
        session_options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
        session_options.DisableCpuMemArena(); // Route to registered OrtDoubleBufferedArena
        custom_arena->set_mode(OrtDoubleBufferedArena::AllocMode::INITIALIZER);
    }

    Ort::Session session(env, model_path.c_str(), session_options);

    Ort::AllocatorWithDefaultOptions default_allocator;
    auto input_name_ptr = session.GetInputNameAllocated(0, default_allocator);
    auto output_name_ptr = session.GetOutputNameAllocated(0, default_allocator);
    const char* input_names[] = { input_name_ptr.get() };
    const char* output_names[] = { output_name_ptr.get() };

    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    std::vector<float> input_tensor_values(input_elements, 0.5f);

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    // Warmup Phase
    for (size_t i = 0; i < warmup_frames; ++i) {
        if (mode == "ort_custom_arena") {
            custom_arena->begin_frame();
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor_values.data(), input_elements,
            input_shape.data(), input_shape.size()
        );

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1
        );
        (void)output_tensors;

        if (mode == "ort_custom_arena") {
            custom_arena->end_frame();
        }
    }

    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::high_resolution_clock::now();

    // Main Benchmark Loop
    for (size_t i = 0; i < num_frames; ++i) {
        if (mode == "ort_custom_arena") {
            custom_arena->begin_frame();
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor_values.data(), input_elements,
            input_shape.data(), input_shape.size()
        );

        auto t0 = std::chrono::high_resolution_clock::now();
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1
        );
        auto t1 = std::chrono::high_resolution_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencies_ms.push_back(elapsed_ms);

        (void)output_tensors;

        if (mode == "ort_custom_arena") {
            custom_arena->end_frame();
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();

    MemorySnapshot final_snap = TelemetryCollector::sample();
    double final_peak_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double final_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double final_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);
    double warmup_rss_mb = TelemetryCollector::to_mb(warmup_snap.rss_kb);
    double steady_delta_mb = final_rss_mb - warmup_rss_mb;
    long minor_faults = final_snap.minor_faults - warmup_snap.minor_faults;

    std::vector<double> sorted = latencies_ms;
    std::sort(sorted.begin(), sorted.end());
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double mean_ms = sum / sorted.size();
    double p50_ms = sorted[sorted.size() * 50 / 100];
    double p95_ms = sorted[sorted.size() * 95 / 100];
    double p99_ms = sorted[sorted.size() * 99 / 100];
    double min_ms = sorted.front();
    double max_ms = sorted.back();
    double fps = static_cast<double>(num_frames) / total_sec;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"mode\": \"" << mode << "\",\n"
                  << "  \"adv_level\": " << adv_level << ",\n"
                  << "  \"multiplier\": " << multiplier << ",\n"
                  << "  \"occupancy\": " << occupancy << ",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"peak_rss_mb\": " << std::fixed << std::setprecision(2) << final_peak_mb << ",\n"
                  << "  \"settled_rss_mb\": " << final_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << final_dirty_mb << ",\n"
                  << "  \"steady_delta_mb\": " << steady_delta_mb << ",\n"
                  << "  \"minor_faults\": " << minor_faults << ",\n"
                  << "  \"latency_mean_ms\": " << std::setprecision(3) << mean_ms << ",\n"
                  << "  \"latency_p50_ms\": " << p50_ms << ",\n"
                  << "  \"latency_p95_ms\": " << p95_ms << ",\n"
                  << "  \"latency_p99_ms\": " << p99_ms << ",\n"
                  << "  \"latency_min_ms\": " << min_ms << ",\n"
                  << "  \"latency_max_ms\": " << max_ms << ",\n"
                  << "  \"fps\": " << std::setprecision(2) << fps;
        if (custom_arena) {
            std::cout << ",\n  \"weights_bytes\": " << custom_arena->get_weight_bytes() << ",\n"
                      << "  \"transient_bytes\": " << custom_arena->get_transient_bytes() << ",\n"
                      << "  \"peak_frame_bytes\": " << custom_arena->get_peak_frame_bytes() << ",\n"
                      << "  \"noop_frees\": " << custom_arena->get_noop_frees();
        }
        std::cout << "\n}\n";
    } else {
        std::cout << "\n======================================================\n";
        std::cout << " Benchmark Results: " << allocator_name << " (" << mode << ") Adv" << adv_level << "\n";
        std::cout << "======================================================\n";
        std::cout << " Frames:          " << num_frames << "\n";
        std::cout << " Mean Latency:    " << std::fixed << std::setprecision(3) << mean_ms << " ms (" << fps << " FPS)\n";
        std::cout << " Median (p50):    " << p50_ms << " ms\n";
        std::cout << " 99th Percentile: " << p99_ms << " ms\n";
        std::cout << " Peak RSS:        " << final_peak_mb << " MB\n";
        std::cout << " Settled RSS:     " << final_rss_mb << " MB\n";
        std::cout << " Minor Faults:    " << minor_faults << "\n";
        if (custom_arena) {
            std::cout << " Weights Stored:  " << (custom_arena->get_weight_bytes() / (1024.0 * 1024.0)) << " MB\n";
            std::cout << " Peak Frame Tens: " << (custom_arena->get_peak_frame_bytes() / (1024.0 * 1024.0)) << " MB\n";
            std::cout << " No-Op Frees:     " << custom_arena->get_noop_frees() << "\n";
        }
        std::cout << "======================================================\n";
    }

    return 0;
}
