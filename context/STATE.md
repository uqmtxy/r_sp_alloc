# Research State — `r_sp_alloc`

Last updated: `2026-11-28` | Base commit: `91300b3` (plus validated local `T1a`, `T2/T3`, `T4`, `T5`, `T6` handoff/multi-allocator, `T7` stochastic ski-rental, and final paper sync)

## Active Tasks

- [x] **T1a (MacBook M2 Pro `16 KiB` Auto-Idle Reclamation):** Implemented `platform_memory.hpp`, `idle_reclaimer.hpp`, `auto_idle_bench.cpp`, and ran 46 protocol-validated trials in [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md) (`max_abs_error=0`, `0` false reclaims, `-60.0%` idle `phys_footprint`).
- [x] **T2 + T3 (Cooperative Multi-Stream Arbiter & Staggered Multi-Camera Benchmark):** Added `GlobalBudgetArbiter` to `idle_reclaimer.hpp`, passed ASan/UBSan on `resource_test.cpp`, `auto_idle_test.cpp`, and `multicam_test.cpp`, and ran 45 protocol-validated trials (`N=4, K=1` & `N=8, K=2`, 25% duty cycle) in [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md).
- [x] **T6 (Back-to-Back Handoff & Multi-Allocator Baselines):** Added `pool_hybrid`, `direct_malloc`, `direct_jemalloc`, `--shed-warmup`, and `--save-trace` to `multicam_staggered_bench.cpp`; ran 50 protocol-validated trials (`N=8, K=2`, `gap=0` and `gap=350`) in [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md) (`gates_pass=True`).
- [x] **T7 (Stochastic Ski-Rental Oracle Benchmark):** Created `stochastic_ski_bench.cpp` and ran 40 protocol-validated trials across `30–1500 ms` gaps in [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md) (`max_abs_error=0`, `pool_auto_250` `1.5875x <= 2.00x`).
- [x] **T4 (Formal Theorems 1–5 & Programmatic Verification):** Verified Theorems 1–5 in [scripts/research/verify_theory.py](../scripts/research/verify_theory.py) (`all_passed=True` in [THEORY_VERIFICATION.json](THEORY_VERIFICATION.json)).
- [x] **T5 (7 Publication Figures, Camera-Ready Paper & Overleaf Bundle):** Generated 7 `.pdf`/`.png` figures in [first_steps/paper/figures/](../first_steps/paper/figures/generate_paper_figures.py), compiled [first_steps/paper/latex/main.pdf](../first_steps/paper/latex/main.pdf) (8 pages, ACM `sigconf`), packaged [first_steps/paper/overleaf_paper_bundle.zip](../first_steps/paper/overleaf_paper_bundle.zip), and synced [first_steps/paper/MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md).
- [ ] **T1b (Pending Future Work — CloudLab `4 KiB` Replication):** Replicate `auto_idle_bench`, `multicam_staggered_bench`, and `stochastic_ski_bench` on bare-metal CloudLab `m510` Linux (`4 KiB` pages).

## Document & Paper Sync Status

- [assignment/latex/main.tex](../assignment/latex/main.tex) (`GE6001-03300-M03_Erdeni_Khatunov.pdf`): `SYNCED_NOV19`
- [first_steps/paper/latex/main.tex](../first_steps/paper/latex/main.tex) (`main.pdf`, `overleaf_paper_bundle.zip`, `MANUSCRIPT.md`): `SYNCED_NOV28_FINAL`

## Known Caveats & Guardrails

- Strictly separate CloudLab `m510` (`4 KiB`, `MADV_DONTNEED`, `PSS`/private RSS) from Apple M2 Pro (`16 KiB`, `mmap(MAP_FIXED)`, `phys_footprint`).
- Never modify immutable raw result directories (`results/cloudlab_20261021/`, `first_steps/bench/utilization/results/{validated,recheck,activity,bounded,screen}/`).
- In immutable report [../first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md), line 79 links to `REJECTED_APPROACHES.md`; that file resides one level up at [../first_steps/bench/utilization/REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md). Left untouched to preserve the historical report.
- Latest handoff: [sessions/2026-11-28_final_complete_research.md](sessions/2026-11-28_final_complete_research.md).
