# CloudLab Bare-Metal Profiling Suite: YOLO26n Systems Memory Evaluation

This directory contains turnkey scripts to deploy, configure, and profile **Ultralytics YOLO26n** (released January 2026, [arXiv:2606.03748](https://arxiv.org/abs/2606.03748)) directly on physical bare-metal hardware via [CloudLab](https://cloudlab.us/).

---

## 1. CloudLab Hardware Selection Guide

When launching an experiment on CloudLab, select any dedicated physical compute node (avoid VMs):
* **Recommended x86_64 Profiles**:
  * `c6525-100g` (Utah): AMD EPYC 7742 (64 cores, 256 GB RAM, PCIe 4.0) — *Best for modern enterprise server profiling*.
  * `m510` (Utah): Intel Xeon D-1548 (8 cores, 64 GB RAM) — *Best for industrial edge server / telecom box profiling*.
  * `xl170` (Utah): Intel Xeon E5-2640v4 (10 cores, 64 GB RAM).
* **Recommended ARM64 Profile**:
  * `m400` or Ampere Altra instances (if available on CloudLab clusters).
* **OS Image**: Standard **Ubuntu 22.04 LTS** or **Ubuntu 24.04 LTS**.

---

## 2. Quickstart Execution (On the CloudLab Node)

Once your CloudLab experiment is active and you SSH into the physical node:

```bash
# 1. Clone your research repository
git clone <YOUR_GIT_REPO_URL> alloc
cd alloc

# 2. Run one-click bare-metal environment setup
# (Installs dependencies, disables THP, locks CPU frequency to 'performance', downloads ONNX Runtime & yolo26n.onnx)
./first_steps/scripts/cloudlab/setup_baremetal_node.sh

# 3. Run the hardware PMU profiling suite (pins to CPU core 2 under Linux perf)
./first_steps/scripts/cloudlab/run_baremetal_perf_bench.sh
```

---

## 3. What the Benchmark Measures on Physical Silicon

| Hardware Counter / Metric | Systems Phenomenon Being Tested | Academic Paper Grounding |
| :--- | :--- | :--- |
| **`dTLB-load-misses`** | Measures translation lookaside buffer misses caused by intermediate tensor buffers straddling $4\text{ KiB}$ boundaries. | **Fistanto et al. (PLOS '23)**: Granularity mismatch forces physical frame fragmentation. |
| **`page-faults` & `minor-faults`** | Verifies zero-fault execution in steady state and measures page-zeroing penalties under memory returning. | **Wilson et al. (IWMM '95)**: Lifetime isolation eliminates steady-state re-faulting. |
| **`L1-dcache-load-misses` & `LLC-misses`** | Tests whether domain-specific page-grid arenas compact cache lines better than general-purpose slab free lists. | **ISMM '24**: Custom memory allocators achieve superior cache density on static DAGs. |
| **`cycles` & `instructions` (IPC)** | Compares CPU instruction efficiency per frame between general-purpose multi-threaded synchronization vs. domain-aware allocation. | **ISMM '24**: Proves general allocators pay unnecessary synchronization overhead for single-stream vision DAGs. |
| **Peak RSS (`VmHWM`) & Private Dirty** | Quantifies physical DRAM locked away from the OS due to thread-cache decay timers (`dirty_decay_ms`). | **Hunter et al. (OSDI '21)**: Proactive memory reclamation on production fleets. |

---

## 4. Output Artifacts

All outputs are saved to `first_steps/bench/yolo26n_pipeline/results_baremetal/`:
* `perf_<config>.txt`: Raw Linux `perf stat` hardware counter outputs.
* `<config>.json`: High-resolution latency and memory metrics from the C++ runner.
* `BAREMETAL_REPORT.md`: Consolidated publication-grade markdown table with IPC, cache miss rates, dTLB miss rates, Peak RSS, and $p99$ tail latency.
