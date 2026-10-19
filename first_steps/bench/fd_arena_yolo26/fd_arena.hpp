#ifndef FD_ARENA_HPP
#define FD_ARENA_HPP

#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>

namespace fd_arena {

constexpr size_t DEFAULT_ARENA_CAPACITY = 64 * 1024 * 1024; // 64 MiB per frame arena
constexpr size_t ALIGNMENT_BYTES = 64; // Cache-line alignment (64 B)

class BumpArena {
public:
    explicit BumpArena(size_t capacity = DEFAULT_ARENA_CAPACITY)
        : capacity_(capacity), cursor_(0), peak_offset_(0) {
        // Map contiguous anonymous virtual memory
        void* ptr = mmap(nullptr, capacity_, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            std::cerr << "[FD-Arena] mmap failed for size " << capacity_ << std::endl;
            base_ = nullptr;
        } else {
            base_ = static_cast<char*>(ptr);
        }
    }

    ~BumpArena() {
        if (base_) {
            munmap(base_, capacity_);
        }
    }

    // Fast bump allocation guaranteed contiguous on adjacent cache lines
    inline void* allocate(size_t bytes) {
        size_t aligned_cursor = (cursor_ + (ALIGNMENT_BYTES - 1)) & ~(ALIGNMENT_BYTES - 1);
        if (aligned_cursor + bytes > capacity_) {
            std::cerr << "[FD-Arena] OOM: requested " << bytes << " but only "
                      << (capacity_ - aligned_cursor) << " remaining.\n";
            return nullptr;
        }
        void* result = base_ + aligned_cursor;
        cursor_ = aligned_cursor + bytes;
        if (cursor_ > peak_offset_) peak_offset_ = cursor_;
        return result;
    }

    // O(1) bulk reset at frame boundary
    inline void reset(bool purge_kernel = false) {
        if (purge_kernel && peak_offset_ > 0) {
            // Drop physical DRAM frames via MADV_DONTNEED
            size_t page_aligned = (peak_offset_ + 4095) & ~size_t(4095);
            madvise(base_, page_aligned, MADV_DONTNEED);
        }
        cursor_ = 0;
    }

    size_t current_offset() const { return cursor_; }
    size_t peak_offset() const { return peak_offset_; }
    size_t capacity() const { return capacity_; }

private:
    char* base_;
    size_t capacity_;
    size_t cursor_;
    size_t peak_offset_;
};

// Double-Buffered Frame Arena (FD-Arena)
class FDArenaManager {
public:
    explicit FDArenaManager(size_t capacity = DEFAULT_ARENA_CAPACITY)
        : arena_0_(capacity), arena_1_(capacity), active_index_(0) {}

    // Get active frame arena
    BumpArena& current_arena() {
        return (active_index_ == 0) ? arena_0_ : arena_1_;
    }

    // Advance to next frame and perform O(1) bulk reset on previous arena
    void advance_frame(bool purge_idle_memory = false) {
        // Reset the arena we just finished
        current_arena().reset(purge_idle_memory);
        // Switch to double-buffered counterpart
        active_index_ ^= 1;
    }

    size_t active_index() const { return active_index_; }

private:
    BumpArena arena_0_;
    BumpArena arena_1_;
    size_t active_index_;
};

} // namespace fd_arena

#endif // FD_ARENA_HPP
