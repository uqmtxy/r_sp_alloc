#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0x20
#endif
#endif

namespace {

static constexpr uint32_t SLAB_MAGIC = 0xD7A110C0;
static constexpr uint32_t LARGE_MAGIC = 0x1A86E000;
static constexpr size_t PAGE_SIZE_4K = 4096;
static constexpr size_t MAX_SMALL_SIZE = 4096;
static constexpr size_t TCACHE_MAX_COUNT = 32;

// Size classes (empirically optimized for Edge CV / YOLO26n)
static const size_t SIZE_CLASSES[] = {
    16, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 640, 800, 1024, 1536, 2048, 3072, 4096
};
static constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

// 16-byte header prepended right before user payload
struct alignas(16) AllocHeader {
    uint32_t magic;
    uint32_t size_class_idx; // 0xFFFF for large allocations
    size_t total_size;
};

static constexpr size_t HEADER_SIZE = sizeof(AllocHeader);

// =========================================================================
// 1. Thread-Local Cache (TLC) to eliminate mutex contention across threads
// =========================================================================
struct ThreadCache {
    void* free_list[NUM_SIZE_CLASSES] = {nullptr};
    size_t count[NUM_SIZE_CLASSES] = {0};
};

static thread_local ThreadCache t_cache;

// =========================================================================
// 2. Global Slab Pool for Small Allocations
// =========================================================================
struct SlabPool {
    pthread_mutex_t lock;
    size_t object_size;
    void* free_list;
    size_t chunk_size;
    size_t active_count;
};

static SlabPool g_pools[NUM_SIZE_CLASSES];
static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

static char g_bootstrap_buf[256 * 1024];
static size_t g_bootstrap_offset = 0;
static bool g_initialized = false;

// =========================================================================
// 3. Page-Aligned Recycling Pool for Large / Tensor Allocations (Zero-Syscall)
// =========================================================================
// Eliminates mmap/munmap thrashing, TLB shootdowns, and steady-state page faults.
struct LargeBufferBlock {
    LargeBufferBlock* next;
    size_t capacity;
};

struct LargePoolBucket {
    pthread_mutex_t lock;
    LargeBufferBlock* head;
    size_t count;
    size_t total_cached_bytes;
};

static constexpr size_t NUM_LARGE_BUCKETS = 15;
static const size_t LARGE_BUCKET_SIZES[NUM_LARGE_BUCKETS] = {
    8 * 1024,           // 8 KB
    16 * 1024,          // 16 KB
    32 * 1024,          // 32 KB
    64 * 1024,          // 64 KB
    128 * 1024,         // 128 KB
    256 * 1024,         // 256 KB
    512 * 1024,         // 512 KB
    1024 * 1024,        // 1 MB
    2 * 1024 * 1024,    // 2 MB
    4 * 1024 * 1024,    // 4 MB
    8 * 1024 * 1024,    // 8 MB
    16 * 1024 * 1024,   // 16 MB
    32 * 1024 * 1024,   // 32 MB
    64 * 1024 * 1024,   // 64 MB
    128 * 1024 * 1024   // 128 MB
};

static LargePoolBucket g_large_buckets[NUM_LARGE_BUCKETS];
static constexpr size_t MAX_CACHED_BYTES_PER_BUCKET = 256 * 1024 * 1024; // 256 MB per bucket

void init_allocator() {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        pthread_mutex_init(&g_pools[i].lock, nullptr);
        // Ensure slot size is at least 16-byte aligned
        size_t raw_slot = SIZE_CLASSES[i] + HEADER_SIZE;
        g_pools[i].object_size = (raw_slot + 15) & ~size_t(15);
        g_pools[i].free_list = nullptr;
        g_pools[i].chunk_size = 64 * 1024; // 64 KB per chunk
        g_pools[i].active_count = 0;
    }

    for (size_t b = 0; b < NUM_LARGE_BUCKETS; ++b) {
        pthread_mutex_init(&g_large_buckets[b].lock, nullptr);
        g_large_buckets[b].head = nullptr;
        g_large_buckets[b].count = 0;
        g_large_buckets[b].total_cached_bytes = 0;
    }

    g_initialized = true;
}

inline void ensure_initialized() {
    pthread_once(&g_init_once, init_allocator);
}

int get_size_class_index(size_t size) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) return static_cast<int>(i);
    }
    return -1;
}

int get_large_bucket_index(size_t needed_capacity) {
    for (size_t b = 0; b < NUM_LARGE_BUCKETS; ++b) {
        if (needed_capacity <= LARGE_BUCKET_SIZES[b]) return static_cast<int>(b);
    }
    return -1;
}

