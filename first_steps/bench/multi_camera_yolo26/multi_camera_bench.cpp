#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <algorithm>
#include <mutex>

#include "onnxruntime_cxx_api.h"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

using namespace factory_cv;

struct ThreadResult {
    std::vector<double> latencies_ms;
    double thread_fps = 0.0;
};

void camera_worker(int thread_id,
                   const std::string& model_path,
                   Ort::Env& env,
                   size_t num_frames,
                   size_t warmup_frames,
                   ThreadResult& result) {
    try {
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        Ort::Session session(env, model_path.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name_ptr = session.GetInputNameAllocated(0, allocator);
        auto output_name_ptr = session.GetOutputNameAllocated(0, allocator);

        const char* input_names[] = { input_name_ptr.get() };
        const char* output_names[] = { output_name_ptr.get() };

        std::vector<int64_t> input_shape = {1, 3, 640, 640};
        size_t input_elements = 1 * 3 * 640 * 640;
        std::vector<float> input_tensor_values(input_elements);
        for (size_t i = 0; i < input_elements; ++i) {
            input_tensor_values[i] = static_cast<float>((i + thread_id * 17) % 256) / 255.0f;
        }

        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            input_tensor_values.data(),
            input_tensor_values.size(),
            input_shape.data(),
            input_shape.size()
        );

        // Warmup
        for (size_t w = 0; w < warmup_frames; ++w) {
            auto out = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
            (void)out;
        }

        result.latencies_ms.reserve(num_frames);
        auto worker_start = std::chrono::high_resolution_clock::now();

        // Main inference stream
        for (size_t f = 0; f < num_frames; ++f) {
            auto t0 = std::chrono::high_resolution_clock::now();

            auto out = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

            float* data = out[0].GetTensorMutableData<float>();
            if (data) {
                volatile float touch = data[0];
                (void)touch;
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
            result.latencies_ms.push_back(lat);
        }

        auto worker_end = std::chrono::high_resolution_clock::now();
        double worker_sec = std::chrono::duration<double>(worker_end - worker_start).count();
        result.thread_fps = static_cast<double>(num_frames) / worker_sec;

    } catch (const std::exception& ex) {
        std::cerr << "Worker " << thread_id << " error: " << ex.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_threads = 4;
    size_t frames_per_thread = 200;
    size_t warmup_frames = 10;
    bool output_json = false;
    std::string allocator_name = "default";

    static struct option long_options[] = {
        {"model", required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"name", required_argument, 0, 'n'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:t:f:w:n:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 't': num_threads = std::stoul(optarg); break;
            case 'f': frames_per_thread = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'n': allocator_name = optarg; break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "multi_camera_yolo26");

    MemorySnapshot init_snap = TelemetryCollector::sample();

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    std::vector<ThreadResult> results(num_threads);

    auto global_start = std::chrono::high_resolution_clock::now();

    for (size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back(camera_worker,
                             static_cast<int>(t),
                             std::cref(model_path),
                             std::ref(env),
                             frames_per_thread,
                             warmup_frames,
                             std::ref(results[t]));
    }

    for (auto& w : workers) {
        w.join();
    }

    auto global_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(global_end - global_start).count();

    MemorySnapshot final_snap = TelemetryCollector::sample();

    // Aggregate statistics
    std::vector<double> all_latencies;
    for (const auto& r : results) {
        all_latencies.insert(all_latencies.end(), r.latencies_ms.begin(), r.latencies_ms.end());
    }
    LatencyStats lstats = LatencyStats::calculate(all_latencies);

    size_t total_processed_frames = num_threads * frames_per_thread;
    double aggregate_fps = static_cast<double>(total_processed_frames) / total_sec;

    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double private_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);
    long minor_faults = final_snap.minor_faults - init_snap.minor_faults;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"threads\": " << num_threads << ",\n"
                  << "  \"frames_per_thread\": " << frames_per_thread << ",\n"
                  << "  \"total_frames\": " << total_processed_frames << ",\n"
                  << "  \"aggregate_fps\": " << aggregate_fps << ",\n"
                  << "  \"peak_rss_mb\": " << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << private_dirty_mb << ",\n"
                  << "  \"minor_faults\": " << minor_faults << ",\n"
                  << "  \"latency\": {\n"
                  << "    \"mean_ms\": " << lstats.mean_ms << ",\n"
                  << "    \"p50_ms\": " << lstats.p50_ms << ",\n"
                  << "    \"p90_ms\": " << lstats.p90_ms << ",\n"
                  << "    \"p99_ms\": " << lstats.p99_ms << ",\n"
                  << "    \"max_ms\": " << lstats.max_ms << "\n"
                  << "  }\n"
                  << "}" << std::endl;
    } else {
        std::cout << "\n=======================================================\n"
                  << " MULTI-CAMERA YOLO26n BENCHMARK: " << allocator_name << "\n"
                  << "=======================================================\n"
                  << " Concurrent Streams : " << num_threads << " camera threads\n"
                  << " Total Frames Run   : " << total_processed_frames << "\n"
                  << " Aggregate Rate     : " << std::fixed << std::setprecision(1) << aggregate_fps << " FPS\n"
                  << " Latency (p50 / p99): " << std::setprecision(3) << lstats.p50_ms << " ms / " << lstats.p99_ms << " ms\n"
                  << " Peak RSS (VmHWM)   : " << std::setprecision(2) << peak_rss_mb << " MB\n"
                  << " Settled RSS (VmRSS): " << settled_rss_mb << " MB\n"
                  << " Private Dirty RAM  : " << private_dirty_mb << " MB\n"
                  << " Minor Page Faults  : " << minor_faults << "\n"
                  << "=======================================================\n" << std::endl;
    }

    return 0;
}
