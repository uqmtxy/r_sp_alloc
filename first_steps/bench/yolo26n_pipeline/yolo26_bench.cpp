#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <algorithm>

#include "onnxruntime_cxx_api.h"
#include "../factory_cv_pipeline/memory_telemetry.hpp"
#include "../../tools/lost_bytes_inspector/page_inspector.hpp"

using namespace factory_cv;
using namespace lost_bytes;

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_frames = 1000;
    size_t warmup_frames = 50;
    bool output_json = false;
    std::string allocator_name = "default";

    static struct option long_options[] = {
        {"model", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"name", required_argument, 0, 'n'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:w:n:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'n': allocator_name = optarg; break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    // 1. Initialize ONNX Runtime Session
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yolo26n_bench");
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

    // 2. Prepare Input Tensor: [1, 3, 640, 640]
    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    std::vector<float> input_tensor_values(input_elements);
    for (size_t i = 0; i < input_elements; ++i) {
        input_tensor_values[i] = static_cast<float>(i % 256) / 255.0f;
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_tensor_values.data(),
        input_tensor_values.size(),
        input_shape.data(),
        input_shape.size()
    );

    // 3. WARMUP PHASE
    for (size_t i = 0; i < warmup_frames; ++i) {
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1
        );
        (void)output_tensors;
    }

    // Capture memory snapshot right after warmup
    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    // 4. MAIN INFERENCE LOOP
    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (size_t fid = 0; fid < num_frames; ++fid) {
        auto t_start = std::chrono::high_resolution_clock::now();

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1
        );

        // Touch output tensor buffer to guarantee memory physical backing
        float* out_data = output_tensors[0].GetTensorMutableData<float>();
        if (out_data) {
            volatile float dummy = out_data[0];
            (void)dummy;
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        latencies_ms.push_back(lat_ms);
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();
    double fps = static_cast<double>(num_frames) / total_sec;

    // 5. SAMPLE TELEMETRY
    MemorySnapshot final_snap = TelemetryCollector::sample();
    LatencyStats lstats = LatencyStats::calculate(latencies_ms);
    long minor_faults = final_snap.minor_faults - warmup_snap.minor_faults;

    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double private_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"achieved_fps\": " << fps << ",\n"
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
                  << " YOLO26n ONNX RUNTIME C++ BENCHMARK: " << allocator_name << "\n"
                  << "=======================================================\n"
                  << " Frames Processed   : " << num_frames << "\n"
                  << " Achieved Throughput: " << std::fixed << std::setprecision(1) << fps << " FPS\n"
                  << " Latency (p50 / p99): " << std::setprecision(3) << lstats.p50_ms << " ms / " << lstats.p99_ms << " ms\n"
                  << " Peak RSS (VmHWM)   : " << std::setprecision(2) << peak_rss_mb << " MB\n"
                  << " Settled RSS (VmRSS): " << settled_rss_mb << " MB\n"
                  << " Minor Page Faults  : " << minor_faults << "\n"
                  << "=======================================================\n" << std::endl;
    }

    return 0;
}
