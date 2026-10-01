# 02: eBPF Feasibility Analysis: Data Path Realities vs. Control Plane Leverage

## 1. Executive Verdict & Core Premise

The user has proposed:
> *"Dynamically define the amount and try using an eBPF approach."*

This is an insightful systems concept: modern Linux infrastructure increasingly leverages eBPF (extended Berkeley Packet Filter) to move policy decisions into the kernel and achieve programmable, zero-copy, highly observable execution.

However, in systems software engineering, **granularity and latency budgets dictate feasibility**. 

Applying eBPF to memory allocation reveals a strict dichotomy:
1. **Synchronous Data Path (Intercepting `malloc()` / `free()` directly with eBPF)**: **FATALLY FLAWED**. The hardware and kernel trap overhead of eBPF probes is $10\times\text{--}100\times$ higher than the entire budget of a modern user-space allocator.
2. **Asynchronous Control Path (Using eBPF for telemetry, phase detection, and out-of-band steering)**: **HIGHLY VIABLE & NOVEL**. eBPF can observe kernel page churn, monitor buddy allocator fragmentation, and trigger external memory reclamation (`process_madvise`) without instrumenting the application's hot path.

This document presents the empirical evidence, cycle-level overhead proofs, kernel architecture breakdowns, and precise boundaries for using eBPF in memory management.

---

## 2. Data Path Analysis: The Nanosecond vs. Microsecond Gap

To understand why eBPF cannot sit on the synchronous allocation data path, one must examine the CPU cycle budget of modern memory allocators versus the cost of entering an eBPF program from user space.

```
+-------------------------------------------------------------------------------+
|                       ALLOCATION HOT-PATH LATENCY BUDGET                      |
+-------------------------------------------------------------------------------+
| jemalloc/mimalloc Thread-Local Hit:                                           |
| [ 10 - 25 ns ] (20 - 60 CPU cycles)                                           |
+-------------------------------------------------------------------------------+
| eBPF uprobe/USDT Trap Overhead:                                               |
| [============================================== 200 - 1,500 ns =============] |
| (600 - 4,500 CPU cycles: breakpoint, IDT, register dump, BPF JIT, context ret)|
+-------------------------------------------------------------------------------+
```

### 2.1 The Modern Allocator Budget
In state-of-the-art allocators (`jemalloc`, `mimalloc`, `tcmalloc`):
- Each thread possesses a thread-local cache (`tcache` or thread freelist).
- Allocating a small object (< 1 KB) on the hot path requires:
  1. Reading thread-local storage (TLS) base register (`FS` or `TPIDR_EL0`): $\sim 1\text{--}3$ cycles.
  2. Indexing the size-class bin array: $\sim 2\text{--}4$ cycles.
  3. Popping a free pointer from a singly linked bump list: $\sim 2\text{--}5$ cycles.
- **Total latency on hit**: **$10\text{--}25\text{ ns}$** ($20\text{--}60$ CPU clock cycles at 3.0 GHz). No syscalls, no locks, no kernel transitions.

### 2.2 The Anatomy of a `uprobe` / `USDT` Invocation
To intercept a user-space `malloc(size)` call using eBPF, the kernel attaches a `uprobe` (user-space probe) or a `USDT` (Userland Statically Defined Tracing) marker at the function entry:
1. **Instruction Replacement**: The kernel replaces the first instruction of `malloc` with a breakpoint trap instruction:
   - On x86_64: `int 3` (`0xCC`).
   - On aarch64: `brk #0x0` (`0xD4200000`).
2. **Hardware Exception**: The CPU executes the breakpoint, halts user-space execution, switches privilege levels from Ring 3 to Ring 0 (kernel mode), and vectors through the Interrupt Descriptor Table (IDT).
3. **Register State Save**: The kernel interrupt handler allocates a `struct pt_regs` on the kernel stack and saves all general-purpose CPU registers ($\approx 168\text{ bytes}$).
4. **Subsystem Dispatch**: The Linux `uprobe` framework intercepts the trap, resolves the virtual address to the active inode, locates attached BPF programs, and prepares the BPF context.
5. **BPF JIT Execution**: The BPF bytecode executes in the kernel.
6. **Hardware Context Restore**: The kernel restores `pt_regs`, handles signal checks, and executes `iretq` or `eret` to return to Ring 3 user space.

