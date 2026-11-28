#pragma once

#include "page_reuse_resource.hpp"
#include "platform_memory.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace utilization {

class GlobalBudgetArbiter;

class IdleReclamationController {
    PageReuseResource pool_;
    size_t budget_bytes_;
    int64_t idle_threshold_ns_;
    int64_t poll_ms_;
    bool trim_system_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool auto_enabled_ = false;

    std::atomic<int64_t> last_activity_ns_{0};
    std::atomic<uint64_t> activity_epoch_{0};
    std::atomic<int> in_flight_frames_{0};
    uint64_t reclaimed_epoch_ = 0;

    std::atomic<size_t> auto_reclaims_{0};
    std::atomic<size_t> explicit_reclaims_{0};
    std::atomic<size_t> pressure_reclaims_{0};
    std::atomic<double> reclaim_ms_total_{0.0};
    std::atomic<size_t> last_idle_private_bytes_{0};
    std::atomic<GlobalBudgetArbiter*> arbiter_{nullptr};
    std::thread watchdog_;

    static int64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void touch_activity_locked() {
        last_activity_ns_.store(now_ns(), std::memory_order_relaxed);
        activity_epoch_.fetch_add(1, std::memory_order_relaxed);
    }

    void run_reclaim_locked(bool is_auto,
                            bool is_pressure = false,
                            size_t override_budget = 0,
                            bool use_override = false) {
        auto t0 = std::chrono::steady_clock::now();
        size_t target = use_override ? override_budget : budget_bytes_;
        pool_.purge_to_budget(target);
        if (trim_system_) trim_system_allocator();
        double elapsed_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
        reclaimed_epoch_ = activity_epoch_.load(std::memory_order_relaxed);
        auto snap = take_process_snapshot();
        last_idle_private_bytes_.store(snap.private_bytes, std::memory_order_relaxed);

        double cur = reclaim_ms_total_.load(std::memory_order_relaxed);
        while (!reclaim_ms_total_.compare_exchange_weak(cur, cur + elapsed_ms, std::memory_order_relaxed)) {
        }
        if (is_pressure) {
            pressure_reclaims_.fetch_add(1, std::memory_order_relaxed);
        } else if (is_auto) {
            auto_reclaims_.fetch_add(1, std::memory_order_relaxed);
        } else {
            explicit_reclaims_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void watchdog_loop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!stop_) {
            cv_.wait_for(lk, std::chrono::milliseconds(poll_ms_), [this] { return stop_; });
            if (stop_) break;
            if (in_flight_frames_.load(std::memory_order_acquire) > 0) continue;
            uint64_t ep = activity_epoch_.load(std::memory_order_acquire);
            if (ep == 0 || ep == reclaimed_epoch_) continue;
            int64_t idle_for = now_ns() - last_activity_ns_.load(std::memory_order_acquire);
            if (idle_for >= idle_threshold_ns_) {
                run_reclaim_locked(true);
            }
        }
    }

public:
    IdleReclamationController(bool auto_enabled,
                              size_t budget_bytes,
                              int64_t idle_threshold_ms = 250,
                              int64_t poll_ms = 20,
                              bool trim_system = true,
                              size_t alignment = 64,
                              size_t capacity = 256ull * 1024 * 1024)
        : pool_(false, alignment, capacity),
          budget_bytes_(budget_bytes),
          idle_threshold_ns_(idle_threshold_ms * 1000000ll),
          poll_ms_(poll_ms),
          trim_system_(trim_system),
          auto_enabled_(auto_enabled) {
        last_activity_ns_.store(now_ns(), std::memory_order_relaxed);
        if (auto_enabled_) {
            watchdog_ = std::thread(&IdleReclamationController::watchdog_loop, this);
        }
    }

    ~IdleReclamationController();

    IdleReclamationController(const IdleReclamationController&) = delete;
    IdleReclamationController& operator=(const IdleReclamationController&) = delete;

    void* allocate(size_t n) {
        std::lock_guard<std::mutex> lk(mu_);
        void* p = pool_.allocate(n);
        touch_activity_locked();
        return p;
    }

    void deallocate(void* p) {
        if (!p) return;
        std::lock_guard<std::mutex> lk(mu_);
        pool_.deallocate(p);
        touch_activity_locked();
    }

    void mark_frame_start() {
        in_flight_frames_.fetch_add(1, std::memory_order_acq_rel);
        last_activity_ns_.store(now_ns(), std::memory_order_relaxed);
    }

