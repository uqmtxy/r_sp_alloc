# CloudLab Bare-Metal YOLO26n Evaluation Report

Target Hardware: CloudLab Utah m510 (Intel Xeon D-1548, 8 cores / 16 threads, 64 GB DRAM).
Workload: Ultralytics YOLO26n ONNX Runtime C++ Pipeline (640x640, FP32).
All single-stream runs executed on pinned logical CPU 2 (1000 measured frames + 50 warmup frames).

## 1. Primary ONNX Runtime Inference Evaluation

Full-process perf counts include startup and warmup; JSON minor faults measure post-warmup steady state.
Memory units are MiB. N/A means metric was uncounted or denominator was zero.

| Allocator / Mode |          Mode | Peak RSS | Settled RSS | Priv Dirty |   IPC | L1 Miss % | dTLB Load Miss % | Minor Faults (Steady) | p99 ms |  FPS |
|------------------|---------------|----------|-------------|------------|-------|-----------|------------------|-----------------------|--------|------|
| glibc_default    |     bfc_arena |   122.77 |      122.77 |     108.47 | 2.376 |     7.112 |            0.008 |                     0 | 159.83 | 6.71 |
| glibc_tuned      |     bfc_arena |   115.07 |      115.07 |     100.77 | 2.379 |     7.086 |            0.009 |                     0 | 232.17 | 6.20 |
| jemalloc_default |     bfc_arena |   128.62 |      114.42 |      99.38 | 2.372 |     7.088 |            0.008 |                    26 | 177.63 | 6.69 |
| jemalloc_tuned   |     bfc_arena |   114.64 |      114.20 |      99.16 | 2.379 |     7.107 |            0.008 |                  5004 | 231.94 | 6.36 |
| tcmalloc         |     bfc_arena |   132.59 |      132.59 |     117.99 | 2.369 |     7.080 |            0.008 |                     0 | 218.36 | 6.50 |
| mimalloc         |     bfc_arena |   122.13 |      120.69 |     106.31 | 2.379 |     7.088 |            0.008 |                  6004 | 217.25 | 6.53 |
| dynalloc         |     bfc_arena |   140.97 |      140.97 |     126.56 | 2.374 |     7.079 |            0.008 |                     0 | 152.39 | 6.72 |
| pare_ta          |       pare_ta |   203.35 |      203.35 |     189.06 | 2.372 |     7.038 |            0.008 |                     0 | 232.47 | 6.46 |
| direct_glibc     | direct_tensor |   107.12 |       71.65 |      57.25 | 2.210 |     7.031 |            0.016 |               9113000 | 235.67 | 5.64 |
| direct_jemalloc  | direct_tensor |   102.59 |       39.57 |      24.53 | 2.212 |     7.028 |            0.015 |               9113494 | 249.57 | 5.74 |
| direct_tcmalloc  | direct_tensor |   111.12 |       82.11 |      67.61 | 2.378 |     7.031 |            0.008 |                 27341 | 205.06 | 6.56 |
| direct_mimalloc  | direct_tensor |    90.14 |       54.02 |      39.54 | 2.291 |     7.008 |            0.012 |               4569004 | 262.03 | 5.94 |
| direct_dynalloc  | direct_tensor |   135.59 |      135.59 |     121.30 | 2.372 |     7.043 |            0.008 |                     0 | 205.44 | 6.56 |
| direct_pare_ta   |       pare_ta |   203.47 |      203.47 |     189.17 | 2.372 |     7.042 |            0.008 |                     0 | 218.22 | 6.41 |

## 2. Model-Specific Retired-Load L3 Cache Validation

Uses grouped un-multiplexed events `{mem_load_uops_retired.l3_hit, mem_load_uops_retired.l3_miss}`.
Miss % is computed as `misses / (hits + misses) * 100`.

| Allocator / Mode | Retired L3 Hits | Retired L3 Misses | Retired L3 Miss % |
|------------------|-----------------|-------------------|-------------------|
| glibc_default    |   1,232,273,486 |       189,593,784 |            13.334 |
| glibc_tuned      |   1,255,800,898 |       182,126,374 |            12.666 |
| jemalloc_default |   1,181,885,039 |       184,684,564 |            13.514 |
| jemalloc_tuned   |   1,179,603,950 |       184,688,451 |            13.537 |
| tcmalloc         |   1,208,530,926 |       187,471,817 |            13.429 |
| mimalloc         |   1,228,095,793 |       178,462,323 |            12.688 |
| dynalloc         |   1,211,010,158 |       187,227,140 |            13.390 |
| pare_ta          |   1,234,623,640 |       182,119,578 |            12.855 |
| direct_glibc     |   1,233,332,298 |       177,170,344 |            12.561 |
| direct_jemalloc  |   1,188,079,037 |       180,269,353 |            13.174 |
| direct_tcmalloc  |   1,227,445,133 |       170,915,678 |            12.223 |
| direct_mimalloc  |   1,212,659,169 |       176,142,300 |            12.683 |
| direct_dynalloc  |   1,210,961,911 |       182,171,017 |            13.076 |

## 3. PARE-TA Custom Arena Architecture Telemetry

Page-Aligned Region-Enclosed Tensor Arena runtime counters under ONNX Runtime inference.

- **Model**: YOLO26n ONNX (640x640 FP32)
- **Persistent Weights Arena**: 9.51 MiB
- **Peak Transient Frame Activation**: 118.07 MiB
- **Total Allocations Handled**: 2,428
- **Eliminated Individual Frees (O(1) No-ops)**: 2,203
- **Reported Trapped DRAM**: 0.0 KiB (hardcoded by the harness; not a physical-hole measurement)
- **Post-Warmup Steady-State Minor Page Faults**: 0

## 4. Multi-Camera Concurrent Stream Evaluation

