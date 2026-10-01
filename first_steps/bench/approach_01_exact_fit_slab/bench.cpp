#include "exact_fit_slab.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

// Workload specification: Realistic production struct sizes (Key-Value / Indexing node patterns)
const std::vector<size_t> WORKLOAD_SIZES = {72, 136, 288, 520, 1040};
const std::vector<double> WORKLOAD_WEIGHTS = {0.40, 0.30, 0.15, 0.10, 0.05}; // Skewed distribution

// Generic power-of-two slab size classes (standard generic slab allocator)
const std::vector<size_t> GENERIC_POW2_SIZES = {16, 32, 64, 128, 256, 512, 1024, 2048};

// Workload-specific exact-fit size classes (tailored directly to the workload)
const std::vector<size_t> EXACT_FIT_SIZES = {72, 136, 288, 520, 1040};

struct BenchConfig {
    size_t live_objects = 200000;
    size_t churn_ops = 1000000;
    int trials = 5;
    uint64_t seed = 0xC0FFEE42;
    int cpu_pin = 0;
};

struct TrialResult {
    double elapsed_sec = 0.0;
    double mops_per_sec = 0.0;
    double ns_per_op = 0.0;
    long peak_rss_kb = 0;
    size_t payload_bytes = 0;
    size_t allocated_bytes = 0;
    double internal_frag_pct = 0.0;
};

struct StatsSummary {
    double median_mops = 0.0;
    double mean_mops = 0.0;
    double stddev_mops = 0.0;
    double median_ns_op = 0.0;
    long median_rss_kb = 0;
    double internal_frag_pct = 0.0;
};

long get_peak_rss_kb() {
#if defined(__linux__)
    // Read from /proc/self/status for exact VmHWM
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            long val = 0;
            std::sscanf(line.c_str(), "VmHWM: %ld kB", &val);
            return val;
        }
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss; // In KB on Linux
    }
#endif
    return 0;
}

