#define _GNU_SOURCE
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>

namespace {

typedef void* (*malloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void* (*calloc_fn)(size_t, size_t);
typedef void* (*realloc_fn)(void*, size_t);
typedef int (*posix_memalign_fn)(void**, size_t, size_t);

static malloc_fn real_malloc = nullptr;
static free_fn real_free = nullptr;
static calloc_fn real_calloc = nullptr;
static realloc_fn real_realloc = nullptr;
static posix_memalign_fn real_posix_memalign = nullptr;

static char init_buffer[128 * 1024];
static size_t init_offset = 0;
static bool in_init = false;
static bool profiler_ready = false;

static thread_local bool t_in_hook = false;

static std::atomic<long> g_live_count{0};
static std::atomic<long> g_peak_count{0};
static std::atomic<size_t> g_total_allocs{0};

static constexpr size_t BUCKETS[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
    131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216
};
static constexpr size_t NUM_BUCKETS = sizeof(BUCKETS) / sizeof(BUCKETS[0]);
static std::atomic<size_t> g_bucket_counts[NUM_BUCKETS + 1];

// Fixed size circular sample reservoir
static constexpr size_t MAX_SAMPLES = 8192;
static size_t g_samples[MAX_SAMPLES];
static std::atomic<size_t> g_sample_idx{0};

void init_profiler() {
    if (profiler_ready || in_init) return;
    in_init = true;
    real_malloc = (malloc_fn)dlsym(RTLD_NEXT, "malloc");
    real_free = (free_fn)dlsym(RTLD_NEXT, "free");
    real_calloc = (calloc_fn)dlsym(RTLD_NEXT, "calloc");
    real_realloc = (realloc_fn)dlsym(RTLD_NEXT, "realloc");
    real_posix_memalign = (posix_memalign_fn)dlsym(RTLD_NEXT, "posix_memalign");
    in_init = false;
    profiler_ready = true;
}

inline void record_alloc_event(size_t size) {
    g_total_allocs.fetch_add(1, std::memory_order_relaxed);
    long current = g_live_count.fetch_add(1, std::memory_order_relaxed) + 1;
    long prev_peak = g_peak_count.load(std::memory_order_relaxed);
    while (current > prev_peak && !g_peak_count.compare_exchange_weak(prev_peak, current, std::memory_order_relaxed)) {}

    size_t b_idx = NUM_BUCKETS;
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        if (size <= BUCKETS[i]) {
            b_idx = i;
            break;
        }
    }
    g_bucket_counts[b_idx].fetch_add(1, std::memory_order_relaxed);

    size_t idx = g_sample_idx.fetch_add(1, std::memory_order_relaxed);
    if (idx < MAX_SAMPLES) {
        g_samples[idx] = size;
    }
}

inline void record_free_event() {
    g_live_count.fetch_sub(1, std::memory_order_relaxed);
}

__attribute__((destructor)) void dump_profile() {
    const char* out_path = getenv("ALLOC_PROFILE_OUTPUT");
    if (!out_path) out_path = "first_steps/tools/adversarial/yolo26n_alloc_profile.json";

    FILE* fp = fopen(out_path, "w");
    if (!fp) return;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"total_allocations\": %zu,\n", g_total_allocs.load());
    fprintf(fp, "  \"peak_live_objects\": %ld,\n", g_peak_count.load());
    fprintf(fp, "  \"final_live_objects\": %ld,\n", g_live_count.load());
    fprintf(fp, "  \"buckets\": [\n");

    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        fprintf(fp, "    {\"max_size\": %zu, \"count\": %zu}%s\n",
                BUCKETS[i], g_bucket_counts[i].load(), (i + 1 < NUM_BUCKETS ? "," : ""));
    }
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"oversized_count\": %zu,\n", g_bucket_counts[NUM_BUCKETS].load());
    fprintf(fp, "  \"sample_sizes\": [");

    size_t total_samples = g_sample_idx.load();
    if (total_samples > MAX_SAMPLES) total_samples = MAX_SAMPLES;
    for (size_t i = 0; i < total_samples; ++i) {
        fprintf(fp, "%zu%s", g_samples[i], (i + 1 < total_samples ? ", " : ""));
    }
    fprintf(fp, "]\n}\n");
    fclose(fp);

    fprintf(stderr, "\n[ALLOC_PROFILER] Profile written to %s (Peak Live: %ld, Total Allocs: %zu, Samples: %zu)\n",
            out_path, g_peak_count.load(), g_total_allocs.load(), total_samples);
}

} // namespace

extern "C" {

void* malloc(size_t size) {
    if (!profiler_ready) {
        init_profiler();
        if (!real_malloc) {
            void* p = init_buffer + init_offset;
            init_offset = (init_offset + size + 15) & ~size_t(15);
            return p;
        }
    }
    if (t_in_hook) return real_malloc(size);

    t_in_hook = true;
    void* p = real_malloc(size);
    if (p) record_alloc_event(size);
    t_in_hook = false;
    return p;
}

void free(void* ptr) {
    if (!ptr) return;
    if (ptr >= init_buffer && ptr < init_buffer + sizeof(init_buffer)) return;
    if (!profiler_ready) init_profiler();
    if (t_in_hook) {
        if (real_free) real_free(ptr);
        return;
    }

    t_in_hook = true;
    record_free_event();
    if (real_free) real_free(ptr);
    t_in_hook = false;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (!profiler_ready) {
        init_profiler();
        if (!real_calloc) {
            void* p = init_buffer + init_offset;
            init_offset = (init_offset + total + 15) & ~size_t(15);
            std::memset(p, 0, total);
            return p;
        }
    }
    if (t_in_hook) return real_calloc(nmemb, size);

    t_in_hook = true;
    void* p = real_calloc(nmemb, size);
    if (p) record_alloc_event(total);
    t_in_hook = false;
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!profiler_ready) init_profiler();
    if (t_in_hook) return real_realloc(ptr, size);

    t_in_hook = true;
    if (ptr) record_free_event();
    void* p = real_realloc(ptr, size);
    if (p) record_alloc_event(size);
    t_in_hook = false;
    return p;
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if (!profiler_ready) init_profiler();
    if (t_in_hook) return real_posix_memalign(memptr, alignment, size);

    t_in_hook = true;
    int rc = real_posix_memalign(memptr, alignment, size);
    if (rc == 0 && memptr && *memptr) {
        record_alloc_event(size);
    }
    t_in_hook = false;
    return rc;
}

} // extern "C"
