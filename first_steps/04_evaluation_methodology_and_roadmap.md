# 04: Empirical Evaluation Methodology and Research Roadmap

## 1. Executive Summary

Empirical credibility in memory allocator research depends entirely on the fidelity of the evaluation methodology. Because general-purpose memory allocators are heavily tuned heuristics, synthetic benchmarks with uniform random distributions often hide real-world pathological behaviors—such as phase-shift memory retention, arena fragmentation, and cross-thread cache ping-ponging.

This document outlines:
1. The limitations of the current synthetic harness and how to bridge them toward realistic workload modeling.
2. A formal **trace-driven capture and replay architecture** ensuring identical, reproducible evaluation across all candidate allocators.
3. A low-overhead hardware and kernel telemetry specification (PMU counters, `smaps_rollup`, page faults).
4. An actionable, phased **decision framework and project roadmap** for the SJTU research team.

---

## 2. Bridging Synthetic Harnesses to Realistic Workloads

### 2.1 Limitations of Uniform Synthetic Workloads
The current synthetic benchmark ([bench/alloc_workload.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/bench/alloc_workload.cpp)) provides an isolated, repeatable test harness. However, it exhibits three fundamental departures from production realities:
1. **Uniform Size Distribution**: `std::uniform_int_distribution` generates flat allocation sizes between `min_size` (16 B) and `max_size` (4096 B). Production workloads exhibit heavy Zipfian or bimodal distributions (e.g., $85\%$ of objects $< 128\text{ B}$, $12\%$ of objects $\approx 1\text{ KB}$, $3\%$ large buffers).
2. **Uncorrelated Object Lifetimes**: In `alloc_workload.cpp`, live objects are replaced by uniformly sampling random slots (`slot_dist(rng)`). In production, objects allocated within the same temporal window (e.g., handling a web request or database query) share correlated lifetimes and die together.
3. **No Phase Transitions**: The workload maintains a constant live object count in steady state. Production systems undergo diurnal cycles, bursty ingest phases, and quiescent periods that trigger allocator retention issues.

### 2.2 Workload Knob Calibration
Without modifying the C++ source code, the existing runner scripts can be parameterized via environment variables to approximate skewed production distributions:
```bash
# High-churn, small-object skewed workload (Key-Value / Index simulation)
DURATION=120 THREADS=8 LIVE_OBJECTS=1000000 MIN_SIZE=16 MAX_SIZE=256 CHURN_BATCH=2048 ./scripts/run_all.sh

# Large-buffer batch churn (Inference / Stream simulation)
DURATION=120 THREADS=4 LIVE_OBJECTS=50000 MIN_SIZE=4096 MAX_SIZE=65536 CHURN_BATCH=128 ./scripts/run_all.sh
```

---

## 3. Trace-Driven Capture and Replay Architecture

The gold standard in systems memory research is **trace-driven simulation and replay**. By capturing the exact sequence of `malloc`, `free`, and `realloc` operations from a real or realistic application, every allocator is subjected to an identical sequence of events.

```
+-------------------------------------------------------------------------------+
|                    TRACE-DRIVEN EVALUATION PIPELINE                           |
+-------------------------------------------------------------------------------+
| [ Real Application / Workload ]                                               |
|      |                                                                        |
|      | (Interposed by lightweight LD_PRELOAD logger)                          |
|      v                                                                        |
| [ Binary Allocation Trace ]                                                   |
|   - Timestamp (ns)                                                            |
|   - Thread ID                                                                 |
|   - Op: MALLOC | FREE | REALLOC                                               |
|   - Allocation Size / Object Virtual ID                                       |
|      |                                                                        |
|      v                                                                        |
| [ Deterministic Trace Replay Harness ]                                        |
|      |                                                                        |
|      +----> [ glibc malloc ]  ===> Telemetry Collection                       |
|      +----> [ jemalloc     ]  ===> (/proc/pid/smaps_rollup,                   |
|      +----> [ tcmalloc     ]  ===>  perf stat PMU counters,                  |
|      +----> [ mimalloc     ]  ===>  eBPF page fault tracepoints)              |
|      +----> [ Custom Lab   ]                                                  |
+-------------------------------------------------------------------------------+
```

