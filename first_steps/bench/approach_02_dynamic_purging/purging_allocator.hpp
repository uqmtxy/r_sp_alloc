#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <sys/mman.h>

#ifndef MADV_FREE
#define MADV_FREE 8
#endif

namespace purge_bench {

enum class PurgePolicy {
    NO_PURGE,           // Retain all physical pages in user-space free list
    SYNC_DONTNEED,      // Synchronously invoke madvise(MADV_DONTNEED)
    SYNC_FREE,          // Synchronously invoke madvise(MADV_FREE)
    BATCH_DONTNEED      // Accumulate dirty pages and purge in batch
};

struct PageHeader {
    PageHeader* next = nullptr;
    size_t active_objects = 0;
    size_t capacity = 0;
    bool in_freelist = false;
};

// Manages allocation of fixed-size objects carved out of 4KB pages
class PagingSlabPool {
public:
    static constexpr size_t PAGE_SIZE = 4096;

    PagingSlabPool(size_t object_size, PurgePolicy policy, size_t batch_threshold = 16)
        : object_size_(std::max(object_size, sizeof(void*))),
          policy_(policy),
          batch_threshold_(batch_threshold),
          free_slots_head_(nullptr),
          empty_pages_head_(nullptr),
          empty_page_count_(0),
          total_mapped_pages_(0),
          active_pages_(0),
          purge_syscall_count_(0) {
        // Ensure 8-byte alignment
        object_size_ = (object_size_ + 7) & ~size_t(7);
        objects_per_page_ = (PAGE_SIZE - sizeof(PageHeader)) / object_size_;
    }

    ~PagingSlabPool() {
        for (void* p : all_pages_) {
            munmap(p, PAGE_SIZE);
        }
    }

    // Non-copyable
    PagingSlabPool(const PagingSlabPool&) = delete;
    PagingSlabPool& operator=(const PagingSlabPool&) = delete;

    inline void* allocate() {
        if (__builtin_expect(!free_slots_head_, 0)) {
            allocate_new_page();
        }
        void* ptr = free_slots_head_;
        free_slots_head_ = *reinterpret_cast<void**>(ptr);

        // Derive page header
        uintptr_t page_base = reinterpret_cast<uintptr_t>(ptr) & ~(PAGE_SIZE - 1);
        PageHeader* header = reinterpret_cast<PageHeader*>(page_base);
        header->active_objects++;

        return ptr;
    }

    inline void deallocate(void* ptr) {
        uintptr_t page_base = reinterpret_cast<uintptr_t>(ptr) & ~(PAGE_SIZE - 1);
        PageHeader* header = reinterpret_cast<PageHeader*>(page_base);

        *reinterpret_cast<void**>(ptr) = free_slots_head_;
        free_slots_head_ = ptr;

        header->active_objects--;

        if (header->active_objects == 0) {
            handle_empty_page(header, reinterpret_cast<void*>(page_base));
        }
    }

    // Flush any pending batched pages
    void flush_batch() {
        if (policy_ == PurgePolicy::BATCH_DONTNEED && !batch_queue_.empty()) {
            for (void* page : batch_queue_) {
                madvise(page, PAGE_SIZE, MADV_DONTNEED);
                purge_syscall_count_++;
            }
            batch_queue_.clear();
        }
    }

    size_t total_mapped_bytes() const { return total_mapped_pages_ * PAGE_SIZE; }
    size_t purge_syscall_count() const { return purge_syscall_count_; }

private:
    void allocate_new_page() {
        void* mem = nullptr;

        // Check if we have recycled empty pages
        if (empty_pages_head_) {
            mem = empty_pages_head_;
            empty_pages_head_ = empty_pages_head_->next;
            empty_page_count_--;
        } else {
            mem = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED) {
                std::abort();
            }
            all_pages_.push_back(mem);
            total_mapped_pages_++;
        }

        PageHeader* header = new (mem) PageHeader();
        header->active_objects = 0;
        header->capacity = objects_per_page_;
        header->next = nullptr;
        header->in_freelist = false;

        // Carve slots
        char* slot_start = static_cast<char*>(mem) + sizeof(PageHeader);
        for (size_t i = 0; i + 1 < objects_per_page_; ++i) {
            void* curr = slot_start + i * object_size_;
            void* next = slot_start + (i + 1) * object_size_;
            *reinterpret_cast<void**>(curr) = next;
        }
        *reinterpret_cast<void**>(slot_start + (objects_per_page_ - 1) * object_size_) = free_slots_head_;
        free_slots_head_ = slot_start;
        active_pages_++;
    }

    void handle_empty_page(PageHeader* header, void* page_base) {
        // Link into empty page list first
        header->next = empty_pages_head_;
        empty_pages_head_ = header;
        empty_page_count_++;

        // Purge advice executed AFTER updating intrusive header
        if (policy_ == PurgePolicy::SYNC_DONTNEED) {
            madvise(page_base, PAGE_SIZE, MADV_DONTNEED);
            purge_syscall_count_++;
        } else if (policy_ == PurgePolicy::SYNC_FREE) {
            madvise(page_base, PAGE_SIZE, MADV_FREE);
            purge_syscall_count_++;
        } else if (policy_ == PurgePolicy::BATCH_DONTNEED) {
            batch_queue_.push_back(page_base);
            if (batch_queue_.size() >= batch_threshold_) {
                for (void* p : batch_queue_) {
                    madvise(p, PAGE_SIZE, MADV_DONTNEED);
                    purge_syscall_count_++;
                }
                batch_queue_.clear();
            }
        }
    }

    size_t object_size_;
    PurgePolicy policy_;
    size_t batch_threshold_;
    size_t objects_per_page_;
    void* free_slots_head_;
    PageHeader* empty_pages_head_;
    size_t empty_page_count_;
    std::vector<void*> all_pages_;
    std::vector<void*> batch_queue_;
    size_t total_mapped_pages_;
    size_t active_pages_;
    size_t purge_syscall_count_;
};

} // namespace purge_bench
