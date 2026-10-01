#include "cv_pipeline_types.hpp"
#include "memory_telemetry.hpp"
#include "optimizations/ring_arena_allocator.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstring>
#include <getopt.h>

using namespace factory_cv;

// Synchronized thread-safe FIFO channel between pipeline stages
template<typename T>
class PipelineQueue {
public:
    explicit PipelineQueue(size_t max_capacity = PIPELINE_QUEUE_CAPACITY)
        : max_capacity_(max_capacity), shutdown_(false) {}

    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_full_.wait(lock, [this]() { return queue_.size() < max_capacity_ || shutdown_; });
        if (shutdown_) return;
        queue_.push(item);
        cv_not_empty_.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_empty_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty() && shutdown_) return false;
        item = queue_.front();
        queue_.pop();
        cv_not_full_.notify_one();
        return true;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

private:
    size_t max_capacity_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    bool shutdown_;
};

// Data package passed through dynamic pipeline
struct DynamicFramePackage {
    uint8_t* raw_frame = nullptr;
    float* preprocessed_tensor = nullptr;
    float* stage1_activation = nullptr;
    float* stage2_activation = nullptr;
    float* stage3_activation = nullptr;
    DetectionBox* detections = nullptr;
    FrameMetadata metadata;
};

int main(int argc, char* argv[]) {
    std::string mode = "dynamic"; // "dynamic" or "optimized"
    size_t num_frames = 500;
    int target_fps = 0; // 0 = unlimited / burst
    bool output_json = false;

    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"fps", required_argument, 0, 'r'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:r:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': mode = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'r': target_fps = std::stoi(optarg); break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    // 1. BOOT PHASE: Load persistent model weights (120 MiB)
    void* model_weights = malloc(MODEL_WEIGHTS_SIZE);
    if (!model_weights) {
        std::cerr << "Failed to allocate model weights" << std::endl;
        return 1;
    }
    // Warm up model weights to fault in pages
    std::memset(model_weights, 0x55, MODEL_WEIGHTS_SIZE);

    // If optimized mode, initialize pre-allocated ring arena
    std::unique_ptr<RingArenaAllocator> ring_allocator;
    if (mode == "optimized") {
        ring_allocator = std::make_unique<RingArenaAllocator>(PIPELINE_QUEUE_CAPACITY);
    }

    MemorySnapshot boot_snap = TelemetryCollector::sample();

    // 2. PIPELINE QUEUES
    PipelineQueue<void*> rx_to_preprocess(PIPELINE_QUEUE_CAPACITY);
    PipelineQueue<void*> preprocess_to_infer(PIPELINE_QUEUE_CAPACITY);
    PipelineQueue<void*> infer_to_postprocess(PIPELINE_QUEUE_CAPACITY);

    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);
    std::mutex latency_mutex;

    auto pipeline_start_time = std::chrono::high_resolution_clock::now();

    // 3. LAUNCH 4 WORKER THREADS
    std::atomic<bool> pipeline_running{true};

    // THREAD 1: CAMERA INGESTION
    std::thread ingestion_thread([&]() {
        for (uint64_t fid = 0; fid < num_frames; ++fid) {
            auto frame_start = std::chrono::high_resolution_clock::now();

            if (mode == "dynamic") {
                DynamicFramePackage* pkg = new DynamicFramePackage();
                pkg->metadata.frame_id = fid;
                pkg->metadata.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    frame_start.time_since_epoch()).count();
                pkg->raw_frame = static_cast<uint8_t*>(malloc(RAW_FRAME_SIZE));
                // Simulate frame arrival by writing a pattern
                pkg->raw_frame[0] = static_cast<uint8_t>(fid & 0xFF);
                pkg->raw_frame[RAW_FRAME_SIZE - 1] = 0xFE;
                rx_to_preprocess.push(pkg);
            } else {
                FrameSlot* slot = ring_allocator->acquire_slot(fid);
                slot->metadata.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    frame_start.time_since_epoch()).count();
                slot->raw_frame[0] = static_cast<uint8_t>(fid & 0xFF);
                slot->raw_frame[RAW_FRAME_SIZE - 1] = 0xFE;
                rx_to_preprocess.push(slot);
            }

            if (target_fps > 0) {
                double target_interval_us = 1000000.0 / target_fps;
                auto elapsed_us = std::chrono::duration<double, std::micro>(
                    std::chrono::high_resolution_clock::now() - frame_start).count();
                if (elapsed_us < target_interval_us) {
                    usleep(static_cast<useconds_t>(target_interval_us - elapsed_us));
                }
            }
        }
        rx_to_preprocess.shutdown();
    });

    // THREAD 2: PREPROCESSING (Color conversion, resize, float32 normalization)
    std::thread preprocess_thread([&]() {
        void* item = nullptr;
        while (rx_to_preprocess.pop(item)) {
            if (mode == "dynamic") {
                auto* pkg = static_cast<DynamicFramePackage*>(item);
                pkg->preprocessed_tensor = static_cast<float*>(malloc(PREPROCESSED_TENSOR_SIZE));
                // Simulate normalization: read raw frame, write tensor
                pkg->preprocessed_tensor[0] = static_cast<float>(pkg->raw_frame[0]) / 255.0f;
                // Once preprocessed, release raw camera frame
                free(pkg->raw_frame);
                pkg->raw_frame = nullptr;
                preprocess_to_infer.push(pkg);
            } else {
                auto* slot = static_cast<FrameSlot*>(item);
                slot->preprocessed_tensor[0] = static_cast<float>(slot->raw_frame[0]) / 255.0f;
                preprocess_to_infer.push(slot);
            }
        }
        preprocess_to_infer.shutdown();
    });

    // THREAD 3: TENSOR INFERENCE (Backbone forward pass activations)
    std::thread inference_thread([&]() {
        void* item = nullptr;
        while (preprocess_to_infer.pop(item)) {
            if (mode == "dynamic") {
                auto* pkg = static_cast<DynamicFramePackage*>(item);
                pkg->stage1_activation = static_cast<float*>(malloc(ACTIVATION_STAGE1_SIZE));
                pkg->stage2_activation = static_cast<float*>(malloc(ACTIVATION_STAGE2_SIZE));
                pkg->stage3_activation = static_cast<float*>(malloc(ACTIVATION_STAGE3_SIZE));

                // Touch activations simulating GEMM compute
                pkg->stage1_activation[0] = pkg->preprocessed_tensor[0] * 1.5f;
                pkg->stage2_activation[0] = pkg->stage1_activation[0] * 0.8f;
                pkg->stage3_activation[0] = pkg->stage2_activation[0] * 1.2f;

                // Free preprocessed tensor as inference finishes
                free(pkg->preprocessed_tensor);
                pkg->preprocessed_tensor = nullptr;
                infer_to_postprocess.push(pkg);
            } else {
                auto* slot = static_cast<FrameSlot*>(item);
                slot->stage1_activation[0] = slot->preprocessed_tensor[0] * 1.5f;
                slot->stage2_activation[0] = slot->stage1_activation[0] * 0.8f;
                slot->stage3_activation[0] = slot->stage2_activation[0] * 1.2f;
                infer_to_postprocess.push(slot);
            }
        }
        infer_to_postprocess.shutdown();
    });

    // THREAD 4: POSTPROCESSING & QC DECISION
    std::thread postprocess_thread([&]() {
        void* item = nullptr;
        while (infer_to_postprocess.pop(item)) {
            auto now = std::chrono::high_resolution_clock::now();
            uint64_t start_ns = 0;

            if (mode == "dynamic") {
                auto* pkg = static_cast<DynamicFramePackage*>(item);
                start_ns = pkg->metadata.timestamp_ns;
                pkg->detections = static_cast<DetectionBox*>(malloc(DETECTION_BUFFER_SIZE));

                // Defect inspection logic
                pkg->detections[0].confidence = 0.95f;
                pkg->detections[0].class_id = (pkg->stage3_activation[0] > 1.0f) ? 1 : 0;
                pkg->metadata.is_defective = (pkg->detections[0].class_id == 1);

                // Free all remaining dynamic buffers
                free(pkg->stage1_activation);
                free(pkg->stage2_activation);
                free(pkg->stage3_activation);
                free(pkg->detections);
                delete pkg;
            } else {
                auto* slot = static_cast<FrameSlot*>(item);
                start_ns = slot->metadata.timestamp_ns;
                slot->detections[0].confidence = 0.95f;
                slot->detections[0].class_id = (slot->stage3_activation[0] > 1.0f) ? 1 : 0;
                slot->metadata.is_defective = (slot->detections[0].class_id == 1);
                ring_allocator->release_slot(slot);
            }

            uint64_t end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
            double latency_ms = static_cast<double>(end_ns - start_ns) / 1000000.0;

            std::lock_guard<std::mutex> lock(latency_mutex);
            latencies_ms.push_back(latency_ms);
        }
    });

    // 4. WAIT FOR PIPELINE TO COMPLETE
    ingestion_thread.join();
    preprocess_thread.join();
    inference_thread.join();
    postprocess_thread.join();

    auto pipeline_end_time = std::chrono::high_resolution_clock::now();
    double total_wall_time_s = std::chrono::duration<double>(pipeline_end_time - pipeline_start_time).count();
    double achieved_fps = static_cast<double>(num_frames) / total_wall_time_s;

    MemorySnapshot final_snap = TelemetryCollector::sample();
    LatencyStats lstats = LatencyStats::calculate(latencies_ms);

    // Live Payload Calculation:
    // Model Weights (120 MiB) + Active in-flight frames in queues (~25 MiB)
    double live_payload_mb = (static_cast<double>(MODEL_WEIGHTS_SIZE) / (1024.0 * 1024.0)) +
        (mode == "optimized" ? (static_cast<double>(ring_allocator->total_allocated_bytes()) / (1024.0 * 1024.0)) : 25.0);
    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double efficiency_eta = (live_payload_mb / peak_rss_mb) * 100.0;
    long minor_faults_during_run = final_snap.minor_faults - boot_snap.minor_faults;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"mode\": \"" << mode << "\",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"achieved_fps\": " << achieved_fps << ",\n"
                  << "  \"boot_rss_mb\": " << TelemetryCollector::to_mb(boot_snap.rss_kb) << ",\n"
                  << "  \"peak_rss_mb\": " << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"private_dirty_mb\": " << TelemetryCollector::to_mb(final_snap.private_dirty_kb) << ",\n"
                  << "  \"minor_faults\": " << minor_faults_during_run << ",\n"
                  << "  \"efficiency_eta_percent\": " << efficiency_eta << ",\n"
                  << "  \"latency\": {\n"
                  << "    \"mean_ms\": " << lstats.mean_ms << ",\n"
                  << "    \"p50_ms\": " << lstats.p50_ms << ",\n"
                  << "    \"p90_ms\": " << lstats.p90_ms << ",\n"
                  << "    \"p99_ms\": " << lstats.p99_ms << ",\n"
                  << "    \"p999_ms\": " << lstats.p999_ms << ",\n"
                  << "    \"max_ms\": " << lstats.max_ms << "\n"
                  << "  }\n"
                  << "}" << std::endl;
    } else {
        std::cout << "\n=======================================================\n"
                  << " FACTORY CV PIPELINE BENCHMARK: " << mode << "\n"
                  << "=======================================================\n"
                  << " Processed Frames   : " << num_frames << "\n"
                  << " Achieved FPS       : " << achieved_fps << " FPS\n"
                  << " Boot RSS           : " << TelemetryCollector::to_mb(boot_snap.rss_kb) << " MB\n"
                  << " Peak RSS (VmHWM)   : " << peak_rss_mb << " MB\n"
                  << " Settled RSS        : " << settled_rss_mb << " MB\n"
                  << " Private Dirty      : " << TelemetryCollector::to_mb(final_snap.private_dirty_kb) << " MB\n"
                  << " Minor Page Faults  : " << minor_faults_during_run << "\n"
                  << " Efficiency Ratio   : " << efficiency_eta << " %\n"
                  << "-------------------------------------------------------\n"
                  << " Latency (p50 / p90 / p99 / p99.9):\n"
                  << "   p50   : " << lstats.p50_ms << " ms\n"
                  << "   p90   : " << lstats.p90_ms << " ms\n"
                  << "   p99   : " << lstats.p99_ms << " ms\n"
                  << "   p99.9 : " << lstats.p999_ms << " ms\n"
                  << "   max   : " << lstats.max_ms << " ms\n"
                  << "=======================================================\n" << std::endl;
    }

    free(model_weights);
    return 0;
}
