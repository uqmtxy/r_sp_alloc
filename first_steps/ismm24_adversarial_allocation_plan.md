# Research Plan: Adversarial Allocation & Region Resilience in Edge CV

## Objective
Ground our systems research in the core findings of **ACM SIGPLAN ISMM '24** (*Reconsidering "Reconsidering Custom Memory Allocation"*) and **ACM PLOS '23** (*Lost Bytes At The Crossroads Between User-And Kernel-Level Memory Management*). Specifically, we will eliminate the "clean-heap evaluation bias" by implementing the paper's **Adversarial Allocation** preconditioning framework to evaluate fragmentation resilience, spatial locality, and tail-latency stability across general-purpose allocators vs. our page-grid region allocator on real YOLO26n inference.

---

## 1. Core Principles from ISMM '24 & PLOS '23

```
+--------------------------------------------------------------------------------------------------+
| Clean Heap (Adv0) vs. Adversarial Fragmented Heap (Adv3 / Adv10)                                 |
+--------------------------------------------------------------------------------------------------+
| Clean Heap (Adv0):                                                                               |
| General allocators place objects contiguously -> False impression of optimal spatial locality.   |
|                                                                                                  |
| Fragmented Heap (Adv3 / Adv10):                                                                  |
| General allocators scatter objects into memory holes -> Cache line fragmentation (2x slowdown).  |
|                                                                                                  |
| Region / Page-Grid Allocator:                                                                    |
| Bumps pointers in contiguous dedicated chunks -> 100% spatial contiguity & zero degradation.     |
+--------------------------------------------------------------------------------------------------+
```

1. **The Clean-Heap Methodological Flaw**: Testing on freshly booted processes hides fragmentation and cache degradation. General allocators appear competitive on clean heaps (`Adv0`), but suffer up to **2× slowdown** once heaps fragment (`Adv3`).
2. **Adversarial Allocation Preconditioning (Algorithm 3)**:
   - Profile the target workload's allocation size distribution and peak live object count ($N_{\text{peak}}$).
   - Pre-condition the heap before the benchmark starts:
     $$N = \text{multiplier} \times N_{\text{peak}}$$
     Allocate $N$ objects, shuffle pointers randomly, and free $m = N \times (1 - \text{occupancy})$ objects.
   - Levels: `Adv0` (Clean heap), `Adv1` (multiplier=1, occ=0.66), `Adv3` (multiplier=3, occ=0.66), `Adv10` (multiplier=10, occ=0.66).
3. **The PLOS '23 Intersection**:
   - In general-purpose allocators, fragmented heaps do not just degrade CPU cache locality—they also create **sub-page, head, and tail trapped holes** that prevent the Linux kernel from reclaiming physical DRAM pages.
   - Region/page-grid allocators guarantee contiguous placement by construction, ensuring both **cache line contiguity** (ISMM '24) and **zero trapped page holes** (PLOS '23).

---

## 2. Actionable Execution Plan

```
+----------------------------------------------------------------------------------------------------+
|                                      EXECUTION ROADMAP                                             |
+------+------------------------------------------+--------------------------------------------------+
| Step | Task                                     | Deliverable                                      |
+------+------------------------------------------+--------------------------------------------------+
| 1    | Profile Size Distribution & Peak Volume  | first_steps/tools/adversarial/profile_cv_allocs.h|
|      | for YOLO26n C++ ONNX Runtime             | -> Extracts exact size bins & N_peak             |
+------+------------------------------------------+--------------------------------------------------+
| 2    | Implement Adversarial Preconditioner     | first_steps/tools/adversarial/adversarial_heap.hpp|
|      | (ISMM '24 Algorithm 3: Adv0, Adv1, Adv3) | -> C++ header with mt19937 pointer shuffling     |
+------+------------------------------------------+--------------------------------------------------+
| 3    | Build Frame-Bound Double-Buffered Arena  | first_steps/bench/fd_arena_yolo26/fd_arena.hpp   |
|      | (FD-Arena) using std::pmr / Bump-Pointer | -> Ping-pong bump arenas with O(1) bulk reset    |
+------+------------------------------------------+--------------------------------------------------+
| 4    | Execute Adversarial Stress Benchmark     | first_steps/bench/adversarial_yolo26_bench/      |
|      | (Adv0 vs Adv1 vs Adv3 vs Adv10)          | -> 5-trial statistical comparison: glibc, jemalloc|
|      |                                          |    tcmalloc, mimalloc, DynAlloc, FD-Arena        |
+------+------------------------------------------+--------------------------------------------------+
| 5    | Hardware Counter Profiling Script (perf) | first_steps/scripts/cloudlab/run_perf_adversarial|
|      | (L1-dcache-load-misses, LLC-misses)      | -> Measures spatial locality degradation         |
+------+------------------------------------------+--------------------------------------------------+
| 6    | Update Publication Manuscript            | first_steps/paper/MANUSCRIPT.md                  |
|      |                                          | -> Section on Adversarial Allocation & Resilience|
+------+------------------------------------------+--------------------------------------------------+
```

---

## 3. Detailed Step-by-Step Breakdown

