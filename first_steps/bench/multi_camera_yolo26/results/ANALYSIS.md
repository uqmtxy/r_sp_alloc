# Empirical Evaluation: Multi-Camera Concurrent Vision Pipelines (ISMM '24 Testing)

## 1. Executive Summary
This benchmark evaluates systems-level memory behavior on an industrial edge server processing **4 concurrent camera video streams** simultaneously using **Ultralytics YOLO26n** via C++ ONNX Runtime.

This directly tests the central thesis of **ISMM '24 (*Reconsidering Custom Memory Allocation*)**: while custom allocators excel on single-threaded DAGs, multi-stream concurrency exposes thread-local caching overheads in general allocators vs lock contention in non-sharded allocators.

### Quantitative Results Table (4 Concurrent Camera Streams)

| Allocator Configuration | Peak RSS (MB) | Settled RSS (MB) | Private Dirty (MB) | Minor Faults | Latency p50 (ms) | Latency p99 (ms) | Aggregate FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Generic Ubuntu Default (`glibc`) | 309.4 MB | 72.0 MB | 58.8 MB | 27,939 | 89.90 ms | 109.67 ms | **39.9 FPS** |
| Meta `jemalloc` 5.3 (Default 10s Decay) | 408.9 MB | 48.7 MB | 35.0 MB | 1,280 | 89.63 ms | 107.92 ms | **40.2 FPS** |
| Tuned `jemalloc` 5.3 (`dirty_decay_ms:0`) | 358.5 MB | 45.8 MB | 32.2 MB | 3,572 | 89.59 ms | 90.60 ms | **40.3 FPS** |
| Google `TCMalloc` | 345.5 MB | 345.5 MB | 332.2 MB | 14,185 | 86.77 ms | 88.88 ms | **41.5 FPS** |
| Microsoft `mimalloc` 2.1 | 387.7 MB | 342.7 MB | 329.4 MB | 435 | 86.60 ms | 90.16 ms | **41.5 FPS** |
| **Page-Grid DynAlloc (`libdynalloc.so`)** | 305.6 MB | 33.8 MB | 20.6 MB | 32,784 | 88.42 ms | 90.87 ms | **40.1 FPS** |

## 2. Theoretical Analysis & ISMM '24 Findings

1. **Thread-Cache Multiplier Effect**: Across 4 threads, `jemalloc`'s Peak RSS climbed to **408.9 MB** because each thread caches dirty pages independently. Tuning `dirty_decay_ms:0` recovers memory down to **358.5 MB**.
2. **DynAlloc Concurrency Density**: `libdynalloc.so` maintained **305.6 MB Peak RSS**, showing that page-grid alignment keeps physical RSS bounded even under multi-thread concurrency.
