# Verified Evidence Registry (`r_sp_alloc`)

<!-- AUTO-GENERATED from context/EVIDENCE.json via `python3 scripts/research/check_evidence.py --render`. Do not hand-edit. -->

Every quantitative claim below is programmatically checked against raw JSON artifacts on disk.

## Summary Matrix

| ID | Status | Commit | Report | Scope & Caveats |
|---|---|---|---|---|
| `E01_cloudlab_oct21_dynalloc` | **DESCRIPTIVE_HISTORICAL** | `1496bd2` | [results/cloudlab_20261021/SUMMARY.md](../results/cloudlab_20261021/SUMMARY.md) | Single descriptive run per config on Xeon D-1548 (1000 frames + 50 warmup). Generic LLC counters failed sanity test; LD_PRELOAD does not capture internal ORT arena allocations; DynAlloc did not beat glibc. |
| `E02_util_single_stream` | **VALIDATED** | `20dc7b8` | [first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md) | Fixed 640x640 FP32 YOLO26n on Xeon D-1548 (5 fresh runs, 60+10 frames, CPU 2). 99.9903% is allocator-local idle payload utilization inside pool-owned mappings, not whole-process RAM; direct_trim slightly beats pool_eager_trim on active peak RSS (73.39 vs 73.74 MiB). |
| `E03_util_4cameras` | **VALIDATED** | `20dc7b8` | [first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md) | 5 fresh processes, 30+5 frames per camera pinned to CPUs 0-3, sessions alive at idle sample. Fixed 640x640 YOLO26n on Broadwell-DE. |
| `E04_recheck_repeatability` | **VALIDATED** | `c9828c5` | [first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md](../first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md) | Repeatability check using the same harness (does not eliminate shared-harness bias). Confirms bit-exact outputs and footprint stability across randomized run order. |
| `E05_activity_full_purge` | **FAILED_GATE** | `c9828c5` | [first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md](../first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md) | 120 frames, 30-frame bursts, four 1 s explicit pauses (n=5 per policy). Cut idle private RAM by 71.1%, but failed latency acceptance (+11.1% p99 regression due to resume minor faults). |
| `E06_bounded_screening` | **SCREENING** | `1fd7ede` | [first_steps/bench/utilization/results/bounded/PROTOCOL.md](../first_steps/bench/utilization/results/bounded/PROTOCOL.md) | Single shuffled run per budget (120 frames, 10-frame bursts, 1 s explicit pauses) used solely to freeze the 24 MiB candidate before confirmation. |
| `E07_bounded_confirmation` | **VALIDATED** | `1fd7ede` | [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md) | Single-stream CPU 2, explicit 1 s pauses, fixed 640x640 FP32 YOLO26n. Continuous p99 showed individual max ratio 1.2737; 5% p99 gate is an assumed research margin. |
| `E08_bounded_endurance` | **VALIDATED** | `1fd7ede` | [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md) | 1 run per policy (1000 frames, 100 pauses of 1 s, burst=10, CPU 2). Byte-seconds are frame-boundary sampled and can undercount transient intra-frame peaks. |
| `E09_bounded_30f_sensitivity` | **VALIDATED** | `1fd7ede` | [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md) | Sensitivity check under original 30-frame burst schedule. Median gates pass, while individual trials had p99 max ratio 1.1541 (24 MiB) and 1.1473 (0 MiB). |
| `E10_paper_manuscript_nov28` | **SYNCED_NOV28_FINAL** | `UNCOMMITTED_LOCAL` | [first_steps/paper/MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md) | Synced Nov 28 final paper covering Theorems 1-5, CloudLab m510 (4 KiB), and Apple M2 Pro (16 KiB, 181 validated runs). CloudLab replication of auto_idle_bench, multicam_staggered_bench, and stochastic_ski_bench remains pending. |
| `E11_macbook_m2pro_auto_idle_screening` | **SCREENING** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md) | Apple M2 Pro / macOS Darwin 25.6.0 / 16 KiB pages / ORT 1.32.0 / core_pinned=false. Single shuffled run per budget (120 frames, 10-frame bursts, 1 s pauses) freezing 0 MiB budget. |
| `E12_macbook_m2pro_auto_idle_confirm` | **VALIDATED** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md) | Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / core_pinned=false (QOS_CLASS_USER_INITIATED) / mmap(MAP_FIXED) discard. Single-stream YOLO26n; continuous p99 max ratio reached 1.3250. Requires separate CloudLab Linux replication. |
| `E13_macbook_m2pro_multicam_4` | **VALIDATED** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md) | Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 25% duty cycle (burst_gap_ms=350 > idle_ms=200). 5 shuffled trials per policy; pool_coop lower p99 includes P-core wakeup before frame 0 while wall_sec/p50 match retain. |
| `E14_macbook_m2pro_multicam_8` | **VALIDATED** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md) | Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 25% duty cycle (burst_gap_ms=350 > idle_ms=200). 5 shuffled trials per policy; CloudLab 4 KiB replication pending. |
| `E15_macbook_m2pro_multicam_handoff` | **VALIDATED** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md) | Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / --shed-warmup / 50 trials (35 gap=0 across 7 policies, 15 gap=350 across 3 policies). CloudLab 4 KiB replication pending. |
| `E16_macbook_m2pro_stochastic_ski` | **VALIDATED** | `UNCOMMITTED_LOCAL` | [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md) | Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 40 trials (8 policies x 5). Break-even tau*=250 ms achieves 1.5875x <= 2.00x bound vs 2.7211x retain; tau=50/150 ms also beat pool_explicit (1.5218x). |

