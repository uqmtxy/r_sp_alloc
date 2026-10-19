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

#include "onnxruntime_cxx_api.h"
// #include "../yolo26n_pipeline/onnxruntime/include/onnxruntime_cxx_api.h"
#include "../../tools/adversarial/adversarial_heap.hpp"
#include "../fd_arena_yolo26/fd_arena.hpp"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

using namespace factory_cv;
using namespace adversarial;
using namespace fd_arena;

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_frames = 500;
    size_t warmup_frames = 20;
    bool output_json = false;
    std::string allocator_name = "default";
    std::string mode = "dynamic"; // "dynamic" or "fd_arena"
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
    // Pre-condition the heap prior to benchmark execution to eliminate clean-heap bias
    AdversarialPreconditioner::precondition(multiplier, occupancy);

    // 2. Initialize ONNX Runtime Session
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "adversarial_yolo26");
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetInterOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    Ort::Session session(env, model_path.c_str(), session_options);

    Ort::AllocatorWithDefaultOptions default_allocator;
    auto input_name_ptr = session.GetInputNameAllocated(0, default_allocator);
    auto output_name_ptr = session.GetOutputNameAllocated(0, default_allocator);

    const char* input_names[] = { input_name_ptr.get() };
    const char* output_names[] = { output_name_ptr.get() };

    // Tensor shape: [1, 3, 640, 640]
    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    size_t tensor_bytes = input_elements * sizeof(float);

    std::unique_ptr<FDArenaManager> fd_arena_mgr;
    if (mode == "fd_arena") {
        fd_arena_mgr = std::make_unique<FDArenaManager>(32 * 1024 * 1024);
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // 3. WARMUP PHASE (20 frames)
    for (size_t i = 0; i < warmup_frames; ++i) {
        float* input_data = nullptr;
        if (mode == "fd_arena") {
            input_data = static_cast<float*>(fd_arena_mgr->current_arena().allocate(tensor_bytes));
        } else {
            input_data = static_cast<float*>(malloc(tensor_bytes));
        }
        for (size_t k = 0; k < input_elements; ++k) {
            input_data[k] = static_cast<float>((k + i) % 256) / 255.0f;
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_data, input_elements,
            input_shape.data(), input_shape.size()
        );

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1
        );
        (void)output_tensors;

        if (mode == "fd_arena") {
            fd_arena_mgr->advance_frame(false);
        } else {
            free(input_data);
        }
    }

    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    // 4. MAIN INFERENCE LOOP
    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_frames; ++i) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        float* input_data = nullptr;
        if (mode == "fd_arena") {
            input_data = static_cast<float*>(fd_arena_mgr->current_arena().allocate(tensor_bytes));
        } else {
            input_data = static_cast<float*>(malloc(tensor_bytes));
        }

        for (size_t k = 0; k < input_elements; ++k) {
            input_data[k] = static_cast<float>((k + i) % 256) / 255.0f;
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_data, input_elements,
            input_shape.data(), input_shape.size()
        );

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1
        );
        (void)output_tensors;

        if (mode == "fd_arena") {
            fd_arena_mgr->advance_frame(false);
        } else {
            free(input_data);
        }

        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_lat = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        latencies_ms.push_back(frame_lat);
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();

    MemorySnapshot final_snap = TelemetryCollector::sample();

    // 5. STATISTICAL REDUCTIONS
    std::sort(latencies_ms.begin(), latencies_ms.end());
    double mean_lat = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0) / latencies_ms.size();
    double p50_lat = latencies_ms[static_cast<size_t>(num_frames * 0.50)];
    double p90_lat = latencies_ms[static_cast<size_t>(num_frames * 0.90)];
    double p99_lat = latencies_ms[static_cast<size_t>(num_frames * 0.99)];
    double max_lat = latencies_ms.back();
    double fps = static_cast<double>(num_frames) / total_sec;

    double final_peak_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double final_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double final_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);
    double warmup_rss_mb = TelemetryCollector::to_mb(warmup_snap.rss_kb);
    double steady_delta_mb = final_rss_mb - warmup_rss_mb;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"mode\": \"" << mode << "\",\n"
                  << "  \"adv_level\": " << adv_level << ",\n"
                  << "  \"multiplier\": " << multiplier << ",\n"
                  << "  \"occupancy\": " << occupancy << ",\n"
                  << "  \"num_frames\": " << num_frames << ",\n"
                  << "  \"peak_rss_mb\": " << std::fixed << std::setprecision(2) << final_peak_mb << ",\n"
                  << "  \"settled_rss_mb\": " << final_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << final_dirty_mb << ",\n"
                  << "  \"warmup_rss_mb\": " << warmup_rss_mb << ",\n"
                  << "  \"steady_delta_mb\": " << steady_delta_mb << ",\n"
                  << "  \"minor_faults\": " << (final_snap.minor_faults - warmup_snap.minor_faults) << ",\n"
                  << "  \"latency_mean_ms\": " << mean_lat << ",\n"
                  << "  \"latency_p50_ms\": " << p50_lat << ",\n"
                  << "  \"latency_p90_ms\": " << p90_lat << ",\n"
                  << "  \"latency_p99_ms\": " << p99_lat << ",\n"
                  << "  \"latency_max_ms\": " << max_lat << ",\n"
                  << "  \"fps\": " << fps << "\n"
                  << "}" << std::endl;
    } else {
        std::cout << "\n=== Adversarial Allocation Results (" << allocator_name << " Adv" << adv_level << ") ===\n"
                  << "Frames Evaluated : " << num_frames << "\n"
                  << "Adv Level        : Adv" << adv_level << " (Multiplier=" << multiplier << ", Occ=" << occupancy << ")\n"
                  << "Peak RSS (VmHWM) : " << final_peak_mb << " MB\n"
                  << "Settled RSS (RSS): " << final_rss_mb << " MB\n"
                  << "Private Dirty RAM: " << final_dirty_mb << " MB\n"
                  << "Steady-State Drift: " << steady_delta_mb << " MB\n"
                  << "Minor Page Faults: " << (final_snap.minor_faults - warmup_snap.minor_faults) << "\n"
                  << "Latency (Mean)   : " << mean_lat << " ms\n"
                  << "Latency (p50)    : " << p50_lat << " ms\n"
                  << "Latency (p99)    : " << p99_lat << " ms\n"
                  << "Throughput       : " << fps << " FPS\n";
    }

    AdversarialPreconditioner::cleanup();
    return 0;
}
