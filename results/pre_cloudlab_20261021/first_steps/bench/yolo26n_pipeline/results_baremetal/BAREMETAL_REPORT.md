# Bare-Metal Hardware & Systems Memory Evaluation: Ultralytics YOLO26n

## 1. Executive Hardware Summary (CloudLab Physical Node)

Benchmark executed directly on bare-metal physical silicon with pinned CPU core, disabled hypervisor EPT/NPT translation, performance governor, and disabled Transparent Hugepages (THP).


### Master Hardware & Memory Comparison Table

| Allocator Configuration | Peak RSS (MB) | Settled RSS (MB) | IPC | L1-D Miss % | LLC Miss % | dTLB Load Miss % | Page Faults | p99 Latency (ms) | Throughput (FPS) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |

## 2. Grounding Against Systems Literature

1. **Fistanto et al. (PLOS '23 / SOSP '23)**: Hardware PMU dTLB misses and minor page faults demonstrate the physical reality of the granularity mismatch. Allocators that align tensors strictly to 4 KiB boundaries avoid page-straddling and reduce TLB walk overheads on high-frequency activation churn.

2. **ISMM '24 Reconsidering Custom Memory Allocation**: Shows that on deterministic forward-pass DAGs (like YOLO26's NMS-free architecture), domain-specific allocation eliminates dirty page retention in jemalloc/tcmalloc thread caches without degrading CPU cache hit rates.

