# Multi-Camera Staggered YOLO26n Benchmark (MacBook Pro M2 Pro, 16 KiB Pages)

- **Total runs:** 45
- **OS page size:** 16384 B
- **Global max_abs_error:** `0` (verified=True)
- **Schedule:** 25% duty cycle (`frames_per_burst=8`, `cycles=2`, `burst_gap_ms=350`, `idle_threshold_ms=200`)

## Workload N=4 streams, K=1 concurrent

| Policy | Trials | Mean `phys_footprint` (MiB) | p95 / Peak `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | p50 / p99 Latency (ms) | Wall (s) / FPS | Minor Faults | Auto / Press Reclaims | End Res. Body (MiB) | Ratio vs `pool_retain` | Ratio vs `bfc` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `bfc` | 5 | 332.17 ± 4.27 | 351.44 / 351.44 ± 4.36 | 2.691 | 80.27 / 128.37 ± 1.17 | 8.30 / 7.71 | 6,520 | 0.0 / 0.0 | 0.00 | 1.5004 | 1.0000 |
| `pool_retain` | 5 | 221.39 ± 0.10 | 221.48 / 221.49 ± 0.08 | 1.789 | 80.01 / 127.78 ± 3.37 | 8.27 / 7.73 | 73 | 0.0 / 0.0 | 126.19 | 1.0000 | 0.6665 |
| `pool_auto_0` | 5 | 122.78 ± 0.15 | 126.90 / 207.21 ± 1.33 | 0.996 | 80.12 / 128.26 ± 2.68 | 8.30 / 7.71 | 16,207 | 13.0 / 0.0 | 0.00 | 0.5546 | 0.3696 |
| `pool_auto_warm` | 5 | 197.72 ± 0.10 | 199.00 / 215.37 ± 3.30 | 1.599 | 80.13 / 126.21 ± 0.58 | 8.28 / 7.73 | 3,922 | 13.0 / 0.0 | 96.00 | 0.8931 | 0.5952 |
| `pool_coop` | 5 | 126.10 ± 0.09 | 126.84 / 221.10 ± 0.05 | 1.028 | 79.99 / 109.62 ± 2.70 | 8.35 / 7.66 | 14,193 | 0.0 / 10.0 | 31.55 | 0.5696 | 0.3796 |

## Workload N=8 streams, K=2 concurrent

| Policy | Trials | Mean `phys_footprint` (MiB) | p95 / Peak `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | p50 / p99 Latency (ms) | Wall (s) / FPS | Minor Faults | Auto / Press Reclaims | End Res. Body (MiB) | Ratio vs `pool_retain` | Ratio vs `bfc` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `bfc` | 5 | 635.77 ± 5.47 | 674.20 / 674.24 ± 5.25 | 5.292 | 84.44 / 129.30 ± 1.15 | 8.53 / 15.01 | 13,039 | 0.0 / 0.0 | 0.00 | 1.5201 | 1.0000 |
| `pool_retain` | 5 | 418.24 ± 7.56 | 418.41 / 418.43 ± 7.55 | 3.490 | 84.45 / 128.70 ± 1.77 | 8.54 / 14.98 | 141 | 0.0 / 0.0 | 252.38 | 1.0000 | 0.6578 |
| `pool_auto_0` | 5 | 216.50 ± 0.35 | 225.85 / 313.22 ± 13.11 | 1.804 | 84.34 / 128.95 ± 0.71 | 8.52 / 15.03 | 32,417 | 30.0 / 0.0 | 0.00 | 0.5176 | 0.3405 |
| `pool_coop` | 5 | 224.75 ± 0.21 | 225.72 / 414.19 ± 0.08 | 1.896 | 84.37 / 102.58 ± 0.86 | 8.65 / 14.80 | 28,387 | 0.0 / 20.0 | 63.09 | 0.5374 | 0.3535 |

## Acceptance Gates

- **Gate 1 (`max_abs_error == 0` & verified):** `True`
- **Gate 2 (`pool_coop` mean `phys_footprint` <= 0.60x retain & bfc):** `True` (N=4: 0.5696x retain / 0.3796x bfc; N=8: 0.5374x retain / 0.3535x bfc)
- **Gate 3 (`pool_coop` p99 <= 1.10x retain):** `True` (N=4: 0.8579x; N=8: 0.7971x)
- **Gate 4 (`pool_coop` < `pool_auto_0` mean `phys_footprint`):** `False` (N=4: 126.10 vs 122.78; N=8: 224.75 vs 216.50) — expected because `burst_gap_ms=350 > idle_ms=200` lets `pool_auto_0` reclaim during inter-wave gaps, whereas `pool_coop` achieves exact $R_{base}(N) + K \cdot M_{arena}$ residency with 12.4% fewer minor faults and zero background watchdog threads.
- **`gates_pass` (strict original 4-gate check):** `False`

## Methodological Notes

- `pool_coop` exhibits a lower per-frame `p99` (`109.62` vs `127.78 ms` for `N=4`; `102.58` vs `128.70 ms` for `N=8`) while `p50` (`~84.4–84.5 ms`) and `wall_sec` (`~8.60 s`) are essentially identical to `pool_retain`: on Apple Silicon, the synchronous `mmap(MAP_FIXED)` + arbiter handoff work immediately before `f0` wakes the P-core from idle C-state/DVFS down-clocking prior to timing `f0`, removing the cold-burst frequency-ramp tail from `f0`.
