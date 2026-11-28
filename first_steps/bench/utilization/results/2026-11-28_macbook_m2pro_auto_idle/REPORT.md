# Apple M2 Pro Autonomous Idle-Reclamation Experiment

- **Host:** `Apple M2 Pro` (`Mac14,10`), OS page size `16384` B, `onnxruntime 1.32.0`, `core_pinned=false` (QoS user-initiated)
- **Frozen Retention Budget:** `0 MiB` (selected from shuffled screening `0, 8, 16, 24, 32 MiB` meeting `<= 0.55 * BFC` idle footprint with lowest `p99_ms`)
- **Autonomous Detector Config:** `idle_ms=250`, `poll_ms=20`, `frames=120`, `warmup=10`, `max_abs_error=0` required on every run.

## Screening Sweep (Burst: 10-frame bursts, 1,000 ms pauses, n=1)

| Budget (MiB) | Policy | Active FPS | p99 (ms) | Resume Med (ms) | Idle Footprint (MiB) | Reclaims | False Reclaims | Minor Faults |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| BFC | `bfc` | 9.352 | 136.41 | 131.13 | 106.02 | 0 | 0 | 0 |
| 0 | `pool_auto` | 9.624 | 122.66 | 121.64 | 42.31 | 12 | 0 | 24230 |
| 8 | `pool_auto` | 9.542 | 135.61 | 133.80 | 50.33 | 12 | 0 | 18086 |
| 16 | `pool_auto` | 9.303 | 138.86 | 132.95 | 58.45 | 12 | 0 | 11942 |
| 24 | `pool_auto` | 9.393 | 139.76 | 132.38 | 66.28 | 12 | 0 | 5805 |
| 32 | `pool_auto` | 9.429 | 133.95 | 130.56 | 73.92 | 12 | 0 | 1 |

## Confirmation Results (n=5 Fresh Processes per Regime, Paired Ratios vs. BFC)

| Regime | Policy | Metric | Median [Min, Max] | Gate |
|---|---|---|---|---|
| continuous | `pool_auto` | fps ratio | 0.9989 [0.9812, 1.0026] | >= 0.97 |
| continuous | `pool_auto` | p99_ms ratio | 1.0285 [0.9912, 1.3250] | diagnostic |
| continuous | `pool_auto` | false_reclaims / reclaims (sum of 5) | 0 / 0 | == 0 |
| burst | `pool_auto` | fps ratio | 0.9984 [0.9930, 1.0034] | diagnostic |
| burst | `pool_auto` | p99_ms ratio | 1.0131 [1.0036, 1.0241] | <= 1.05 |
| burst | `pool_auto` | idle_private_bytes ratio | 0.3993 [0.3986, 0.4008] | <= 0.55 |
| burst | `pool_auto` | estimated_private_byte_seconds ratio | 0.6265 [0.6255, 0.6288] | < 1.00 |
| burst | `pool_auto` | resume_median_ms ratio | 1.0133 [1.0116, 1.0285] | diagnostic |
| burst | `pool_auto` | false_reclaims (sum) / reclaim_calls (med) | 0 / 12 | 0 false |
| burst | `pool_explicit` | fps ratio | 0.9985 [0.9936, 1.0012] | diagnostic |
| burst | `pool_explicit` | p99_ms ratio | 1.0109 [1.0090, 1.0262] | comparator |
| burst | `pool_explicit` | idle_private_bytes ratio | 0.3997 [0.3992, 0.4013] | comparator |
| burst | `pool_explicit` | estimated_private_byte_seconds ratio | 0.5529 [0.5523, 0.5592] | comparator |
| burst | `pool_explicit` | resume_median_ms ratio | 1.0255 [1.0135, 1.0263] | diagnostic |
| burst | `pool_explicit` | false_reclaims (sum) / reclaim_calls (med) | 0 / 12 | comparator |
| mixed | `pool_auto` | fps ratio | 0.9989 [0.9969, 0.9999] | diagnostic |
| mixed | `pool_auto` | p99_ms ratio | 1.0130 [0.9920, 1.0404] | <= 1.05 |
| mixed | `pool_auto` | idle_private_bytes ratio | 0.4003 [0.3987, 0.4005] | <= 0.55 |
| mixed | `pool_auto` | estimated_private_byte_seconds ratio | 0.6517 [0.6506, 0.6520] | < 1.00 |
| mixed | `pool_auto` | resume_median_ms ratio | 1.0111 [0.9839, 1.0262] | diagnostic |
| mixed | `pool_auto` | false_reclaims (sum) / reclaim_calls (med) | 0 / 6 | 0 false |
| mixed | `pool_explicit` | fps ratio | 0.9992 [0.9833, 1.0005] | diagnostic |
| mixed | `pool_explicit` | p99_ms ratio | 1.0139 [0.9842, 1.0200] | comparator |
| mixed | `pool_explicit` | idle_private_bytes ratio | 0.3996 [0.3986, 0.4012] | comparator |
| mixed | `pool_explicit` | estimated_private_byte_seconds ratio | 0.5960 [0.5952, 0.6056] | comparator |
| mixed | `pool_explicit` | resume_median_ms ratio | 1.0147 [0.9905, 1.0257] | diagnostic |
| mixed | `pool_explicit` | false_reclaims (sum) / reclaim_calls (med) | 0 / 12 | comparator |

## Absolute Medians (Burst & Mixed)

| Regime | Policy | Active FPS | p99 (ms) | Idle Footprint (MiB) | Reclaims (Med) | Minor Faults (Med) |
|---|---|---:|---:|---:|---:|---:|
| continuous | `bfc` | 9.790 | 104.39 | 105.75 | 0 | 3 |
| continuous | `pool_auto` | 9.785 | 106.71 | 73.97 | 0 | 2021 |
| burst | `bfc` | 9.513 | 136.74 | 105.80 | 0 | 3 |
| burst | `pool_explicit` | 9.516 | 137.96 | 42.27 | 12 | 24232 |
| burst | `pool_auto` | 9.482 | 138.50 | 42.25 | 12 | 24231 |
| mixed | `bfc` | 9.652 | 134.19 | 105.73 | 0 | 2 |
| mixed | `pool_explicit` | 9.648 | 135.96 | 42.23 | 12 | 24229 |
| mixed | `pool_auto` | 9.641 | 136.56 | 42.31 | 6 | 12114 |

Prespecified Apple M2 Pro median gates: **PASS**.

## Methodological Notes
- On macOS XNU (`16 KiB` pages), `MADV_DONTNEED` is a no-op on `phys_footprint`; `PageReuseResource::discard()` uses `mmap(..., MAP_FIXED)` anonymous remap so both `mincore()` and `phys_footprint` drop immediately while preserving live neighbors.
- `idle_private_bytes` maps to `TASK_VM_INFO.phys_footprint` on macOS. Threads run with `QOS_CLASS_USER_INITIATED` (`core_pinned=false`).
- All 46 experiment runs verified complete and retained FP32 outputs against `reference_m2pro.bin` (`max_abs_error == 0`).
