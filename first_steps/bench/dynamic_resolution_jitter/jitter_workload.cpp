#include "../page_aware_cv_arena/page_aligned_arena.hpp"
#include "../../tools/lost_bytes_inspector/page_inspector.hpp"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>
#include <random>
#include <algorithm>
#include <pthread.h>
#include <sched.h>

using namespace page_aware_cv;
using namespace lost_bytes;
using namespace factory_cv;

// Persistent model weights: 120 MiB
constexpr size_t MODEL_WEIGHTS_BYTES = 120 * 1024 * 1024;

// Inspection Resolution Scales
struct InspectionScale {
    const char* name;
    size_t raw_frame_bytes;
    size_t stem_bytes;
    size_t p1_bytes;
    size_t p2_bytes;
    size_t p3_bytes;
    size_t detection_bytes;
};

static const InspectionScale SCALES[] = {
    // 0: Standard 1080p PCB Overview
    {"1080p_Overview", 1920 * 1080 * 3, 1 * 32 * 320 * 320 * sizeof(float),
     1 * 64 * 160 * 160 * sizeof(float), 1 * 128 * 80 * 80 * sizeof(float),
     1 * 256 * 40 * 40 * sizeof(float), 300 * 32},
    
    // 1: 1K Square Micro-Defect Zoom
    {"1K_Zoom", 1024 * 1024 * 3, 1 * 32 * 256 * 256 * sizeof(float),
     1 * 64 * 128 * 128 * sizeof(float), 1 * 128 * 64 * 64 * sizeof(float),
     1 * 256 * 32 * 32 * sizeof(float), 150 * 32},

    // 2: Ultra-HD 1440p Surface Scratch Scan
    {"1440p_UltraHD", 2560 * 1440 * 3, 1 * 32 * 480 * 480 * sizeof(float),
     1 * 64 * 240 * 240 * sizeof(float), 1 * 128 * 120 * 120 * sizeof(float),
     1 * 256 * 60 * 60 * sizeof(float), 500 * 32},

    // 3: Fast-Pass Bounding-Box ROI Crop (VGA)
    {"VGA_FastPass", 640 * 480 * 3, 1 * 32 * 160 * 160 * sizeof(float),
     1 * 64 * 80 * 80 * sizeof(float), 1 * 128 * 40 * 40 * sizeof(float),
     1 * 256 * 20 * 20 * sizeof(float), 50 * 32}
};

static constexpr size_t NUM_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

inline void touch_pages(void* ptr, size_t bytes) {
    if (!ptr || bytes == 0) return;
    char* p = static_cast<char*>(ptr);
    for (size_t i = 0; i < bytes; i += page_aware_cv::PAGE_SIZE_4K) {
        p[i] = 1;
    }
    p[bytes - 1] = 1;
}

