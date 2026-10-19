#ifndef PAGE_AWARE_CV_PAGE_ALIGNED_ARENA_HPP
#define PAGE_AWARE_CV_PAGE_ALIGNED_ARENA_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include <cstring>
#include <iostream>

namespace page_aware_cv {

constexpr size_t PAGE_SIZE_4K = 4096;

inline size_t align_to_page(size_t bytes) {
    return (bytes + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
}

struct FreeBlock {
    uintptr_t start;
    size_t size; // always a multiple of PAGE_SIZE_4K
};

class PageAlignedArena {
public:
    explicit PageAlignedArena(size_t pool_capacity_bytes)
        : pool_capacity_(align_to_page(pool_capacity_bytes)), bump_offset_(0) {
        
        // Map large anonymous virtual address range for the arena
        void* ptr = mmap(nullptr, pool_capacity_, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            std::cerr << "PageAlignedArena mmap failed" << std::endl;
            base_address_ = 0;
        } else {
            base_address_ = reinterpret_cast<uintptr_t>(ptr);
        }
    }

    ~PageAlignedArena() {
        if (base_address_ != 0) {
            munmap(reinterpret_cast<void*>(base_address_), pool_capacity_);
        }
    }

    // Allocate a page-aligned buffer of exact multiple of 4 KiB
    void* allocate(size_t raw_bytes) {
        size_t aligned_size = align_to_page(raw_bytes);

        // 1. Search free list for a coalesced block that fits (Best-Fit)
        int best_idx = -1;
        size_t min_slack = SIZE_MAX;

        for (size_t i = 0; i < free_list_.size(); ++i) {
            if (free_list_[i].size >= aligned_size) {
                size_t slack = free_list_[i].size - aligned_size;
                if (slack < min_slack) {
                    min_slack = slack;
                    best_idx = static_cast<int>(i);
                    if (slack == 0) break; // Exact fit
                }
            }
        }

        if (best_idx >= 0) {
            FreeBlock b = free_list_[best_idx];
            if (b.size == aligned_size) {
                free_list_.erase(free_list_.begin() + best_idx);
                return reinterpret_cast<void*>(b.start);
            } else {
                // Split the block: return prefix, keep remaining page-aligned suffix in free list
                free_list_[best_idx].start += aligned_size;
                free_list_[best_idx].size -= aligned_size;
                return reinterpret_cast<void*>(b.start);
            }
        }

        // 2. No free block: Bump allocate from arena pool
        if (bump_offset_ + aligned_size <= pool_capacity_) {
            uintptr_t alloc_addr = base_address_ + bump_offset_;
            bump_offset_ += aligned_size;
            return reinterpret_cast<void*>(alloc_addr);
        }

        std::cerr << "PageAlignedArena out of memory! Capacity: " << pool_capacity_ << std::endl;
        return nullptr;
    }

    // Free a page-aligned buffer and virtually coalesce with adjacent free holes (H_min)
    void deallocate(void* ptr, size_t raw_bytes) {
        if (!ptr) return;
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        size_t aligned_size = align_to_page(raw_bytes);

        // Insert into free list while maintaining sorted order by virtual start address
        FreeBlock new_block{addr, aligned_size};
        auto it = std::lower_bound(free_list_.begin(), free_list_.end(), new_block,
            [](const FreeBlock& a, const FreeBlock& b) {
                return a.start < b.start;
            });
        it = free_list_.insert(it, new_block);

        // Virtual Coalescing (H_min): Merge with right neighbor if contiguous
        if (it + 1 != free_list_.end()) {
            if (it->start + it->size == (it + 1)->start) {
                it->size += (it + 1)->size;
                free_list_.erase(it + 1);
            }
        }

        // Virtual Coalescing (H_min): Merge with left neighbor if contiguous
        if (it != free_list_.begin()) {
            auto prev = it - 1;
            if (prev->start + prev->size == it->start) {
                prev->size += it->size;
                free_list_.erase(it);
            }
        }
    }

    uintptr_t base_address() const { return base_address_; }
    size_t high_water_mark() const { return bump_offset_; }
    size_t free_list_size() const { return free_list_.size(); }

    // Calculate total body bytes currently available in the free list
    size_t total_free_body_bytes() const {
        size_t total = 0;
        for (const auto& b : free_list_) {
            total += b.size;
        }
        return total;
    }

private:
    uintptr_t base_address_;
    size_t pool_capacity_;
    size_t bump_offset_;
    std::vector<FreeBlock> free_list_;
};

} // namespace page_aware_cv

#endif // PAGE_AWARE_CV_PAGE_ALIGNED_ARENA_HPP
