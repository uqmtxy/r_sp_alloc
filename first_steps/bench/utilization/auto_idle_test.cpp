#include "idle_reclaimer.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using namespace utilization;

int main() {
    const size_t P = os_page_size();
    const size_t cap = 64 * P;
    const size_t budget = 2 * P;

    IdleReclamationController ctrl(true, budget, /*idle_ms=*/120, /*poll_ms=*/15, /*trim_system=*/false, 64, cap);

    // Hold a small retained output alive across bursts
    auto* retained = static_cast<unsigned char*>(ctrl.allocate(1024));
    std::memset(retained, 0x3C, 1024);

    // 1. Active burst: alloc/free every 10 ms (< 120 ms) -> MUST NOT trigger auto reclaim
    for (int i = 0; i < 12; ++i) {
        ctrl.mark_frame_start();
        auto* tmp = static_cast<unsigned char*>(ctrl.allocate(16 * P));
        std::memset(tmp, 0x7E, 16 * P);
        ctrl.deallocate(tmp);
        ctrl.mark_frame_end();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ctrl.auto_reclaims() == 0);

    // 2. Short jitter gap: 50 ms (< 120 ms) -> MUST NOT trigger auto reclaim
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(ctrl.auto_reclaims() == 0);

    // Allocate & free large block right before true idle pause
    {
        ctrl.mark_frame_start();
        auto* burst_buf = static_cast<unsigned char*>(ctrl.allocate(24 * P));
        std::memset(burst_buf, 0x55, 24 * P);
        auto before = ctrl.sample_pages();
        assert(before.resident_payload >= 24 * P);
        ctrl.deallocate(burst_buf);
        ctrl.mark_frame_end();
    }

    // 3. True idle pause: 260 ms (> 120 ms) -> MUST trigger EXACTLY 1 reclaim
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    assert(ctrl.auto_reclaims() == 1);
    auto after = ctrl.sample_pages();
    assert(after.resident == after.resident_payload + after.waste());
    assert(after.resident_body <= budget);
    for (int i = 0; i < 1024; ++i) assert(retained[i] == 0x3C);

    // 4. Continued idle without new activity (300 ms) -> MUST NOT re-trigger redundantly
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(ctrl.auto_reclaims() == 1);

    // 5. In-flight frame protection: even if frame runs longer than idle_ms, watchdog waits
    ctrl.mark_frame_start();
    auto* long_op = static_cast<unsigned char*>(ctrl.allocate(8 * P));
    std::memset(long_op, 0x11, 8 * P);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    assert(ctrl.auto_reclaims() == 1);  // still 1 while in_flight_frames_ > 0
    ctrl.deallocate(long_op);
    ctrl.mark_frame_end();

    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    assert(ctrl.auto_reclaims() == 2);
    ctrl.deallocate(retained);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(ctrl.auto_reclaims() == 3);
    assert(ctrl.sample_pages().resident <= budget);

    std::cout << "auto_idle_test PASS (page_size=" << P
              << ", auto_reclaims=" << ctrl.auto_reclaims()
              << ", reclaim_ms_total=" << ctrl.reclaim_ms_total() << ")\n";
    return 0;
}