### 2.3 Empirical Latency Breakdown
Recent benchmark studies by kernel developers (see [Olsa & Nakryiko, "Optimizing uprobes", 2023](https://lwn.net/Articles/939842/)) measure the cost of an un-optimized and optimized uprobe:

| Architecture / Mechanism | Minimum Overhead per Call | Typical Production Overhead |
| :--- | :--- | :--- |
| Standard Linux `uprobe` (x86_64) | $850\text{ ns}$ | $1,200\text{--}1,800\text{ ns}$ |
| Optimized `uprobe-multi` (Linux 6.6+) | $280\text{ ns}$ | $350\text{--}500\text{ ns}$ |
| USDT with Semaphore Disabled | $\approx 1\text{ ns}$ (NOP) | $1\text{ ns}$ |
| USDT with eBPF Attached | $300\text{ ns}$ | $450\text{--}700\text{ ns}$ |

### 2.4 Throughput Collapse Proof
Consider a production key-value service or inference engine processing:
$$\lambda = 1,000,000\text{ allocations/second}$$
- At baseline without eBPF:
  $$T_{\text{alloc}} = 10^6 \times 20\text{ ns} = 0.02\text{ seconds of CPU time per second (2% of 1 core)}$$
- With eBPF `uprobe` attached to `malloc()`:
  $$T_{\text{uprobe}} = 10^6 \times 1,000\text{ ns} = 1.00\text{ second of CPU time per second (100% of 1 core)}$$

**Conclusion**: Intercepting allocations on the synchronous data path induces a **$50\times$ CPU overhead explosion**, completely saturating dedicated compute cores purely handling interrupt traps.

---

## 3. Demystifying Kernel-Level eBPF Memory Allocators (`bpf_mem_alloc`)

In discussions surrounding eBPF and memory allocation, there is widespread confusion regarding the `bpf_mem_alloc` subsystem introduced in **Linux 6.1**.

### 3.1 What is `bpf_mem_alloc`?
Introduced by Alexei Starovoitov and Kumar Kartikeya Dwivedi (["bpf: Add BPF memory allocator", LKML, 2022](https://lore.kernel.org/bpf/20220902203649.337850-1-alexei.starovoitov@gmail.com/)), `bpf_mem_alloc` is a lockless, per-CPU, cache-aligned memory allocator designed to allocate memory **inside the kernel for BPF programs**.

```
+--------------------------------------------------------------------+
|                         LINUX MEMORY BOUNDARY                      |
+--------------------------------------------------------------------+
| USER SPACE (Applications, glibc, jemalloc, C++ Heap)               |
| [ Virtual Address Space: VMAs, Page Tables, Heap, Stack ]          |
|                                                                    |
| ===================== SYSCALL / TRAP BOUNDARY ==================== |
|                                                                    |
| KERNEL SPACE                                                       |
| +----------------------------------------------------------------+ |
| | bpf_mem_alloc                                                  | |
| | - Allocates kmalloc slabs for in-kernel BPF maps, lists, trees | |
| | - Operates on kernel physical/virtual pages                    | |
| | - CANNOT map user-space virtual addresses to user tasks        | |
| +----------------------------------------------------------------+ |
+--------------------------------------------------------------------+
```

### 3.2 Why Was It Built?
Prior to Linux 6.1, BPF programs executing in non-sleepable tracing or packet-processing contexts (such as NMI handlers, kprobes, or XDP) could not invoke standard kernel allocators (`kmalloc(..., GFP_KERNEL)`) because standard allocators can sleep or take mutexes. `bpf_mem_alloc` provides asynchronous, pre-allocated, per-CPU slab caches that allow kernel BPF code to allocate elements dynamically (e.g., dynamic BPF maps, linked lists, rbtrees) without sleeping.

### 3.3 Can `bpf_mem_alloc` Allocate Memory for User-Space Programs?
**Mathematically and architecturally: NO.**
1. `bpf_mem_alloc` allocates kernel memory chunks for kernel data structures.
2. User-space programs require memory within their own process address space, backed by page tables (`mm_struct`) and Virtual Memory Areas (`vm_area_struct`).
3. To deliver memory from kernel to user space, the kernel must execute page mapping (`remap_pfn_range` or page-fault handlers), which requires heavyweight syscalls (`mmap`).
4. Attempting to use `bpf_mem_alloc` for user-space heap allocation conflates kernel object lifetimes with user-space application memory.

---

## 4. Where eBPF Actually Succeeds: The Asynchronous Control Plane

While eBPF cannot serve as the synchronous allocator, it is an **exceptionally powerful tool for out-of-band memory telemetry and adaptive steering**.

```
+-------------------------------------------------------------------------------+
|                 VIABLE ARCHITECTURE: eBPF CONTROL PLANE LOOP                  |
+-------------------------------------------------------------------------------+
| [ Production Application ]                                                    |
|      |                                                                        |
|      | 1. High-throughput malloc/free (Unmodified fast path: 15 ns)           |
|      v                                                                        |
| [ Workload-Specific User Allocator ] <---+                                    |
|      |                                   |                                    |
|      | 2. Page faults, mmap, unmap       | 5. Out-of-band policy tuning       |
|      v                                   |    (via IPC or process_madvise)    |
| [ Linux Kernel Page Subsystem ]          |                                    |
|      |                                   |                                    |
|      | 3. Kernel tracepoints triggered   |                                    |
|      v                                   |                                    |
| [ eBPF Observability Programs ]          |                                    |
|      | (mm_page_alloc, page_fault_user)  |                                    |
|      v                                   |                                    |
| [ BPF Ring Buffer ]                      |                                    |
|      | 4. Asynchronous telemetry events  |                                    |
|      v                                   |                                    |
| [ External Tuning Daemon (User Space) ] -+                                    |
+-------------------------------------------------------------------------------+
```

### 4.1 Statistical Low-Overhead Profiling via PMU Sampling
Instead of trapping every allocation, an eBPF program attaches to the CPU's **Performance Monitoring Unit (PMU)** (e.g., sampling every $100,000$ CPU cycles or every $10,000\text{th}$ allocation via hardware counter overflow):
- Overhead: $< 0.1\%$ CPU utilization.
- Capability: Accurately samples the allocation size distribution, identifies object allocation call sites, and tracks allocation burstiness without disturbing latency-sensitive workloads.

### 4.2 Kernel Page Allocation & Fragmentation Telemetry
eBPF programs can hook directly into kernel tracepoints without touching user-space code:
- `tracepoint:kmem:mm_page_alloc`: Tracks when the allocator requests new physical page frames from the buddy allocator.
- `tracepoint:kmem:mm_page_free`: Measures real-time physical memory return rates.
- `tracepoint:exceptions:page_fault_user`: Measures minor page faults occurring when the user-space allocator writes to newly committed memory spans.
- `tracepoint:cgroup:cgroup_memory_high`: Detects when the container or cgroup approaches its memory ceiling before the OOM killer triggers.

### 4.3 Out-of-Band Memory Purging via `process_madvise(2)`
Introduced in **Linux 5.10**, `process_madvise(int pidfd, const struct iovec *iovec, size_t vlen, int advice, unsigned int flags)` allows an external supervisor process with `CAP_SYS_ADMIN` or `PTRACE_MODE_READ_REALCREDS` to issue memory management advice directly to another target process:
- The supervisor process (informed by eBPF telemetry) identifies that an application has entered an idle or low-churn phase.
- Instead of forcing the application to execute a blocking garbage collection or page-purging loop, the external daemon executes `process_madvise(..., MADV_DONTNEED)` on the target process's idle arena ranges.
- Result: Unused physical pages are instantly returned to the kernel's buddy allocator with **zero application CPU cycles consumed**.

### 4.4 Co-Scheduling with `sched_ext` (Linux 6.12+)
With the recent upstream merge of **`sched_ext`** in Linux 6.12 ([Corbet, "sched_ext merged for 6.12", LWN.net](https://lwn.net/Articles/991632/)), user-defined eBPF schedulers can replace the default CFS/EEVDF scheduler:
- eBPF can track which CPU sockets or NUMA nodes hold the physical pages allocated by specific worker threads.
- `sched_ext` can dynamically pin or migrate worker threads to ensure absolute NUMA and L3 cache locality with their active heap arenas, eliminating cross-socket memory bus latency.

---

## 5. Decision Matrix: How to Apply eBPF Safely

| Proposal / Use Case | Technical Viability | Primary Bottleneck | Verdict |
| :--- | :--- | :--- | :--- |
| **Intercepting `malloc()`/`free()` with uprobes** | **Fatal** | $300\text{--}1500\text{ ns}$ trap overhead vs $15\text{ ns}$ allocation budget; severe CPU saturation. | **REJECT** |
| **Using `bpf_mem_alloc` for User-Space Heap** | **Impossible** | `bpf_mem_alloc` is restricted to kernel memory; cannot map virtual addresses to user tasks. | **REJECT** |
| **PMU-Sampled Allocation Profiling** | **Excellent** | Requires debug symbols or USDT markers; $< 0.5\%$ CPU overhead. | **ADOPT** |
| **Kernel Page Fault & Churn Monitoring** | **Excellent** | Zero application modification; hooks pure kernel tracepoints. | **ADOPT** |
| **External `process_madvise` Reclamation** | **High** | Requires `pidfd` access and arena address export via shared memory/procfs. | **ADOPT** |
| **NUMA Locality Co-Scheduling via `sched_ext`** | **Emerging** | Requires Linux $\ge 6.12$; powerful for multi-socket systems. | **EXPLORE** |

---

## 6. Formal Evidence & Literature References

* [Olsa, J., & Nakryiko, A. (2023). "Optimizing uprobes and uretprobes in the Linux Kernel." *Linux Weekly News (LWN.net)*.](https://lwn.net/Articles/939842/)
  * *Evidence*: Quantifies the minimum trap latency of Linux uprobes; demonstrates that even after multi-uprobe kernel optimizations, uprobe latency remains bounded above $250\text{ ns}$ due to hardware interrupt overhead.

* [Starovoitov, A., & Dwivedi, K. K. (2022). "bpf: Add BPF Memory Allocator." *Linux Kernel Mailing List (LKML)*.](https://lore.kernel.org/bpf/20220902203649.337850-1-alexei.starovoitov@gmail.com/)
  * *Evidence*: Explains the architectural design of `bpf_mem_alloc` as a lockless, per-CPU slab allocator tailored specifically for in-kernel dynamic BPF data structures (maps, lists, trees) in non-sleepable contexts.

* [Kerrisk, M. (2020). "`process_madvise(2)` — Give advice about use of memory to a process." *Linux Programmer's Manual*.](https://man7.org/linux/man-pages/man2/process_madvise.2.html)
  * *Evidence*: Formal specification of out-of-band asynchronous page reclamation across process boundaries introduced in Linux 5.10.

* [Gregg, B. (2019). *BPF Performance Tools: Deep Analysis and Linux Systems Performance*. Addison-Wesley.](https://www.brendangregg.com/bpf-performance-tools-book.html)
  * *Evidence*: Analyzes the performance overhead of dynamic tracing; demonstrates that high-frequency tracing (> 100k events/sec) must rely on statistical sampling rather than synchronous event tracing to avoid probe effect skew.

* [Corbet, J. (2024). "Extensible scheduling with sched_ext in Linux 6.12." *Linux Weekly News (LWN.net)*.](https://lwn.net/Articles/991632/)
  * *Evidence*: Documents the architecture of eBPF-programmable scheduling policies in the upstream Linux kernel, enabling memory-aware and NUMA-aware task placement.
