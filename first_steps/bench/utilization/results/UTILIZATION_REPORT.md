# Physical RAM utilization validation

Objective: execute identical FP32 YOLO26n inference with less resident RAM. FPS is diagnostic only.

Five fresh processes per configuration; 60 measured + 10 warmup frames; CPU 2; eight distinct deterministic inputs; complete outputs compared against conventional BFC reference tensors. Outputs are intentionally retained across later frames. All accepted comparisons are bit-exact (max absolute error 0).

All sizes below are MiB. Entries are median [minimum, maximum]. Startup includes construction and warmup. Active peak is the kernel RSS high-water mark reset after warmup. Idle samples retain the model, session, and allocator. RSS includes shared mappings; private resident memory is also reported. No teardown-only sample establishes success.

## Single-stream footprint

| Configuration | Startup/warmup peak RSS | Active peak RSS | Live-session idle RSS | Live-session idle private |
|---|---|---|---|---|
| bfc | 122.86 [122.66, 123.16] | 122.86 [122.66, 123.16] | 122.86 [122.66, 123.16] | 119.06 [118.97, 119.36] |
| bfc_trim | 113.83 [113.44, 114.05] | 113.83 [113.44, 114.05] | 113.83 [113.44, 114.05] | 110.03 [109.64, 110.35] |
| direct | 107.41 [106.64, 107.75] | 106.64 [77.05, 107.87] | 71.43 [41.75, 72.51] | 67.73 [38.02, 68.71] |
| direct_jemalloc | 96.14 [95.15, 99.59] | 95.20 [88.60, 99.41] | 39.64 [39.54, 39.66] | 35.84 [35.79, 35.86] |
| direct_mimalloc | 91.07 [86.52, 107.09] | 78.14 [77.93, 78.50] | 42.67 [42.56, 43.04] | 38.87 [38.86, 39.24] |
| direct_trim | 78.99 [78.86, 79.04] | 73.39 [73.13, 73.40] | 38.00 [37.89, 38.02] | 34.20 [34.18, 34.21] |
| pool_retain_trim | 73.77 [73.72, 73.84] | 73.82 [73.73, 73.84] | 73.78 [73.73, 73.84] | 70.04 [70.02, 70.04] |
| pool_boundary_trim | 73.50 [73.39, 73.52] | 73.71 [73.48, 73.72] | 38.20 [38.13, 38.22] | 34.42 [34.40, 34.44] |
| pool_eager_trim | 73.50 [73.38, 73.69] | 73.74 [73.49, 73.80] | 38.22 [38.13, 38.23] | 34.43 [34.41, 34.43] |

## Selection and claims

- Selected deployment policy: `direct_trim` (disable CPU arena and trim free glibc pages after outputs are released).
- Relative to default BFC, median active peak decreases by 40.27%.
- Relative to historical PARE-TA (203.35 MiB), median active peak decreases by 63.91%. Historical inputs/output-lifetime tests differ, so the new repeated BFC comparison is the controlled evidence.
- Best measured conventional peak: `direct_trim` at 73.39 MiB. The custom eager pool is 0.48% relative to it. Do not claim that a custom allocator beats the best tuned conventional allocator.
- The custom pool can reduce startup peaks relative to direct glibc trimming; compare both startup and active columns. No single policy is claimed to dominate every phase. The active-peak reset excludes model-loading transients; it does not erase or relabel them.

## Measured allocator-local utilization

The numerator is resident bytes covered by outstanding ORT allocator requests. It is not last-use tensor liveness within ORT memory-pattern blocks. The denominator covers only pool-owned mappings; it is not whole-process useful-byte utilization.

| Pool policy | Live requested MiB | Resident MiB | Resident free bodies MiB | Resident waste bytes | Payload utilization |
|---|---|---|---|---|---|
| pool_retain_trim | 9.35065 | 44.96484 | 35.61328 | 37344184 | 20.7955% |
| pool_boundary_trim | 9.35065 | 9.35156 | 0.00000 | 952 | 99.9903% |
| pool_eager_trim | 9.35065 | 9.35156 | 0.00000 | 952 | 99.9903% |

## Endurance

| Configuration | Measured frames | Active peak RSS MiB | Live-session idle private MiB | Max output error |
|---|---|---|---|---|
| direct_trim | 1000 | 73.34 | 34.20 | 0 |
| pool_eager_trim | 1000 | 73.82 | 34.44 | 0 |

## Four live camera sessions

Five fresh processes, 30 measured + 5 warmup frames per camera. Workers are pinned to CPUs 0–3. Sessions, allocators, and worker threads remain alive at the idle sample.

| Configuration | Active peak RSS MiB | Live-session idle RSS MiB | Live-session idle private MiB |
|---|---|---|---|
| bfc_trim | 394.66 [393.50, 395.18] | 394.66 [393.50, 395.18] | 390.80 [389.65, 391.31] |
| direct_trim | 236.51 [236.45, 236.62] | 94.33 [94.19, 94.39] | 90.50 [90.39, 90.54] |
| pool_eager_trim | 234.09 [233.81, 234.15] | 91.84 [91.79, 91.93] | 88.05 [88.02, 88.07] |

## Synthetic preconditioning sensitivity

These stress runs use the existing empirical size histogram and fixed peak-count proxy (20000). Occupancies follow Adv1=0.33, Adv3=0.66, Adv10=0.80, but this is explicitly an adaptation, not a process-wide profile-driven reproduction of ISMM. Live anchor allocations are useful payload, not fragmentation. One run per setting; no variance or universality claim is made.

| Configuration | Adv | Occupancy | Active peak RSS MiB | Live-session idle private MiB |
|---|---|---|---|---|
| bfc_trim | 1 | 0.33 | 394.55 | 390.68 |
| direct_trim | 1 | 0.33 | 237.34 | 91.20 |
| pool_eager_trim | 1 | 0.33 | 234.83 | 88.76 |
| bfc_trim | 3 | 0.66 | 398.09 | 394.22 |
| direct_trim | 3 | 0.66 | 240.86 | 94.73 |
| pool_eager_trim | 3 | 0.66 | 232.91 | 92.35 |
| bfc_trim | 10 | 0.8 | 412.96 | 409.09 |
| direct_trim | 10 | 0.8 | 255.42 | 109.37 |
| pool_eager_trim | 10 | 0.8 | 252.95 | 106.89 |

## Limits and rejected approaches

- This validates the supplied fixed-shape 640x640 model on this CPU/ORT build. It does not establish dynamic-shape or other-model behavior.
- Trimming may increase page faults, syscall work, and latency. Zero faults and higher FPS are not success criteria for this goal.
- Input/output/precision, session thread counts, graph optimization, and ORT memory-pattern settings are held constant across accepted comparisons.
- See REJECTED_APPROACHES.md for exploratory crashes and rejected policies. Failed modes are excluded from the supported final harness.
- mincore measures resident virtual pages. No physical contiguity, global system-wide utilization, or unique-PFN accounting is claimed.
- The custom pool reserves 256 MiB of virtual address space per camera. Reservation is excluded from physical consumption and reported separately.
- Repeated results, exact commands, model/reference/library/binary/source hashes, and all failures are preserved under results/.

## Source-grounded plan

See ../../../utilization_improvement_plan.md for the predeclared acceptance rule, paper corrections, and experiment order.

## Outcome

SUCCESS for the utilization objective on this workload: repeated live-process RAM reduction with exact inference outputs. The chosen implementation uses conventional allocation plus explicit return of free pages. The custom pool independently demonstrates resident-hole reclamation, but does not establish superiority over the strongest tuned baseline.
