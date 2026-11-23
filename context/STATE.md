# Project State (`r_sp_alloc`)

**Last Verified:** 2026-11-23  
**Git Baseline:** `1fd7ede` (`feat r_sp_alloc: better`, 2026-11-19)  
**Active Frontier:** [../first_steps/bench/utilization/](../first_steps/bench/utilization/)  
**Compute Split:** Edit/analyze/audit locally on macOS arm64; compile/run Linux C++ & ORT benchmarks on CloudLab `m510` (Xeon D-1548, Ubuntu `6.8.0-147-generic`).

---

## 1. Current Research Status

We have validated physical RAM reduction (`mincore()` + private bytes + active peak RSS) on fixed-shape `640x640` FP32 **YOLO26n** inference under ONNX Runtime CPU EP with bit-exact outputs (`max_abs_error == 0`):
- **Continuous / Eager Trimming (`20dc7b8`, rechecked `c9828c5`):** `direct_trim` reduced single-stream active peak RSS by **40.27%** (`73.39` vs. `122.86 MiB`). `pool_eager_trim` matched steady active peak (`73.74 MiB`), lowered startup peak (`73.50` vs. `78.99 MiB`), lifted allocator-local idle payload utilization to `99.9903%`, and lowered 4-camera live idle private RAM from `390.80` (`bfc_trim`) to `88.05 MiB`.
- **Full Pause Purging (`c9828c5` — `FAILED_GATE`):** Purging all free `body` pages (`0 MiB`) at 1 s scheduled pauses cut idle private RAM by `71.1%`, but increased burst p99 latency by `+11.1%` (`298.87` vs. `268.92 ms`).
- **Bounded 24 MiB Retention (`1fd7ede` — `VALIDATED` on single-stream explicit pauses):** `purge_to_budget(24 MiB)` passed all prespecified median gates across 5 fresh pairs (`0.4916` idle private ratio, `0.5604` byte-seconds ratio, `0.9947` burst FPS ratio, `1.0108` burst p99 ratio) and 1,000 endurance frames (`58.37` vs. `118.79 MiB` idle private, `5.751` vs. `5.796` FPS, `290.87` vs. `277.87 ms` p99).

See [EVIDENCE.md](EVIDENCE.md) for machine-verified numbers and scope limits.

---

## 2. Active Tasks & Research Roadmap

- [ ] **T1 (Next Technical Gate): Automatic idle / inter-burst detector** — replace explicit scheduled pause callbacks in [activity_bench.cpp](../first_steps/bench/utilization/activity_bench.cpp) with an autonomous activity/inter-arrival trigger and test under variable pause timings.
- [ ] **T2: Staggered 4-camera bounded retention** — extend `purge_to_budget()` from single-stream explicit pauses to staggered multi-camera activity schedules without sharing a single global freeze assumption.
- [ ] **T3: Real OS memory-pressure responsiveness** — test demand-driven reclamation under actual cgroup / OS memory pressure (never conflating scheduled pauses with memory pressure).
- [ ] **T4: Sync primary paper (`first_steps/paper/`)** — rewrite [../first_steps/paper/MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md), [../first_steps/paper/latex/main.tex](../first_steps/paper/latex/main.tex), and regenerate `first_steps/paper/figures/` strictly from `VALIDATED` entries in [EVIDENCE.json](EVIDENCE.json) (use [../assignment/latex/main.tex](../assignment/latex/main.tex) as the clean Nov 19 baseline).
- [ ] **T5: Generalization & pre-fault mitigation** — test dynamic input shapes, newer ARM/x86 CPUs beyond Broadwell-DE, and selective pre-faulting to tighten individual-trial p99 max dispersion (`1.2737` continuous / `1.1541` 30-frame burst).

---

## 3. Paper & Deliverable Status

| Deliverable | Status | Notes |
|---|---|---|
| [../assignment/GE6001-03300-M03_Erdeni_Khatunov.pdf](../assignment/GE6001-03300-M03_Erdeni_Khatunov.pdf) & [../assignment/latex/main.tex](../assignment/latex/main.tex) | `SYNCED_NOV19` | Graduate English ACM `acmart` report (6 pages, 2,889 body words), compiled & visually verified against Nov 19 data |
| [../first_steps/paper/MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md) & [../first_steps/paper/latex/main.tex](../first_steps/paper/latex/main.tex) | `STALE_DRAFT` | Oct 15/19 pre-CloudLab draft; contains invalidated claims (`18,842 FPS`, `0.0 KB trapped DRAM`, `Zheng et al.`). **Do not cite.** |
| [../first_steps/paper/figures/](../first_steps/paper/figures/) | `STALE_OCT19` | Outdated Oct 19 plots; do not mix with Nov 5–19 utilization figures ([../assignment/figures/](../assignment/figures/)) |
| [../first_steps/paper/latex/references.bib](../first_steps/paper/latex/references.bib) | `CORRECTED` | Updated Nov 6 with accurate 2026 PLOS/ISMM metadata (note legacy BibTeX keys `fistanto2023lostbytes` & `ismm2024reconsidering` kept for compatibility) |

---

## 4. Known Repository Provenance Notes

- In immutable report [../first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md), line 79 links to `REJECTED_APPROACHES.md`; that file actually resides one level up at [../first_steps/bench/utilization/REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md). Left untouched to preserve the historical report.
- Latest handoff: [sessions/2026-11-23_infra_setup.md](sessions/2026-11-23_infra_setup.md).