## Machine-Verified Assertions

### `E01_cloudlab_oct21_dynalloc` — Oct 21 CloudLab Baseline: DynAlloc vs. Conventional Allocators (`DESCRIPTIVE_HISTORICAL`)
- **Commit:** `1496bd2` | **Report:** [results/cloudlab_20261021/SUMMARY.md](../results/cloudlab_20261021/SUMMARY.md)
- **Scope / Caveats:** Single descriptive run per config on Xeon D-1548 (1000 frames + 50 warmup). Generic LLC counters failed sanity test; LD_PRELOAD does not capture internal ORT arena allocations; DynAlloc did not beat glibc.
  - `PASS`: `results/cloudlab_20261021/primary_and_l3/dynalloc.json:achieved_fps=5.6215 (exp 5.6215±0.001)`
  - `PASS`: `results/cloudlab_20261021/primary_and_l3/dynalloc.json:latency.p99_ms=221.371 (exp 221.371±0.01)`
  - `PASS`: `results/cloudlab_20261021/primary_and_l3/dynalloc.json:peak_rss_mb=115.25 (exp 115.25±0.01)`
  - `PASS`: `results/cloudlab_20261021/primary_and_l3/glibc_default.json:achieved_fps=6.46142 (exp 6.46142±0.001)`
  - `PASS`: `results/cloudlab_20261021/primary_and_l3/glibc_default.json:peak_rss_mb=122.609 (exp 122.609±0.01)`

### `E02_util_single_stream` — Single-Stream Live Utilization & Allocator-Local mincore() Accounting (`VALIDATED`)
- **Commit:** `20dc7b8` | **Report:** [first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md)
- **Scope / Caveats:** Fixed 640x640 FP32 YOLO26n on Xeon D-1548 (5 fresh runs, 60+10 frames, CPU 2). 99.9903% is allocator-local idle payload utilization inside pool-owned mappings, not whole-process RAM; direct_trim slightly beats pool_eager_trim on active peak RSS (73.39 vs 73.74 MiB).
  - `PASS`: `all_zero max_abs_error across 45 files`
  - `PASS`: `median(5):active_peak_rss_bytes=122.859 (exp 122.86±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=73.3867 (exp 73.39±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=73.7383 (exp 73.74±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=119.062 (exp 119.06±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=34.1953 (exp 34.2±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=34.4258 (exp 34.43±0.02)`
  - `PASS`: `median(5):pages_idle.payload_utilization=0.999903 (exp 0.999903±1e-05)`
  - `PASS`: `median(5):pages_idle.resident_body_bytes=0 (exp 0±0.0)`

