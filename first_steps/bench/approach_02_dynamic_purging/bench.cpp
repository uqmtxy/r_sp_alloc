#include "purging_allocator.hpp"

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

struct BenchConfig {
    size_t burst_objects = 50000;
    size_t object_size = 256;
    double free_ratio = 0.80; // Free 80% of objects in quiescent phase
    int trials = 5;
    uint64_t seed = 0xDECAFBAD;
    int cpu_pin = 0;
};

struct TrialResult {
    long peak_rss_kb = 0;
    long settled_rss_kb = 0;
    double dealloc_and_purge_ms = 0.0;
    double realloc_ms = 0.0;
    long page_faults_on_realloc = 0;
    double rss_reduction_pct = 0.0;
    size_t purge_syscalls = 0;
};

struct StatsSummary {
    long median_peak_rss_kb = 0;
    long median_settled_rss_kb = 0;
    double median_dealloc_ms = 0.0;
    double median_realloc_ms = 0.0;
    long median_page_faults = 0;
    double median_rss_reduction_pct = 0.0;
    size_t purge_syscalls = 0;
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

long get_minor_faults() {
#if defined(__linux__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_minflt;
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

TrialResult run_trial(purge_bench::PurgePolicy policy, const BenchConfig& cfg, bool warmup) {
    (void)warmup;
    purge_bench::PagingSlabPool pool(cfg.object_size, policy, 16);

    std::vector<void*> ptrs(cfg.burst_objects, nullptr);

    // 1. Ingest/Burst phase: allocate all
    for (size_t i = 0; i < cfg.burst_objects; ++i) {
        ptrs[i] = pool.allocate();
        // Touch memory to ensure physical backing
        *reinterpret_cast<uint64_t*>(ptrs[i]) = 0x1122334455667788ULL;
    }

    long peak_rss = get_current_rss_kb();

    // 2. Quiescent phase: free consecutive objects to yield completely empty pages
    size_t to_free = static_cast<size_t>(cfg.burst_objects * cfg.free_ratio);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < to_free; ++i) {
        pool.deallocate(ptrs[i]);
        ptrs[i] = nullptr;
    }
    pool.flush_batch();
    auto t1 = std::chrono::high_resolution_clock::now();
    double dealloc_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    long settled_rss = get_current_rss_kb();

    // 3. Re-allocation phase: reallocate freed slots and touch memory (measures page-fault cost)
    long faults_before = get_minor_faults();
    auto t2 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < to_free; ++i) {
        ptrs[i] = pool.allocate();
        *reinterpret_cast<uint64_t*>(ptrs[i]) = 0x8877665544332211ULL;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double realloc_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    long faults_after = get_minor_faults();

    // Cleanup remaining
    for (size_t i = 0; i < cfg.burst_objects; ++i) {
        if (ptrs[i]) pool.deallocate(ptrs[i]);
    }

    TrialResult res;
    res.peak_rss_kb = peak_rss;
    res.settled_rss_kb = settled_rss;
    res.dealloc_and_purge_ms = dealloc_ms;
    res.realloc_ms = realloc_ms;
    res.page_faults_on_realloc = std::max(0L, faults_after - faults_before);
    res.purge_syscalls = pool.purge_syscall_count();
    if (peak_rss > 0) {
        res.rss_reduction_pct = 100.0 * (1.0 - (static_cast<double>(settled_rss) / peak_rss));
        if (res.rss_reduction_pct < 0.0) res.rss_reduction_pct = 0.0;
    }
    return res;
}

StatsSummary compute_stats(std::vector<TrialResult>& trials) {
    std::sort(trials.begin(), trials.end(), [](const TrialResult& a, const TrialResult& b) {
        return a.dealloc_and_purge_ms < b.dealloc_and_purge_ms;
    });

    size_t n = trials.size();
    StatsSummary s;
    s.median_peak_rss_kb = trials[n / 2].peak_rss_kb;
    s.median_settled_rss_kb = trials[n / 2].settled_rss_kb;
    s.median_dealloc_ms = trials[n / 2].dealloc_and_purge_ms;
    s.median_realloc_ms = trials[n / 2].realloc_ms;
    s.median_page_faults = trials[n / 2].page_faults_on_realloc;
    s.median_rss_reduction_pct = trials[n / 2].rss_reduction_pct;
    s.purge_syscalls = trials[n / 2].purge_syscalls;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = "no_purge";
    if (argc > 1) mode = argv[1];

    BenchConfig cfg;
    pin_to_core(cfg.cpu_pin);

    purge_bench::PurgePolicy policy = purge_bench::PurgePolicy::NO_PURGE;
    if (mode == "sync_dontneed") {
        policy = purge_bench::PurgePolicy::SYNC_DONTNEED;
    } else if (mode == "sync_free") {
        policy = purge_bench::PurgePolicy::SYNC_FREE;
    } else if (mode == "batch_dontneed") {
        policy = purge_bench::PurgePolicy::BATCH_DONTNEED;
    }

    // Warmup
    run_trial(policy, cfg, true);

    std::vector<TrialResult> trials;
    for (int t = 0; t < cfg.trials; ++t) {
        cfg.seed = 0xDECAFBAD + t * 10007;
        trials.push_back(run_trial(policy, cfg, false));
    }

    StatsSummary stats = compute_stats(trials);

    std::cout << "{"
              << "\"policy\": \"" << mode << "\","
              << "\"trials\": " << cfg.trials << ","
              << "\"burst_objects\": " << cfg.burst_objects << ","
              << "\"object_size\": " << cfg.object_size << ","
              << "\"peak_rss_kb\": " << stats.median_peak_rss_kb << ","
              << "\"settled_rss_kb\": " << stats.median_settled_rss_kb << ","
              << "\"rss_reclaimed_pct\": " << std::fixed << std::setprecision(1) << stats.median_rss_reduction_pct << ","
              << "\"dealloc_purge_ms\": " << std::setprecision(2) << stats.median_dealloc_ms << ","
              << "\"realloc_ms\": " << std::setprecision(2) << stats.median_realloc_ms << ","
              << "\"page_faults\": " << stats.median_page_faults << ","
              << "\"purge_syscalls\": " << stats.purge_syscalls
              << "}" << std::endl;

    return 0;
}
