#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <algorithm>

namespace slab_bench {

// A simple, single-size slab pool managing fixed-size blocks via mmap
class SingleSizeSlabPool {
public:
    SingleSizeSlabPool(size_t object_size, size_t chunk_size = 64 * 1024)
        : object_size_(std::max(object_size, sizeof(void*))),
          chunk_size_(chunk_size),
          free_list_(nullptr),
          allocated_chunks_bytes_(0),
          live_objects_(0) {
        // Align object size to 8 bytes for 64-bit alignment
        object_size_ = (object_size_ + 7) & ~size_t(7);
    }

    ~SingleSizeSlabPool() {
        for (void* chunk : chunks_) {
            munmap(chunk, chunk_size_);
        }
    }

    // Non-copyable
    SingleSizeSlabPool(const SingleSizeSlabPool&) = delete;
    SingleSizeSlabPool& operator=(const SingleSizeSlabPool&) = delete;

    inline void* allocate() {
        if (__builtin_expect(!free_list_, 0)) {
            grow();
        }
        void* ptr = free_list_;
        free_list_ = *reinterpret_cast<void**>(ptr);
        live_objects_++;
        return ptr;
    }

    inline void deallocate(void* ptr) {
        *reinterpret_cast<void**>(ptr) = free_list_;
        free_list_ = ptr;
        live_objects_--;
    }

    size_t object_size() const { return object_size_; }
    size_t allocated_bytes() const { return allocated_chunks_bytes_; }
    size_t live_objects() const { return live_objects_; }

private:
    void grow() {
        void* mem = mmap(nullptr, chunk_size_, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            std::abort();
        }
        chunks_.push_back(mem);
        allocated_chunks_bytes_ += chunk_size_;

        // Format memory chunk into intrusive free list
        char* start = static_cast<char*>(mem);
        size_t count = chunk_size_ / object_size_;
        for (size_t i = 0; i + 1 < count; ++i) {
            void* curr = start + i * object_size_;
            void* next = start + (i + 1) * object_size_;
            *reinterpret_cast<void**>(curr) = next;
        }
        *reinterpret_cast<void**>(start + (count - 1) * object_size_) = free_list_;
        free_list_ = mem;
    }

    size_t object_size_;
    size_t chunk_size_;
    void* free_list_;
    std::vector<void*> chunks_;
    size_t allocated_chunks_bytes_;
    size_t live_objects_;
};

// Generic Multi-Size Slab Allocator: Routes allocations to corresponding size pool
class MultiSlabAllocator {
public:
    MultiSlabAllocator(const std::vector<size_t>& supported_sizes) {
        sizes_ = supported_sizes;
        std::sort(sizes_.begin(), sizes_.end());
        for (size_t s : sizes_) {
            pools_.push_back(new SingleSizeSlabPool(s));
        }
    }

    ~MultiSlabAllocator() {
        for (auto* p : pools_) {
            delete p;
        }
    }

    inline void* allocate(size_t size, int& out_bin_index) {
        for (size_t i = 0; i < sizes_.size(); ++i) {
            if (size <= sizes_[i]) {
                out_bin_index = static_cast<int>(i);
                return pools_[i]->allocate();
            }
        }
        // Fallback to standard malloc for sizes exceeding max bin
        out_bin_index = -1;
        return std::malloc(size);
    }

    inline void deallocate(void* ptr, int bin_index) {
        if (bin_index >= 0 && bin_index < static_cast<int>(pools_.size())) {
            pools_[bin_index]->deallocate(ptr);
        } else {
            std::free(ptr);
        }
    }

    size_t total_allocated_bytes() const {
        size_t total = 0;
        for (const auto* p : pools_) {
            total += p->allocated_bytes();
        }
        return total;
    }

    const std::vector<size_t>& sizes() const { return sizes_; }

private:
    std::vector<size_t> sizes_;
    std::vector<SingleSizeSlabPool*> pools_;
};

} // namespace slab_bench
