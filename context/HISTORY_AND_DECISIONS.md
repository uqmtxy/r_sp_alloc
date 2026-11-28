# Research History & Architectural Decisions

## 1. Chronological Evolution

| Commit | Date | Milestone | Verified Outcome |
|---|---|---|---|
| Pre-Oct | 2026-06 / 09 | Phase-1 Docker lab (`bench/alloc_workload.cpp`) & feasibility docs (`first_steps/01..06_*.md`) | Established problem taxonomy; **rejected** eBPF malloc/free hot-path interception |
| `969766a`..`955b5f8` | 2026-10-01..19 | Exploratory `approach_01..06`, `dynalloc.cpp`, `pare_ta_arena`, early `MANUSCRIPT.md` | Produced speculative pre-CloudLab numbers later invalidated on bare metal |
| `1496bd2` | 2026-10-21 | First CloudLab `m510` bare-metal benchmark ([../results/cloudlab_20261021/SUMMARY.md](../results/cloudlab_20261021/SUMMARY.md)) | `DynAlloc` slower than `glibc_tuned` (`5.62` vs `6.68 FPS`); exposed generic LLC anomaly, PARE-TA hardcoded zero, `large_alloc +16 B` bug |
| `9399e4e`, `5be613f` | 2026-11-05 | Remediated `dynalloc.cpp` & `pare_ta_bench` rebench on CloudLab | Historical rebench preserved; still did not validate end-to-end live ORT RAM utilization |
| `20dc7b8` | 2026-11-05 | **Utilization Pivot** (`first_steps/bench/utilization/`, [UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md)) | Built `PageReuseResource` + `mincore()` accounting + bit-exact full/retained FP32 verification; −40.27% active peak RSS (`direct_trim`), 99.9903% allocator-local idle payload utilization |
| `c9828c5` | 2026-11-09 | 46-run repeatability audit + pause purging ([RECHECK_REPORT.md](../first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md), [ACTIVITY_REPORT.md](../first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md)) | Confirmed baseline repeatability; `pool_idle` (0 MiB purge) cut idle private RAM −71.1% (`34.34 MiB`) but failed p99 gate (+11.1%) |
| `1fd7ede` | 2026-11-19 | **Bounded Retention** (`purge_to_budget(24 MiB)`, [REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md)) | Passed all prespecified median gates: −50.8%–50.9% idle private RAM, −44.0%–44.3% byte-seconds, 1.0108 burst p99 ratio |
| `91300b3` | 2026-11-23 | **Research OS & Audit Infrastructure** ([AGENTS.md](../AGENTS.md), [INFRASTRUCTURE.md](INFRASTRUCTURE.md)) | Added `EVIDENCE.json` + `check_evidence.py`, `check_state.py`, `validate_run.py`, `reindex.py`, `.agents/skills/` & `guard_immutable.py` |
| Local `2026-11-28` | 2026-11-28 | **Autonomous Idle Detector & M2 Pro `16 KiB` Support** ([REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md)) | Proved XNU `MADV_DONTNEED` is a no-op; switched macOS `discard()` to `mmap(MAP_FIXED)`; built `IdleReclamationController`; 0 false reclaims on continuous & 80 ms jitter, −60% idle footprint (`0 MiB` budget) on M2 Pro |

---

## 2. Active System Architecture (`first_steps/bench/utilization/`)

1. **`ObservedAllocator` ([utilization_bench.cpp](../first_steps/bench/utilization/utilization_bench.cpp)):** Implements ORT's `OrtAllocator` C API (`Alloc`, `Reserve`, `Free`, `Info`) with ORT's CPU arena disabled (`DisableCpuMemArena()`) and environment-level allocator registration enabled, ensuring ORT routes tensor allocations through the test policy.
2. **`PageReuseResource` ([page_reuse_resource.hpp](../first_steps/bench/utilization/page_reuse_resource.hpp)):**
   - Reserves `256 MiB` anonymous `mmap` virtual address space (reported separately from physical RAM).
   - Packs allocations to `64 B` alignment (matching ORT alignment) instead of wasteful per-object `4 KiB` alignment.
   - Keeps `live_` and `free_` `std::map` metadata **outside** the purgeable mapping so metadata pages are never discarded.
   - Performs best-fit allocation and eager two-sided coalescing on `deallocate()`.
   - Reclaims only wholly free `4 KiB` page interiors (`[round_up(start, 4096), end_down(end, 4096))`) via `madvise(..., MADV_DONTNEED)`, preserving pages with live or caller-retained tensors.
   - `purge_to_budget(budget)` queries `mincore()`, leaves up to `budget / 4096` already-resident wholly free pages warm in RAM, and discards excess free pages.
   - `sample()` asserts `resident == resident_payload + resident_body + resident_head + resident_tail + resident_sub`.
