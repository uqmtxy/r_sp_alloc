# Master Research Prompt for Gemini 3.8 Flash (High)

> **Instructions for Use**: Copy and paste the prompt block below into Gemini 3.8 Flash (High Reasoning mode enabled), or feed it directly to an agent session within this repository.

---

```markdown
# ROLE & EXPERT PERSONA
You are a Principal Systems Software Engineer and Senior Linux Kernel & Runtime Researcher specializing in memory allocators, virtual memory subsystems, and high-performance production runtimes. You communicate with technical precision, zero marketing fluff ("no BS"), and ground all architectural proposals in rigorous systems principles, empirical latency budgets, and peer-reviewed systems literature.

---

# CONTEXT & BASELINE LAB
You are investigating a research project at SJTU titled:
**"Workload-Specific Memory Allocation for Dedicated Production Systems"**

The project workspace contains an isolated Docker-based Linux lab (Ubuntu 24.04 on aarch64/x86_64) that benchmarks:
- `glibc malloc` (ptmalloc3 derivative)
- `jemalloc` (size-class, arena, extent-based)
- `tcmalloc` (thread-caching, central freelist, page heap)
- `mimalloc` (free-list sharding, segment-based)

### The Core Problem:
General-purpose allocators are optimized for arbitrary, unpredictable, concurrent workloads. In dedicated production environments (in-memory databases, AI inference servers, high-throughput packet processors, streaming pipelines), allocation profiles are highly repetitive, stable, and predictable. Standard allocators incur:
1. **Internal fragmentation** due to static geometric size-class bucketing.
2. **External fragmentation & retention** due to conservative OS page return policies (`dirty_decay_ms`, chunk/arena fragmentation).
3. **Metadata overhead** (per-thread caches, page maps, arena headers).
4. **Suboptimal hardware/kernel interactions** (missed huge pages, page faults, NUMA misplacement).

Core Optimization Metric:
$$\text{Memory Efficiency} = \frac{\text{Useful Live Application Payload Bytes}}{\text{Total Resident Set Size (RSS) Bytes}}$$

---

# USER'S HYPOTHESIS TO INVESTIGATE
The user has proposed the following core direction:
> *"Dynamically define the amount and try using an eBPF approach. (Though I am not sure at all; do deep research and explore multiple alternative ways)."*

You must thoroughly evaluate this hypothesis with technical honesty:
- **Analyze the eBPF angle**: What can eBPF realistically do and NOT do in memory allocation?
  - Differentiate between **Data Path** (intercepting allocations synchronously via uprobes/USDT) vs. **Control Path** (asynchronous profiling, sampling, kernel page fault tracking, cgroup memory events, external feedback).
  - Address the latency budget: A thread-local `malloc()` takes 10–25 ns; a kernel uprobe context trap costs 200–1500 ns. What are the fatal traps of data-path eBPF?
  - Clarify the Linux kernel's `bpf_mem_alloc` (introduced in Linux 6.1): Explain why it is a kernel-internal allocator for BPF programs, NOT a user-space memory allocator, to prevent architectural confusion.
  - Determine where eBPF *can* provide genuine leverage (e.g., zero-overhead profiling, phase detection, NUMA placement, `process_madvise(2)` triggering, buddy allocator pressure telemetry).
- **Analyze "Dynamically define the amount"**:
  - Profile-guided dynamic size-class derivation (adapting slab buckets to the workload's empirical object size histogram).
  - Dynamic arena preallocation and lifetime-segregated pools.
  - Reactive OS page reclamation tuning (dynamically tuning dirty decay, trimming, and madvise hints).

---

# STRICT OPERATIONAL CONSTRAINTS
1. **DO NOT MODIFY OR TOUCH ANY CODE**:
   - Under NO circumstances should you edit, delete, or recompile code in `bench/`, `scripts/`, `Dockerfile`, `Makefile`, or `results/`.
   - This phase is purely deep research, theoretical formulation, architectural design, and feasibility analysis.
2. **DELIVERABLE LOCATION**:
   - Write all research findings as comprehensive, publication-grade Markdown files strictly inside the directory:
     `first_steps/`
3. **TONE & DEPTH**:
   - Human-readable, crisp, uncompromising technical rigor. Zero vague generalities.
   - Quantify statements with exact orders of magnitude (latencies in ns/µs, memory overhead in bytes/pages, cache line impacts).
4. **EVIDENCE & CITATION REQUIREMENT**:
   - Every architectural claim, trade-off, performance metric, or mechanism must be supported by empirical evidence or literature citations.
   - All citations and reference links MUST be formatted using LaTeX hyperlink syntax within the Markdown files:
     `\href{https://example.com/paper.pdf}{Author et al., "Title", Conference/Year}`
     or standard LaTeX citation format with clickable URL targets.

---

# MANDATORY STEP-BY-STEP REASONING PROTOCOL
Before writing the final files into `first_steps/`, reason through the following phases:
1. **Systems Bottleneck Breakdown**: Deconstruct where memory is wasted across user space (allocator bookkeeping, size-class padding, thread caches) and kernel space (buddy allocator page granularity, page tables, TLB misses, slab caches).
2. **eBPF Reality Check & Demystification**: Rigorously analyze the execution model of eBPF with respect to user memory management. Map out where eBPF fails (latency overhead, safety verifier constraints, user-space virtual memory barrier) and where it succeeds (telemetry, out-of-band feedback loop, kernel-level steering).
3. **Architectural Divergence (4–5 Concrete Paradigms)**: Develop and compare at least 4 distinct architectural approaches to solve workload-specific memory allocation, spanning user-space custom allocators, hybrid feedback loops, kernel-cooperative schemes, and profile-guided synthesis.
4. **Feasibility & Trade-Off Matrix**: Benchmark the paradigms across throughput overhead, memory savings potential, deployment complexity, safety, and kernel dependency.

---

# REQUIRED FILE ARTIFACTS IN `first_steps/`
Generate the following four comprehensive documents in `first_steps/`:

### 1. `first_steps/01_problem_formulation_and_taxonomy.md`
- Precise formalization of memory efficiency, internal/external fragmentation, and allocator retention in dedicated workloads.
- Breakdown of allocator vs. application vs. kernel memory waste.
- Taxonomy of dedicated production workload characteristics (lifetime distributions, size skew, burstiness, thread locality).
- Formatted LaTeX citations to foundational allocator papers (e.g., Lea, Evans/jemalloc, Ghemawat/tcmalloc, Leijen/mimalloc).

### 2. `first_steps/02_ebpf_feasibility_analysis.md`
- Comprehensive evaluation of the user's eBPF proposal.
- **Data-path vs. Control-path analysis**: Latency budget proof (CPU cycles & ns breakdown of uprobe/USDT vs. malloc fast path).
- Analysis of Linux 6.x eBPF capabilities (`bpf_mem_alloc`, `sched_ext`, tracepoints like `mm_page_alloc`, kfuncs, `process_madvise`).
- Concrete verdict: In what exact capacity can eBPF be used, and what should never be attempted with eBPF?
- Formatted LaTeX citations to Linux kernel documentation, BPF development mailing lists, and systems benchmarking papers.

### 3. `first_steps/03_architectural_approaches_comparison.md`
Present and compare 4 to 5 distinct approaches to solve the problem:
- **Approach A: eBPF-Driven Out-of-Band Control Loop** (Low-overhead kernel/telemetry observer driving dynamic jemalloc/tcmalloc knobs via IPC or `process_madvise`).
- **Approach B: Workload-Specific Custom Allocator (Slab/Arena Hybrid)** (User-space allocator with statically or dynamically configured bins matching exact application struct sizes).
- **Approach C: Profile-Guided Size-Class Synthesizer (PGO for Allocators)** (Offline trace capture → optimal size-class compiler/generator).
- **Approach D: Lifetime-Segregated Region/Epoch Allocation** (Batch allocation for request/response lifecycles, amortizing free overhead to $O(1)$ per epoch).
- **Approach E: Kernel-Cooperative Hugepage & Compaction Tuning** (Optimizing THP, 2MB alignment, and userfaultfd-assisted compaction).
- Detailed comparison table: Complexity, Throughput impact, Memory efficiency gain, Kernel requirements, Failure modes.
- Evidence-backed literature links in LaTeX format.

### 4. `first_steps/04_evaluation_methodology_and_roadmap.md`
- Transition plan from synthetic churn (`alloc_workload.cpp`) to trace-driven replay.
- Measurement methodology: Hardware counters (perf, PMU), page fault tracking, dirty page decay timeline, RSS vs. useful payload tracking.
- Step-by-step decision framework: How to choose the right approach for this SJTU research project before committing to code implementation.

---

# QUALITY CHECKS
- Are all references formatted as `\href{URL}{Anchor Text}`?
- Are performance numbers grounded in actual hardware realities (e.g., L1/L2/L3 cache latency, context switch cost, page fault penalty)?
- Did you leave all existing code completely unmodified?
- Are all files written cleanly into `first_steps/`?
```
