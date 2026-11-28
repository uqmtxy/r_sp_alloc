# Stochastic Ski-Rental Oracle Benchmark (Apple M2 Pro, 16 KiB Pages)

- **Total runs:** 40
- **Global `max_abs_error`:** `0`
- **Break-even calibration:** $\tau^* = 250$ ms (normalized holding rate $1/\tau^*$ vs. unit refault cost)

| Policy | Trials | Mean `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | Minor Faults | Reclaim Calls | p99 (ms) | Empirical Competitive Ratio (ALG/OPT) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `bfc` | 5 | 105.99 | 1.376 | 7 | 0.0 | 121.82 | 2.7165x |
| `pool_retain` | 5 | 74.09 | 0.973 | 6 | 0.0 | 122.77 | 2.7211x |
| `pool_explicit` | 5 | 56.57 | 0.736 | 30,291 | 16.0 | 120.28 | 1.5218x |
| `pool_auto_50` | 5 | 58.84 | 0.761 | 24,638 | 12.2 | 122.78 | 1.1287x |
| `pool_auto_150` | 5 | 61.96 | 0.817 | 16,160 | 8.0 | 126.65 | 1.3739x |
| `pool_auto_250` | 5 | 63.35 | 0.833 | 14,141 | 7.0 | 123.47 | 1.5875x |
| `pool_auto_500` | 5 | 67.07 | 0.881 | 10,105 | 5.0 | 126.70 | 2.0421x |
| `pool_auto_1000` | 5 | 71.56 | 0.922 | 6,066 | 3.0 | 119.99 | 2.5931x |

## Acceptance Gates

- **Gate 1 (`max_abs_error == 0`):** `True`
- **Gate 2 (`ALG(250 ms) / OPT <= 2.0`):** `True` (1.5875x)
- **Gate 3 (`ALG(250 ms) < ALG(tau=0)` and `< ALG(tau=inf)`):** `False` (1.5875x < 1.5218x and 2.7211x)
- **`gates_pass`:** `False`