### Step 1: Profiling YOLO26n Allocation Distribution & Peak Object Volume
* **Goal**: Accurately capture $N_{\text{peak}}$ and size distribution $P(\text{size})$ during real C++ ONNX Runtime inference on `yolo26n.onnx`.
* **Method**:
  - Run an instrumented pass over 100 frames tracking active allocations.
  - Group sizes into power-of-two / quantum bins ($16\text{ B}$ to $4\text{ MiB}$).
  - Record the maximum concurrent active allocations ($N_{\text{peak}}$).

### Step 2: Implementing Adversarial Preconditioning Engine (`adversarial_heap.hpp`)
* **Goal**: Implement Algorithm 3 from ISMM '24 in reusable C++.
* **Logic**:
  ```cpp
  void precondition_heap(const SizeDistribution& dist, size_t n_peak, double multiplier, double occupancy) {
      if (multiplier <= 0.0) return; // Adv0: Clean heap
      size_t total_allocs = static_cast<size_t>(multiplier * n_peak);
      std::vector<void*> ptrs(total_allocs);
      for (size_t i = 0; i < total_allocs; ++i) {
          size_t sz = dist.sample_size();
          ptrs[i] = malloc(sz);
          if (ptrs[i]) std::memset(ptrs[i], 0xAA, sz); // Touch physical DRAM
      }
      std::mt19937_64 rng(42);
      std::shuffle(ptrs.begin(), ptrs.end(), rng);
      size_t to_free = static_cast<size_t>(total_allocs * (1.0 - occupancy));
      for (size_t i = 0; i < to_free; ++i) {
          free(ptrs[i]);
      }
      // Pointers left allocated simulate fragmented background heap state
  }
  ```

### Step 3: Frame-Bound Double-Buffered Arena (FD-Arena)
* **Goal**: A dedicated region allocator modeled after Clang's `llvm::BumpPtrAllocator` and Blender's `LinearAllocator` adapted for vision pipelines.
* **Mechanism**:
  - **Static Arena**: Permanent allocation for model weights and static graph metadata.
  - **Ping-Pong Frame Arenas (Arena 0 & Arena 1)**:
    - Frame $i$ allocates all intermediate activation tensors sequentially from Arena $i \pmod 2$ via a simple bump pointer: `ptr = cursor; cursor += align(sz, 64);`.
    - At the end of Frame $i$, the entire arena is reset in $O(1)$ by resetting `cursor = base`. No individual `free()` calls.

### Step 4: Adversarial Allocation Benchmark Suite
* **Configurations**:
  - Allocators: `glibc` default, `glibc` tuned, `jemalloc` default, `jemalloc` tuned (`dirty_decay_ms:0`), `TCMalloc`, `mimalloc`, `libdynalloc.so` (`LD_PRELOAD`), and `FD-Arena`.
  - Fragmentation Settings:
    - `Adv0`: Clean heap (multiplier=0, occupancy=0.0).
    - `Adv1`: Light fragmentation (multiplier=1, occupancy=0.66).
    - `Adv3`: Standard ISMM '24 fragmentation (multiplier=3, occupancy=0.66).
    - `Adv10`: Severe fragmentation (multiplier=10, occupancy=0.66).
* **Metrics**:
  - Latency mean, p50, p90, p99, max (ms).
  - Slowdown ratio: $\text{Latency}(\text{Adv3}) / \text{Latency}(\text{Adv0})$.
  - Peak RSS (`VmHWM`), Settled RSS, Private Dirty.
  - Minor page faults.

### Step 5: Hardware Cache Miss Profiling via `perf`
* For bare-metal / CloudLab validation:
  ```bash
  perf stat -e L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads,cycles,instructions \
      ./adversarial_bench --adv 3 ...
  ```
* Validates whether L1-dcache misses increase in `glibc`/`jemalloc` under `Adv3`, while remaining flat in DynAlloc / FD-Arena.

### Step 6: Synthesis into Research Paper Draft
* Update [`first_steps/paper/MANUSCRIPT.md`](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/paper/MANUSCRIPT.md) with:
  - Theoretical analysis of the Clean-Heap Bias.
  - Empirical tables comparing clean (`Adv0`) vs. adversarial (`Adv3`) execution.
  - Proven degradation curves demonstrating region allocator immunity.

---

## 4. Key Hypotheses to Prove

1. **Hypothesis 1 (Performance Degradation in General Allocators)**: Under `Adv3`, `glibc`, `jemalloc`, and `mimalloc` will suffer measurable latency degradation ($>15\%$) and increased tail latency ($p99$) due to scattered free-slot placement breaking cache line contiguity.
2. **Hypothesis 2 (Region Allocator Immunity)**: `FD-Arena` and `libdynalloc.so` will demonstrate flat, near-zero performance degradation ($<2\%$) across `Adv0` $\to$ `Adv1` $\to$ `Adv3` $\to$ `Adv10`.
3. **Hypothesis 3 (Physical DRAM Trapping)**: Under `Adv3`, general-purpose allocators will trap significantly more unreturnable physical RAM in head/tail holes (verified via `mincore(2)`), whereas page-grid alignment retains zero trapped bytes.