### `E03_util_4cameras` — Four Live Camera Sessions Utilization (CPUs 0-3) (`VALIDATED`)
- **Commit:** `20dc7b8` | **Report:** [first_steps/bench/utilization/results/UTILIZATION_REPORT.md](../first_steps/bench/utilization/results/UTILIZATION_REPORT.md)
- **Scope / Caveats:** 5 fresh processes, 30+5 frames per camera pinned to CPUs 0-3, sessions alive at idle sample. Fixed 640x640 YOLO26n on Broadwell-DE.
  - `PASS`: `all_zero max_abs_error across 24 files`
  - `PASS`: `median(5):active_peak_rss_bytes=394.664 (exp 394.66±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=236.512 (exp 236.51±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=234.094 (exp 234.09±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=390.801 (exp 390.8±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=90.5039 (exp 90.5±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=88.0469 (exp 88.05±0.02)`

### `E04_recheck_repeatability` — Randomized Fresh-Process Repeatability Audit (46 Runs) (`VALIDATED`)
- **Commit:** `c9828c5` | **Report:** [first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md](../first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md)
- **Scope / Caveats:** Repeatability check using the same harness (does not eliminate shared-harness bias). Confirms bit-exact outputs and footprint stability across randomized run order.
  - `PASS`: `all_zero max_abs_error across 46 files`
  - `PASS`: `median(5):active_peak_rss_bytes=122.766 (exp 122.77±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=73.2695 (exp 73.27±0.02)`
  - `PASS`: `median(5):active_peak_rss_bytes=73.6211 (exp 73.62±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=390.156 (exp 390.16±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=90.418 (exp 90.42±0.02)`
  - `PASS`: `median(5):live_session_idle.private_bytes=87.9453 (exp 87.95±0.02)`

### `E05_activity_full_purge` — Unbudgeted Pause-Boundary Purge (pool_idle 0 MiB vs. BFC) (`FAILED_GATE`)
- **Commit:** `c9828c5` | **Report:** [first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md](../first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md)
- **Scope / Caveats:** 120 frames, 30-frame bursts, four 1 s explicit pauses (n=5 per policy). Cut idle private RAM by 71.1%, but failed latency acceptance (+11.1% p99 regression due to resume minor faults).
  - `PASS`: `all_zero max_abs_error across 18 files`
  - `PASS`: `median(5):fps=6.4054 (exp 6.405±0.005)`
  - `PASS`: `median(5):p99_ms=268.918 (exp 268.918±0.01)`
  - `PASS`: `median(5):idle_private_bytes=118.703 (exp 118.703±0.01)`
  - `PASS`: `median(5):fps=6.30785 (exp 6.308±0.005)`
  - `PASS`: `median(5):p99_ms=298.87 (exp 298.87±0.01)`
  - `PASS`: `median(5):idle_private_bytes=34.3438 (exp 34.344±0.01)`

### `E06_bounded_screening` — Bounded Retention Budget Screening (0, 8, 16, 24 MiB vs. BFC) (`SCREENING`)
- **Commit:** `1fd7ede` | **Report:** [first_steps/bench/utilization/results/bounded/PROTOCOL.md](../first_steps/bench/utilization/results/bounded/PROTOCOL.md)
- **Scope / Caveats:** Single shuffled run per budget (120 frames, 10-frame bursts, 1 s explicit pauses) used solely to freeze the 24 MiB candidate before confirmation.
  - `PASS`: `all_zero max_abs_error across 5 files`
  - `PASS`: `first_steps/bench/utilization/results/bounded/screen_0.json:minor_faults=100517 (exp 100517±0.0)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/screen_24.json:minor_faults=32918 (exp 32918±0.0)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/screen_-1.json:idle_private_bytes=118.793 (exp 118.79±0.02)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/screen_24.json:idle_private_bytes=58.3438 (exp 58.34±0.02)`

