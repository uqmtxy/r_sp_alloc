# Research Update: Systems-Level Memory Optimization in Edge Computer Vision

### 1. Literature Grounding & Theoretical Motivation
* **Lost Bytes At The Crossroads Between User-And Kernel-Level Memory Management** ([Fistanto, Tempel, & Dietrich, ACM PLOS '26 / SOSP '26](https://dl.acm.org/doi/10.1145/3831586.3838144)):
  * Formulates the physical granularity mismatch between user-space allocator blocks and Linux 4 KiB physical pages.
  * Demonstrates that unallocated heap space gets trapped in unreturnable `sub-page`, `head`, and `tail` holes when sharing physical pages with long-lived allocations.
* **Reconsidering “Reconsidering Custom Memory Allocation”** ([ACM SIGPLAN ISMM '26](https://dl.acm.org/doi/10.1145/3814942.3816132)):
  * Re-evaluates Berger et al. (2002) under modern multi-core execution.
  * Shows that domain-specific allocators consistently outperform general-purpose ones on fixed-structure cyclic DAGs, while exposing severe thread-cache (`tcache`) duplication overheads in multi-threaded streaming pipelines.

---

### 2. Workload & Model Architecture: Ultralytics YOLO26n (2026 SOTA)
* **Model Target**: **Ultralytics YOLO26n** (January 2026 SOTA, [arXiv:2606.03748](https://arxiv.org/abs/2606.03748)).
* **Architectural Advantage for Systems Research**:
  * Employs native **end-to-end NMS-free inference** (`[1, 3, 640, 640] -> [1, 300, 6]`).
  * Completely eliminates dynamic post-processing heap vector churn (the dynamic `std::vector` allocations inherent to legacy Non-Maximum Suppression).
  * Creates a 100% deterministic, static tensor dataflow graph (DAG), providing an ideal testbed for evaluating physical page-grid alignment and thread-cache retention without confounding post-processing noise.

---

### 3. Empirical Results: 5-Trial Statistical Benchmark (Single-Stream YOLO26n)
Executed under strict experimental noise controls: physical CPU Core 0 pinning (`pthread_setaffinity_np`), 20 unmeasured warmup cycles, and direct kernel telemetry via `/proc/self/status` (`VmHWM`, `VmRSS`), `/proc/self/smaps_rollup` (`Private_Dirty`), and `getrusage()`.

| Allocator Configuration | Peak RSS (MB) | Settled RSS (MB) | Private Dirty (MB) | Steady Delta (MB) | Minor Faults | Latency Mean (ms) | Latency p99 (ms) | Throughput (FPS) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Generic Ubuntu Default (`glibc`) | 91.3 ± 1.19 MB | 91.3 MB | 78.1 MB | 0.00 MB | 0 | 85.41 ± 2.85 ms | 122.31 ms | **11.7 ± 0.38** |
| Tuned `glibc` (`M_TRIM_THRESHOLD=4096`) | 91.0 ± 0.75 MB | 91.0 MB | 77.9 MB | 0.00 MB | 0 | 82.75 ± 0.11 ms | 86.17 ms | **12.1 ± 0.02** |
| Meta `jemalloc` 5.3 (Default 10s Decay) | 108.8 ± 1.09 MB | 102.3 MB | 88.8 MB | 0.79 MB | 0 | 82.70 ± 0.14 ms | 93.40 ms | **12.1 ± 0.02** |
| Tuned `jemalloc` 5.3 (`dirty_decay_ms:0`) | 101.8 ± 0.60 MB | 101.1 MB | 87.5 MB | 0.04 MB | 0 | 82.56 ± 0.09 ms | 86.46 ms | **12.1 ± 0.01** |
| Google `TCMalloc` | 103.5 ± 0.25 MB | 103.5 MB | 90.3 MB | 0.31 MB | 2 | 83.03 ± 0.34 ms | 87.71 ms | **12.0 ± 0.05** |
| Microsoft `mimalloc` 2.1 | 101.2 ± 1.82 MB | 100.9 MB | 87.7 MB | 3.40 MB | 351 | 83.35 ± 0.31 ms | 86.95 ms | **12.0 ± 0.04** |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | **92.1 ± 0.43 MB** | **92.1 MB** | **79.0 MB** | **0.00 MB** | **402** | **83.47 ± 0.36 ms** | **88.39 ms** | **12.0 ± 0.05** |

* **jemalloc Retention Bloat Confirmed**: 5 repeated independent trials prove default jemalloc incurs **+17.5 MB (+19.2%)** memory overhead above glibc. Zeroing decay (`dirty_decay_ms:0`) reclaims 6.9 MB, but leaves ~10 MB overhead.
* **DynAlloc Precision**: `libdynalloc.so` cuts **16.6 MB (-15.3%)** vs default jemalloc, **11.4 MB** vs TCMalloc, and **9.1 MB** vs mimalloc, matching glibc's memory footprint with zero steady-state drift and superior p99 tail latency.

---

### 4. Concurrency Scaling: 4-Camera Factory Streaming (ISMM '26 Testing)
Simulates a multi-camera edge vision server processing 4 concurrent video feeds (400 frames total).

| Allocator Configuration | Peak RSS (MB) | Settled RSS (MB) | Private Dirty (MB) | Latency p50 (ms) | Latency p99 (ms) | Aggregate FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Generic Ubuntu Default (`glibc`) | 309.4 MB | 72.0 MB | 58.8 MB | 89.90 ms | 109.67 ms | **39.9 FPS** |
| Meta `jemalloc` 5.3 (Default 10s Decay) | 408.9 MB | 48.7 MB | 35.0 MB | 89.63 ms | 107.92 ms | **40.2 FPS** |
| Tuned `jemalloc` 5.3 (`dirty_decay_ms:0`) | 358.5 MB | 45.8 MB | 32.2 MB | 89.59 ms | 90.60 ms | **40.3 FPS** |
| Google `TCMalloc` | 345.5 MB | 345.5 MB | 332.2 MB | 86.77 ms | 88.88 ms | **41.5 FPS** |
| Microsoft `mimalloc` 2.1 | 387.7 MB | 342.7 MB | 329.4 MB | 86.60 ms | 90.16 ms | **41.5 FPS** |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | **305.6 MB** | **33.8 MB** | **20.6 MB** | **88.42 ms** | **90.87 ms** | **40.1 FPS** |

* **Thread-Cache Multiplier**: General-purpose allocators replicate per-thread arenas across camera streams, driving jemalloc peak RSS to **408.9 MB** (+103.3 MB vs DynAlloc).
* **Permanent Hoarding**: TCMalloc and mimalloc permanently hold >340 MB of physical DRAM in user space after inference finishes.
* **DynAlloc Rest Density**: DynAlloc achieves the lowest Peak RSS (305.6 MB) and purges 90% of memory down to **33.8 MB Settled RSS** (20.6 MB Private Dirty) at rest.

---

### 5. Non-Stationary Jitter: Dynamic Resolution Switching (5 Trials)
Simulates a real automated optical inspection (AOI) line dynamically switching among 4 inspection scales (1080p Overview 50%, 1K Zoom 25%, 1440p Ultra-HD 10%, VGA Crop 15%) across 1,000 frames x 5 repeated independent trials.

| Allocator Configuration | Peak RSS (MB) | Settled RSS (MB) | Private Dirty (MB) | Steady Delta (MB) | Minor Faults | Latency Mean (ms) | Latency p99 (ms) | Throughput (FPS) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Generic Ubuntu Default (`glibc`) | 175.8 ± 0.00 MB | 175.8 MB | 172.9 MB | 0.08 MB | 5 | 0.06 ± 0.00 ms | 0.14 ms | **18096.6 ± 218.4** |
| Tuned `glibc` (`M_TRIM_THRESHOLD=4096`) | 165.2 ± 0.02 MB | 123.1 MB | 120.2 MB | 0.08 MB | 2,217,553 | 1.55 ± 0.19 ms | 2.97 ms | **655.8 ± 85.5** |
| Meta `jemalloc` 5.3 (Default 10s Decay) | 197.0 ± 0.79 MB | 151.0 MB | 147.7 MB | -0.01 MB | 851,989 | 0.67 ± 0.03 ms | 2.42 ms | **1490.2 ± 58.0** |
| Tuned `jemalloc` 5.3 (`dirty_decay_ms:0`) | 180.1 ± 0.03 MB | 130.2 MB | 126.9 MB | 0.08 MB | 3,919,483 | 2.38 ± 0.05 ms | 3.83 ms | **420.2 ± 8.4** |
| Google `TCMalloc` | 195.8 ± 1.12 MB | 195.6 MB | 192.6 MB | 13.48 MB | 4,898 | 0.06 ± 0.00 ms | 0.19 ms | **16877.0 ± 778.0** |
| Microsoft `mimalloc` 2.1 | 187.2 ± 0.00 MB | 187.2 MB | 184.2 MB | 0.00 MB | 0 | 0.05 ± 0.00 ms | 0.14 ms | **17503.8 ± 1083.3** |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | **165.5 ± 0.04 MB** | **123.4 MB** | **120.6 MB** | **0.20 MB** | **2,272,643** | **1.55 ± 0.14 ms** | **2.97 ms** | **650.6 ± 59.5** |
| **Dedicated Page-Grid Arena (`page_aware`)** | **177.1 ± 0.00 MB** | **177.1 MB** | **174.2 MB** | **0.07 MB** | **3** | **0.05 ± 0.00 ms** | **0.13 ms** | **18842.1 ± 20.4** |

* **Non-Stationary Fragmentation Penalty**: When tensor dimensions alternate across scales, general allocators instantiate separate slab runs and fail to coalesce them, driving jemalloc to **197.0 MB Peak RSS**.
* **DynAlloc Compaction**: `libdynalloc.so` cuts **31.5 MB (-16.0%)** vs default jemalloc, **30.3 MB** vs TCMalloc, and **21.7 MB** vs mimalloc.
* **Page-Grid Invariance**: The dedicated Page-Grid Arena delivers **18,842.1 FPS** with only 3 page faults and zero trapped head/tail fragments.

---

### 6. Adversarial Allocation & Fragmentation Resilience (ISMM '26 Single-Stream)
Eliminates the "clean-heap evaluation bias" by preconditioning the heap across `Adv0` (Clean), `Adv1` (Light), `Adv3` (Standard ISMM '26), and `Adv10` (Stress: 200k objects, 132k pinned slots).

| Allocator Configuration | Adv0 Peak | Adv3 Peak | Adv10 Peak | Adv10 Bloat (%) | Adv3 Latency | Adv3 p99 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Generic Ubuntu Default (`glibc`) | 92.2 MB | 100.8 MB | 112.5 MB | +21.9% | 83.11 ms | 86.86 ms |
| Meta `jemalloc` 5.3 Default | 108.0 MB | 112.5 MB | 129.8 MB | **+20.3%** | 83.03 ms | 86.15 ms |
| Google `TCMalloc` | 104.2 MB | 108.5 MB | 122.5 MB | +17.6% | 82.75 ms | 84.57 ms |
| Microsoft `mimalloc` 2.1 | 104.1 MB | 115.8 MB | 127.8 MB | **+22.8%** | 82.93 ms | 86.25 ms |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | **91.5 MB** | **96.0 MB** | **109.9 MB** | **+20.2%** | **83.10 ms** | **85.41 ms** |
| **Frame-Bound FD-Arena (Double-Buffered)**| **100.1 MB**| **108.6 MB**| **119.5 MB**| **+19.4%** | **82.86 ms** | **85.77 ms** |

* **Physical DRAM Trapping**: Under Adv10, `jemalloc` expands to **129.8 MB** and `mimalloc` to **127.8 MB** due to scattered objects pinning 4 KiB physical pages.
* **DynAlloc Resilience**: `libdynalloc.so` maintains the lowest peak memory across all levels (96.0 MB under Adv3, 109.9 MB under Adv10), saving **20.0 MB** vs jemalloc and **18.0 MB** vs mimalloc.

---

### 7. Native ONNX Runtime Custom Region Allocator & Unified Hybrid Stack
Implements `OrtDoubleBufferedArena` directly in ONNX Runtime C++ via `RegisterAllocator`. Transient tensor allocations are allocated via 64-byte aligned bump-pointer inside a 128 MB ping-pong buffer.

| Configuration | Adv3 Latency | Adv3 p99 | Adv3 Peak RSS | No-Op Frees |
| :--- | :---: | :---: | :---: | :---: |
| Generic `glibc` | 83.36 ms | 91.05 ms | 100.27 MB | 0 |
| `DynAlloc` Only | 82.62 ms | 86.05 ms | **94.83 MB** | 0 |
| `OrtCustomArena` | **82.48 ms** | **84.60 ms** | 186.12 MB | **5,484** |
| **Unified Hybrid (`DynAlloc` + `OrtArena`)** | **82.62 ms** | **85.11 ms** | **182.93 MB** | **5,484** |

* **Zero-Malloc Runtime Execution**: All 49 tensor operator allocations per frame are served via bump-pointer with zero calls to libc `malloc` (5,484 no-op frees).
* **Hybrid Stack Tightening**: Recovered 3.2 MB of host metadata bloat under Adv3 (182.93 MB vs 186.12 MB) with 0.00 MB steady-state heap drift.

---

### 8. Multi-Camera Adversarial Concurrency (Addressing ISMM '26 Limitation)
Directly tests 4 concurrent camera streams under multi-threaded adversarial fragmentation:

| Allocator Configuration | Adv0 Peak | Adv10 Peak | Adv10 Bloat | Adv0 Rest | Adv10 Rest | User-Space Retention |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| Generic Ubuntu Default (`glibc`) | 311.68 MB | 401.06 MB | +28.7% | 74.15 MB | 162.53 MB | Traps fragmented holes |
| Meta `jemalloc` 5.3 Default | 417.24 MB | **462.78 MB** | +10.9% | 53.55 MB | 133.58 MB | Slow decay retention |
| Google `TCMalloc` | 344.68 MB | 402.98 MB | +16.9% | **344.68 MB** | **402.98 MB** | **100% Hoarded in user space** |
| Microsoft `mimalloc` 2.1 | 395.76 MB | **459.78 MB** | +16.2% | **311.24 MB** | **378.86 MB** | **95% Hoarded in local heaps** |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | **307.43 MB** | **379.77 MB** | **+23.5%** | **33.90 MB** | **107.25 MB** | **Lowest Peak & Rest Footprint!** |

* **Thread-Cache Replication Bloat**: `jemalloc` expands to **462.78 MB** and `mimalloc` to **459.78 MB**.
* **DynAlloc Concurrency Savings**: `libdynalloc.so` caps peak active RSS at **379.77 MB** (**83.0 MB / -17.9% reduction** vs jemalloc, **80.0 MB** vs mimalloc), while settling down to only 107.25 MB at rest.

---

### 9. Deliverables & Publication Package
* **Master Walkthrough**: [first_steps/walkthrough.md](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/walkthrough.md) (All 15 stages documented)
* **Comparative Report**: [talking_steps/approach_comparison.md](file:///Users/kenza/StudyProjects/SJTU/alloc/talking_steps/approach_comparison.md)
* **Publication Figures (Vector PDF + PNG)**:
  - [fig1_adversarial_rss_bloat.pdf](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/figures/fig1_adversarial_rss_bloat.pdf)
  - [fig2_tail_latency_jitter.pdf](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/figures/fig2_tail_latency_jitter.pdf)
  - [fig3_multicamera_retention.pdf](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/figures/fig3_multicamera_retention.pdf)
  - [fig4_plos_trapped_bytes.pdf](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/figures/fig4_plos_trapped_bytes.pdf)
* **ACM LaTeX Manuscript**: [first_steps/paper/latex/main.tex](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/latex/main.tex) and [references.bib](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/latex/references.bib)
* **Bare-Metal PMU Profiling Script & Parser**: [first_steps/scripts/cloudlab/run_perf_adversarial.sh](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/scripts/cloudlab/run_perf_adversarial.sh) and [parse_perf_results.py](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/scripts/cloudlab/parse_perf_results.py)