void pin_to_core(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

// Allocator interface for benchmarking
enum class AllocatorType {
    SYSTEM_MALLOC,
    GENERIC_POW2_SLAB,
    EXACT_FIT_SLAB
};

struct AllocItem {
    void* ptr = nullptr;
    size_t size = 0;
    int bin_index = -1;
};

TrialResult run_trial(AllocatorType type, const BenchConfig& cfg, bool warmup) {
    (void)warmup;
    std::mt19937_64 rng(cfg.seed);
    std::discrete_distribution<size_t> dist(WORKLOAD_WEIGHTS.begin(), WORKLOAD_WEIGHTS.end());
    std::uniform_int_distribution<size_t> slot_dist(0, cfg.live_objects - 1);

    slab_bench::MultiSlabAllocator* slab_alloc = nullptr;
    if (type == AllocatorType::GENERIC_POW2_SLAB) {
        slab_alloc = new slab_bench::MultiSlabAllocator(GENERIC_POW2_SIZES);
    } else if (type == AllocatorType::EXACT_FIT_SLAB) {
        slab_alloc = new slab_bench::MultiSlabAllocator(EXACT_FIT_SIZES);
    }

    std::vector<AllocItem> live(cfg.live_objects);
    size_t total_payload_bytes = 0;
    size_t total_effective_bytes = 0;

    auto do_alloc = [&](size_t size, int& bin_idx) -> void* {
        if (slab_alloc) {
            return slab_alloc->allocate(size, bin_idx);
        } else {
            bin_idx = -1;
            return std::malloc(size);
        }
    };

    auto do_free = [&](void* ptr, int bin_idx) {
        if (slab_alloc) {
            slab_alloc->deallocate(ptr, bin_idx);
        } else {
            std::free(ptr);
        }
    };

    // 1. Initial live set population
    for (size_t i = 0; i < cfg.live_objects; ++i) {
        size_t size = WORKLOAD_SIZES[dist(rng)];
        int bin_idx = -1;
        void* p = do_alloc(size, bin_idx);
        // Touch 8 bytes to force physical page commit
        *reinterpret_cast<uint64_t*>(p) = 0xAA55AA55AA55AA55ULL;
        live[i] = {p, size, bin_idx};
        total_payload_bytes += size;

        if (type == AllocatorType::EXACT_FIT_SLAB) {
            total_effective_bytes += size; // 0% slack
        } else if (type == AllocatorType::GENERIC_POW2_SLAB) {
            // Find next power of two
            size_t eff = 16;
            while (eff < size) eff *= 2;
            total_effective_bytes += eff;
        } else {
            // Approx for glibc/jemalloc (16-byte alignment or standard chunk)
            size_t eff = (size + 15) & ~size_t(15);
            total_effective_bytes += eff;
        }
    }

    // 2. Churn phase (timing critical)
    auto t_start = std::chrono::high_resolution_clock::now();

    for (size_t op = 0; op < cfg.churn_ops; ++op) {
        size_t slot = slot_dist(rng);
        do_free(live[slot].ptr, live[slot].bin_index);

        size_t new_size = WORKLOAD_SIZES[dist(rng)];
        int new_bin = -1;
        void* p = do_alloc(new_size, new_bin);
        *reinterpret_cast<uint64_t*>(p) = 0x55AA55AA55AA55AAULL;
        live[slot] = {p, new_size, new_bin};
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();

    long peak_rss = get_peak_rss_kb();

    // Cleanup
    for (size_t i = 0; i < cfg.live_objects; ++i) {
        do_free(live[i].ptr, live[i].bin_index);
    }
    delete slab_alloc;

    TrialResult res;
    res.elapsed_sec = elapsed_sec;
    res.mops_per_sec = (static_cast<double>(cfg.churn_ops) / elapsed_sec) / 1e6;
    res.ns_per_op = (elapsed_sec / static_cast<double>(cfg.churn_ops)) * 1e9;
    res.peak_rss_kb = peak_rss;
    res.payload_bytes = total_payload_bytes;
    res.allocated_bytes = total_effective_bytes;
    if (total_effective_bytes > total_payload_bytes) {
        res.internal_frag_pct = 100.0 * (1.0 - (static_cast<double>(total_payload_bytes) / total_effective_bytes));
    } else {
        res.internal_frag_pct = 0.0;
    }

    return res;
}

StatsSummary compute_stats(std::vector<TrialResult>& trials) {
    std::sort(trials.begin(), trials.end(), [](const TrialResult& a, const TrialResult& b) {
        return a.mops_per_sec < b.mops_per_sec;
    });

    size_t n = trials.size();
    StatsSummary s;
    s.median_mops = trials[n / 2].mops_per_sec;
    s.median_ns_op = trials[n / 2].ns_per_op;
    s.median_rss_kb = trials[n / 2].peak_rss_kb;
    s.internal_frag_pct = trials[n / 2].internal_frag_pct;

    double sum = 0.0;
    for (const auto& t : trials) sum += t.mops_per_sec;
    s.mean_mops = sum / n;

    double sq_diff = 0.0;
    for (const auto& t : trials) {
        double d = t.mops_per_sec - s.mean_mops;
        sq_diff += d * d;
    }
    s.stddev_mops = std::sqrt(sq_diff / n);

    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::string alloc_mode = "system";
    if (argc > 1) alloc_mode = argv[1];

    BenchConfig cfg;
    pin_to_core(cfg.cpu_pin);

    AllocatorType type = AllocatorType::SYSTEM_MALLOC;
    if (alloc_mode == "exact_fit") {
        type = AllocatorType::EXACT_FIT_SLAB;
    } else if (alloc_mode == "generic_pow2") {
        type = AllocatorType::GENERIC_POW2_SLAB;
    }

    // Warmup run (discarded)
    run_trial(type, cfg, true);

    // Measurement trials
    std::vector<TrialResult> trials;
    for (int t = 0; t < cfg.trials; ++t) {
        cfg.seed = 0xC0FFEE42 + t * 10007; // Deterministic varied seed per trial
        trials.push_back(run_trial(type, cfg, false));
    }

    StatsSummary stats = compute_stats(trials);

    // Print JSON output for automated collation
    std::cout << "{"
              << "\"allocator\": \"" << alloc_mode << "\","
              << "\"trials\": " << cfg.trials << ","
              << "\"live_objects\": " << cfg.live_objects << ","
              << "\"churn_ops\": " << cfg.churn_ops << ","
              << "\"median_mops\": " << std::fixed << std::setprecision(3) << stats.median_mops << ","
              << "\"mean_mops\": " << stats.mean_mops << ","
              << "\"stddev_mops\": " << stats.stddev_mops << ","
              << "\"median_ns_per_op\": " << std::setprecision(1) << stats.median_ns_op << ","
              << "\"median_rss_kb\": " << stats.median_rss_kb << ","
              << "\"internal_frag_pct\": " << std::setprecision(2) << stats.internal_frag_pct
              << "}" << std::endl;

    return 0;
}