### `E07_bounded_confirmation` — Confirmed Paired Evaluation of 24 MiB Bounded Retention (n=5 pairs) (`VALIDATED`)
- **Commit:** `1fd7ede` | **Report:** [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md)
- **Scope / Caveats:** Single-stream CPU 2, explicit 1 s pauses, fixed 640x640 FP32 YOLO26n. Continuous p99 showed individual max ratio 1.2737; 5% p99 gate is an assumed research margin.
  - `PASS`: `all_zero max_abs_error across 20 files`
  - `PASS`: `paired_ratio_median(5):fps=0.9977 (exp 0.9977±0.0005)`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0182 (exp 1.0182±0.0005)`
  - `PASS`: `paired_ratio_median(5):fps=0.9947 (exp 0.9947±0.0005)`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0108 (exp 1.0108±0.0005)`
  - `PASS`: `paired_ratio_median(5):idle_private_bytes=0.4916 (exp 0.4916±0.0005)`
  - `PASS`: `paired_ratio_median(5):estimated_private_byte_seconds=0.5604 (exp 0.5604±0.0005)`
  - `PASS`: `paired_ratio_median(5):resume_median_ms=1.0321 (exp 1.0321±0.0005)`

### `E08_bounded_endurance` — 1,000-Frame Endurance Validation (100 Scheduled Pauses) (`VALIDATED`)
- **Commit:** `1fd7ede` | **Report:** [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md)
- **Scope / Caveats:** 1 run per policy (1000 frames, 100 pauses of 1 s, burst=10, CPU 2). Byte-seconds are frame-boundary sampled and can undercount transient intra-frame peaks.
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_-1.json:max_abs_error=0 (exp 0±0.0)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_24.json:max_abs_error=0 (exp 0±0.0)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_-1.json:fps=5.79596 (exp 5.79596±0.001)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_24.json:fps=5.75121 (exp 5.75121±0.001)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_-1.json:p99_ms=277.867 (exp 277.867±0.01)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_24.json:p99_ms=290.872 (exp 290.872±0.01)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_-1.json:idle_private_bytes=118.793 (exp 118.79±0.02)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_24.json:idle_private_bytes=58.3672 (exp 58.37±0.02)`
  - `PASS`: `first_steps/bench/utilization/results/bounded/endurance_24.json:minor_faults=296226 (exp 296226±0.0)`

### `E09_bounded_30f_sensitivity` — 30-Frame Burst Sensitivity Check (5 Shuffled Blocks) (`VALIDATED`)
- **Commit:** `1fd7ede` | **Report:** [first_steps/bench/utilization/results/bounded/REPORT.md](../first_steps/bench/utilization/results/bounded/REPORT.md)
- **Scope / Caveats:** Sensitivity check under original 30-frame burst schedule. Median gates pass, while individual trials had p99 max ratio 1.1541 (24 MiB) and 1.1473 (0 MiB).
  - `PASS`: `all_zero max_abs_error across 15 files`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0500 (exp 1.05±0.0005)`
  - `PASS`: `paired_ratio_median(5):idle_private_bytes=0.2891 (exp 0.2891±0.0005)`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0474 (exp 1.0474±0.0005)`
  - `PASS`: `paired_ratio_median(5):idle_private_bytes=0.4912 (exp 0.4912±0.0005)`

### `E10_paper_manuscript_nov28` — Camera-Ready ACM Sigconf Paper, Overleaf Bundle & Theorems 1-5 Verification (`SYNCED_NOV28_FINAL`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/paper/MANUSCRIPT.md](../first_steps/paper/MANUSCRIPT.md)
- **Scope / Caveats:** Synced Nov 28 final paper covering Theorems 1-5, CloudLab m510 (4 KiB), and Apple M2 Pro (16 KiB, 181 validated runs). CloudLab replication of auto_idle_bench, multicam_staggered_bench, and stochastic_ski_bench remains pending.
  - `PASS`: `context/THEORY_VERIFICATION.json:all_passed=True (exp True)`

### `E11_macbook_m2pro_auto_idle_screening` — Apple M2 Pro (16 KiB Pages) Autonomous Idle-Detector Screening (`SCREENING`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS Darwin 25.6.0 / 16 KiB pages / ORT 1.32.0 / core_pinned=false. Single shuffled run per budget (120 frames, 10-frame bursts, 1 s pauses) freezing 0 MiB budget.
  - `PASS`: `all_zero max_abs_error across 6 files`
  - `PASS`: `first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/screen_-1.json:idle_private_bytes=106.016 (exp 106.02±0.02)`
  - `PASS`: `first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/screen_0.json:idle_private_bytes=42.313 (exp 42.31±0.02)`
  - `PASS`: `first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/screen_0.json:false_reclaims=0 (exp 0±0.0)`

### `E12_macbook_m2pro_auto_idle_confirm` — Apple M2 Pro Autonomous Idle-Detector Confirmation (Continuous, Burst, Mixed) (`VALIDATED`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / core_pinned=false (QOS_CLASS_USER_INITIATED) / mmap(MAP_FIXED) discard. Single-stream YOLO26n; continuous p99 max ratio reached 1.3250. Requires separate CloudLab Linux replication.
  - `PASS`: `all_zero max_abs_error across 40 files`
  - `PASS`: `all_zero false_reclaims across 15 files`
  - `PASS`: `paired_ratio_median(5):fps=0.9989 (exp 0.9989±0.0005)`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0131 (exp 1.0131±0.0005)`
  - `PASS`: `paired_ratio_median(5):idle_private_bytes=0.3993 (exp 0.3993±0.0005)`
  - `PASS`: `paired_ratio_median(5):p99_ms=1.0130 (exp 1.013±0.0005)`
  - `PASS`: `paired_ratio_median(5):idle_private_bytes=0.4003 (exp 0.4003±0.0005)`
  - `PASS`: `median(5):reclaim_calls=6 (exp 6±0.0)`
  - `PASS`: `median(5):reclaim_calls=12 (exp 12±0.0)`

