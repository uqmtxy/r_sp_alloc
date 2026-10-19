#ifndef LOST_BYTES_PAGE_INSPECTOR_HPP
#define LOST_BYTES_PAGE_INSPECTOR_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <fstream>
#include <string>

namespace lost_bytes {

constexpr size_t PAGE_SIZE_4K = 4096;

struct Range {
    uintptr_t start;
    uintptr_t end; // exclusive: [start, end)
    size_t size() const { return (end > start) ? (end - start) : 0; }
};

struct HoleClassification {
    size_t active_bytes = 0;
    size_t body_bytes = 0;        // Full, page-aligned 4 KiB slabs (madvise returnable)
    size_t head_bytes = 0;        // Unallocated prefix on shared page
    size_t tail_bytes = 0;        // Unallocated suffix on shared page
    size_t sub_page_bytes = 0;    // Unallocated holes < 4 KiB on shared page
    size_t resident_holes_bytes = 0; // Total unallocated bytes backed by physical DRAM

    size_t total_unallocated() const {
        return body_bytes + head_bytes + tail_bytes + sub_page_bytes;
    }

    size_t trapped_bytes() const {
        return head_bytes + tail_bytes + sub_page_bytes;
    }

    double trapped_ratio() const {
        size_t total = total_unallocated();
        return total > 0 ? (static_cast<double>(trapped_bytes()) / static_cast<double>(total)) * 100.0 : 0.0;
    }
};

class PageInspector {
public:
    // Check if a virtual address page is currently resident in physical DRAM via mincore(2)
    static bool is_page_resident(uintptr_t page_addr) {
        unsigned char vec[1] = {0};
        void* ptr = reinterpret_cast<void*>(page_addr & ~(PAGE_SIZE_4K - 1));
        if (mincore(ptr, PAGE_SIZE_4K, vec) == 0) {
            return (vec[0] & 1) != 0;
        }
        return false;
    }

    // Classify memory holes in an arena span [arena_start, arena_end) given a sorted list of active allocations
    static HoleClassification classify_arena(uintptr_t arena_start, uintptr_t arena_end,
                                             const std::vector<Range>& sorted_active_allocs) {
        HoleClassification result;

        uintptr_t current_cursor = arena_start;

        for (const auto& alloc : sorted_active_allocs) {
            if (alloc.start > current_cursor) {
                // There is an unallocated hole: [current_cursor, alloc.start)
                classify_hole(current_cursor, alloc.start, result);
            }
            result.active_bytes += alloc.size();
            current_cursor = std::max(current_cursor, alloc.end);
        }

        if (current_cursor < arena_end) {
            // Trailing hole at the end of the arena
            classify_hole(current_cursor, arena_end, result);
        }

        return result;
    }

private:
    static void classify_hole(uintptr_t hole_start, uintptr_t hole_end, HoleClassification& result) {
        if (hole_end <= hole_start) return;
        size_t hole_size = hole_end - hole_start;

        uintptr_t start_page = hole_start & ~(PAGE_SIZE_4K - 1);
        uintptr_t end_page = (hole_end - 1) & ~(PAGE_SIZE_4K - 1);

        // Case 1: Hole is smaller than 4 KiB and does not cross a 4 KiB page boundary
        if (start_page == end_page && (hole_start % PAGE_SIZE_4K != 0 || hole_size < PAGE_SIZE_4K)) {
            result.sub_page_bytes += hole_size;
            if (is_page_resident(start_page)) {
                result.resident_holes_bytes += hole_size;
            }
            return;
        }

        uintptr_t body_start = (hole_start + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
        uintptr_t body_end = hole_end & ~(PAGE_SIZE_4K - 1);

        // Head segment: from hole_start up to the next page boundary
        if (body_start > hole_start) {
            size_t head_len = body_start - hole_start;
            result.head_bytes += head_len;
            if (is_page_resident(hole_start)) {
                result.resident_holes_bytes += head_len;
            }
        }

        // Body segments: full 4096-byte pages in the middle
        if (body_end > body_start) {
            size_t body_len = body_end - body_start;
            result.body_bytes += body_len;
            for (uintptr_t p = body_start; p < body_end; p += PAGE_SIZE_4K) {
                if (is_page_resident(p)) {
                    result.resident_holes_bytes += PAGE_SIZE_4K;
                }
            }
        }

        // Tail segment: from the last page boundary up to hole_end
        if (hole_end > body_end && body_end >= body_start) {
            size_t tail_len = hole_end - body_end;
            result.tail_bytes += tail_len;
            if (is_page_resident(hole_end - 1)) {
                result.resident_holes_bytes += tail_len;
            }
        }
    }
};

} // namespace lost_bytes

#endif // LOST_BYTES_PAGE_INSPECTOR_HPP
