# Technical Execution Plan: PLOS '23 + ISMM '24 + YOLOv8 + Bare Metal

## Objective
A direct, stage-based technical plan executing:
1. **Lost Bytes (ACM PLOS '23)**: Granularity mismatch elimination, $4\text{ KiB}$ page-grid alignment, $H_{\text{min}}$ virtual hole coalescing.
2. **Reconsidering Custom Memory Allocation (ACM ISMM '24)**: Testing boundaries of custom vision allocators vs general-purpose allocators.
3. **Model**: Ultralytics YOLO11 / YOLOv8.
4. **Environment**: Bare-metal compute on CloudLab.
5. **Fairness**: Tuned baselines (`jemalloc` with `dirty_decay_ms:0`, `glibc` with `M_TRIM_THRESHOLD`), hybrid small-tensor slab thresholding.

---

## The 5 Concrete Stages

### Stage 1: Baseline Fairness & Small-Buffer Hybrid Slabs
* **Goal**: Fix the small-tensor padding penalty and re-test against properly tuned baseline allocators.
* **Actions**:
  1. **Tune General-Purpose Baselines**:
     * `jemalloc`: Set `MALLOC_CONF="dirty_decay_ms:0,muzzy_decay_ms:0"` (forces immediate dirty page purging, matching our arena).
     * `glibc`: Inject `mallopt(M_TRIM_THRESHOLD, 4096)` and `mallopt(M_MMAP_THRESHOLD, 65536)`.
  2. **Implement Hybrid Sizing in Arena**:
     * Buffers $< 4096\text{ B}$ (e.g. detection boxes): Route to compact slab slots without $4\text{ KiB}$ padding.
     * Buffers $\ge 4096\text{ B}$ (feature maps): Enforce strict $4\text{ KiB}$ grid alignment and $H_{\text{min}}$ hole coalescing.
  3. **Target Metric**: Beat `glibc`'s 147.7 MB peak RSS while maintaining 0.0 KB trapped DRAM.

---

### Stage 2: Testing *Reconsidering Custom Memory Allocation* (ISMM '24) on YOLO
* **Goal**: Measure the exact boundary where custom vision memory management beats vs. loses to general-purpose allocators.
* **Actions**:
  1. **Evaluate 3 Boundary Regimes**:
     * *Regime A (Static DAG)*: Fixed $640 \times 640$ frame stream, batch = 1.
     * *Regime B (Dynamic Resolution Jitter)*: Random frame-by-frame resolution changes ($480\text{p} \longleftrightarrow 720\text{p} \longleftrightarrow 1080\text{p}$).
     * *Regime C (Multi-Camera Concurrency)*: 1, 4, and 8 concurrent camera threads.
  2. **Measure Hardware Metrics via Linux `perf`**:
     * CPU cycles per allocation / deallocation.
     * L1 data cache misses (`L1-dcache-load-misses`) and Last-Level Cache misses (`LLC-load-misses`).
     * Internal fragmentation vs. external page pinning.

---

### Stage 3: Real ONNX Runtime C++ Pipeline with YOLOv8/11
* **Goal**: Move from synthetic DAG loops to executing real exported `.onnx` models via the C++ ONNX Runtime API.
* **Actions**:
  1. Export `yolov8n.onnx` and `yolo11n.onnx`.
  2. Implement `first_steps/bench/onnx_cv_pipeline/onnx_runner.cpp` using the official ONNX Runtime C++ API (`Ort::Session`).
  3. Stream 1,000 real video frames through the engine and log Resident Set Size (RSS) curves and minor page faults across allocators.

---

### Stage 4: CloudLab Bare-Metal Profiling
* **Goal**: Eliminate hypervisor, VM, and Docker container noise by benchmarking directly on bare-metal silicon.
* **Actions**:
  1. Provision bare-metal server on [CloudLab](https://cloudlab.us/signup.php) (Ubuntu 24.04 LTS on physical Intel Xeon or AMD EPYC / ARM Neoverse hardware).
  2. Pin workload to isolated physical CPU cores (`taskset -c 2` or `isolcpus`) and lock CPU clock governors to `performance`.
  3. Run hardware performance counter profiling:
     ```bash
     perf stat -e cycles,instructions,cache-misses,dTLB-load-misses,page-faults \
       ./onnx_runner
     ```
  4. Measure physical DRAM memory channel bandwidth and TLB pressure differences between page-aligned vs unaligned tensors.

---

### Stage 5: Transparent Drop-In Interposition (`libdynalloc.so`)
* **Goal**: Package verified optimizations into an `LD_PRELOAD` shared library requiring zero client application code modifications.
* **Actions**:
  1. Upgrade [`first_steps/src/dynalloc.cpp`](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/src/dynalloc.cpp) with:
     * Hybrid thresholding ($< 4\text{ KiB}$ in compact slabs, $\ge 4\text{ KiB}$ page-aligned).
     * Strict $4\text{ KiB}$ grid alignment + $H_{\text{min}}$ hole coalescing.
     * Linux 5.10+ `process_madvise(2)` cooperative out-of-band reclamation.
  2. Run unmodified C++ ONNX Runtime under `LD_PRELOAD=libdynalloc.so` and verify that memory reductions match standalone arena results.
