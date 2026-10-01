#include "segregated_allocator.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

struct BenchConfig {
    size_t ephemeral_objects = 50000;
    size_t persistent_objects = 2000;
    size_t object_size = 256;
    int trials = 5;
    uint64_t seed = 0xCAFEFEED;
    int cpu_pin = 0;
};

struct TrialResult {
    long peak_rss_kb = 0;
    long settled_rss_kb = 0;
    size_t active_pages_after_free = 0;
    double free_latency_ms = 0.0;
    double rss_reduction_pct = 0.0;
};

struct StatsSummary {
    long median_peak_rss_kb = 0;
    long median_settled_rss_kb = 0;
    size_t active_pages_after_free = 0;
    double median_free_ms = 0.0;
    double median_rss_reduction_pct = 0.0;
};

long get_current_rss_kb() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long val = 0;
            std::sscanf(line.c_str(), "VmRSS: %ld kB", &val);
            return val;
        }
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss;
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

// Simulated Call Site A: Persistent Index / Cache
__attribute__((noinline)) void* callsite_persistent_alloc(lifetime_bench::TransparentLifetimeAllocator& alloc) {
    void* pc = __builtin_return_address(0);
    return alloc.allocate(pc, true);
}

// Simulated Call Site B: Ephemeral Request / Query buffer
__attribute__((noinline)) void* callsite_ephemeral_alloc(lifetime_bench::TransparentLifetimeAllocator& alloc) {
    void* pc = __builtin_return_address(0);
    return alloc.allocate(pc, false);
}

TrialResult run_trial(bool enable_segregation, const BenchConfig& cfg) {
    lifetime_bench::TransparentLifetimeAllocator alloc(cfg.object_size, enable_segregation);

    std::vector<void*> persistent_ptrs;
    persistent_ptrs.reserve(cfg.persistent_objects);
    std::vector<void*> ephemeral_ptrs;
    ephemeral_ptrs.reserve(cfg.ephemeral_objects);

    // Interleave allocations to simulate real server workload
    size_t p_step = cfg.ephemeral_objects / cfg.persistent_objects;
    for (size_t i = 0; i < cfg.ephemeral_objects; ++i) {
        void* p = callsite_ephemeral_alloc(alloc);
        *reinterpret_cast<uint64_t*>(p) = 0xAA55AA55AA55AA55ULL;
        ephemeral_ptrs.push_back(p);

        if (i % p_step == 0 && persistent_ptrs.size() < cfg.persistent_objects) {
            void* lp = callsite_persistent_alloc(alloc);
            *reinterpret_cast<uint64_t*>(lp) = 0xDEADBEEFCAFEBABEULL;
            persistent_ptrs.push_back(lp);
        }
    }

    long peak_rss = get_current_rss_kb();

    // Now, free all ephemeral request objects (leaving only persistent objects)
    auto t0 = std::chrono::high_resolution_clock::now();
    for (void* p : ephemeral_ptrs) {
        alloc.deallocate(p, false);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double free_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    long settled_rss = get_current_rss_kb();
    size_t active_pages = alloc.active_pages();

    // Cleanup persistent objects
    for (void* p : persistent_ptrs) {
        alloc.deallocate(p, true);
    }

    TrialResult res;
    res.peak_rss_kb = peak_rss;
    res.settled_rss_kb = settled_rss;
    res.active_pages_after_free = active_pages;
    res.free_latency_ms = free_ms;
    if (peak_rss > 0) {
        res.rss_reduction_pct = 100.0 * (1.0 - (static_cast<double>(settled_rss) / peak_rss));
        if (res.rss_reduction_pct < 0.0) res.rss_reduction_pct = 0.0;
    }
    return res;
}

StatsSummary compute_stats(std::vector<TrialResult>& trials) {
    std::sort(trials.begin(), trials.end(), [](const TrialResult& a, const TrialResult& b) {
        return a.settled_rss_kb < b.settled_rss_kb;
    });

    size_t n = trials.size();
    StatsSummary s;
    s.median_peak_rss_kb = trials[n / 2].peak_rss_kb;
    s.median_settled_rss_kb = trials[n / 2].settled_rss_kb;
    s.active_pages_after_free = trials[n / 2].active_pages_after_free;
    s.median_free_ms = trials[n / 2].free_latency_ms;
    s.median_rss_reduction_pct = trials[n / 2].rss_reduction_pct;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = "unsegregated";
    if (argc > 1) mode = argv[1];

    BenchConfig cfg;
    pin_to_core(cfg.cpu_pin);

    bool segregated = (mode == "segregated");

    // Warmup
    run_trial(segregated, cfg);

    std::vector<TrialResult> trials;
    for (int t = 0; t < cfg.trials; ++t) {
        trials.push_back(run_trial(segregated, cfg));
    }

    StatsSummary stats = compute_stats(trials);

    std::cout << "{"
              << "\"mode\": \"" << mode << "\","
              << "\"trials\": " << cfg.trials << ","
              << "\"ephemeral_objects\": " << cfg.ephemeral_objects << ","
              << "\"persistent_objects\": " << cfg.persistent_objects << ","
              << "\"peak_rss_kb\": " << stats.median_peak_rss_kb << ","
              << "\"settled_rss_kb\": " << stats.median_settled_rss_kb << ","
              << "\"active_pages_pinned\": " << stats.active_pages_after_free << ","
              << "\"rss_reclaimed_pct\": " << std::fixed << std::setprecision(1) << stats.median_rss_reduction_pct << ","
              << "\"free_latency_ms\": " << std::setprecision(2) << stats.median_free_ms
              << "}" << std::endl;

    return 0;
}
