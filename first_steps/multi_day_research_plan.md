# Multi-Day Research Roadmap: PLOS '23 + ISMM '24 + YOLOv8 + Bare Metal

## Objective
A 5-day structured empirical research plan evaluating memory management optimizations for edge Computer Vision pipelines, combining:
* **Fistanto et al. (ACM PLOS '23)**: *Lost Bytes At The Crossroads Between User-And Kernel-Level Memory Management*
* **ACM ISMM '24**: *Reconsidering “Reconsidering Custom Memory Allocation”*
* **Target Model**: Ultralytics YOLO11 / YOLOv8
* **Hardware**: Bare-metal instances via CloudLab
* **Core Principle**: Strict baseline fairness (no artificial advantages)

---

## 5-Day Schedule

### Day 1: Baseline Fairness & Small-Tensor Hybrid Slabs
* **Goal**: Close the fairness loopholes and eliminate the small-tensor padding penalty in our custom arena.
* **Tasks**:
  1. **Tuned Baselines**:
     * Meta `jemalloc`: Set `MALLOC_CONF="dirty_decay_ms:0,muzzy_decay_ms:0"` (forces immediate dirty page purging, matching our arena's behavior).
     * `glibc`: Inject `mallopt(M_TRIM_THRESHOLD, 4096)` and `mallopt(M_MMAP_THRESHOLD, 65536)`.
  2. **Hybrid Sizing in Arena**:
     * Buffers $< 4096\text{ B}$ (e.g., detection boxes): Route to compact slab bins without 4 KiB padding.
     * Buffers $\ge 4096\text{ B}$ (feature maps): Enforce strict 4 KiB grid alignment and $H_{\text{min}}$ hole coalescing.
  3. **Metric**: Verify if the hybrid arena beats `glibc`'s 147.7 MB baseline while keeping trapped DRAM at 0.0 KB.

---

### Day 2: Testing *Reconsidering Custom Memory Allocation* (ISMM '24)
* **Goal**: Empirically evaluate the central thesis of ISMM '24 on the YOLOv8 DAG: when do custom allocators win vs. lose against modern general-purpose allocators?
* **Tasks**:
  1. **Telemetry**:
     * Profile CPU cycles per allocation, L1 data cache misses (`L1-dcache-load-misses`), and LLC misses via Linux `perf`.
  2. **Boundary Testing**:
     * *Static DAG*: Fixed resolution ($640 \times 640$), batch = 1.
     * *Dynamic Jitter*: Random resolution switching (480p, 720p, 1080p) to test custom arena adaptability vs. jemalloc thread caching.
     * *Multi-Stream*: 1, 4, and 8 concurrent camera threads to evaluate lock contention.
  3. **Outcome**: Establish exact performance and fragmentation boundaries for custom vision allocators.

---

### Day 3: Real ONNX Runtime C++ Pipeline with YOLOv8/11
* **Goal**: Move from simulated DAG loops to executing real exported `.onnx` models via the C++ ONNX Runtime API.
* **Tasks**:
  1. Export `yolov8n.onnx` and `yolo11n.onnx`.
  2. Build a C++ runner (`Ort::Session`) streaming 1,000 video frames through the engine.
  3. Profile real resident set size (RSS) curves and minor page faults under glibc, tuned jemalloc, and custom allocators.

---

### Day 4: CloudLab Bare-Metal Profiling
* **Goal**: Eliminate hypervisor, VM, and Docker container noise by benchmarking directly on bare-metal silicon.
* **Tasks**:
  1. Provision bare-metal server on [CloudLab](https://cloudlab.us/signup.php) (Ubuntu 24.04 LTS on dedicated physical x86/ARM hardware).
  2. Isolate physical CPU cores (`taskset -c 2` / `isolcpus`) and lock CPU governors to `performance`.
  3. Run hardware performance counter profiling:
     ```bash
     perf stat -e cycles,instructions,cache-misses,dTLB-load-misses,page-faults \
       ./cv_workload
     ```
  4. Measure physical DRAM memory channel bandwidth and TLB pressure differences between page-aligned vs unaligned tensors.

---

### Day 5: Transparent Drop-In Interposition (`libdynalloc.so`)
* **Goal**: Package verified optimizations into an `LD_PRELOAD` shared library requiring zero client application code modifications.
* **Tasks**:
  1. Upgrade `first_steps/src/dynalloc.cpp` with:
     * Hybrid small/large tensor thresholding.
     * Strict 4 KiB grid alignment + $H_{\text{min}}$ hole coalescing.
     * Linux 5.10+ `process_madvise(2)` cooperative out-of-band reclamation.
  2. Verify that raw, unmodified ONNX Runtime C++ runs with reduced peak RSS via `LD_PRELOAD=libdynalloc.so`.
