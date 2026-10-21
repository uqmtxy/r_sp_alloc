# CloudLab bare-metal YOLO26n measurements

Full-process perf counts include startup and warmup; JSON minor faults exclude warmup. PMU counts are multiplexed estimates: inspect raw perf files for coverage and unsupported events. N/A means absent, uncounted, or an invalid denominator. Memory units are MiB.

| Allocator | Peak RSS | Settled RSS | Private dirty | IPC | L1 miss % | LLC miss % | dTLB load miss % | dTLB store misses | Process page faults | Process minor faults | Post-warmup minor faults | p99 ms | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| glibc_default | 122.61 | 122.61 | 108.32 | 2.376 | 7.098 | 0.000 | 0.009 | 72719236 | 30918 | 30918 | 0 | 205.35 | 6.46 |
| glibc_tuned | 115.08 | 115.08 | 100.81 | 2.366 | 7.050 | 0.000 | 0.008 | 73908309 | 39486 | 39486 | 0 | 159.32 | 6.68 |
| jemalloc_default | 128.50 | 114.22 | 99.20 | 2.372 | 7.083 | 0.000 | 0.008 | 71213470 | 29740 | 29740 | 11 | 165.44 | 6.70 |
| jemalloc_tuned | 114.78 | 114.40 | 99.48 | 2.370 | 7.106 | 0.000 | 0.009 | 71497332 | 47731 | 47731 | 6003 | 164.02 | 6.70 |
| tcmalloc | 134.91 | 134.91 | 120.32 | 2.372 | 7.074 | 0.000 | 0.008 | 71355644 | 32287 | 32287 | 0 | 206.02 | 6.39 |
| mimalloc | 121.73 | 120.31 | 105.85 | 2.376 | 7.100 | 0.000 | 0.008 | 68095000 | 36804 | 36804 | 6004 | 205.08 | 6.53 |
| dynalloc | 115.25 | 115.25 | 100.86 | 2.002 | 7.322 | 0.000 | 0.008 | 71637786 | 47512 | 47512 | 5004 | 221.37 | 5.62 |

## Retired-load L3 validation pass

The 128 MiB cache sanity test reported zero generic LLC-load-misses but 16.8 million mem_load_uops_retired.l3_miss events. Generic LLC zeros above are unreliable. This separate pass uses grouped retired L3 hit/miss events, 1000 measured frames plus 50 warmup, and the same allocator settings. Counts include startup and warmup. Its miss fraction is misses / (hits + misses), not the generic LLC event ratio.

| Allocator | Retired L3 hits | Retired L3 misses | Retired L3 miss % |
|---|---:|---:|---:|
| glibc_default | 1260440584 | 188379435 | 13.002 |
| glibc_tuned | 1253685337 | 181487458 | 12.646 |
| jemalloc_default | 1196109099 | 186767751 | 13.506 |
| jemalloc_tuned | 1221718768 | 182527592 | 12.998 |
| tcmalloc | 1216592182 | 186386051 | 13.285 |
| mimalloc | 1228015391 | 179232488 | 12.736 |
| dynalloc | 1134864270 | 173396817 | 13.254 |

## PARE-TA allocation microbenchmark

This benchmark touches the first and last element of each simulated tensor; it does not run ONNX inference. Total minor faults include warmup; the post-warmup column reports the separate steady-state interval. Previously hard-coded trapped DRAM values have been replaced with null (not measured).

