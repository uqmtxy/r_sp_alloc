#include "idle_reclaimer.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace utilization;

namespace {

void touch_pages(void* p, size_t n, size_t page_size) {
    auto* bytes = static_cast<unsigned char*>(p);
    for (size_t off = 0; off < n; off += page_size) {
        bytes[off] = static_cast<unsigned char>((off / page_size) & 0xff);
    }
}

void test_cooperative_arbiter_sheds_oldest_idle_only() {
    constexpr size_t kMiB = 1024ull * 1024ull;
    constexpr size_t kBudget = 16ull * kMiB;
    GlobalBudgetArbiter arbiter(kBudget);

    IdleReclamationController c0(false, 64 * kMiB, 5000, 20, false);
    IdleReclamationController c1(false, 64 * kMiB, 5000, 20, false);
    IdleReclamationController c2(false, 64 * kMiB, 5000, 20, false);
    IdleReclamationController c3(false, 64 * kMiB, 5000, 20, false);

    arbiter.register_controller(&c0);
    arbiter.register_controller(&c1);
    arbiter.register_controller(&c2);
    arbiter.register_controller(&c3);
    assert(arbiter.controller_count() == 4);

    const size_t ps = c0.page_size();
    const size_t kChunk = 10ull * kMiB;

    // Stream 0: oldest idle with ~10 MiB free body.
    void* p0 = c0.allocate(kChunk);
    touch_pages(p0, kChunk, ps);
    c0.deallocate(p0);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // Stream 1: newer idle with ~10 MiB free body.
    void* p1 = c1.allocate(kChunk);
    touch_pages(p1, kChunk, ps);
    c1.deallocate(p1);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // Stream 2: active stream with ~10 MiB free body (must NOT be reclaimed).
    c2.mark_frame_start();
    void* p2 = c2.allocate(kChunk);
    touch_pages(p2, kChunk, ps);
    c2.deallocate(p2);

    size_t before = arbiter.total_resident_free_body_bytes();
    assert(before > kBudget);

    size_t reclaimed = arbiter.notify_burst_start();
    size_t after = arbiter.total_resident_free_body_bytes();

    assert(reclaimed > 0);
    assert(after <= kBudget);
    assert(c2.pressure_reclaims() == 0);
    assert(c0.pressure_reclaims() >= 1);

    c2.mark_frame_end();
    std::printf(
        "test_cooperative_arbiter_sheds_oldest_idle_only PASS: before=%zu after=%zu reclaimed=%zu c0_pr=%zu c1_pr=%zu c2_pr=%zu\n",
        before, after, reclaimed, c0.pressure_reclaims(), c1.pressure_reclaims(), c2.pressure_reclaims());
}

void test_destruction_order_safety() {
    // Case A: controller destroyed before arbiter.
    {
        GlobalBudgetArbiter arbiter(8ull * 1024 * 1024);
        auto c = std::make_unique<IdleReclamationController>(true, 0, 50, 10, false);
        arbiter.register_controller(c.get());
        assert(arbiter.controller_count() == 1);
        c.reset();
        assert(arbiter.controller_count() == 0);
    }
    // Case B: arbiter destroyed before controller.
    {
        IdleReclamationController c(true, 0, 50, 10, false);
        {
            GlobalBudgetArbiter arbiter(8ull * 1024 * 1024);
            arbiter.register_controller(&c);
            assert(arbiter.controller_count() == 1);
        }
        void* p = c.allocate(65536);
        assert(p != nullptr);
        c.deallocate(p);
    }
    std::printf("test_destruction_order_safety PASS\n");
}

}  // namespace

int main() {
    test_cooperative_arbiter_sheds_oldest_idle_only();
    test_destruction_order_safety();
    return 0;
}