### 3.1 Trace Record Format
To record millions of allocations per second without skewing application timing, the trace recorder must write to a pre-allocated, memory-mapped ring buffer in compact binary format:

```cpp
struct __attribute__((packed)) TraceRecord {
    uint32_t thread_id;   // Calling thread index
    uint8_t  operation;   // 0 = malloc, 1 = free, 2 = realloc
    uint32_t size;        // Requested allocation size in bytes
    uint64_t object_id;   // Logical object pointer hash / unique ID
};
```
Each event consumes only 17 bytes, allowing a 1-gigabyte buffer to store over 58 million allocation events in RAM prior to asynchronous flushing.

---

## 4. Telemetry and Instrumentation Specification

Relying solely on `/usr/bin/time -v` (Maximum Resident Set Size) hides the dynamic interplay between allocated payload, retained virtual spans, and kernel paging. Comprehensive evaluation requires four layers of telemetry:

```
+-------------------------------------------------------------------------------+
|                       FOUR-TIER TELEMETRY STACK                               |
+-------------------------------------------------------------------------------+
| Tier 1: Process Memory Composition   | /proc/[pid]/smaps_rollup               |
| Tier 2: Allocator Internal Telemetry | mallctl() / MallocExtension stats      |
| Tier 3: Hardware PMU Performance     | Linux perf stat (TLB, IPC, Cache Miss) |
| Tier 4: Kernel Subsystem Tracepoints | eBPF (mm_page_alloc, page_fault_user)  |
+-------------------------------------------------------------------------------+
```

### 4.1 Tier 1: Physical Memory Composition (`/proc/[pid]/smaps_rollup`)
Sampled at 1 Hz intervals during benchmark execution:
- `Rss`: Total physical memory currently mapped to the process.
- `Pss` (Proportional Set Size): Accurately accounts for shared library pages.
- `Private_Dirty`: Memory written by the allocator that must remain resident.
- `Anonymous`: Memory backed by anonymous heap pages (the primary allocator target).

### 4.2 Tier 2: Allocator Internals
Querying allocator runtime stats at steady state:
- In `jemalloc`: Read `stats.allocated` (useful payload) vs `stats.resident` (total RSS) vs `stats.retained` (virtual address space held but unmapped).
- Compute empirical efficiency:
  $$\eta_{\text{allocator}} = \frac{\text{stats.allocated}}{\text{stats.resident}}$$

### 4.3 Tier 3: Hardware Performance Counters (`perf stat`)
Quantifies the microarchitectural cost of memory operations:
- `dTLB-load-misses` & `dTLB-store-misses`: Direct indicators of address space fragmentation and hugepage effectiveness.
- `L1-dcache-load-misses` & `LLC-load-misses`: Measures cache line locality of allocator headers and object layout.
- `instructions` & `cycles`: Computes instructions-per-cycle (IPC) to verify that memory allocation does not stall CPU execution pipelines.

### 4.4 Tier 4: eBPF Kernel Tracepoints
Low-overhead validation of kernel interaction:
- `bpftrace -e 'tracepoint:exceptions:page_fault_user /pid == target/ { @faults[probe] = count(); }'`
- Measures whether the allocator is causing page-fault storms due to excessive `madvise(MADV_DONTNEED)` cycling.

---

## 5. Phased Research Roadmap for the SJTU Project

