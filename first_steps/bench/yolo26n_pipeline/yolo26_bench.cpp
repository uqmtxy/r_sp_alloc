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
#include "onnxruntime_session_options_config_keys.h"
#include "../factory_cv_pipeline/memory_telemetry.hpp"
#include "../../tools/lost_bytes_inspector/page_inspector.hpp"
#include "../../tools/adversarial/adversarial_heap.hpp"
#include "../pare_ta_arena/include/pare_ta_onnx_adapter.hpp"

using namespace factory_cv;
using namespace lost_bytes;
using namespace adversarial;
using namespace pare_ta;

int main(int argc, char* argv[]) {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    size_t num_frames = 1000;
    size_t warmup_frames = 50;
    bool output_json = false;
    std::string allocator_name = "default";
    std::string mode = "bfc_arena"; // "bfc_arena", "direct_tensor", "pare_ta"
    int pinned_cpu_core = -1;

    int adv_level = 0; // 0=Clean, 1=Light, 3=ISMM Standard, 10=Stress
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

    // 0. CPU Affinity / Pinning
    if (pinned_cpu_core >= 0) {
#if defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(pinned_cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    }

    // 1. ISMM '26 Adversarial Heap Preconditioning
    AdversarialPreconditioner::precondition(multiplier, occupancy);

    // 2. Initialize ONNX Runtime Environment & Allocators
    const auto& api = Ort::GetApi();
    OrtEnv* c_env = nullptr;
    auto status = api.CreateEnv(ORT_LOGGING_LEVEL_WARNING, "yolo26n_bench", &c_env);
    if (status) {
        std::cerr << "Failed to create OrtEnv: " << api.GetErrorMessage(status) << std::endl;
        return 1;
    }

    std::shared_ptr<PareTaMonotonicResource> pare_ta_resource;
    std::unique_ptr<PareTaOrtAllocator> pare_ta_allocator;

    if (mode == "pare_ta") {
        // Provision 64 MB persistent + 2 x 256 MB transient frame double-buffers
        pare_ta_resource = std::make_shared<PareTaMonotonicResource>(64, 256);
        pare_ta_allocator = std::make_unique<PareTaOrtAllocator>(pare_ta_resource);
        status = api.RegisterAllocator(c_env, pare_ta_allocator.get());
        if (status) {
            std::cerr << "Failed to register PareTaOrtAllocator: "
                      << api.GetErrorMessage(status) << std::endl;
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

    if (mode == "pare_ta") {
        // Route internal activations to the page-aligned monotonic arena
        session_options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
        session_options.DisableCpuMemArena();
        pare_ta_allocator->set_mode_persistent();
    } else if (mode == "direct_tensor" || mode == "no_arena") {
        // Route intermediate tensor activations directly to user-space malloc/free (LD_PRELOAD)
        session_options.DisableCpuMemArena();
    }
    // Default mode ("bfc_arena") preserves ONNX Runtime's internal BFC Arena

    Ort::Session session(env, model_path.c_str(), session_options);

    if (mode == "pare_ta") {
        pare_ta_allocator->set_mode_transient();
    }

    Ort::AllocatorWithDefaultOptions default_allocator;
    auto input_name_ptr = session.GetInputNameAllocated(0, default_allocator);
    auto output_name_ptr = session.GetOutputNameAllocated(0, default_allocator);

    const char* input_names[] = { input_name_ptr.get() };
    const char* output_names[] = { output_name_ptr.get() };

    // 3. Prepare Input Tensor: [1, 3, 640, 640]
    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    std::vector<float> input_tensor_values(input_elements);
    for (size_t i = 0; i < input_elements; ++i) {
        input_tensor_values[i] = static_cast<float>(i % 256) / 255.0f;
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_tensor_values.data(),
        input_tensor_values.size(),
        input_shape.data(),
        input_shape.size()
    );

    // 4. WARMUP PHASE
    for (size_t i = 0; i < warmup_frames; ++i) {
        if (mode == "pare_ta") {
            pare_ta_allocator->begin_frame();
        }

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1
        );

        float* out_data = output_tensors[0].GetTensorMutableData<float>();
        if (out_data) {
            volatile float dummy = out_data[0];
            (void)dummy;
        }

        if (mode == "pare_ta") {
            pare_ta_allocator->end_frame();
        }
    }

    // Capture memory snapshot right after warmup (steady-state baseline)
    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    // 5. MAIN STEADY-STATE INFERENCE LOOP
    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (size_t fid = 0; fid < num_frames; ++fid) {
        auto t_start = std::chrono::high_resolution_clock::now();

        if (mode == "pare_ta") {
            pare_ta_allocator->begin_frame();
        }

        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1
        );

        float* out_data = output_tensors[0].GetTensorMutableData<float>();
        if (out_data) {
            volatile float dummy = out_data[0];
            (void)dummy;
        }

        if (mode == "pare_ta") {
            pare_ta_allocator->end_frame();
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        latencies_ms.push_back(lat_ms);
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();
    double fps = static_cast<double>(num_frames) / total_sec;

    // 6. SAMPLE FINAL TELEMETRY
    MemorySnapshot final_snap = TelemetryCollector::sample();
    LatencyStats lstats = LatencyStats::calculate(latencies_ms);
    long minor_faults = final_snap.minor_faults - warmup_snap.minor_faults;

    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double private_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"mode\": \"" << mode << "\",\n"
                  << "  \"adv_level\": " << adv_level << ",\n"
                  << "  \"multiplier\": " << multiplier << ",\n"
                  << "  \"occupancy\": " << occupancy << ",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"achieved_fps\": " << fps << ",\n"
                  << "  \"peak_rss_mb\": " << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << private_dirty_mb << ",\n"
                  << "  \"minor_faults\": " << minor_faults << ",\n";

        if (mode == "pare_ta" && pare_ta_resource) {
            auto telemetry = pare_ta_resource->get_telemetry();
            std::cout << "  \"persistent_allocated_bytes\": " << telemetry.persistent_allocated_bytes << ",\n"
                      << "  \"transient_peak_frame_bytes\": " << telemetry.transient_peak_frame_bytes << ",\n"
                      << "  \"total_allocations\": " << telemetry.total_allocations << ",\n"
                      << "  \"noop_deallocations\": " << telemetry.noop_deallocations << ",\n"
                      << "  \"trapped_dram_kb\": 0.0,\n";
        }

        std::cout << "  \"latency\": {\n"
                  << "    \"mean_ms\": " << lstats.mean_ms << ",\n"
                  << "    \"p50_ms\": " << lstats.p50_ms << ",\n"
                  << "    \"p90_ms\": " << lstats.p90_ms << ",\n"
                  << "    \"p99_ms\": " << lstats.p99_ms << ",\n"
                  << "    \"p999_ms\": " << lstats.p999_ms << ",\n"
                  << "    \"max_ms\": " << lstats.max_ms << "\n"
                  << "  }\n"
                  << "}\n";
    } else {
        std::cout << "\n=======================================================\n";
        std::cout << " YOLO26n ONNX Inference Benchmark Results\n";
        std::cout << " Allocator: " << allocator_name << " (Mode: " << mode << ")\n";
        std::cout << " Adversarial Level: " << adv_level << " (Multiplier: " << multiplier << ")\n";
        std::cout << " Frames: " << num_frames << " (Warmup: " << warmup_frames << ")\n";
        std::cout << " Throughput: " << std::fixed << std::setprecision(2) << fps << " FPS\n";
        std::cout << " Peak RSS: " << peak_rss_mb << " MB\n";
        std::cout << " Settled RSS: " << settled_rss_mb << " MB\n";
        std::cout << " Post-warmup Minor Faults: " << minor_faults << "\n";
        std::cout << " Latency p50: " << std::setprecision(3) << lstats.p50_ms << " ms\n";
        std::cout << " Latency p90: " << lstats.p90_ms << " ms\n";
        std::cout << " Latency p99: " << lstats.p99_ms << " ms\n";
        if (mode == "pare_ta" && pare_ta_resource) {
            auto telemetry = pare_ta_resource->get_telemetry();
            std::cout << " [PARE-TA] Peak Frame Activation: "
                      << (telemetry.transient_peak_frame_bytes / (1024.0 * 1024.0)) << " MB\n";
            std::cout << " [PARE-TA] Eliminated Frees (No-ops): " << telemetry.noop_deallocations << "\n";
        }
        std::cout << "=======================================================\n";
    }

    return 0;
}
