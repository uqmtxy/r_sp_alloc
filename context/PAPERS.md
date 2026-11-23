# Indexed Research Papers & Citation Guardrails

Authoritative bibliography metadata verified on **2026-11-05** (source URLs & PDF SHA256 hashes recorded in [../first_steps/bench/utilization/results/ENVIRONMENT.json](../first_steps/bench/utilization/results/ENVIRONMENT.json) and [../first_steps/utilization_improvement_plan.md](../first_steps/utilization_improvement_plan.md)).

---

## 1. Core Primary Papers

### [1] Fistanto, Tempel, & Dietrich — PLOS '26
- **Full Citation:** Pasha Fistanto, Sören Tempel, and Christian Dietrich. 2026. *Lost Bytes At The Crossroads Between User-And Kernel-Level Memory Management.* Proceedings of the 14th Workshop on Programming Languages and Operating Systems (PLOS '26), ACM.
- **DOI:** `10.1145/3831586.3838144`
- **URL:** `https://www.ibr.cs.tu-bs.de/vss/Publications/2026/fistanto_26_lost_bytes.pdf` (`sha256: 9458264a3d4a5e43d98bec61219a31f2e95eb4a0fd5eb2ee2f6e927cc161bc32`)
- **What this project uses:**
  - Formal classification of free user-space intervals against 4 KiB OS pages: **sub-page**, **head**, **tail**, and contiguous whole-page **body** holes (Sections 3–5).
  - Direct foundation for `PageReuseResource::sample()` and `discard()` in [page_reuse_resource.hpp](../first_steps/bench/utilization/page_reuse_resource.hpp).
- **What it does NOT establish (Do NOT overclaim):**
  - The paper's 75% figure measures **accumulated hole size**, NOT a universal 75% drop in total application RSS.
  - Aligning objects to 4 KiB pages alone proves neither low internal waste nor actual physical page reclamation.

### [2] van Kempen & Berger — ISMM '26
- **Full Citation:** Nicolas van Kempen and Emery D. Berger. 2026. *Reconsidering "Reconsidering Custom Memory Allocation".* Proceedings of the 2026 ACM SIGPLAN International Symposium on Memory Management (ISMM '26), pp. 14–24, ACM.
- **DOI:** `10.1145/3814942.3816132`
- **URL:** `https://arxiv.org/html/2605.17119v1` (`sha256: 9b26159a92d02c5aaae35414dcd9ddc5dbd09e8f1b2b005e791671569eb1a931`)
- **What this project uses:**
  - Section 2.2.2: identifies **deferred region deallocation** as a direct cause of increased memory consumption.
  - Adversarial fragmentation preconditioning (`Adv1` = 0.33, `Adv3` = 0.66, `Adv10` = 0.80 occupancy, Table 3).
- **What it does NOT establish / Repo adaptation limits:**
  - Section 5 limits evaluation to **single-threaded** applications; cache-locality improvements do not establish reduced memory consumption.
  - Our repo's stress runs (`N_peak=20000`, empirical size histogram in [utilization_bench.cpp](../first_steps/bench/utilization/utilization_bench.cpp)) are an **adaptation**, not a process-wide profile-driven ISMM reproduction.

---

## 2. Supporting Workload & Allocator References

| Key | Reference | Role in `r_sp_alloc` |
|---|---|---|
| `jocher2026yolo26` | Jocher, Chaurasia, & Ultralytics Team (2026). *YOLO26: High-Performance Real-Time Object Detection with NMS-Free End-to-End Latency.* `arXiv:2606.03748` | Fixed-shape `640x640` FP32 evaluation workload (`yolo26n.onnx`) |
| `berger2002reconsidering` | Berger, Zorn, & McKinley (2002). *Reconsidering Custom Memory Allocation.* OOPSLA '02, pp. 1–12, ACM. `10.1145/582419.582421` | Historical baseline on custom vs. general-purpose allocators |
| `leijen2019mimalloc` | Leijen, Zorn, & de Moura (2019). *Mimalloc: Free List Sharding in Action.* APLAS '19, pp. 244–265, Springer | Baseline allocator (`mimalloc 2.1.2`) |
| `evans2006jemalloc` | Evans (2006). *A Scalable Concurrent Malloc(3) Implementation for FreeBSD.* BSDCan | Baseline allocator (`jemalloc 5.3.0`) |
| `ghemawat2005tcmalloc` | Ghemawat & Menage (2005). *TCMalloc: Thread-Caching Malloc.* Google Open Source Docs | Baseline allocator (`gperftools 2.15`) |

---

## 3. Citation Errata in Legacy Files (Avoid Repeating)

- Legacy docs such as [../first_steps/walkthrough.md](../first_steps/walkthrough.md) and early BibTeX keys (`fistanto2023lostbytes`, `ismm2024reconsidering`) mislabeled the two core papers as **2023 / 2024 / SOSP '23** and misattributed authors (`E. Fistanto` / `Zheng et al.`). Always use **2026** (`PLOS '26`, `ISMM '26`) and the exact authors/DOIs in Section 1 above.
- Synced BibTeX entries reside in [../assignment/latex/references.bib](../assignment/latex/references.bib) and [../first_steps/paper/latex/references.bib](../first_steps/paper/latex/references.bib).
