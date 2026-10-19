# Research Plan: Advanced Region Allocation, Concurrency Resilience, and Publication Delivery

## 1. Context & Motivation

Our recent empirical findings successfully reconciled two landmark systems papers:
1. **ACM PLOS '23** (*Lost Bytes At The Crossroads Between User-And Kernel-Level Memory Management*): Identified trapped sub-page, head, and tail holes due to granularity mismatch between allocator bins and Linux 4 KiB physical page frames.
2. **ACM SIGPLAN ISMM '24** (*Reconsidering "Reconsidering Custom Memory Allocation"*): Exposed the 25-year-old "clean-heap evaluation bias", showing that general-purpose allocators suffer cache degradation under synthetic heap fragmentation, while region/arena allocators exhibit structural immunity.

On real Ultralytics YOLO26n C++ ONNX Runtime inference, we demonstrated that under high-stress adversarial fragmentation (`Adv10`), generic allocators suffer up to **+23.7 MB (+22.8%) memory inflation** because scattered live objects pin physical 4 KiB pages and trap unreturnable memory holes. In contrast, **Page-Grid DynAlloc (`libdynalloc.so`)** caps memory at 109.9 MB (saving 20.0 MB RAM), and **Frame-Bound Arena (`FD-Arena`)** eliminates tail-latency jitter.

---

## 2. Unresolved Challenges & Research Gaps

The ISMM '24 paper identified clear avenues for future research, and our industrial edge vision problem domain presents unique opportunities:

1. **The Single-Thread Limitation of ISMM '24**:
   - ISMM '24 evaluated exclusively single-threaded benchmarks (`mudlle`, `176.gcc`, `Clang`, `geometry_nodes`).
   - Industrial vision servers operate 4, 8, or 16 camera pipelines simultaneously across multi-core CPUs.
   - General-purpose allocators (`jemalloc`, `mimalloc`, `TCMalloc`) use independent thread-caches (`tcache`) to avoid global lock contention. Under adversarial fragmentation, how does thread-cache replication compound physical memory bloat and cross-core page pinning?

2. **First-Class Tensor Runtime Integration**:
   - Up to now, our evaluation used either drop-in `LD_PRELOAD` interposition (`libdynalloc.so`) or isolated microbenchmarks (`FD-Arena`).
   - ONNX Runtime exposes a native C++ allocator API (`OrtCustomAllocator` / `OrtArenaCfg`).
   - Can we provide a first-class, zero-copy, double-buffered region allocator integrated directly into ONNX Runtime sessions, bypassing libc `malloc` entirely for all tensor operations?