4 Concurrent Camera Streams executing YOLO26n on pinned logical CPUs 0–3 under adversarial heap fragmentation.

| Configuration  | Adv Level | Aggregate FPS | p99 ms | Peak RSS MiB | Settled RSS MiB | Priv Dirty MiB | Minor Faults |
|----------------|-----------|---------------|--------|--------------|-----------------|----------------|--------------|
| dynalloc_adv0  |         0 |         21.69 | 194.97 |       484.75 |          413.83 |         399.33 |            2 |
| dynalloc_adv1  |         1 |         21.73 | 196.61 |       496.12 |          425.07 |         410.59 |            2 |
| dynalloc_adv10 |        10 |         21.88 | 189.51 |       532.75 |          461.70 |         447.10 |            2 |
| dynalloc_adv3  |         3 |         21.48 | 190.94 |       512.38 |          441.48 |         426.97 |            2 |
| glibc_adv0     |         0 |         21.79 | 192.67 |       444.00 |          128.28 |         113.81 |            2 |
| glibc_adv1     |         1 |         21.81 | 192.05 |       457.88 |          141.98 |         127.48 |            2 |
| glibc_adv10    |        10 |         21.70 | 228.12 |       497.31 |          181.35 |         166.77 |            2 |
| glibc_adv3     |         3 |         21.60 | 190.86 |       454.56 |          139.56 |         125.07 |            2 |
| jemalloc_adv0  |         0 |         21.62 | 195.33 |       456.75 |           22.50 |           7.34 |            5 |
| jemalloc_adv1  |         1 |         21.70 | 193.77 |       457.38 |           29.55 |          14.46 |            2 |
| jemalloc_adv10 |        10 |         21.84 | 215.47 |       518.62 |           91.71 |          76.55 |            2 |
| jemalloc_adv3  |         3 |         22.21 | 191.79 |       475.43 |           45.36 |          30.30 |            2 |
| mimalloc_adv0  |         0 |         21.95 | 192.53 |       421.74 |           98.64 |          84.05 |          806 |
| mimalloc_adv1  |         1 |         21.97 | 189.51 |       440.48 |           93.45 |          78.77 |          446 |
| mimalloc_adv10 |        10 |         21.66 | 207.72 |       516.59 |          173.69 |         159.01 |            2 |
| mimalloc_adv3  |         3 |         21.62 | 198.03 |       480.25 |          112.53 |          97.85 |            4 |
| pare_ta_adv0   |         0 |         21.94 | 211.35 |       775.95 |           40.82 |          26.17 |            3 |
| pare_ta_adv1   |         1 |         22.09 | 193.24 |       787.91 |           39.31 |          24.66 |            3 |
| pare_ta_adv10  |        10 |         21.90 | 190.55 |       832.86 |          125.34 |         110.69 |            3 |
| pare_ta_adv3   |         3 |         22.23 | 252.90 |       787.51 |           61.45 |          46.90 |            3 |
| tcmalloc_adv0  |         0 |         21.68 | 190.44 |       462.05 |          462.05 |         447.39 |            2 |
| tcmalloc_adv1  |         1 |         21.69 | 201.57 |       464.38 |          464.38 |         449.65 |            2 |
| tcmalloc_adv10 |        10 |         21.74 | 191.59 |       511.22 |          511.22 |         496.49 |            2 |
| tcmalloc_adv3  |         3 |         21.81 | 193.40 |       479.52 |          479.52 |         464.79 |            2 |

## 5. Hypothesis Verification

| Criterion                                                          |     Status |                                                                                                                                Evidence |
|--------------------------------------------------------------------|------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| DynAlloc steady minor faults <= 5                                  |       PASS |                                                                                                                                       0 |
| DynAlloc throughput >= 6.70 FPS                                    |       PASS |                                                                                                                             6.71965 FPS |
| PARE-TA steady minor faults = 0                                    |       PASS |                                                                                                                                       0 |
| PARE-TA trapped DRAM < 4 KiB; zero body holes                      | UNVERIFIED |                                                                             Inference field is hardcoded; no physical-hole measurement. |
| PARE-TA beats direct glibc/jemalloc in FPS, p99, L3 miss %         |       FAIL |                                                         Retired L3 miss %: PARE-TA 12.855, direct glibc 12.561, direct jemalloc 13.174. |
| Adv10 bounded live PARE-TA DRAM; TCMalloc/mimalloc retain >400 MiB |    PARTIAL | Post-teardown settled MiB (PARE-TA/TCMalloc/mimalloc): 125.34/511.22/173.69; live bound not measured, mimalloc >400 MiB not reproduced. |

The performance criterion uses direct_pare_ta primary FPS/p99 and the pare_ta grouped L3 pass (both use native pare_ta mode). These are separate process runs.

Standalone arena results are supplemental endpoint-touch microbenchmarks; they do not replace full-inference physical-hole validation.

## 6. Measurement Limits

- CPU affinity pins single-stream inference to logical CPU 2 and camera workers to CPUs 0–3; affinity alone does not provide kernel or IRQ isolation.
- Primary perf events are collected together and may be multiplexed. Consult raw perf logs for running percentages; ratios across differently scheduled events need caution.
- Retired L3 events are grouped in a separate pass and include initialization and warmup. They do not isolate steady-state tensor accesses.
- The inference harness emits a literal zero for trapped_dram_kb. This does not establish zero physical body holes or a measured <4 KiB bound.
- Arena mappings provide contiguous virtual addresses; physical DRAM contiguity is not established.
- Zero minor faults do not by themselves establish zero syscalls. No syscall trace is collected by these scripts.
- Multi-camera settled RSS is sampled after workers join and destroy their sessions and arenas; it measures process retention after teardown, not live arena residency.
