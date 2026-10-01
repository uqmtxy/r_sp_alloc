#ifndef FACTORY_CV_MEMORY_TELEMETRY_HPP
#define FACTORY_CV_MEMORY_TELEMETRY_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <sys/resource.h>
#include <unistd.h>

namespace factory_cv {

struct MemorySnapshot {
    long rss_kb = 0;
    long hwm_kb = 0;
    long private_dirty_kb = 0;
    long minor_faults = 0;
    long major_faults = 0;
};

class TelemetryCollector {
public:
    static MemorySnapshot sample() {
        MemorySnapshot snap;

        // 1. Process VmRSS and VmHWM from /proc/self/status
        std::ifstream status_file("/proc/self/status");
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                sscanf(line.c_str(), "VmRSS: %ld kB", &snap.rss_kb);
            } else if (line.rfind("VmHWM:", 0) == 0) {
                sscanf(line.c_str(), "VmHWM: %ld kB", &snap.hwm_kb);
            }
        }

        // 2. Process Private_Dirty from /proc/self/smaps_rollup
        std::ifstream smaps_file("/proc/self/smaps_rollup");
        while (std::getline(smaps_file, line)) {
            if (line.rfind("Private_Dirty:", 0) == 0) {
                sscanf(line.c_str(), "Private_Dirty: %ld kB", &snap.private_dirty_kb);
                break;
            }
        }

        // 3. Page faults from getrusage
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            snap.minor_faults = usage.ru_minflt;
            snap.major_faults = usage.ru_majflt;
        }

        return snap;
    }

    static double to_mb(long kb) {
        return static_cast<double>(kb) / 1024.0;
    }
};

struct LatencyStats {
    double p50_ms = 0.0;
    double p90_ms = 0.0;
    double p99_ms = 0.0;
    double p999_ms = 0.0;
    double mean_ms = 0.0;
    double max_ms = 0.0;

    static LatencyStats calculate(std::vector<double>& latencies) {
        if (latencies.empty()) return {};
        std::sort(latencies.begin(), latencies.end());
        size_t n = latencies.size();

        LatencyStats s;
        s.p50_ms = latencies[n * 50 / 100];
        s.p90_ms = latencies[n * 90 / 100];
        s.p99_ms = latencies[n * 99 / 100];
        s.p999_ms = latencies[std::min(n - 1, n * 999 / 1000)];
        s.max_ms = latencies.back();

        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        s.mean_ms = sum / static_cast<double>(n);
        return s;
    }
};

} // namespace factory_cv

#endif // FACTORY_CV_MEMORY_TELEMETRY_HPP
