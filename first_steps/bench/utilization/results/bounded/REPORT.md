# Bounded retention experiment

Screening selected 24 MiB. Five fresh pairs per regime. Values below are candidate/BFC ratios calculated within each pair.

| Regime | Metric | Median | Min | Max |
|---|---|---:|---:|---:|
| continuous | fps | 0.9977 | 0.9794 | 1.0107 |
| continuous | p99_ms | 1.0182 | 0.9895 | 1.2737 |
| burst | fps | 0.9947 | 0.9892 | 1.0068 |
| burst | p99_ms | 1.0108 | 0.9359 | 1.0362 |
| burst | idle_private_bytes | 0.4916 | 0.4896 | 0.4918 |
| burst | estimated_private_byte_seconds | 0.5604 | 0.5537 | 0.5626 |
| burst | resume_median_ms | 1.0321 | 1.0167 | 1.0334 |

Prespecified median gates: PASS.

Endurance results from 1000 frames and 100 scheduled pauses per policy.

| Policy | Active FPS | p99 ms | Resume median ms | Idle private MiB | Faults |
|---|---:|---:|---:|---:|---:|
| BFC | 5.796 | 277.87 | 275.85 | 118.79 | 2 |
| 24 | 5.751 | 290.87 | 286.19 | 58.37 | 296226 |

Full outputs and caller-retained outputs were checked against saved FP32 references. The runner required zero numerical error. Each process used CPU 2 and the same model. Reclamation time during scheduled pauses is recorded separately. FPS excludes scheduled sleep, validation and telemetry. Estimated byte-seconds uses frame-boundary private residency and can undercount transient peaks. Idle RAM is the final live pause sample. Five pairs on one host characterize this workload. The 5% p99 margin is an assumed research threshold.

## Original 30-frame bursts

Five shuffled blocks of BFC, full purge and bounded retention. Each row reports paired ratios against BFC.

| Budget MiB | Metric | Median | Min | Max |
|---:|---|---:|---:|---:|
| 0 | fps | 1.0015 | 0.9990 | 1.0080 |
| 0 | p99_ms | 1.0500 | 0.9945 | 1.1473 |
| 0 | idle_private_bytes | 0.2891 | 0.2884 | 0.2897 |
| 0 | estimated_private_byte_seconds | 0.5569 | 0.5523 | 0.5577 |
| 0 | resume_median_ms | 1.0500 | 0.9945 | 1.1473 |
| 24 | fps | 1.0020 | 0.9922 | 1.0077 |
| 24 | p99_ms | 1.0474 | 0.9184 | 1.1541 |
| 24 | idle_private_bytes | 0.4912 | 0.4899 | 0.4918 |
| 24 | estimated_private_byte_seconds | 0.5918 | 0.5871 | 0.5946 |
| 24 | resume_median_ms | 1.0474 | 0.9184 | 1.1541 |