### `E13_macbook_m2pro_multicam_4` — Apple M2 Pro Staggered Multi-Camera YOLO26n (N=4, K=1, 25% Duty Cycle) (`VALIDATED`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 25% duty cycle (burst_gap_ms=350 > idle_ms=200). 5 shuffled trials per policy; pool_coop lower p99 includes P-core wakeup before frame 0 while wall_sec/p50 match retain.
  - `PASS`: `all_zero max_abs_error across 25 files`
  - `PASS`: `context/THEORY_VERIFICATION.json:theorem_3.passed=True (exp True)`

### `E14_macbook_m2pro_multicam_8` — Apple M2 Pro Staggered Multi-Camera YOLO26n (N=8, K=2, 25% Duty Cycle) (`VALIDATED`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 25% duty cycle (burst_gap_ms=350 > idle_ms=200). 5 shuffled trials per policy; CloudLab 4 KiB replication pending.
  - `PASS`: `all_zero max_abs_error across 20 files`
  - `PASS`: `context/THEORY_VERIFICATION.json:theorem_4.passed=True (exp True)`

### `E15_macbook_m2pro_multicam_handoff` — Apple M2 Pro N=8 K=2 Back-to-Back Handoff & Multi-Allocator Baselines (`VALIDATED`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / --shed-warmup / 50 trials (35 gap=0 across 7 policies, 15 gap=350 across 3 policies). CloudLab 4 KiB replication pending.
  - `PASS`: `all_zero max_abs_error across 50 files`
  - `PASS`: `context/THEORY_VERIFICATION.json:theorem_5.passed=True (exp True)`

### `E16_macbook_m2pro_stochastic_ski` — Apple M2 Pro Stochastic Ski-Rental Oracle Validation (30-1500 ms Gaps) (`VALIDATED`)
- **Commit:** `UNCOMMITTED_LOCAL` | **Report:** [first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md](../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski/REPORT.md)
- **Scope / Caveats:** Apple M2 Pro / macOS / 16 KiB pages / ORT 1.32.0 / 40 trials (8 policies x 5). Break-even tau*=250 ms achieves 1.5875x <= 2.00x bound vs 2.7211x retain; tau=50/150 ms also beat pool_explicit (1.5218x).
  - `PASS`: `all_zero max_abs_error across 40 files`
  - `PASS`: `context/THEORY_VERIFICATION.json:theorem_2.passed=True (exp True)`
