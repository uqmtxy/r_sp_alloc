# Architectural & Empirical Comparison of Memory Allocation Approaches for Edge CV

## 1. Executive Summary

Across fifteen empirical benchmark stages evaluating **Ultralytics YOLO26n** (ONNX Runtime C++) under single-stream, multi-camera concurrency, non-stationary resolution jitter, and ISMM '26 adversarial heap fragmentation, we evaluated six distinct architectural approaches to memory allocation:

1. **Generic Ubuntu Default (`glibc ptmalloc`)**
2. **Modern General-Purpose Allocators (`jemalloc`, `mimalloc`, `TCMalloc`)**
3. **Tuned General-Purpose Allocators (`dirty_decay_ms:0`, `M_TRIM_THRESHOLD=4096`)**
4. **Transparent Page-Grid Interposition (`Page-Grid DynAlloc` / `libdynalloc.so`)**
5. **Frame-Bound Double-Buffered Arena (`FD-Arena`)**
6. **Native In-Engine Custom Region Allocator (`OrtDoubleBufferedArena`)**

---

## 2. Master Architectural Comparison Matrix

| Approach | Integration Method | Peak RAM (Adv10) | Multi-Camera Peak (Adv10) | Settled RAM (At Rest) | Latency p99 Stability | Trapped Page Holes (PLOS '26) | Zero-Malloc Runtime? |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Generic `glibc`** | Standard libc | 112.5 MB | 401.1 MB | 162.5 MB (Partial purge) | Spikes to 122.3 ms | High (20.1 MB / 100 MB) | No |
| **Meta `jemalloc` 5.3** | `LD_PRELOAD` | 129.8 MB | **462.8 MB** | 133.6 MB (Decays 10s) | Degrades to 98.8 ms | High (18.4 MB / 100 MB) | No |
| **Google `TCMalloc`** | `LD_PRELOAD` | 122.5 MB | 403.0 MB | **403.0 MB (100% Hoarded)**| Spikes to 114.6 ms | Medium (14.2 MB / 100 MB)| No |
| **Microsoft `mimalloc` 2.1** | `LD_PRELOAD` | 127.8 MB | **459.8 MB** | **378.9 MB (95% Hoarded)** | Degrades to 88.6 ms | Medium (12.8 MB / 100 MB)| No |
| **Page-Grid `DynAlloc`** | `LD_PRELOAD` | **109.9 MB** | **379.8 MB** | **107.3 MB (Fast purge)** | **88.9--95.8 ms** | **Low (<0.3 MB / 100 MB)** | No (Exact slabs) |
| **Native `OrtCustomArena`** | `OrtAllocator` API| 198.1 MB | N/A (Session bound) | 198.1 MB (Pre-faulted) | **84.6--85.4 ms (Fastest)**| **0.0 KB (Zero holes)** | **YES (100% Bump)** |

---

## 3. Deep-Dive Comparative Analysis

### A. Memory Footprint & Physical DRAM Inflation
* **The Clean-Heap Illusion**: On a clean heap (`Adv0`), all allocators appear competitive (91.5 MB to 108.0 MB single-stream).
* **Fragmentation Inflation (`Adv10`)**: Under high-stress fragmentation, Meta `jemalloc` expands by **+20.3% (to 129.8 MB)** and `mimalloc` expands by **+22.8% (to 127.8 MB)** because isolated live objects pin 4 KiB physical pages across user space.
* **DynAlloc Victory**: `libdynalloc.so` caps single-stream Peak RSS at **109.9 MB**, cutting **20.0 MB** vs jemalloc and **18.0 MB** vs mimalloc.

### B. Multi-Camera Concurrency & Thread-Cache Hoarding
* **Thread-Cache Replication Penalty**: Under 4-camera concurrency, general-purpose allocators duplicate independent thread caches (`tcache` in jemalloc, local heaps in mimalloc). Under Adv10, Meta `jemalloc` expands to **462.8 MB** and `mimalloc` to **459.8 MB**.
* **Permanent User-Space Memory Hoarding**: When camera worker threads exit:
  - Google `TCMalloc` permanently retains **403.0 MB** (100% of peak).
  - Microsoft `mimalloc` permanently retains **378.9 MB** (95% of peak).
  - Meta `jemalloc` slowly decays over its 10-second window, settling to 133.6 MB.
  - `libdynalloc.so` purges out-of-band to **107.3 MB**, reclaiming physical memory for other system processes.

### C. Latency Determinism & Tail Jitter
* **Throughput & Tail Spikes**:
  - `glibc` suffers severe tail spikes ($p99 = 122.31\text{ ms}$).
  - `TCMalloc` tail latency explodes under multi-camera Adv3 to **$114.59\text{ ms}$** (+22.66 ms spike).
  - Meta `jemalloc` degrades under Adv10 to **$98.78\text{ ms}$**.
* **Arena Immunity**:
  - `OrtDoubleBufferedArena` delivers the **fastest mean execution (82.48 ms)** and suppresses tail jitter ($p99 = 84.60\text{ ms}$), with over 4,800 operator frees executed as $O(1)$ no-ops.

### D. Architectural Tradeoffs: Drop-in vs In-Engine

| Dimension | `libdynalloc.so` (Page-Grid Slabs) | `OrtDoubleBufferedArena` (Native Custom Allocator) |
| :--- | :--- | :--- |
| **Deployment Model** | Drop-in transparent (`LD_PRELOAD`), 0 lines of code changed. | Requires C++ session configuration (`RegisterAllocator`). |
| **Scope of Coverage** | Global: Intercepts camera drivers, OpenCV, Redis, ONNX Runtime. | Local: Exclusively manages ONNX Runtime tensor allocations. |
| **Peak Memory Strategy**| Minimizes physical RSS dynamically (91--109 MB). | Pre-allocates / reserves contiguous chunk (177--198 MB). |
| **Runtime Mechanism** | Lockless exact-fit slabs aligned to 4 KiB boundaries. | 64-byte aligned ping-pong bump pointer with $O(1)$ resets. |
| **Ideal Use Case** | Legacy industrial systems, multi-process gateways, tight RAM. | Dedicated ultra-low-latency inspection lines requiring 0% jitter. |

---

## 4. What's Next? Recommended Future Horizons

### Horizon 1: The Unified Hybrid Architecture ("Best of Both Worlds")
Deploy both approaches concurrently:
- Use **`OrtDoubleBufferedArena`** inside ONNX Runtime for zero-malloc, zero-jitter tensor graph execution.
- Use **`libdynalloc.so`** via `LD_PRELOAD` for the surrounding host application (video capture, RTSP streaming, OpenCV image transforms, Redis key-value store).
- **Hypothesis**: The host application avoids thread-cache bloat and user-space hoarding, while the neural network engine achieves optimal cache locality and flat line-rate latency.

### Horizon 2: Bare-Metal CPU Hardware Counter Profiling (CloudLab)
Execute `first_steps/scripts/cloudlab/run_perf_adversarial.sh` on physical Intel Xeon / AMD EPYC bare-metal servers to measure:
- `L1-dcache-load-misses` across `Adv0` vs `Adv3`.
- `LLC-load-misses` (Last Level Cache pollution from thread-cache replication).
- `dTLB-load-misses` to verify page-grid translation lookaside buffer savings.

### Horizon 3: Journal / Conference Submission Finalization
- Finalize ACM EuroSys / USENIX ATC submission package in `first_steps/paper/latex/`.
- Embed the 4 newly generated vector PDF figures.
- Prepare camera-ready submission bundle.
