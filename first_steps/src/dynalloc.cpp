#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>

namespace {

static constexpr uint32_t SLAB_MAGIC = 0xD7A110C0;
static constexpr uint32_t LARGE_MAGIC = 0x1A86E000;
static constexpr size_t MAX_SMALL_SIZE = 4096;

// Size classes (empirically optimized)
static const size_t SIZE_CLASSES[] = {
    16, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 640, 800, 1024, 1536, 2048, 3072, 4096
};
static constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

// 16-byte header prepended to every user allocation
struct alignas(16) AllocHeader {
    uint32_t magic;
    uint32_t size_class_idx; // 0xFFFF for large allocations
    size_t total_size;
};

static constexpr size_t HEADER_SIZE = sizeof(AllocHeader);

struct SlabPool {
    pthread_mutex_t lock;
    size_t object_size;
    void* free_list;
    size_t chunk_size;
    size_t active_count;
};

static SlabPool g_pools[NUM_SIZE_CLASSES];
static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

static char g_bootstrap_buf[128 * 1024];
static size_t g_bootstrap_offset = 0;
static bool g_initialized = false;

void init_allocator() {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        pthread_mutex_init(&g_pools[i].lock, nullptr);
        g_pools[i].object_size = SIZE_CLASSES[i] + HEADER_SIZE;
        g_pools[i].free_list = nullptr;
        g_pools[i].chunk_size = 64 * 1024; // 64 KB per chunk
        g_pools[i].active_count = 0;
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

    pthread_mutex_unlock(&pool.lock);
    return ptr;
}

void slab_free(AllocHeader* header, void* ptr) {
    int sc_idx = static_cast<int>(header->size_class_idx);
    if (sc_idx < 0 || sc_idx >= static_cast<int>(NUM_SIZE_CLASSES)) return;

    SlabPool& pool = g_pools[sc_idx];
    pthread_mutex_lock(&pool.lock);

    *reinterpret_cast<void**>(ptr) = pool.free_list;
    pool.free_list = ptr;
    pool.active_count--;

    pthread_mutex_unlock(&pool.lock);
}

void* large_alloc(size_t size) {
    size_t total = HEADER_SIZE + size;
    total = (total + 4095) & ~size_t(4095);

    void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return nullptr;

    AllocHeader* header = reinterpret_cast<AllocHeader*>(mem);
    header->magic = LARGE_MAGIC;
    header->size_class_idx = 0xFFFF;
    header->total_size = total;
    return reinterpret_cast<void*>(header + 1);
}

void large_free(AllocHeader* header) {
    munmap(header, header->total_size);
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
        large_free(header);
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
    if (header->magic == SLAB_MAGIC || header->magic == LARGE_MAGIC) {
        old_size = header->total_size;
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

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* p = malloc(size);
    if (!p) return 12;
    *memptr = p;
    return 0;
}

} // extern "C"
