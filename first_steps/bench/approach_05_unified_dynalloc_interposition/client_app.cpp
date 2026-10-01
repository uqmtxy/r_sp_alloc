#include <algorithm>
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

// Workload size distribution representing real production microservices / caches
const std::vector<size_t> SIZES = {48, 72, 128, 256, 512, 1024, 1536};
const std::vector<double> WEIGHTS = {0.25, 0.35, 0.15, 0.10, 0.08, 0.05, 0.02};

struct BenchConfig {
    size_t live_objects = 150000;
    size_t churn_ops = 800000;
    int trials = 5;
    uint64_t seed = 0x11223344;
    int cpu_pin = 0;
};

struct TrialResult {
    double mops_per_sec = 0.0;
    double ns_per_op = 0.0;
    long peak_rss_kb = 0;
};

struct StatsSummary {
    double median_mops = 0.0;
    double stddev_mops = 0.0;
    double median_ns_op = 0.0;
    long median_rss_kb = 0;
};

long get_peak_rss_kb() {
#if defined(__linux__)
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
    if (getrusage(RUSAGE_SELF, &usage) == 0) return usage.ru_maxrss;
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

TrialResult run_trial(const BenchConfig& cfg) {
    std::mt19937_64 rng(cfg.seed);
    std::discrete_distribution<size_t> dist(WEIGHTS.begin(), WEIGHTS.end());
    std::uniform_int_distribution<size_t> slot_dist(0, cfg.live_objects - 1);

    std::vector<void*> live(cfg.live_objects, nullptr);

    // Initial live set population
    for (size_t i = 0; i < cfg.live_objects; ++i) {
        size_t s = SIZES[dist(rng)];
        void* p = std::malloc(s);
        *reinterpret_cast<uint64_t*>(p) = 0xDEADBEEFCAFEBABEULL;
        live[i] = p;
    }

    // Churn loop
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t op = 0; op < cfg.churn_ops; ++op) {
        size_t slot = slot_dist(rng);
        std::free(live[slot]);

        size_t new_size = SIZES[dist(rng)];
        void* p = std::malloc(new_size);
        *reinterpret_cast<uint64_t*>(p) = 0xCAFEBABE11223344ULL;
        live[slot] = p;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    long peak_rss = get_peak_rss_kb();

    // Cleanup
    for (size_t i = 0; i < cfg.live_objects; ++i) {
        std::free(live[i]);
    }

    TrialResult res;
    res.mops_per_sec = (static_cast<double>(cfg.churn_ops) / sec) / 1e6;
    res.ns_per_op = (sec / static_cast<double>(cfg.churn_ops)) * 1e9;
    res.peak_rss_kb = peak_rss;
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

    double sum = 0.0;
    for (const auto& t : trials) sum += t.mops_per_sec;
    double mean = sum / n;
    double sq = 0.0;
    for (const auto& t : trials) {
        double d = t.mops_per_sec - mean;
        sq += d * d;
    }
    s.stddev_mops = std::sqrt(sq / n);
    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::string tag = "default";
    if (argc > 1) tag = argv[1];

    BenchConfig cfg;
    pin_to_core(cfg.cpu_pin);

    // Warmup
    run_trial(cfg);

    std::vector<TrialResult> trials;
    for (int t = 0; t < cfg.trials; ++t) {
        cfg.seed = 0x11223344 + t * 10007;
        trials.push_back(run_trial(cfg));
    }

    StatsSummary stats = compute_stats(trials);

    std::cout << "{"
              << "\"allocator\": \"" << tag << "\","
              << "\"trials\": " << cfg.trials << ","
              << "\"live_objects\": " << cfg.live_objects << ","
              << "\"churn_ops\": " << cfg.churn_ops << ","
              << "\"median_mops\": " << std::fixed << std::setprecision(2) << stats.median_mops << ","
              << "\"stddev_mops\": " << stats.stddev_mops << ","
              << "\"median_ns_op\": " << std::setprecision(1) << stats.median_ns_op << ","
              << "\"peak_rss_kb\": " << stats.median_rss_kb
              << "}" << std::endl;

    return 0;
}
