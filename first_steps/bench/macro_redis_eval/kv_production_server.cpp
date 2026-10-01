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
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

// Realistic production KV size distribution (Zipfian skewed)
const size_t KEY_SIZES[] = {16, 24, 32, 48};
const size_t VAL_SIZES[] = {64, 128, 256, 512, 1024, 2048};
const double VAL_WEIGHTS[] = {0.35, 0.30, 0.15, 0.10, 0.07, 0.03};

struct KVEntry {
    char* key = nullptr;
    size_t key_len = 0;
    char* val = nullptr;
    size_t val_len = 0;
};

// Sharded Hash Table for multi-threaded key-value caching
class ShardedKVStore {
public:
    static constexpr size_t NUM_SHARDS = 32;

    ShardedKVStore(size_t initial_capacity_per_shard = 16384) {
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            shards_[i].entries.reserve(initial_capacity_per_shard);
        }
    }

    ~ShardedKVStore() {
        clear();
    }

    void put(const std::string& key, const std::string& val) {
        size_t shard_idx = hash_str(key) % NUM_SHARDS;
        auto& shard = shards_[shard_idx];
        std::lock_guard<std::mutex> lock(shard.mtx);

        // Check if existing key
        for (auto& entry : shard.entries) {
            if (entry.key_len == key.size() && std::memcmp(entry.key, key.data(), key.size()) == 0) {
                // Update value
                std::free(entry.val);
                entry.val_len = val.size();
                entry.val = static_cast<char*>(std::malloc(val.size()));
                std::memcpy(entry.val, val.data(), val.size());
                return;
            }
        }

        // New entry
        KVEntry entry;
        entry.key_len = key.size();
        entry.key = static_cast<char*>(std::malloc(key.size()));
        std::memcpy(entry.key, key.data(), key.size());

        entry.val_len = val.size();
        entry.val = static_cast<char*>(std::malloc(val.size()));
        std::memcpy(entry.val, val.data(), val.size());

        shard.entries.push_back(entry);
    }

    bool get(const std::string& key, std::string& out_val) {
        size_t shard_idx = hash_str(key) % NUM_SHARDS;
        auto& shard = shards_[shard_idx];
        std::lock_guard<std::mutex> lock(shard.mtx);

        for (const auto& entry : shard.entries) {
            if (entry.key_len == key.size() && std::memcmp(entry.key, key.data(), key.size()) == 0) {
                out_val.assign(entry.val, entry.val_len);
                return true;
            }
        }
        return false;
    }

    bool del(const std::string& key) {
        size_t shard_idx = hash_str(key) % NUM_SHARDS;
        auto& shard = shards_[shard_idx];
        std::lock_guard<std::mutex> lock(shard.mtx);

        for (size_t i = 0; i < shard.entries.size(); ++i) {
            if (shard.entries[i].key_len == key.size() &&
                std::memcmp(shard.entries[i].key, key.data(), key.size()) == 0) {
                std::free(shard.entries[i].key);
                std::free(shard.entries[i].val);
                shard.entries[i] = shard.entries.back();
                shard.entries.pop_back();
                return true;
            }
        }
        return false;
    }

    // Evict a percentage of entries to simulate TTL/LRU expiration
    size_t evict_percentage(double ratio) {
        size_t total_evicted = 0;
        for (size_t s = 0; s < NUM_SHARDS; ++s) {
            auto& shard = shards_[s];
            std::lock_guard<std::mutex> lock(shard.mtx);
            size_t count = static_cast<size_t>(shard.entries.size() * ratio);
            for (size_t i = 0; i < count && !shard.entries.empty(); ++i) {
                std::free(shard.entries.back().key);
                std::free(shard.entries.back().val);
                shard.entries.pop_back();
                total_evicted++;
            }
        }
        return total_evicted;
    }

    size_t live_payload_bytes() const {
        size_t total = 0;
        for (size_t s = 0; s < NUM_SHARDS; ++s) {
            for (const auto& e : shards_[s].entries) {
                total += e.key_len + e.val_len;
            }
        }
        return total;
    }

    void clear() {
        for (size_t s = 0; s < NUM_SHARDS; ++s) {
            auto& shard = shards_[s];
            std::lock_guard<std::mutex> lock(shard.mtx);
            for (auto& e : shard.entries) {
                std::free(e.key);
                std::free(e.val);
            }
            shard.entries.clear();
        }
    }

private:
    struct Shard {
        std::mutex mtx;
        std::vector<KVEntry> entries;
    };

    static size_t hash_str(const std::string& str) {
        size_t h = 14695981039346656037ULL;
        for (char c : str) {
            h ^= static_cast<size_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }

    Shard shards_[NUM_SHARDS];
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
#endif
    return 0;
}

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
#endif
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string tag = "default";
    if (argc > 1) tag = argv[1];

    size_t num_keys = 200000;
    size_t churn_ops = 500000;

    ShardedKVStore store;
    std::mt19937_64 rng(0xC0FFEE);
    std::discrete_distribution<size_t> val_dist(std::begin(VAL_SIZES), std::end(VAL_SIZES));

    // Phase 1: Ingestion
    for (size_t i = 0; i < num_keys; ++i) {
        std::string k = "key:" + std::to_string(i);
        size_t v_len = VAL_SIZES[val_dist(rng)];
        std::string v(v_len, 'x');
        store.put(k, v);
    }

    long peak_rss = get_peak_rss_kb();
    size_t payload_bytes = store.live_payload_bytes();

    // Phase 2: Churn (50% GET, 30% SET, 20% DEL)
    auto t0 = std::chrono::high_resolution_clock::now();
    std::uniform_int_distribution<size_t> key_dist(0, num_keys - 1);
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);

    for (size_t op = 0; op < churn_ops; ++op) {
        std::string k = "key:" + std::to_string(key_dist(rng));
        double r = op_dist(rng);
        if (r < 0.50) {
            std::string dummy;
            store.get(k, dummy);
        } else if (r < 0.80) {
            size_t v_len = VAL_SIZES[val_dist(rng)];
            std::string v(v_len, 'y');
            store.put(k, v);
        } else {
            store.del(k);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double churn_sec = std::chrono::duration<double>(t1 - t0).count();

    // Phase 3: Eviction (simulate TTL cleanup of 60% of keys)
    store.evict_percentage(0.60);
    long settled_rss = get_current_rss_kb();

    double throughput_mops = (static_cast<double>(churn_ops) / churn_sec) / 1e6;
    double latency_ns = (churn_sec / static_cast<double>(churn_ops)) * 1e9;
    double efficiency_eta = (static_cast<double>(payload_bytes) / (peak_rss * 1024.0)) * 100.0;

    std::cout << "{"
              << "\"allocator\": \"" << tag << "\","
              << "\"peak_rss_mb\": " << std::fixed << std::setprecision(1) << (peak_rss / 1024.0) << ","
              << "\"settled_rss_mb\": " << (settled_rss / 1024.0) << ","
              << "\"memory_efficiency_pct\": " << std::setprecision(1) << efficiency_eta << ","
              << "\"throughput_mops\": " << std::setprecision(2) << throughput_mops << ","
              << "\"latency_ns_op\": " << std::setprecision(1) << latency_ns
              << "}" << std::endl;

    return 0;
}