int main(int argc, char* argv[]) {
    std::string mode = "dynamic"; // "dynamic" or "page_aware"
    size_t num_frames = 1000;
    size_t warmup_frames = 20;
    bool output_json = false;
    std::string allocator_name = "default";
    int pinned_cpu_core = 0;

    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"warmup", required_argument, 0, 'w'},
        {"name", required_argument, 0, 'n'},
        {"core", required_argument, 0, 'c'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:w:n:c:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': mode = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'w': warmup_frames = std::stoul(optarg); break;
            case 'n': allocator_name = optarg; break;
            case 'c': pinned_cpu_core = std::stoi(optarg); break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    // 0. CPU Core Pinning
    if (pinned_cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(pinned_cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    // 1. BOOT PHASE: Model weights (120 MiB persistent)
    void* model_weights = nullptr;
    if (mode == "dynamic") {
        model_weights = malloc(MODEL_WEIGHTS_BYTES);
    } else {
        int rc = posix_memalign(&model_weights, 4096, MODEL_WEIGHTS_BYTES);
        (void)rc;
    }
    std::memset(model_weights, 0x55, MODEL_WEIGHTS_BYTES);

    std::unique_ptr<PageAlignedArena> transient_arena;
    if (mode == "page_aware") {
        // 96 MiB pool for non-stationary activations
        transient_arena = std::make_unique<PageAlignedArena>(96 * 1024 * 1024);
    }

    // Pre-generate deterministic scale sequence for 100% reproducible jitter
    std::mt19937_64 rng(42);
    // Probability weights: Overview 50%, Zoom 25%, UltraHD 10%, FastPass 15%
    std::discrete_distribution<int> scale_dist({50, 25, 10, 15});

    std::vector<int> frame_scales;
    frame_scales.reserve(warmup_frames + num_frames);
    for (size_t i = 0; i < warmup_frames + num_frames; ++i) {
        frame_scales.push_back(scale_dist(rng));
    }

    // 2. WARMUP PHASE (20 non-stationary frames)
    for (size_t fid = 0; fid < warmup_frames; ++fid) {
        const auto& scale = SCALES[frame_scales[fid]];
        if (mode == "dynamic") {
            void* raw = malloc(scale.raw_frame_bytes);
            touch_pages(raw, scale.raw_frame_bytes);

            void* stem = malloc(scale.stem_bytes);
            touch_pages(stem, scale.stem_bytes);
            free(raw);

            void* p1 = malloc(scale.p1_bytes);
            touch_pages(p1, scale.p1_bytes);
            free(stem);

            void* p2 = malloc(scale.p2_bytes);
            touch_pages(p2, scale.p2_bytes);
            free(p1);

            void* p3 = malloc(scale.p3_bytes);
            touch_pages(p3, scale.p3_bytes);
            free(p2);

            void* det = malloc(scale.detection_bytes);
            touch_pages(det, scale.detection_bytes);
            free(p3);
            free(det);
        } else {
            void* raw = transient_arena->allocate(scale.raw_frame_bytes);
            touch_pages(raw, scale.raw_frame_bytes);

            void* stem = transient_arena->allocate(scale.stem_bytes);
            touch_pages(stem, scale.stem_bytes);
            transient_arena->deallocate(raw, scale.raw_frame_bytes);

            void* p1 = transient_arena->allocate(scale.p1_bytes);
            touch_pages(p1, scale.p1_bytes);
            transient_arena->deallocate(stem, scale.stem_bytes);

            void* p2 = transient_arena->allocate(scale.p2_bytes);
            touch_pages(p2, scale.p2_bytes);
            transient_arena->deallocate(p1, scale.p1_bytes);

            void* p3 = transient_arena->allocate(scale.p3_bytes);
            touch_pages(p3, scale.p3_bytes);
            transient_arena->deallocate(p2, scale.p2_bytes);

            void* det = transient_arena->allocate(scale.detection_bytes);
            touch_pages(det, scale.detection_bytes);
            transient_arena->deallocate(p3, scale.p3_bytes);
            transient_arena->deallocate(det, scale.detection_bytes);
        }
    }

    MemorySnapshot warmup_snap = TelemetryCollector::sample();

    // 3. MAIN BENCHMARK LOOP (num_frames with non-stationary jitter)
    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (size_t fid = 0; fid < num_frames; ++fid) {
        const auto& scale = SCALES[frame_scales[warmup_frames + fid]];
        auto frame_start = std::chrono::high_resolution_clock::now();

        if (mode == "dynamic") {
            void* raw = malloc(scale.raw_frame_bytes);
            touch_pages(raw, scale.raw_frame_bytes);

            void* stem = malloc(scale.stem_bytes);
            touch_pages(stem, scale.stem_bytes);
            free(raw);

            void* p1 = malloc(scale.p1_bytes);
            touch_pages(p1, scale.p1_bytes);
            free(stem);

            void* p2 = malloc(scale.p2_bytes);
            touch_pages(p2, scale.p2_bytes);
            free(p1);

            void* p3 = malloc(scale.p3_bytes);
            touch_pages(p3, scale.p3_bytes);
            free(p2);

            void* det = malloc(scale.detection_bytes);
            touch_pages(det, scale.detection_bytes);
            free(p3);
            free(det);
        } else {
            void* raw = transient_arena->allocate(scale.raw_frame_bytes);
            touch_pages(raw, scale.raw_frame_bytes);

            void* stem = transient_arena->allocate(scale.stem_bytes);
            touch_pages(stem, scale.stem_bytes);
            transient_arena->deallocate(raw, scale.raw_frame_bytes);

            void* p1 = transient_arena->allocate(scale.p1_bytes);
            touch_pages(p1, scale.p1_bytes);
            transient_arena->deallocate(stem, scale.stem_bytes);

            void* p2 = transient_arena->allocate(scale.p2_bytes);
            touch_pages(p2, scale.p2_bytes);
            transient_arena->deallocate(p1, scale.p1_bytes);

            void* p3 = transient_arena->allocate(scale.p3_bytes);
            touch_pages(p3, scale.p3_bytes);
            transient_arena->deallocate(p2, scale.p2_bytes);

            void* det = transient_arena->allocate(scale.detection_bytes);
            touch_pages(det, scale.detection_bytes);
            transient_arena->deallocate(p3, scale.p3_bytes);
            transient_arena->deallocate(det, scale.detection_bytes);
        }

        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        latencies_ms.push_back(frame_ms);
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();

    MemorySnapshot final_snap = TelemetryCollector::sample();

    // 4. STATISTICAL REDUCTIONS
    std::sort(latencies_ms.begin(), latencies_ms.end());
    double mean_lat = 0.0;
    for (double l : latencies_ms) mean_lat += l;
    mean_lat /= latencies_ms.size();

    double p50_lat = latencies_ms[static_cast<size_t>(num_frames * 0.50)];
    double p90_lat = latencies_ms[static_cast<size_t>(num_frames * 0.90)];
    double p99_lat = latencies_ms[static_cast<size_t>(num_frames * 0.99)];
    double max_lat = latencies_ms.back();
    double fps = static_cast<double>(num_frames) / total_sec;

    // 5. OUTPUT
    double final_peak_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double final_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);
    double final_dirty_mb = TelemetryCollector::to_mb(final_snap.private_dirty_kb);
    double warmup_rss_mb = TelemetryCollector::to_mb(warmup_snap.rss_kb);
    double steady_delta_mb = final_rss_mb - warmup_rss_mb;

    if (output_json) {
        std::cout << "{\n"
                  << "  \"allocator\": \"" << allocator_name << "\",\n"
                  << "  \"mode\": \"" << mode << "\",\n"
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
        std::cout << "\n=== Dynamic Resolution Jitter Results (" << allocator_name << ") ===\n"
                  << "Frames Evaluated : " << num_frames << "\n"
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

    if (mode == "dynamic") {
        free(model_weights);
    } else {
        free(model_weights);
    }

    return 0;
}
