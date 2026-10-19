# Implementation Plan: YOLO26n Systems Memory Evaluation

## Goal Description
Evaluate physical memory (RAM) behavior, granularity mismatch (PLOS '23), and custom allocator boundaries (ISMM '24) on the 2026 flagship **Ultralytics YOLO26n** model under real C++ ONNX Runtime execution on Linux.

---

## Plan Overview

1. **Download Official Assets**:
   * Official `yolo26n.onnx` (9.5 MB) directly from Ultralytics releases.
   * Official `onnxruntime-linux-aarch64` C++ library (eliminates Python overhead).
2. **Build C++ Benchmark Harness**:
   * Implement `first_steps/bench/yolo26n_pipeline/yolo26_bench.cpp`:
     * Runs 1,000 continuous frames ($640 \times 640 \times 3$) through `Ort::Session`.
     * Tracks Peak RSS (`VmHWM`), Settled RSS (`VmRSS`), minor page faults, and physical page mapping via `mincore(2)`.
     * Records latency percentiles ($p50, p90, p99$, max) and FPS.
3. **Run Fair Benchmark Suite**:
   * Default `glibc` vs. Tuned `glibc` (`M_TRIM_THRESHOLD=4096`).
   * Default `jemalloc` vs. Tuned `jemalloc` (`dirty_decay_ms:0`).
   * Google `TCMalloc`.
   * Microsoft `mimalloc`.
   * Page-Grid Aligned Memory Engine.
4. **Generate Analysis Report**:
   * Save complete quantitative comparison to `first_steps/bench/yolo26n_pipeline/results/ANALYSIS.md`.
