# Plan: What to Do

## What to Do

1. **Fix fairness and small buffer padding**:
   * Test `jemalloc` with `MALLOC_CONF="dirty_decay_ms:0"` so it purges memory immediately instead of holding it for 10 seconds.
   * Test `glibc` with `mallopt(M_TRIM_THRESHOLD, 4096)`.
   * In our arena, do not pad buffers smaller than 4KB to a full 4KB. Only align tensors $\ge 4\text{ KB}$ to page boundaries. This fixes the 1.3 MB padding waste that let glibc win in the previous test.

2. **Test ISMM '24 paper claims on YOLOv8**:
   * Test where the custom arena helps and where it hurts compared to jemalloc/glibc:
     * Fixed input size ($640 \times 640$).
     * Variable input sizes (switching between 480p, 720p, 1080p).
     * Multi-threading (1, 4, and 8 camera streams).
   * Measure CPU cycles, cache misses, and memory fragmentation with `perf`.

3. **Run on real ONNX Runtime C++ with YOLOv8**:
   * Export real `yolov8n.onnx` weights.
   * Write a C++ runner using `onnxruntime` to process 1,000 video frames.
   * Measure true RSS and frame latency across glibc, tuned jemalloc, and our arena.

4. **Run on bare metal (CloudLab)**:
   * Set up the benchmark on a CloudLab bare-metal machine (Ubuntu 24.04).
   * Pin to dedicated CPU cores (`taskset`).
   * Collect hardware performance counters (`perf`: page faults, dTLB misses, cache misses) without Docker or VM noise.

5. **Make it drop-in (`LD_PRELOAD`)**:
   * Put the page-grid alignment logic into `libdynalloc.so`.
   * Run the unmodified ONNX Runtime C++ binary with `LD_PRELOAD=libdynalloc.so` to confirm the RAM savings work with zero code changes.