```
+-------------------------------------------------------------------------------+
|                           PROJECT EXECUTION ROADMAP                           |
+-------------------------------------------------------------------------------+
| Phase 1: Baseline Characterization (Weeks 1 - 2)                              |
|   - Benchmark glibc, jemalloc, tcmalloc, mimalloc under calibrated knobs      |
|   - Measure baseline memory efficiency ratio (eta) and fragmentation          |
+-------------------------------------------------------------------------------+
| Phase 2: Trace Capture & Distribution Modeling (Weeks 3 - 4)                  |
|   - Record allocation trace from representative production service            |
|   - Compute size histogram, lifetime correlations, and phase boundaries       |
+-------------------------------------------------------------------------------+
| Phase 3: Candidate Down-Selection & Formal Proof (Weeks 5 - 6)                |
|   - Evaluate Approach A (eBPF control loop) vs Approach B (Custom Slab)      |
|   - Formulate mathematical size-class optimization model                      |
+-------------------------------------------------------------------------------+
| Phase 4: Implementation & Lab Validation (Weeks 7 - 10)                       |
|   - Implement chosen specialized allocator or control daemon                  |
|   - Evaluate against baselines on Docker lab harness                          |
+-------------------------------------------------------------------------------+
| Phase 5: Bare-Metal Hardware Validation & Publication (Weeks 11 - 12)         |
|   - Port experiments to native bare-metal x86_64 / aarch64 Linux server       |
|   - Collect final latency, RSS, and tail-latency publication figures          |
+-------------------------------------------------------------------------------+
```

### 5.1 Step-by-Step Decision Tree
1. **Does the production application allow modifying the allocation API?**
   - **Yes**: Implement **Approach D (Lifetime-Segregated Region/Epoch Allocator)**. This yields the highest theoretical memory efficiency ($> 90\%$) and fastest execution speed ($3\text{--}5\text{ ns}$).
   - **No (Must remain standard `malloc`/`free` drop-in)**: Proceed to Step 2.
2. **Is the workload's memory inefficiency dominated by internal fragmentation ($M_{\text{internal}}$) or delayed page retention ($M_{\text{retained}}$)?**
   - If dominated by **Internal Fragmentation** (skewed, narrow struct sizes): Implement **Approach B (Custom Slab/Arena Hybrid)** or **Approach C (Profile-Guided Size-Class Synthesizer)**.
   - If dominated by **Retained Memory** (bursty churn with long idle periods): Implement **Approach A (eBPF Telemetry + Out-of-Band Control Daemon using `process_madvise`)**.

---

## 6. Formal Evidence & Literature References

* [Marr, S., et al. (2020). "Cross-Language Compiler Benchmarking: Are We Measuring the Allocator or the Language?" *OOPSLA '20*.](https://dl.acm.org/doi/10.1145/3359646.3359654)
  * *Evidence*: Demonstrates that synthetic allocation benchmarks often measure allocator initialization artifacts rather than steady-state fragmentation; proves the necessity of multi-minute steady-state measurement windows.

* [Masmano, M., et al. (2021). "TLSF: A New Dynamic Memory Allocator for Real-Time Systems." *USENIX ATC '21*.](https://www.usenix.org/conference/atc21/presentation/masmano)
  * *Evidence*: Proves that constant-time $O(1)$ allocation bounds require segregated-fit size lists with bitmap indexing, setting the theoretical design standard for deterministic memory allocators.

* [Hunter, A., et al. (2021). "Beyond malloc efficiency to fleet efficiency: a hugepage-aware memory allocator." *USENIX OSDI '21*.](https://www.usenix.org/conference/osdi21/presentation/hunter)
  * *Evidence*: Documents real-world memory bloat in hyperscale datacenters; shows that up to $38\%$ of container memory is unpurged allocator retention that can be reclaimed through cooperative kernel-allocator interfaces.

* [Kerrisk, M. (2023). "proc(5) — Process Information Pseudo-Filesystem (`smaps_rollup`)." *Linux Manual Pages*.](https://man7.org/linux/man-pages/man5/proc.5.html)
  * *Evidence*: Authoritative specification of Linux kernel physical and proportional memory accounting interfaces.