3. **Burst Harness ([activity_bench.cpp](../first_steps/bench/utilization/activity_bench.cpp)):** Runs 10 warmup frames + `frames` measured frames (e.g., 120 or 1,000) pinned to CPU 2, holding caller-retained outputs across an 11-frame window (`i % 11`), separating active inference/free timing from verification, `/proc/self/smaps_rollup` telemetry, and pause reclamation cost.

---

## 3. Rejected & Failed Approaches (Do Not Re-Propose Without New Evidence)

| Approach | Source | Why Rejected / Failed |
|---|---|---|
| Direct eBPF interception of `malloc`/`free` hot path | [02_ebpf_feasibility_analysis.md](../first_steps/02_ebpf_feasibility_analysis.md) | Uprobe/eBPF overhead on high-frequency user-space alloc/free hot paths is prohibitive; use out-of-band supervision only |
| `legacy_pare` frame-reset wrapper | [REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md) | Crashed under full-output validation; incompatible with arbitrary live/retained tensor lifetimes |
| `bfc_exact` experimental env-arena mode | [REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md) | Crashed with ORT `RegionFor` error on ORT 1.20.1 |
| One-buffer unconditional frame reset | [REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md) | Corrupts caller-retained outputs across frames (`max_abs_error != 0`) |
| `4 KiB` page alignment for every allocation | [REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md) | Increased internal resident padding compared to `64 B` packing |
| Initialization-only trimming | [REJECTED_APPROACHES.md](../first_steps/bench/utilization/REJECTED_APPROACHES.md) | Failed to clear recurring steady-state resident free pages |
| Unbudgeted pause purge (`pool_idle`, `0 MiB`) | [ACTIVITY_REPORT.md](../first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md) | Cut idle RAM −71.1%, but induced ~27k minor resume faults and **+11.1% burst p99 latency** (`FAILED_GATE`) |
| Early `DynAlloc` (`large_alloc +16 B`, `posix_memalign`) & generic LLC stats | [MEASUREMENT_NOTES.md](../results/cloudlab_20261021/MEASUREMENT_NOTES.md) | Early `DynAlloc` ignored alignment (`+16 B`); generic `LLC-load-misses` returned invalid zeros; `glibc_tuned` outperformed `DynAlloc` |
| `T2/T3` Staggered Multi-Camera & Formal Theory (`pool_coop`, M2 Pro `16 KiB`) | [2026-11-28_macbook_m2pro_multicam/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md), [THEORY_VERIFICATION.json](THEORY_VERIFICATION.json) | 45 validated runs (`N=4 K=1`, `N=8 K=2`, `max_abs_error=0`): `pool_coop` & `pool_auto_0` cut mean `phys_footprint` by `62%–66%` vs `BFC`; Theorems 1–4 verified (`0.36%–0.51%` multi-camera error, $R^2 \ge 0.99987$); [main.tex](../first_steps/paper/latex/main.tex) & [MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md) synced |
| `T6/T7` Handoff Multi-Allocator + Stochastic Ski-Rental + Theorem 5 (`pool_hybrid`) | [2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md), [2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md) | 90 additional validated runs (`max_abs_error=0`, 181 total on M2 Pro): `pool_hybrid` beats `pool_auto_0` peak (`225.84 < 289.04 MiB`, `gap=0`), beats `direct_jemalloc` minor faults by `8.0x` (`32,412` vs `258,802`), beats `pool_coop` under `gap=350`; `pool_auto_250` achieves `1.59x <= 2.00x` competitive ratio; 7 figures + 8-page ACM paper + Overleaf bundle compiled |
