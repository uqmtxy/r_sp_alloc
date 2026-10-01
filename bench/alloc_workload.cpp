#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Config {
  int threads = 4;
  int seconds = 60;
  int live_objects_per_thread = 200000;
  int min_size = 16;
  int max_size = 4096;
  int churn_batch = 512;
  int touch_bytes = 64;
  uint64_t seed = 0xC0FFEE;
};

struct Stats {
  uint64_t allocations = 0;
  uint64_t frees = 0;
  uint64_t requested_bytes = 0;
  uint64_t live_requested_bytes = 0;
  uint64_t live_requested_bytes_before_teardown = 0;
  uint64_t peak_live_requested_bytes = 0;
};

int read_int_arg(char** argv, int argc, const std::string& name, int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      return std::max(0, std::atoi(argv[i + 1]));
    }
  }
  return fallback;
}

uint64_t read_u64_arg(char** argv, int argc, const std::string& name, uint64_t fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      return std::strtoull(argv[i + 1], nullptr, 10);
    }
  }
  return fallback;
}

Config parse_config(int argc, char** argv) {
  Config cfg;
  cfg.threads = read_int_arg(argv, argc, "--threads", cfg.threads);
  cfg.seconds = read_int_arg(argv, argc, "--seconds", cfg.seconds);
  cfg.live_objects_per_thread = read_int_arg(argv, argc, "--live-objects", cfg.live_objects_per_thread);
  cfg.min_size = read_int_arg(argv, argc, "--min-size", cfg.min_size);
  cfg.max_size = read_int_arg(argv, argc, "--max-size", cfg.max_size);
  cfg.churn_batch = read_int_arg(argv, argc, "--churn-batch", cfg.churn_batch);
  cfg.touch_bytes = read_int_arg(argv, argc, "--touch-bytes", cfg.touch_bytes);
  cfg.seed = read_u64_arg(argv, argc, "--seed", cfg.seed);

  if (cfg.threads <= 0) cfg.threads = 1;
  if (cfg.min_size <= 0) cfg.min_size = 1;
  if (cfg.max_size < cfg.min_size) cfg.max_size = cfg.min_size;
  if (cfg.churn_batch <= 0) cfg.churn_batch = 1;
  if (cfg.live_objects_per_thread <= 0) cfg.live_objects_per_thread = 1;
  return cfg;
}

void touch_memory(void* ptr, int size, int touch_bytes) {
  const int n = std::min(size, std::max(1, touch_bytes));
  std::memset(ptr, 0xA5, static_cast<size_t>(n));
}

void worker(int id, const Config& cfg, std::atomic<bool>& stop, Stats& stats) {
  std::mt19937_64 rng(cfg.seed + static_cast<uint64_t>(id) * 0x9E3779B97F4A7C15ULL);
  std::uniform_int_distribution<int> size_dist(cfg.min_size, cfg.max_size);
  std::uniform_int_distribution<int> slot_dist(0, cfg.live_objects_per_thread - 1);

  struct Allocation {
    void* ptr = nullptr;
    int size = 0;
  };

  std::vector<Allocation> live(static_cast<size_t>(cfg.live_objects_per_thread));

  for (auto& item : live) {
    item.size = size_dist(rng);
    item.ptr = std::malloc(static_cast<size_t>(item.size));
    if (!item.ptr) std::abort();
    touch_memory(item.ptr, item.size, cfg.touch_bytes);
    stats.allocations++;
    stats.requested_bytes += static_cast<uint64_t>(item.size);
    stats.live_requested_bytes += static_cast<uint64_t>(item.size);
    stats.peak_live_requested_bytes = std::max(stats.peak_live_requested_bytes, stats.live_requested_bytes);
  }

  while (!stop.load(std::memory_order_relaxed)) {
    for (int i = 0; i < cfg.churn_batch; ++i) {
      const int slot = slot_dist(rng);
      auto& item = live[static_cast<size_t>(slot)];
      stats.live_requested_bytes -= static_cast<uint64_t>(item.size);
      std::free(item.ptr);
      stats.frees++;

      item.size = size_dist(rng);
      item.ptr = std::malloc(static_cast<size_t>(item.size));
      if (!item.ptr) std::abort();
      touch_memory(item.ptr, item.size, cfg.touch_bytes);
      stats.allocations++;
      stats.requested_bytes += static_cast<uint64_t>(item.size);
      stats.live_requested_bytes += static_cast<uint64_t>(item.size);
      stats.peak_live_requested_bytes = std::max(stats.peak_live_requested_bytes, stats.live_requested_bytes);
    }
  }

  stats.live_requested_bytes_before_teardown = stats.live_requested_bytes;

  for (auto& item : live) {
    stats.live_requested_bytes -= static_cast<uint64_t>(item.size);
    std::free(item.ptr);
    stats.frees++;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = parse_config(argc, argv);
  std::vector<std::thread> threads;
  std::vector<Stats> per_thread(static_cast<size_t>(cfg.threads));
  std::atomic<bool> stop{false};

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < cfg.threads; ++i) {
    threads.emplace_back(worker, i, std::cref(cfg), std::ref(stop), std::ref(per_thread[static_cast<size_t>(i)]));
  }

  std::this_thread::sleep_for(std::chrono::seconds(cfg.seconds));
  stop.store(true, std::memory_order_relaxed);

  for (auto& thread : threads) {
    thread.join();
  }

  const auto end = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(end - start).count();

  Stats total;
  for (const auto& stats : per_thread) {
    total.allocations += stats.allocations;
    total.frees += stats.frees;
    total.requested_bytes += stats.requested_bytes;
    total.live_requested_bytes += stats.live_requested_bytes;
    total.live_requested_bytes_before_teardown += stats.live_requested_bytes_before_teardown;
    total.peak_live_requested_bytes += stats.peak_live_requested_bytes;
  }

  std::cout
      << "threads=" << cfg.threads << '\n'
      << "seconds=" << cfg.seconds << '\n'
      << "live_objects_per_thread=" << cfg.live_objects_per_thread << '\n'
      << "min_size=" << cfg.min_size << '\n'
      << "max_size=" << cfg.max_size << '\n'
      << "allocations=" << total.allocations << '\n'
      << "frees=" << total.frees << '\n'
      << "requested_bytes_total=" << total.requested_bytes << '\n'
      << "live_requested_bytes_before_teardown=" << total.live_requested_bytes_before_teardown << '\n'
      << "peak_live_requested_bytes=" << total.peak_live_requested_bytes << '\n'
      << "live_requested_bytes_end=" << total.live_requested_bytes << '\n'
      << "elapsed_seconds=" << elapsed << '\n'
      << "allocations_per_second=" << static_cast<double>(total.allocations) / elapsed << '\n';
}
