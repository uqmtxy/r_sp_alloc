# CloudLab m510 allocator evaluation

Intel Xeon D-1548; all primary configurations used 1000 measured frames and 50 warmup frames on logical CPU 2. See environment.json and MEASUREMENT_NOTES.md for provenance and limitations.

## Primary inference results

| Allocator | FPS | p99 ms | Peak RSS MiB | Post-warmup minor faults |
|---|---:|---:|---:|---:|
| glibc_default | 6.46 | 205.35 | 122.61 | 0 |
| glibc_tuned | 6.68 | 159.32 | 115.08 | 0 |
| jemalloc_default | 6.70 | 165.44 | 128.50 | 11 |
| jemalloc_tuned | 6.70 | 164.02 | 114.78 | 6003 |
| tcmalloc | 6.39 | 206.02 | 134.91 | 0 |
| mimalloc | 6.53 | 205.08 | 121.73 | 6004 |
| dynalloc | 5.62 | 221.37 | 115.25 | 5004 |

DynAlloc had lower throughput and higher p99 than default glibc in this run. L1 load-miss fractions were approximately 7.05–7.11% for generic allocators and 7.32% for DynAlloc; this does not demonstrate the hypothesized L1 locality gain. dTLB load-miss fractions were similar (approximately 0.008–0.009%) and do not establish elimination of page straddling. Generic LLC-load-misses failed a sanity test; see the separate retired-load L3 validation table in the full report.

## Four-camera results

| Allocator | Adv0 settled RSS MiB | Adv10 settled RSS MiB | Adv10 aggregate FPS |
|---|---:|---:|---:|
| glibc | 112.15 | 177.04 | 21.67 |
| jemalloc | 22.41 | 93.63 | 21.93 |
| tcmalloc | 461.95 | 519.33 | 22.00 |
| mimalloc | 386.51 | 435.93 | 22.03 |
| dynalloc | 37.88 | 108.98 | 18.90 |

TCMalloc and mimalloc retained substantial RSS at the 200 ms snapshot. Live adversarial objects remain allocated, and the experiment cannot establish permanent retention or attribute all retained bytes to thread caches.

## PARE-TA

Allocation-only microbenchmark; ONNX inference is not executed. Corrected post-warmup telemetry is shown below. Physical trapped DRAM is not measured; the original hard-coded zero was replaced with null.

| Fragmentation | p99 µs | Post-warmup minor faults | Trapped DRAM |
|---|---:|---:|---|
| Adv0 | 1.278 | 0 | not measured |
| Adv3 | 0.586 | 0 | not measured |
| Adv10 | 0.756 | 0 | not measured |

PARE-TA had zero observed post-warmup minor faults at all three levels. Its p99 did not rise monotonically with adversarial level. The benchmark only touches tensor endpoints, so it does not establish full-inference latency or full-buffer residency.

## Model-specific L3 validation

| Allocator | Retired L3 load miss % |
|---|---:|
| glibc_default | 13.002 |
| glibc_tuned | 12.646 |
| jemalloc_default | 13.506 |
| jemalloc_tuned | 12.998 |
| tcmalloc | 13.285 |
| mimalloc | 12.736 |
| dynalloc | 13.254 |

All seven L3 validation passes used 1000 measured frames plus 50 warmup frames and grouped hit/miss events without multiplexing. Their full-process counts include initialization. DynAlloc lies within the generic-allocator range and does not show a clear L3 locality improvement in this experiment.