void grow_pool(SlabPool& pool, int sc_idx) {
    void* mem = mmap(nullptr, pool.chunk_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return;

    size_t slot_size = pool.object_size;
    size_t count = pool.chunk_size / slot_size;
    char* start = static_cast<char*>(mem);

    for (size_t i = 0; i < count; ++i) {
        char* slot = start + i * slot_size;
        AllocHeader* h = reinterpret_cast<AllocHeader*>(slot);
        h->magic = SLAB_MAGIC;
        h->size_class_idx = static_cast<uint32_t>(sc_idx);
        h->total_size = SIZE_CLASSES[sc_idx];

        void* user_payload = slot + HEADER_SIZE;
        void* next_slot = (i + 1 < count) ? (start + (i + 1) * slot_size + HEADER_SIZE) : pool.free_list;
        *reinterpret_cast<void**>(user_payload) = next_slot;
    }
    pool.free_list = start + HEADER_SIZE;
}

void* slab_alloc(size_t size) {
    int sc_idx = get_size_class_index(size);
    if (sc_idx < 0) return nullptr;

    // Fast Path: Thread-Local Cache (Zero Lock Contention)
    if (t_cache.free_list[sc_idx] != nullptr) {
        void* ptr = t_cache.free_list[sc_idx];
        t_cache.free_list[sc_idx] = *reinterpret_cast<void**>(ptr);
        t_cache.count[sc_idx]--;
        return ptr;
    }

    // Slow Path: Fetch from Global Slab Pool with batch refill
    SlabPool& pool = g_pools[sc_idx];
    pthread_mutex_lock(&pool.lock);

    if (!pool.free_list) {
        grow_pool(pool, sc_idx);
        if (!pool.free_list) {
            pthread_mutex_unlock(&pool.lock);
            return nullptr;
        }
    }

    void* ptr = pool.free_list;
    pool.free_list = *reinterpret_cast<void**>(ptr);
    pool.active_count++;

    // Batch refill thread-local cache up to 16 objects
    size_t refill = 0;
    while (pool.free_list && refill < 16) {
        void* extra = pool.free_list;
        pool.free_list = *reinterpret_cast<void**>(extra);
        pool.active_count++;

        *reinterpret_cast<void**>(extra) = t_cache.free_list[sc_idx];
        t_cache.free_list[sc_idx] = extra;
        t_cache.count[sc_idx]++;
        refill++;
    }

    pthread_mutex_unlock(&pool.lock);
    return ptr;
}

void slab_free(AllocHeader* header, void* ptr) {
    int sc_idx = static_cast<int>(header->size_class_idx);
    if (sc_idx < 0 || sc_idx >= static_cast<int>(NUM_SIZE_CLASSES)) return;

    // Fast Path: Push to Thread-Local Cache
    if (t_cache.count[sc_idx] < TCACHE_MAX_COUNT) {
        *reinterpret_cast<void**>(ptr) = t_cache.free_list[sc_idx];
        t_cache.free_list[sc_idx] = ptr;
        t_cache.count[sc_idx]++;
        return;
    }

    // Slow Path: Batch return half to global pool
    SlabPool& pool = g_pools[sc_idx];
    pthread_mutex_lock(&pool.lock);

    // Drain half of t_cache to global free_list
    size_t drain_count = t_cache.count[sc_idx] / 2;
    for (size_t i = 0; i < drain_count && t_cache.free_list[sc_idx]; ++i) {
        void* p = t_cache.free_list[sc_idx];
        t_cache.free_list[sc_idx] = *reinterpret_cast<void**>(p);
        t_cache.count[sc_idx]--;

        *reinterpret_cast<void**>(p) = pool.free_list;
        pool.free_list = p;
        pool.active_count--;
    }

    // Return the current ptr directly
    *reinterpret_cast<void**>(ptr) = pool.free_list;
    pool.free_list = ptr;
    pool.active_count--;

    pthread_mutex_unlock(&pool.lock);
}

// =========================================================================
// Page-Aligned Large Allocation with Zero-Syscall Recycling Pool
// =========================================================================
// Architecture (Paper 1 PLOS '26 & Paper 2 ISMM '26):
// 1. Base pointer is strictly 4 KiB page-aligned (uintptr_t(p) % 4096 == 0).
// 2. Base pointer is strictly 64-byte cache-line aligned (uintptr_t(p) % 64 == 0).
// 3. AllocHeader lives at (char*)p - 16, within the page-aligned guard prefix.
// 4. Freed blocks are held in g_large_buckets rather than munmap, driving
//    post-warmup minor page faults to ZERO!
void* large_alloc(size_t size) {
    size_t needed_capacity = size + PAGE_SIZE_4K;
    int b_idx = get_large_bucket_index(needed_capacity);

    void* raw_mem = nullptr;
    size_t total_capacity = 0;

    if (b_idx >= 0) {
        total_capacity = LARGE_BUCKET_SIZES[b_idx];
        LargePoolBucket& bucket = g_large_buckets[b_idx];
        pthread_mutex_lock(&bucket.lock);
        if (bucket.head != nullptr) {
            LargeBufferBlock* blk = bucket.head;
            bucket.head = blk->next;
            bucket.count--;
            bucket.total_cached_bytes -= total_capacity;
            raw_mem = blk;
        }
        pthread_mutex_unlock(&bucket.lock);
    } else {
        total_capacity = (needed_capacity + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    }

    if (!raw_mem) {
        raw_mem = mmap(nullptr, total_capacity, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw_mem == MAP_FAILED) return nullptr;
    }

    // Base user pointer is offset by exactly 4096 bytes:
    // raw_mem % 4096 == 0 => user_ptr % 4096 == 0!
    void* user_ptr = static_cast<char*>(raw_mem) + PAGE_SIZE_4K;

    // AllocHeader resides immediately before user_ptr:
    AllocHeader* header = reinterpret_cast<AllocHeader*>(user_ptr) - 1;
    header->magic = LARGE_MAGIC;
    header->size_class_idx = (b_idx >= 0) ? static_cast<uint32_t>(b_idx) : 0xFFFF;
    header->total_size = total_capacity;

    return user_ptr;
}

void large_free(AllocHeader* header, void* ptr) {
    void* raw_mem = static_cast<char*>(ptr) - PAGE_SIZE_4K;
    size_t total_capacity = header->total_size;
    uint32_t b_idx = header->size_class_idx;

    if (b_idx < NUM_LARGE_BUCKETS) {
        LargePoolBucket& bucket = g_large_buckets[b_idx];
        pthread_mutex_lock(&bucket.lock);
        if (bucket.total_cached_bytes + total_capacity <= MAX_CACHED_BYTES_PER_BUCKET) {
            LargeBufferBlock* blk = static_cast<LargeBufferBlock*>(raw_mem);
            blk->next = bucket.head;
            blk->capacity = total_capacity;
            bucket.head = blk;
            bucket.count++;
            bucket.total_cached_bytes += total_capacity;
            pthread_mutex_unlock(&bucket.lock);
            return;
        }
        pthread_mutex_unlock(&bucket.lock);
    }

    // Fallback: unmap if bucket is full or oversized
    munmap(raw_mem, total_capacity);
}

} // namespace

// =========================================================================
// POSIX Interposition Interface (LD_PRELOAD)
// =========================================================================
extern "C" {

void* malloc(size_t size) {
    if (!g_initialized) {
        if (g_bootstrap_offset + size + 32 <= sizeof(g_bootstrap_buf)) {
            void* p = g_bootstrap_buf + g_bootstrap_offset;
            g_bootstrap_offset = (g_bootstrap_offset + size + 15) & ~size_t(15);
            return p;
        }
        ensure_initialized();
    }

    if (size == 0) size = 1;

    if (size <= MAX_SMALL_SIZE) {
        void* ptr = slab_alloc(size);
        if (ptr) return ptr;
    }
    return large_alloc(size);
}

void free(void* ptr) {
    if (!ptr) return;

    if (ptr >= g_bootstrap_buf && ptr < g_bootstrap_buf + sizeof(g_bootstrap_buf)) {
        return;
    }

    AllocHeader* header = reinterpret_cast<AllocHeader*>(ptr) - 1;
    if (header->magic == SLAB_MAGIC) {
        slab_free(header, ptr);
        return;
    }
    if (header->magic == LARGE_MAGIC) {
        large_free(header, ptr);
        return;
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return nullptr;
    void* ptr = malloc(total);
    if (ptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }

    AllocHeader* header = reinterpret_cast<AllocHeader*>(ptr) - 1;
    size_t old_size = 0;
    if (header->magic == SLAB_MAGIC) {
        old_size = header->total_size;
        if (new_size <= old_size) return ptr;
    } else if (header->magic == LARGE_MAGIC) {
        old_size = (header->total_size > PAGE_SIZE_4K) ? (header->total_size - PAGE_SIZE_4K) : 0;
        if (new_size <= old_size) return ptr;
    }

    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        size_t copy_bytes = (old_size > 0) ? std::min(old_size, new_size) : new_size;
        std::memcpy(new_ptr, ptr, copy_bytes);
        free(ptr);
    }
    return new_ptr;
}

// Conforming posix_memalign: guarantees exact alignment (64-byte SIMD or 4 KiB page)
int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if (!memptr) return EINVAL;
    if ((alignment & (alignment - 1)) != 0 || alignment < sizeof(void*)) {
        return EINVAL;
    }
    if (size == 0) {
        *memptr = nullptr;
        return 0;
    }

    if (alignment <= 16 && size <= MAX_SMALL_SIZE) {
        void* p = malloc(size);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }

    // large_alloc returns 4096-aligned pointers, which satisfies any power-of-two alignment <= 4096!
    if (alignment <= PAGE_SIZE_4K) {
        void* p = large_alloc(size);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }

    // For alignments > 4096 (e.g. 2MB hugepage)
    void* p = large_alloc(size + alignment);
    if (!p) return ENOMEM;
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    *memptr = reinterpret_cast<void*>(aligned);
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
    void* ptr = nullptr;
    int ret = posix_memalign(&ptr, alignment, size);
    return (ret == 0) ? ptr : nullptr;
}

void* valloc(size_t size) {
    return aligned_alloc(PAGE_SIZE_4K, size);
}

void* pvalloc(size_t size) {
    size_t rounded = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    return aligned_alloc(PAGE_SIZE_4K, rounded);
}

} // extern "C"
