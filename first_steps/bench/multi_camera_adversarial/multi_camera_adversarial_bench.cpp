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
#include <atomic>
#include <pthread.h>
#include <sched.h>

#include "onnxruntime_cxx_api.h"
#include "../../tools/adversarial/adversarial_heap.hpp"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

using namespace factory_cv;
using namespace adversarial;

struct ThreadTelemetry {
    std::vector<double> latencies_ms;
    double thread_fps = 0.0;
};

void camera_worker(int thread_id,
                   const std::string& model_path,
                   Ort::Env& env,
                   size_t num_frames,
                   size_t warmup_frames,
                   int adv_level,
                   double multiplier,
                   double occupancy,
                   std::atomic<bool>& start_barrier,
                   std::atomic<int>& ready_count,
                   ThreadTelemetry& telemetry) {
    try {
        // Pin to CPU core
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(thread_id % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

        // Precondition local heap for this thread
        if (adv_level > 0 && multiplier > 0.0) {
            AdversarialPreconditioner::precondition_thread(
                multiplier, occupancy,
                AdversarialPreconditioner::DEFAULT_N_PEAK,
                42 + thread_id * 10007
            );
        }

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

        telemetry.latencies_ms.reserve(num_frames);

        // Signal ready and wait for barrier
        ready_count++;
        while (!start_barrier.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

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
            telemetry.latencies_ms.push_back(lat);
        }

        auto worker_end = std::chrono::high_resolution_clock::now();
        double worker_sec = std::chrono::duration<double>(worker_end - worker_start).count();
        telemetry.thread_fps = static_cast<double>(num_frames) / worker_sec;

    } catch (const std::exception& ex) {
        std::cerr << "Worker " << thread_id << " error: " << ex.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_threads = 4;
    size_t frames_per_thread = 50;
    size_t warmup_frames = 5;
    bool output_json = false;
    std::string allocator_name = "default";

    int adv_level = 0; // 0=Adv0 (Clean), 1=Adv1, 3=Adv3, 10=Adv10
    double multiplier = -1.0;
    double occupancy = 0.66;

    static struct option long_options[] = {
        {"model", required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"name", required_argument, 0, 'n'},
        {"adv", required_argument, 0, 'a'},
        {"multiplier", required_argument, 0, 'u'},
        {"occupancy", required_argument, 0, 'p'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:t:f:w:n:a:u:p:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 't': num_threads = std::stoul(optarg); break;
            case 'f': frames_per_thread = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'n': allocator_name = optarg; break;
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

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "multi_camera_adversarial");

    std::vector<std::thread> workers;
    std::vector<ThreadTelemetry> telemetries(num_threads);
    std::atomic<bool> start_barrier(false);
    std::atomic<int> ready_count(0);

    workers.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back(camera_worker,
                             t,
                             std::ref(model_path),
                             std::ref(env),
                             frames_per_thread,
                             warmup_frames,
                             adv_level,
                             multiplier,
                             occupancy,
                             std::ref(start_barrier),
                             std::ref(ready_count),
                             std::ref(telemetries[t]));
    }

    // Wait until all workers finish warmup and preconditioning
    while (ready_count.load() < static_cast<int>(num_threads)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    MemorySnapshot warmup_snap = TelemetryCollector::sample();
    auto global_start = std::chrono::high_resolution_clock::now();

    // Release all workers simultaneously
    start_barrier.store(true, std::memory_order_release);

    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    auto global_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(global_end - global_start).count();

    MemorySnapshot peak_snap = TelemetryCollector::sample();

    // Settle pause to observe memory retention
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    MemorySnapshot settled_snap = TelemetryCollector::sample();

    // Aggregate statistics
    std::vector<double> all_latencies;
    double sum_fps = 0.0;
    for (size_t t = 0; t < num_threads; ++t) {
        all_latencies.insert(all_latencies.end(), telemetries[t].latencies_ms.begin(), telemetries[t].latencies_ms.end());
        sum_fps += telemetries[t].thread_fps;
    }

    std::sort(all_latencies.begin(), all_latencies.end());
    double mean_lat = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
    double p50_lat = all_latencies[static_cast<size_t>(all_latencies.size() * 0.50)];
    double p95_lat = all_latencies[static_cast<size_t>(all_latencies.size() * 0.95)];
    double p99_lat = all_latencies[static_cast<size_t>(all_latencies.size() * 0.99)];
    double max_lat = all_latencies.back();

    double peak_rss_mb = TelemetryCollector::to_mb(peak_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(settled_snap.rss_kb);
    double priv_dirty_mb = TelemetryCollector::to_mb(settled_snap.private_dirty_kb);
    long minor_faults = peak_snap.minor_faults - warmup_snap.minor_faults;
    size_t total_frames = num_threads * frames_per_thread;
    double aggregate_fps = static_cast<double>(total_frames) / total_sec;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"adv_level\": " << adv_level << ",\n"
                  << "  \"multiplier\": " << multiplier << ",\n"
                  << "  \"occupancy\": " << occupancy << ",\n"
                  << "  \"threads\": " << num_threads << ",\n"
                  << "  \"frames_per_thread\": " << frames_per_thread << ",\n"
                  << "  \"total_frames\": " << total_frames << ",\n"
                  << "  \"peak_rss_mb\": " << std::fixed << std::setprecision(2) << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << priv_dirty_mb << ",\n"
                  << "  \"minor_faults\": " << minor_faults << ",\n"
                  << "  \"latency_mean_ms\": " << std::setprecision(3) << mean_lat << ",\n"
                  << "  \"latency_p50_ms\": " << p50_lat << ",\n"
                  << "  \"latency_p95_ms\": " << p95_lat << ",\n"
                  << "  \"latency_p99_ms\": " << p99_lat << ",\n"
                  << "  \"latency_max_ms\": " << max_lat << ",\n"
                  << "  \"aggregate_fps\": " << std::setprecision(2) << aggregate_fps << ",\n"
                  << "  \"sum_thread_fps\": " << sum_fps << "\n"
                  << "}\n";
    } else {
        std::cout << "\n======================================================\n";
        std::cout << " 4-Camera Adversarial Benchmark: " << allocator_name << " Adv" << adv_level << "\n";
        std::cout << "======================================================\n";
        std::cout << " Threads:          " << num_threads << " cameras\n";
        std::cout << " Total Frames:     " << total_frames << "\n";
        std::cout << " Aggregate FPS:    " << std::fixed << std::setprecision(2) << aggregate_fps << "\n";
        std::cout << " Mean Latency:     " << std::setprecision(3) << mean_lat << " ms\n";
        std::cout << " p50 Latency:      " << p50_lat << " ms\n";
        std::cout << " p99 Latency:      " << p99_lat << " ms\n";
        std::cout << " Peak RSS:         " << peak_rss_mb << " MB\n";
        std::cout << " Settled RSS:      " << settled_rss_mb << " MB\n";
        std::cout << " Private Dirty:    " << priv_dirty_mb << " MB\n";
        std::cout << " Minor Faults:     " << minor_faults << "\n";
        std::cout << "======================================================\n";
    }

    return 0;
}
