#ifndef ADVERSARIAL_HEAP_HPP
#define ADVERSARIAL_HEAP_HPP

#include <vector>
#include <random>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <mutex>

namespace adversarial {

// Empirical size classes and cumulative probabilities profiled from Ultralytics YOLO26n ONNX Runtime
struct EmpiricalBucket {
    size_t min_sz;
    size_t max_sz;
    double weight;
};

static const EmpiricalBucket YOLO26_BUCKETS[] = {
    {8, 16, 127003.0},
    {17, 32, 110141.0},
    {33, 64, 164973.0},
    {65, 128, 63316.0},
    {129, 256, 11682.0},
    {257, 512, 9202.0},
    {513, 1024, 4231.0},
    {1025, 2048, 487.0},
    {2049, 4096, 580.0},
    {4097, 16384, 225.0},
    {16385, 65536, 46.0},
    {65537, 262144, 31.0},
    {262145, 1048576, 13.0}
};
static constexpr size_t NUM_YOLO_BUCKETS = sizeof(YOLO26_BUCKETS) / sizeof(YOLO26_BUCKETS[0]);

class AdversarialPreconditioner {
public:
    // Profiled baseline peak live allocations for YOLO26n
    static constexpr size_t DEFAULT_N_PEAK = 20000;

    static void precondition(double multiplier, double occupancy = 0.66,
                             size_t n_peak = DEFAULT_N_PEAK, uint64_t seed = 42) {
        if (multiplier <= 0.0) {
            std::cout << "[Adversarial Heap] Adv0 (Clean Heap) selected. No preconditioning.\n";
            return;
        }

        size_t total_n = static_cast<size_t>(multiplier * n_peak);
        std::cout << "[Adversarial Heap] Applying ISMM '24 Preconditioning: Multiplier=" << multiplier
                  << ", Occupancy=" << occupancy << ", Allocations=" << total_n << "...\n";

        // Build probability distribution
        std::vector<double> weights;
        weights.reserve(NUM_YOLO_BUCKETS);
        for (size_t i = 0; i < NUM_YOLO_BUCKETS; ++i) {
            weights.push_back(YOLO26_BUCKETS[i].weight);
        }
        std::mt19937_64 rng(seed);
        std::discrete_distribution<size_t> bucket_dist(weights.begin(), weights.end());

        // Phase 1: Allocate objects sampled from empirical YOLO26n size distribution
        std::vector<void*> allocated_ptrs;
        allocated_ptrs.reserve(total_n);

        size_t total_bytes_allocated = 0;
        for (size_t i = 0; i < total_n; ++i) {
            size_t b = bucket_dist(rng);
            size_t sz = YOLO26_BUCKETS[b].min_sz;
            if (YOLO26_BUCKETS[b].max_sz > YOLO26_BUCKETS[b].min_sz) {
                sz += rng() % (YOLO26_BUCKETS[b].max_sz - YOLO26_BUCKETS[b].min_sz + 1);
            }

            void* p = malloc(sz);
            if (p) {
                // Touch physical DRAM to force page table population
                std::memset(p, 0xAA, sz);
                allocated_ptrs.push_back(p);
                total_bytes_allocated += sz;
            }
        }

        // Phase 2: Randomly shuffle pointer array
        std::shuffle(allocated_ptrs.begin(), allocated_ptrs.end(), rng);

        // Phase 3: Free a fraction to leave targeted occupancy with interleaved free holes
        size_t to_free = static_cast<size_t>(allocated_ptrs.size() * (1.0 - occupancy));
        for (size_t i = 0; i < to_free; ++i) {
            free(allocated_ptrs[i]);
            allocated_ptrs[i] = nullptr;
        }

        // Store remaining live pointers in persistent anchor to keep heap pre-fragmented
        for (size_t i = to_free; i < allocated_ptrs.size(); ++i) {
            if (allocated_ptrs[i]) {
                live_anchor_.push_back(allocated_ptrs[i]);
            }
        }

        std::cout << "[Adversarial Heap] Preconditioning complete. Live fragmented objects: "
                  << live_anchor_.size() << " (" << (total_bytes_allocated * occupancy / (1024.0 * 1024.0))
                  << " MB physical DRAM pinned in gaps).\n";
    }

    static void precondition_thread(double multiplier, double occupancy = 0.66,
                                   size_t n_peak = DEFAULT_N_PEAK, uint64_t seed = 42) {
        if (multiplier <= 0.0) return;
        size_t total_n = static_cast<size_t>(multiplier * n_peak);

        std::vector<double> weights;
        weights.reserve(NUM_YOLO_BUCKETS);
        for (size_t i = 0; i < NUM_YOLO_BUCKETS; ++i) {
            weights.push_back(YOLO26_BUCKETS[i].weight);
        }
        std::mt19937_64 rng(seed);
        std::discrete_distribution<size_t> bucket_dist(weights.begin(), weights.end());

        std::vector<void*> allocated_ptrs;
        allocated_ptrs.reserve(total_n);

        for (size_t i = 0; i < total_n; ++i) {
            size_t b = bucket_dist(rng);
            size_t sz = YOLO26_BUCKETS[b].min_sz;
            if (YOLO26_BUCKETS[b].max_sz > YOLO26_BUCKETS[b].min_sz) {
                sz += rng() % (YOLO26_BUCKETS[b].max_sz - YOLO26_BUCKETS[b].min_sz + 1);
            }
            void* p = malloc(sz);
            if (p) {
                std::memset(p, 0xAA, sz);
                allocated_ptrs.push_back(p);
            }
        }

        std::shuffle(allocated_ptrs.begin(), allocated_ptrs.end(), rng);
        size_t to_free = static_cast<size_t>(allocated_ptrs.size() * (1.0 - occupancy));
        for (size_t i = 0; i < to_free; ++i) {
            free(allocated_ptrs[i]);
            allocated_ptrs[i] = nullptr;
        }

        static std::mutex s_mutex;
        std::lock_guard<std::mutex> lock(s_mutex);
        for (size_t i = to_free; i < allocated_ptrs.size(); ++i) {
            if (allocated_ptrs[i]) {
                live_anchor_.push_back(allocated_ptrs[i]);
            }
        }
    }

    static void cleanup() {
        for (void* p : live_anchor_) {
            free(p);
        }
        live_anchor_.clear();
    }

private:
    static inline std::vector<void*> live_anchor_;
};

} // namespace adversarial

#endif // ADVERSARIAL_HEAP_HPP
