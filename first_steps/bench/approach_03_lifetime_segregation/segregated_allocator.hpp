#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <sys/mman.h>
#include <algorithm>

namespace lifetime_bench {

static constexpr size_t PAGE_SIZE = 4096;

struct PageHeader {
    PageHeader* next = nullptr;
    size_t active_objects = 0;
    size_t capacity = 0;
};

class SimplePageSlab {
public:
    SimplePageSlab(size_t object_size)
        : object_size_((object_size + 7) & ~size_t(7)),
          free_slots_head_(nullptr),
          empty_pages_head_(nullptr),
          total_pages_(0),
          active_pages_(0) {
        objects_per_page_ = (PAGE_SIZE - sizeof(PageHeader)) / object_size_;
    }

    ~SimplePageSlab() {
        for (void* p : all_pages_) {
            munmap(p, PAGE_SIZE);
        }
    }

    inline void* allocate() {
        if (__builtin_expect(!free_slots_head_, 0)) {
            grow();
        }
        void* ptr = free_slots_head_;
        free_slots_head_ = *reinterpret_cast<void**>(ptr);

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
            header->next = empty_pages_head_;
            empty_pages_head_ = header;
            // Purge physical page frame
            madvise(reinterpret_cast<void*>(page_base), PAGE_SIZE, MADV_DONTNEED);
            active_pages_--;
        }
    }

    size_t total_mapped_bytes() const { return total_pages_ * PAGE_SIZE; }
    size_t active_pages() const { return active_pages_; }

private:
    void grow() {
        void* mem = nullptr;
        if (empty_pages_head_) {
            mem = empty_pages_head_;
            empty_pages_head_ = empty_pages_head_->next;
        } else {
            mem = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED) std::abort();
            all_pages_.push_back(mem);
            total_pages_++;
        }

        PageHeader* header = new (mem) PageHeader();
        header->active_objects = 0;
        header->capacity = objects_per_page_;
        header->next = nullptr;

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

    size_t object_size_;
    size_t objects_per_page_;
    void* free_slots_head_;
    PageHeader* empty_pages_head_;
    std::vector<void*> all_pages_;
    size_t total_pages_;
    size_t active_pages_;
};

// Segregated Allocator: Interposes allocations and routes by caller return address
class TransparentLifetimeAllocator {
public:
    TransparentLifetimeAllocator(size_t object_size, bool enable_segregation)
        : enable_segregation_(enable_segregation),
          shared_pool_(object_size),
          ephemeral_pool_(object_size),
          persistent_pool_(object_size) {}

    // Transparent allocate: inspects caller PC
    __attribute__((noinline)) void* allocate(void* caller_pc, bool hint_persistent) {
        if (!enable_segregation_) {
            return shared_pool_.allocate();
        }

        // In production, caller_pc is hashed against a profile table.
        // Here we simulate the classifier matching the callsite.
        (void)caller_pc;
        if (hint_persistent) {
            return persistent_pool_.allocate();
        } else {
            return ephemeral_pool_.allocate();
        }
    }

    inline void deallocate(void* ptr, bool hint_persistent) {
        if (!enable_segregation_) {
            shared_pool_.deallocate(ptr);
        } else {
            if (hint_persistent) {
                persistent_pool_.deallocate(ptr);
            } else {
                ephemeral_pool_.deallocate(ptr);
            }
        }
    }

    size_t active_pages() const {
        if (!enable_segregation_) {
            return shared_pool_.active_pages();
        }
        return ephemeral_pool_.active_pages() + persistent_pool_.active_pages();
    }

private:
    bool enable_segregation_;
    SimplePageSlab shared_pool_;
    SimplePageSlab ephemeral_pool_;
    SimplePageSlab persistent_pool_;
};

} // namespace lifetime_bench
