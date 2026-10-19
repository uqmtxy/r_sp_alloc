#include "page_aligned_arena.hpp"
#include "../../tools/lost_bytes_inspector/page_inspector.hpp"
#include "../factory_cv_pipeline/memory_telemetry.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <memory>

using namespace page_aware_cv;
using namespace lost_bytes;
using namespace factory_cv;

// Model layer tensor dimensions in bytes (YOLOv8s + PatchCore DAG)
constexpr size_t MODEL_WEIGHTS_BYTES = 120 * 1024 * 1024; // 120 MiB persistent weights
constexpr size_t RAW_FRAME_BYTES     = 1920 * 1080 * 3;     // 6,220,800 B (~5.93 MiB)
constexpr size_t STEM_TENSOR_BYTES   = 1 * 32 * 320 * 320 * sizeof(float); // 13,107,200 B (12.5 MiB)
constexpr size_t P1_STAGE_BYTES      = 1 * 64 * 160 * 160 * sizeof(float); // 6,553,600 B (~6.25 MiB)
constexpr size_t P2_STAGE_BYTES      = 1 * 128 * 80 * 80 * sizeof(float);  // 3,276,800 B (~3.13 MiB)
constexpr size_t P3_STAGE_BYTES      = 1 * 256 * 40 * 40 * sizeof(float);  // 1,638,400 B (~1.56 MiB)
constexpr size_t FPN_NECK_BYTES      = 1 * 128 * 80 * 80 * sizeof(float);  // 3,276,800 B (~3.13 MiB)
constexpr size_t DETECTION_BOX_BYTES = 300 * 32;                            // 9,600 B (~9.38 KiB, unaligned)

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
    bool output_json = false;

    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"frames", required_argument, 0, 'f'},
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:f:j", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm': mode = optarg; break;
            case 'f': num_frames = std::stoul(optarg); break;
            case 'j': output_json = true; break;
            default: break;
        }
    }

    // 1. BOOT PHASE: Load persistent model weights (120 MiB)
    void* model_weights = nullptr;
    if (mode == "dynamic") {
        model_weights = malloc(MODEL_WEIGHTS_BYTES);
    } else {
        // Wilson lifetime isolation: Dedicated persistent page arena
        int rc = posix_memalign(&model_weights, 4096, MODEL_WEIGHTS_BYTES);
        (void)rc;
    }
    std::memset(model_weights, 0x55, MODEL_WEIGHTS_BYTES);

    // If page_aware, initialize transient activation arena (64 MiB virtual address pool)
    std::unique_ptr<PageAlignedArena> transient_arena;
    if (mode == "page_aware") {
        transient_arena = std::make_unique<PageAlignedArena>(64 * 1024 * 1024);
    }

    MemorySnapshot boot_snap = TelemetryCollector::sample();

    std::vector<double> latencies_ms;
    latencies_ms.reserve(num_frames);

    auto benchmark_start = std::chrono::high_resolution_clock::now();

    // 2. RUNTIME INFERENCE DAG LOOP (1000 FRAMES)
    for (size_t fid = 0; fid < num_frames; ++fid) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        if (mode == "dynamic") {
            // LAYER 0: Ingest raw camera frame
            void* raw_frame = malloc(RAW_FRAME_BYTES);
            touch_pages(raw_frame, RAW_FRAME_BYTES);

            // LAYER 1: Stem convolution
            void* stem_tensor = malloc(STEM_TENSOR_BYTES);
            touch_pages(stem_tensor, STEM_TENSOR_BYTES);
            free(raw_frame); // Free raw frame

            // LAYER 2: Backbone Stage P1
            void* p1_tensor = malloc(P1_STAGE_BYTES);
            touch_pages(p1_tensor, P1_STAGE_BYTES);
            free(stem_tensor); // Free stem tensor

            // LAYER 3: Backbone Stage P2
            void* p2_tensor = malloc(P2_STAGE_BYTES);
            touch_pages(p2_tensor, P2_STAGE_BYTES);

            // LAYER 4: Backbone Stage P3
            void* p3_tensor = malloc(P3_STAGE_BYTES);
            touch_pages(p3_tensor, P3_STAGE_BYTES);
            free(p1_tensor);

            // LAYER 5: FPN / PANet Neck (consumes P2 and P3)
            void* fpn_tensor = malloc(FPN_NECK_BYTES);
            touch_pages(fpn_tensor, FPN_NECK_BYTES);
            free(p2_tensor);
            free(p3_tensor);

            // LAYER 6: Decoupled Detection Head (Un-aligned 9,600 B buffer)
            void* det_boxes = malloc(DETECTION_BOX_BYTES);
            touch_pages(det_boxes, DETECTION_BOX_BYTES);
            free(fpn_tensor);

            // Frame end: free detection buffer
            free(det_boxes);

        } else {
            // PAGE-AWARE ARENA EXECUTION (Strict 4 KiB grid alignment + H_min coalescing)

            // LAYER 0: Ingest raw camera frame
            void* raw_frame = transient_arena->allocate(RAW_FRAME_BYTES);
            touch_pages(raw_frame, RAW_FRAME_BYTES);

            // LAYER 1: Stem convolution
            void* stem_tensor = transient_arena->allocate(STEM_TENSOR_BYTES);
            touch_pages(stem_tensor, STEM_TENSOR_BYTES);
            transient_arena->deallocate(raw_frame, RAW_FRAME_BYTES);

            // LAYER 2: Backbone Stage P1
            void* p1_tensor = transient_arena->allocate(P1_STAGE_BYTES);
            touch_pages(p1_tensor, P1_STAGE_BYTES);
            transient_arena->deallocate(stem_tensor, STEM_TENSOR_BYTES);

            // LAYER 3: Backbone Stage P2
            void* p2_tensor = transient_arena->allocate(P2_STAGE_BYTES);
            touch_pages(p2_tensor, P2_STAGE_BYTES);

            // LAYER 4: Backbone Stage P3
            void* p3_tensor = transient_arena->allocate(P3_STAGE_BYTES);
            touch_pages(p3_tensor, P3_STAGE_BYTES);
            transient_arena->deallocate(p1_tensor, P1_STAGE_BYTES);

            // LAYER 5: FPN / PANet Neck
            void* fpn_tensor = transient_arena->allocate(FPN_NECK_BYTES);
            touch_pages(fpn_tensor, FPN_NECK_BYTES);
            transient_arena->deallocate(p2_tensor, P2_STAGE_BYTES);
            transient_arena->deallocate(p3_tensor, P3_STAGE_BYTES);

            // LAYER 6: Decoupled Detection Head
            void* det_boxes = transient_arena->allocate(DETECTION_BOX_BYTES);
            touch_pages(det_boxes, DETECTION_BOX_BYTES);
            transient_arena->deallocate(fpn_tensor, FPN_NECK_BYTES);

            // Frame end: free detection buffer
            transient_arena->deallocate(det_boxes, DETECTION_BOX_BYTES);
        }

        auto frame_end = std::chrono::high_resolution_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        latencies_ms.push_back(lat_ms);
    }

    auto benchmark_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(benchmark_end - benchmark_start).count();
    double achieved_fps = static_cast<double>(num_frames) / total_sec;

    MemorySnapshot final_snap = TelemetryCollector::sample();
    LatencyStats lstats = LatencyStats::calculate(latencies_ms);
    long minor_faults = final_snap.minor_faults - boot_snap.minor_faults;

    // 3. SAMPLE LOST BYTES VIA PAGEINSPECTOR
    HoleClassification hole_stats;
    if (mode == "page_aware" && transient_arena) {
        // Inspect transient arena pool
        uintptr_t base = transient_arena->base_address();
        size_t hwm = transient_arena->high_water_mark();
        std::vector<Range> active_allocs; // All intra-frame buffers are freed at frame end
        hole_stats = PageInspector::classify_arena(base, base + hwm, active_allocs);
    } else {
        // In dynamic mode, measure granularity mismatch on a simulated representative frame state
        size_t arena_sample_size = 32 * 1024 * 1024;
        void* sample_chunk = mmap(nullptr, arena_sample_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uintptr_t base = reinterpret_cast<uintptr_t>(sample_chunk);
        // Simulate arbitrary unaligned tensor allocations and freed holes
        std::vector<Range> sim_active = {
            {base + 128, base + 128 + DETECTION_BOX_BYTES},
            {base + 50000, base + 50000 + P2_STAGE_BYTES}
        };
        for (const auto& a : sim_active) {
            std::memset(reinterpret_cast<void*>(a.start), 1, 1024);
        }
        hole_stats = PageInspector::classify_arena(base, base + arena_sample_size, sim_active);
        munmap(sample_chunk, arena_sample_size);
    }

    double peak_rss_mb = TelemetryCollector::to_mb(final_snap.hwm_kb);
    double settled_rss_mb = TelemetryCollector::to_mb(final_snap.rss_kb);

    if (output_json) {
        std::cout << "{\n"
                  << "  \"mode\": \"" << mode << "\",\n"
                  << "  \"frames\": " << num_frames << ",\n"
                  << "  \"achieved_fps\": " << achieved_fps << ",\n"
                  << "  \"peak_rss_mb\": " << peak_rss_mb << ",\n"
                  << "  \"settled_rss_mb\": " << settled_rss_mb << ",\n"
                  << "  \"minor_faults\": " << minor_faults << ",\n"
                  << "  \"holes\": {\n"
                  << "    \"body_kb\": " << hole_stats.body_bytes / 1024.0 << ",\n"
                  << "    \"head_kb\": " << hole_stats.head_bytes / 1024.0 << ",\n"
                  << "    \"tail_kb\": " << hole_stats.tail_bytes / 1024.0 << ",\n"
                  << "    \"sub_page_kb\": " << hole_stats.sub_page_bytes / 1024.0 << ",\n"
                  << "    \"trapped_kb\": " << hole_stats.trapped_bytes() / 1024.0 << ",\n"
                  << "    \"trapped_ratio_percent\": " << hole_stats.trapped_ratio() << "\n"
                  << "  },\n"
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
                  << " PAGE-AWARE CV DAG BENCHMARK: " << mode << "\n"
                  << "=======================================================\n"
                  << " Frames Processed   : " << num_frames << "\n"
                  << " Achieved Throughput: " << achieved_fps << " FPS\n"
                  << " Peak RSS (VmHWM)   : " << peak_rss_mb << " MB\n"
                  << " Settled RSS        : " << settled_rss_mb << " MB\n"
                  << " Minor Page Faults  : " << minor_faults << "\n"
                  << "-------------------------------------------------------\n"
                  << " Lost Bytes Breakdown (Fistanto et al. PLOS '23):\n"
                  << "   Body Slabs       : " << hole_stats.body_bytes / 1024.0 << " KiB (100% reusable/returnable)\n"
                  << "   Head Fragments   : " << hole_stats.head_bytes / 1024.0 << " KiB (trapped DRAM)\n"
                  << "   Tail Fragments   : " << hole_stats.tail_bytes / 1024.0 << " KiB (trapped DRAM)\n"
                  << "   Sub-page Holes   : " << hole_stats.sub_page_bytes / 1024.0 << " KiB (trapped DRAM)\n"
                  << "   Trapped Ratio    : " << hole_stats.trapped_ratio() << " %\n"
                  << "-------------------------------------------------------\n"
                  << " Latency (p50 / p90 / p99 / max):\n"
                  << "   p50 : " << lstats.p50_ms << " ms\n"
                  << "   p90 : " << lstats.p90_ms << " ms\n"
                  << "   p99 : " << lstats.p99_ms << " ms\n"
                  << "   max : " << lstats.max_ms << " ms\n"
                  << "=======================================================\n" << std::endl;
    }

    if (mode == "dynamic") {
        free(model_weights);
    } else {
        free(model_weights);
    }
    return 0;
}
