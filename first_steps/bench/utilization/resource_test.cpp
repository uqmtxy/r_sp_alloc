#include "page_reuse_resource.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <unistd.h>
using namespace utilization;
int main() {
    const size_t P = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t cap = 16 * P;
    const size_t b_size = 3 * P;
    PageReuseResource arena(false, 64, cap);
    auto* a = static_cast<unsigned char*>(arena.allocate(1000));
    auto* b = static_cast<unsigned char*>(arena.allocate(b_size));
    auto* c = static_cast<unsigned char*>(arena.allocate(1000));
    assert(reinterpret_cast<uintptr_t>(a) % 64 == 0);
    assert(reinterpret_cast<uintptr_t>(b) % 64 == 0);
    memset(a, 17, 1000); memset(b, 31, b_size); memset(c, 43, 1000);
    auto full = arena.sample();
    assert(full.resident == full.resident_payload + full.waste());
    assert(full.resident_payload == 2000 + b_size);
    arena.deallocate(b);
    auto before = arena.sample();
    assert(before.resident_body >= P);
    arena.purge();
    auto after = arena.sample();
    assert(after.resident < before.resident && after.resident_body == 0);
    for (int i = 0; i < 1000; ++i) assert(a[i] == 17 && c[i] == 43);
    // The freed block is reusable without touching either live neighbor.
    auto* reused = static_cast<unsigned char*>(arena.allocate(b_size));
    assert(reused == b);
    memset(reused, 61, b_size);
    for (int i = 0; i < 1000; ++i) assert(a[i] == 17 && c[i] == 43);
    arena.deallocate(a); arena.deallocate(c); arena.deallocate(reused);
    // Both-side merging makes the entire mapping purgeable.
    arena.purge(); assert(arena.sample().resident == 0);
    auto* entire = arena.allocate(cap); assert(entire == a);
    bool overflow = false;
    try { arena.allocate(1); } catch (const std::bad_alloc&) { overflow = true; }
    assert(overflow);
    memset(entire, 3, cap);
    arena.deallocate(entire);
    arena.purge_to_budget(2 * P);
    assert(arena.sample().resident_body == 2 * P);
    arena.purge_to_budget(P - 1);
    assert(arena.sample().resident_body == 0);
    bool duplicate = false;
    try { arena.deallocate(entire); } catch (const std::runtime_error&) { duplicate = true; }
    assert(duplicate);
    PageReuseResource page_aligned(true, P, cap);
    auto* d = page_aligned.allocate(3);
    assert(reinterpret_cast<uintptr_t>(d) % P == 0);
    memset(d, 1, 3); page_aligned.deallocate(d);
    assert(page_aligned.sample().resident == 0);
    std::cout << "page residency (" << P << " B), conservation, coalescing, reuse, alignment, bounds, live-neighbor preservation: PASS\n";
}