| Configuration | p99 ms | Peak RSS MiB | Private dirty MiB | Minor faults including warmup | Post-warmup minor faults |
|---|---:|---:|---:|---:|---:|
| jemalloc_default_adv0 | 0.182289 | 4.92 | 0.67 | 7864 | 7514 |
| jemalloc_default_adv10 | 0.076692 | 28.90 | 24.59 | 8876 | 8510 |
| jemalloc_default_adv3 | 0.098018 | 11.48 | 7.17 | 7320 | 7007 |
| jemalloc_tuned_adv0 | 0.472586 | 4.80 | 0.53 | 18751 | 18006 |
| malloc_default_adv0 | 0.278248 | 3.95 | 0.27 | 9901 | 9500 |
| malloc_default_adv10 | 0.127579 | 27.47 | 21.31 | 8852 | 8500 |
| malloc_default_adv3 | 0.156455 | 10.28 | 6.46 | 8855 | 8500 |
| mimalloc_adv0 | 0.017764 | 4.48 | 0.74 | 100 | 0 |
| mimalloc_adv3 | 0.019362 | 10.92 | 7.18 | 85 | 0 |
| pare_ta_adv0 | 0.001278 | 4.24 | 0.56 | 78 | 0 |
| pare_ta_adv10 | 0.000756 | 27.47 | 21.86 | 76 | 0 |
| pare_ta_adv3 | 0.000586 | 10.40 | 6.76 | 76 | 0 |
| std_pmr_adv0 | 0.001263 | 67.95 | 64.27 | 4 | 0 |
| std_pmr_adv10 | 0.000810 | 89.30 | 85.56 | 2 | 0 |
| std_pmr_adv3 | 0.000543 | 74.16 | 70.46 | 2 | 0 |
| tcmalloc_adv0 | 0.031991 | 8.17 | 4.29 | 159 | 104 |
| tcmalloc_adv3 | 0.041244 | 14.38 | 10.15 | 147 | 112 |

## Four-camera inference

Settled RSS is sampled 200 ms after worker teardown. Adversarial live anchors remain allocated at this point. This cannot establish permanent retention or attribute retained bytes exclusively to thread caches.

| Configuration | p99 ms | FPS | Peak RSS MiB | Settled RSS MiB | Private dirty MiB | Measured interval minor faults |
|---|---:|---:|---:|---:|---:|---:|
| dynalloc_adv0 | 215.36 | 19.33 | 396.34 | 37.88 | 23.29 | 1020 |
| dynalloc_adv1 | 218.01 | 19.38 | 403.96 | 44.33 | 29.83 | 1020 |
| dynalloc_adv10 | 231.66 | 18.90 | 467.91 | 108.98 | 94.48 | 1004 |
| dynalloc_adv3 | 280.39 | 19.31 | 418.55 | 59.51 | 44.92 | 1020 |
| glibc_adv0 | 202.83 | 21.99 | 429.80 | 112.15 | 97.57 | 2 |
| glibc_adv1 | 256.63 | 22.17 | 462.11 | 146.29 | 131.80 | 2 |
| glibc_adv10 | 232.41 | 21.67 | 493.68 | 177.04 | 162.46 | 2 |
| glibc_adv3 | 194.38 | 21.54 | 449.16 | 133.11 | 118.62 | 2 |
| jemalloc_adv0 | 193.28 | 22.04 | 453.75 | 22.41 | 7.25 | 2 |
| jemalloc_adv1 | 194.63 | 22.12 | 460.75 | 30.05 | 14.98 | 2 |
| jemalloc_adv10 | 210.13 | 21.93 | 521.12 | 93.63 | 78.58 | 2 |
| jemalloc_adv3 | 208.91 | 22.04 | 476.00 | 46.25 | 31.18 | 2 |
| mimalloc_adv0 | 208.85 | 22.15 | 416.69 | 386.51 | 371.89 | 623 |
| mimalloc_adv1 | 230.72 | 22.06 | 434.21 | 389.12 | 374.55 | 235 |
| mimalloc_adv10 | 192.84 | 22.03 | 506.09 | 435.93 | 421.25 | 2 |
| mimalloc_adv3 | 197.56 | 21.88 | 479.25 | 367.82 | 353.25 | 2 |
| tcmalloc_adv0 | 218.48 | 22.16 | 461.95 | 461.95 | 447.22 | 2 |
| tcmalloc_adv1 | 235.13 | 21.83 | 462.47 | 462.47 | 447.84 | 2 |
| tcmalloc_adv10 | 244.34 | 22.00 | 519.33 | 519.33 | 504.72 | 2 |
| tcmalloc_adv3 | 200.13 | 22.00 | 485.97 | 485.97 | 471.31 | 2 |

## Interpretation limits

CPU affinity does not provide kernel CPU isolation. CPU 2 shares a physical core with CPU 10. Single runs do not establish statistical significance. Zero LLC counts require independent validation. Cache and TLB counters alone cannot prove elimination of page straddling or trapped DRAM. The primary inference suite uses the ONNX Runtime arena, so preload allocators may not see every tensor allocation.