3. **Tensor Lifetime Segregation (Wilson et al. 1995 + PLOS '23)**:
   - Allocating static model weights, persistent session structures, and transient intra-frame activations from a shared heap leads to page pinning.
   - We must formally evaluate a three-tier segregated hierarchy:
     - Tier 1: Static Model Weights (Read-only `mmap`, zero heap churn).
     - Tier 2: Inter-frame Persistent State (Output bounding boxes, tracker state).
     - Tier 3: Intra-frame Activation Arenas (Double-buffered ping-pong bump-pointer).

4. **Publication Package & Bare-Metal PMU Profiling**:
   - Prepare the formal ACM EuroSys / USENIX ATC / ACM ISMM LaTeX paper package with publication-quality vector charts (`matplotlib` PDF/SVG) and clear empirical tables.

---

## 3. Four-Phase Actionable Roadmap

```
+---------------------------------------------------------------------------------------------------------+
|                                    NEXT-STEPS RESEARCH ROADMAP                                          |
+-------+------------------------------------------+------------------------------------------------------+
| Phase | Focus Area                               | Key Deliverable                                      |
+-------+------------------------------------------+------------------------------------------------------+
| 1     | Native ONNX Runtime Custom Arena         | first_steps/bench/ort_custom_arena/                  |
|       | (OrtCustomAllocator + Double Buffer)     | -> Zero-malloc C++ session with bump-pointer tensors |
+-------+------------------------------------------+------------------------------------------------------+
| 2     | Multi-Core Adversarial Concurrency       | first_steps/bench/multi_camera_adversarial/          |
|       | (Evaluating ISMM '24 Concurrency Limit)  | -> 4-stream concurrent YOLO26n under Adv3 & Adv10    |
+-------+------------------------------------------+------------------------------------------------------+
| 3     | Publication Vector Visualizations        | first_steps/paper/figures/                           |
|       | (Python Matplotlib publication scripts)  | -> Vector PDFs: RSS bloat, tail latency, page holes  |
+-------+------------------------------------------+------------------------------------------------------+
| 4     | Complete LaTeX Manuscript Package        | first_steps/paper/latex/                             |
|       | (acmart.cls, EuroSys / ATC target)       | -> Camera-ready ACM format with references.bib       |
+-------+------------------------------------------+------------------------------------------------------+
```

---

## 4. Phase 1: Native ONNX Runtime Custom Region Allocator (`OrtCustomAllocator`)

### Technical Approach
In ONNX Runtime C++, custom memory allocators can be registered via `Ort::SessionOptions`:
```cpp
struct OrtDoubleBufferedArena : public OrtAllocator {
    // 64-byte aligned ping-pong memory chunk pre-faulted at initialization
    uint8_t* buffer_a;
    uint8_t* buffer_b;
    size_t offset;
    int active_buffer;
    
    void* Alloc(size_t size) {
        // Fast bump pointer alignment
        size_t aligned_size = (size + 63) & ~63;
        uint8_t* ptr = (active_buffer == 0 ? buffer_a : buffer_b) + offset;
        offset += aligned_size;
        return ptr;
    }
    
    void Free(void* p) {
        // O(1) no-op during inference
    }
    
    void FlipFrame() {
        offset = 0;
        active_buffer ^= 1;
    }
};
```

### Empirical Questions to Answer
1. Does native `OrtCustomAllocator` beat general-purpose `malloc` in throughput by eliminating allocator metadata updates?
2. Does it completely isolate the ONNX Runtime engine from any heap fragmentation existing in the host application?
3. What is the peak RSS and memory stability over 10,000 continuous frames?

---

## 5. Phase 2: Multi-Core Adversarial Concurrency (Addressing ISMM '24 Limitation)

### Technical Approach
1. Extend `adversarial_runner.cpp` to launch 4 parallel POSIX worker threads, each pinned to a dedicated CPU core (Cores 0, 1, 2, 3).
2. Each worker independently preconditions its local heap state to `Adv0`, `Adv1`, `Adv3`, and `Adv10` using thread-specific distributions.
3. Measure:
   - Aggregate Peak RSS across all cores.
   - Settled RSS and unreleased thread-cache memory after all threads finish.
   - Tail latency jitter ($p99$ degradation) across camera streams.

### Scientific Hypothesis
In multi-threaded execution, general-purpose thread caches (`tcache` in jemalloc, local heaps in mimalloc) multiply memory fragmentation. When 4 threads fragment their local regions, physical page trapping scales super-linearly with thread count, whereas page-grid and region allocators maintain strictly bounded, linear scaling.

---

## 6. Phase 3 & 4: Publication Figures & ACM LaTeX Package

### Deliverables
1. **Publication Scripts** (`first_steps/paper/figures/generate_figures.py`):
   - Generates high-DPI publication plots matching ACM guidelines:
     - `fig1_adversarial_rss_bloat.pdf`: Bar chart showing physical DRAM inflation under `Adv0`, `Adv1`, `Adv3`, `Adv10`.
     - `fig2_tail_latency_jitter.pdf`: Latency CDF / box plots comparing $p50, p95, p99$ stability.
     - `fig3_multicamera_retention.pdf`: Active vs Settled RSS across 4 camera streams showing memory hoarding.
     - `fig4_plos_trapped_bytes.pdf`: Breakdown of sub-page, head, and tail trapped bytes.
2. **ACM LaTeX Document** (`first_steps/paper/latex/main.tex`):
   - Fully formatted two-column ACM paper draft using `acmart` document class.
   - Embedded tables from Stages 1 through 12.
   - Complete `references.bib` containing SOSP, PLOS, ISMM, OSDI, and EuroSys citations.

---

## 7. Immediate Next Action

Begin Phase 1 by implementing the native ONNX Runtime Custom Arena (`first_steps/bench/ort_custom_arena/`) to test whether direct runtime bump-pointer integration provides superior execution determinism compared to transparent `LD_PRELOAD` interposition.
