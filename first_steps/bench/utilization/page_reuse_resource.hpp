#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace utilization {

struct PageAccounting {
    size_t resident = 0, resident_payload = 0;
    size_t resident_body = 0, resident_head = 0, resident_tail = 0, resident_sub = 0;
    size_t live_requested = 0, live_rounded = 0, reserved = 0;
    size_t waste() const { return resident_body + resident_head + resident_tail + resident_sub; }
};

// Metadata is deliberately outside the reclaimable mapping. No page containing
// any outstanding allocation is passed to madvise. All offsets are half-open.
class PageReuseResource {
    struct Allocation { size_t requested, rounded; };
    uint8_t* base_ = nullptr;
    size_t capacity_, page_, alignment_;
    std::map<size_t, size_t> free_;
    std::map<size_t, Allocation> live_;
    bool eager_;

    size_t round_up(size_t n, size_t a) const {
        if (n > std::numeric_limits<size_t>::max() - (a - 1)) throw std::bad_alloc();
        return (n + a - 1) / a * a;
    }
    void discard(size_t start, size_t length) {
        const size_t begin = round_up(start, page_);
        const size_t end = (start + length) / page_ * page_;
        if (begin < end) {
            if (madvise(base_ + begin, end - begin, MADV_DONTNEED) != 0)
                throw std::runtime_error("madvise failed");
            advised_bytes += end - begin;
            ++advice_calls;
        }
    }

public:
    size_t allocations = 0, frees = 0, peak_live = 0, live_bytes = 0;
    size_t advised_bytes = 0, advice_calls = 0;

    explicit PageReuseResource(bool eager = false, size_t alignment = 64,
                               size_t capacity = 256ull * 1024 * 1024)
        : capacity_(capacity), page_(static_cast<size_t>(sysconf(_SC_PAGESIZE))),
          alignment_(alignment), eager_(eager) {
        if (page_ != 4096 || (alignment_ != 64 && alignment_ != page_))
            throw std::runtime_error("This experiment requires 4 KiB pages and 64/4096-byte alignment");
        capacity_ = round_up(capacity_, page_);
        void* p = mmap(nullptr, capacity_, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) throw std::bad_alloc();
        base_ = static_cast<uint8_t*>(p);
        free_.emplace(0, capacity_);
    }
    ~PageReuseResource() { if (base_) munmap(base_, capacity_); }
    PageReuseResource(const PageReuseResource&) = delete;
    PageReuseResource& operator=(const PageReuseResource&) = delete;

    void* allocate(size_t requested) {
        size_t rounded = round_up(std::max<size_t>(requested, 1), alignment_);
        auto best = free_.end();
        for (auto it = free_.begin(); it != free_.end(); ++it)
            if (it->second >= rounded && (best == free_.end() || it->second < best->second)) best = it;
        if (best == free_.end()) throw std::bad_alloc();
        size_t offset = best->first, remaining = best->second - rounded;
        // Insert allocation bookkeeping before mutating free ranges.
        live_.emplace(offset, Allocation{requested, rounded});
        free_.erase(best);
        if (remaining) free_.emplace(offset + rounded, remaining);
        live_bytes += requested;
        peak_live = std::max(peak_live, live_bytes);
        ++allocations;
        return base_ + offset;
    }

    void deallocate(void* p) {
        if (!p) return;
        uintptr_t address = reinterpret_cast<uintptr_t>(p);
        uintptr_t base = reinterpret_cast<uintptr_t>(base_);
        if (address < base || address - base >= capacity_) throw std::runtime_error("foreign pointer");
        size_t offset = address - base;
        auto allocated = live_.find(offset);
        if (allocated == live_.end()) throw std::runtime_error("unknown/double free");
        size_t length = allocated->second.rounded;
        live_bytes -= allocated->second.requested;
        live_.erase(allocated);
        ++frees;
        auto next = free_.lower_bound(offset);
        if (next != free_.begin()) {
            auto previous = std::prev(next);
            if (previous->first + previous->second == offset) {
                offset = previous->first;
                length += previous->second;
                free_.erase(previous);
            }
        }
        if (next != free_.end() && offset + length == next->first) {
            length += next->second;
            free_.erase(next);
        }
        free_.emplace(offset, length);
        if (eager_) discard(offset, length);
    }

    void purge() { for (const auto& [offset, length] : free_) discard(offset, length); }

    PageAccounting sample() const {
        PageAccounting s;
        s.reserved = capacity_;
        s.live_requested = live_bytes;
        std::vector<unsigned char> residency(capacity_ / page_);
        if (mincore(base_, capacity_, residency.data()) != 0) throw std::runtime_error("mincore failed");
        for (auto v : residency) if (v & 1) s.resident += page_;
        auto resident_intersection = [&](size_t start, size_t end) {
            size_t bytes = 0;
            while (start < end) {
                size_t boundary = std::min(end, (start / page_ + 1) * page_);
                if (residency[start / page_] & 1) bytes += boundary - start;
                start = boundary;
            }
            return bytes;
        };
        auto hole = [&](size_t start, size_t end) {
            if (start >= end) return;
            size_t body_start = round_up(start, page_);
            size_t body_end = end / page_ * page_;
            if (body_start > body_end) {
                s.resident_sub += resident_intersection(start, end);
            } else {
                s.resident_head += resident_intersection(start, body_start);
                s.resident_body += resident_intersection(body_start, body_end);
                s.resident_tail += resident_intersection(body_end, end);
            }
        };
        size_t cursor = 0;
        for (const auto& [offset, a] : live_) {
            hole(cursor, offset);
            s.resident_payload += resident_intersection(offset, offset + a.requested);
            s.live_rounded += a.rounded;
            cursor = offset + a.requested;
        }
        hole(cursor, capacity_);
        if (s.resident != s.resident_payload + s.waste()) throw std::runtime_error("page accounting mismatch");
        return s;
    }
};
} // namespace utilization