    void mark_frame_end() {
        last_activity_ns_.store(now_ns(), std::memory_order_release);
        in_flight_frames_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void explicit_reclaim() {
        std::lock_guard<std::mutex> lk(mu_);
        run_reclaim_locked(false);
    }

    PageAccounting sample_pages() const {
        std::lock_guard<std::mutex> lk(mu_);
        return pool_.sample();
    }

    size_t pressure_reclaim(size_t target_budget) {
        std::lock_guard<std::mutex> lk(mu_);
        size_t before = pool_.sample().resident_body;
        if (before <= target_budget) return 0;
        run_reclaim_locked(false, true, target_budget, true);
        size_t after = pool_.sample().resident_body;
        return before > after ? (before - after) : 0;
    }

    size_t resident_free_body_bytes() const {
        std::lock_guard<std::mutex> lk(mu_);
        return pool_.sample().resident_body;
    }

    bool is_idle() const {
        return in_flight_frames_.load(std::memory_order_acquire) == 0;
    }

    int64_t last_activity_ns() const {
        return last_activity_ns_.load(std::memory_order_acquire);
    }

    void set_arbiter(GlobalBudgetArbiter* arbiter) {
        arbiter_.store(arbiter, std::memory_order_release);
    }

    size_t auto_reclaims() const { return auto_reclaims_.load(std::memory_order_relaxed); }
    size_t explicit_reclaims() const { return explicit_reclaims_.load(std::memory_order_relaxed); }
    size_t pressure_reclaims() const { return pressure_reclaims_.load(std::memory_order_relaxed); }
    size_t total_reclaims() const {
        return auto_reclaims() + explicit_reclaims() + pressure_reclaims();
    }
    double reclaim_ms_total() const { return reclaim_ms_total_.load(std::memory_order_relaxed); }
    size_t last_idle_private_bytes() const { return last_idle_private_bytes_.load(std::memory_order_relaxed); }
    size_t page_size() const { return pool_.page_size(); }
};

class GlobalBudgetArbiter {
    size_t global_budget_bytes_;
    mutable std::mutex arbiter_mu_;
    std::vector<IdleReclamationController*> controllers_;

public:
    explicit GlobalBudgetArbiter(size_t global_budget_bytes)
        : global_budget_bytes_(global_budget_bytes) {}

    ~GlobalBudgetArbiter() {
        std::vector<IdleReclamationController*> snapshot;
        {
            std::lock_guard<std::mutex> lk(arbiter_mu_);
            snapshot.swap(controllers_);
        }
        for (auto* ctrl : snapshot) {
            if (ctrl) ctrl->set_arbiter(nullptr);
        }
    }

    GlobalBudgetArbiter(const GlobalBudgetArbiter&) = delete;
    GlobalBudgetArbiter& operator=(const GlobalBudgetArbiter&) = delete;

    void register_controller(IdleReclamationController* ctrl) {
        if (!ctrl) return;
        {
            std::lock_guard<std::mutex> lk(arbiter_mu_);
            if (std::find(controllers_.begin(), controllers_.end(), ctrl) == controllers_.end()) {
                controllers_.push_back(ctrl);
            }
        }
        ctrl->set_arbiter(this);
    }

    void unregister_controller(IdleReclamationController* ctrl) {
        if (!ctrl) return;
        {
            std::lock_guard<std::mutex> lk(arbiter_mu_);
            controllers_.erase(
                std::remove(controllers_.begin(), controllers_.end(), ctrl),
                controllers_.end());
        }
        ctrl->set_arbiter(nullptr);
    }

    size_t controller_count() const {
        std::lock_guard<std::mutex> lk(arbiter_mu_);
        return controllers_.size();
    }

    size_t total_resident_free_body_bytes() const {
        std::vector<IdleReclamationController*> snapshot;
        {
            std::lock_guard<std::mutex> lk(arbiter_mu_);
            snapshot = controllers_;
        }
        size_t total = 0;
        for (auto* ctrl : snapshot) {
            if (ctrl) total += ctrl->resident_free_body_bytes();
        }
        return total;
    }

    size_t enforce_budget() {
        std::vector<IdleReclamationController*> snapshot;
        {
            std::lock_guard<std::mutex> lk(arbiter_mu_);
            snapshot = controllers_;
        }
        struct Candidate {
            IdleReclamationController* ctrl;
            size_t free_body;
            bool idle;
            int64_t last_ns;
        };
        std::vector<Candidate> cands;
        cands.reserve(snapshot.size());
        size_t total_free = 0;
        for (auto* ctrl : snapshot) {
            if (!ctrl) continue;
            size_t fb = ctrl->resident_free_body_bytes();
            total_free += fb;
            cands.push_back({ctrl, fb, ctrl->is_idle(), ctrl->last_activity_ns()});
        }
        if (total_free <= global_budget_bytes_) return 0;

        size_t excess = total_free - global_budget_bytes_;
        // Reclaim idle victims first (oldest activity first, then largest free body).
        std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) {
            if (a.idle != b.idle) return a.idle > b.idle;
            if (a.last_ns != b.last_ns) return a.last_ns < b.last_ns;
            return a.free_body > b.free_body;
        });

        size_t total_reclaimed = 0;
        for (const auto& c : cands) {
            if (excess == 0) break;
            if (!c.idle || c.free_body == 0) continue;
            size_t target = c.free_body > excess ? (c.free_body - excess) : 0;
            size_t reclaimed = c.ctrl->pressure_reclaim(target);
            total_reclaimed += reclaimed;
            excess = excess > reclaimed ? (excess - reclaimed) : 0;
        }
        return total_reclaimed;
    }

    size_t notify_burst_start() {
        return enforce_budget();
    }
};

inline IdleReclamationController::~IdleReclamationController() {
    if (auto_enabled_) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (watchdog_.joinable()) watchdog_.join();
    }
    if (auto* a = arbiter_.exchange(nullptr, std::memory_order_acq_rel)) {
        a->unregister_controller(this);
    }
}

}  // namespace utilization
