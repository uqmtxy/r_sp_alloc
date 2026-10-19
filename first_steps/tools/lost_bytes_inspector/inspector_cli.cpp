#include "page_inspector.hpp"
#include <iostream>
#include <iomanip>

int main() {
    using namespace lost_bytes;

    std::cout << "Testing PageInspector on simulated heap segment..." << std::endl;

    // Allocate 16 pages (64 KiB) using mmap
    size_t arena_size = 16 * PAGE_SIZE_4K;
    void* mem = mmap(nullptr, arena_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        std::cerr << "mmap failed" << std::endl;
        return 1;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(mem);

    // Scenario 1: Unaligned allocations creating head, tail, and sub-page holes
    // Alloc 1: starts at +1000 B, size 2500 B (straddles page 0 into page 0)
    // Alloc 2: starts at +5000 B, size 3000 B (page 1)
    // Alloc 3: starts at +20000 B, size 10000 B (page 4 to 7)
    std::vector<Range> allocs = {
        {base + 1000, base + 3500},
        {base + 5000, base + 8000},
        {base + 20000, base + 30000}
    };

    // Touch allocations so physical pages are faulted in
    for (const auto& a : allocs) {
        char* p = reinterpret_cast<char*>(a.start);
        for (size_t i = 0; i < a.size(); i += 1024) {
            p[i] = 1;
        }
    }

    HoleClassification h = PageInspector::classify_arena(base, base + arena_size, allocs);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Arena Size       : " << arena_size / 1024.0 << " KiB\n"
              << "Active Payload   : " << h.active_bytes / 1024.0 << " KiB\n"
              << "Body Bytes       : " << h.body_bytes / 1024.0 << " KiB (madvise returnable)\n"
              << "Head Bytes       : " << h.head_bytes / 1024.0 << " KiB (trapped)\n"
              << "Tail Bytes       : " << h.tail_bytes / 1024.0 << " KiB (trapped)\n"
              << "Sub-page Holes   : " << h.sub_page_bytes / 1024.0 << " KiB (trapped)\n"
              << "Total Trapped    : " << h.trapped_bytes() / 1024.0 << " KiB\n"
              << "Trapped Ratio    : " << h.trapped_ratio() << " %\n"
              << "Resident Holes   : " << h.resident_holes_bytes / 1024.0 << " KiB DRAM\n";

    munmap(mem, arena_size);
    std::cout << "Test completed successfully." << std::endl;
    return 0;
}
