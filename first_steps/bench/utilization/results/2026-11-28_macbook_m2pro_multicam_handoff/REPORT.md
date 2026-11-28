# Multi-Camera Handoff & Allocator Baselines (`N=8, K=2`, MacBook Pro M2 Pro, 16 KiB Pages)

- **Total runs:** 50
- **OS page size:** 16384 B
- **Global `max_abs_error`:** `0` (verified=True)
- **Config:** `streams=8, concurrent=2, frames_per_burst=8, cycles=2, idle_ms=200, shed_warmup=True`

## Regime: `burst_gap_ms = 0`

| Policy | Trials | Mean `phys_footprint` (MiB) | Peak `phys_footprint` (MiB) | Final `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | p50 / p99 Latency (ms) | Wall (s) / FPS | Minor Faults | Auto / Press Rec. |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `bfc` | 5 | 631.24 ± 0.88 | 670.71 ± 0.17 | 670.50 | 3.345 | 84.53 / 89.16 ± 1.21 | 5.42 / 23.60 | 13,037 | 0.0 / 0.0 |
| `direct_malloc` | 5 | 267.52 ± 0.23 | 268.10 ± 0.20 | 267.90 | 1.419 | 84.71 / 87.19 ± 1.01 | 5.43 / 23.59 | 4,146 | 0.0 / 0.0 |
| `direct_jemalloc` | 5 | 213.18 ± 3.39 | 237.18 ± 5.62 | 159.54 | 1.172 | 87.46 / 89.76 ± 1.66 | 5.61 / 22.83 | 258,802 | 0.0 / 0.0 |
| `pool_retain` | 5 | 414.98 ± 0.22 | 415.10 ± 0.23 | 414.88 | 2.192 | 84.50 / 87.03 ± 2.16 | 5.41 / 23.67 | 140 | 0.0 / 0.0 |
| `pool_auto_0` | 5 | 240.42 ± 0.39 | 289.04 ± 0.17 | 225.68 | 1.276 | 84.49 / 87.94 ± 0.67 | 5.43 / 23.58 | 32,415 | 25.0 / 3.0 |
| `pool_coop` | 5 | 226.31 ± 7.99 | 229.16 ± 8.01 | 228.92 | 1.227 | 84.50 / 88.41 ± 1.85 | 5.55 / 23.08 | 32,410 | 0.0 / 22.0 |
| `pool_hybrid` | 5 | 222.98 ± 0.24 | 225.84 ± 0.16 | 225.61 | 1.206 | 84.40 / 87.37 ± 0.63 | 5.53 / 23.13 | 32,412 | 11.0 / 17.0 |

## Regime: `burst_gap_ms = 350`

| Policy | Trials | Mean `phys_footprint` (MiB) | Peak `phys_footprint` (MiB) | Final `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | p50 / p99 Latency (ms) | Wall (s) / FPS | Minor Faults | Auto / Press Rec. |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `pool_auto_0` | 5 | 214.51 ± 0.28 | 225.83 ± 0.17 | 162.52 | 1.771 | 84.38 / 115.88 ± 0.79 | 8.43 / 15.19 | 32,416 | 27.0 / 3.0 |
| `pool_coop` | 5 | 223.29 ± 0.28 | 225.72 ± 0.15 | 225.49 | 1.866 | 84.50 / 99.00 ± 0.73 | 8.55 / 14.97 | 32,415 | 0.0 / 22.0 |
| `pool_hybrid` | 5 | 216.78 ± 4.41 | 227.83 ± 4.26 | 164.51 | 1.791 | 84.29 / 113.39 ± 0.91 | 8.43 / 15.18 | 32,415 | 27.0 / 3.0 |

## Acceptance Gates

- **Gate 1 (`max_abs_error == 0` & verified):** `True`
- **Gate 2 (`gap=0` peak: `pool_hybrid` < `pool_auto_0` < `pool_retain` < `bfc`):** `True` (225.84 < 289.04 < 415.10 < 670.71 MiB)
- **Gate 3 (`gap=350` mean/final: `pool_hybrid` < `pool_coop`):** `True` (mean 216.78 < 223.29; final 164.51 < 225.49 MiB)
- **`gates_pass`:** `True`
