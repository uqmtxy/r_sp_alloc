# CloudLab run notes — 2026-10-21

Host: Utah CloudLab m510, Intel Xeon D-1548, 8 cores / 16 logical CPUs, 64 GiB installed RAM. See environment.json for observed topology, software versions, model checksum, and kernel settings. Prior result files, including interrupted runs, are preserved under ../pre_cloudlab_20261021; they are not used in the new report.

## Setup repairs

The checked-in ONNX Runtime library was AArch64 and could not link on this host. Replaced it with the official x86_64 ONNX Runtime 1.20.1 archive. Rebuilt the inference harness and DynAlloc. The initial package install failed because apt already had broken dependencies; repaired dependencies and installed jemalloc 5.3.0, gperftools TCMalloc 2.15, mimalloc 2.1.2, and matching kernel perf tools. THP is disabled, CPU governors are set to performance, and perf_event_paranoid is -1. A performance governor does not fix CPU frequency or disable turbo.

## Interpretation limits found in source and raw measurements

- CPU 2 is an affinity target, not a kernel-isolated CPU. Its SMT sibling CPU 10 remains online. No claim of complete elimination of scheduling or OS jitter is supported.
- The primary runner uses 14 perf events at once. Hardware events are multiplexed. Raw event coverage and scaling must accompany comparisons; different event scheduling windows can distort ratios.
- Initial glibc telemetry reported zero LLC-load-misses despite substantial cache activity, and generic cache misses exceeded generic cache references. Preserve these raw outputs, but independently validate the event mappings before using them as locality evidence.
- Primary perf counts cover process startup, warmup, and measurement. The primary JSON minor_faults field covers the post-warmup interval; these are different scopes.
- Primary inference runs use the default ONNX Runtime arena. Preloading another malloc does not replace every internal tensor allocation. Inputs are synthetic rather than captured camera frames.
- PARE-TA is an allocation microbenchmark, not ONNX inference. It touches the first and last element of simulated tensors. Its minor-fault interval includes warmup.
- PARE-TA trapped_dram_kb is hard-coded to 0, while other modes are hard-coded to 18400. Neither is a physical DRAM measurement. Reported 0.00 KB therefore cannot validate the hypothesis.
- DynAlloc large_alloc returns mmap base + sizeof(AllocHeader), i.e. +16 bytes. Its posix_memalign ignores alignment. Thus the current implementation does not provide the claimed page alignment and has an allocator API conformance defect. It was not redesigned during this experiment.
- Multi-camera settled RSS is sampled 200 ms after workers exit. Live adversarial anchors remain allocated at sampling time. This cannot prove permanent retention or attribute all retained memory to thread caches.
- The multi-camera launcher now passes an absolute model path so it works when launched from its own directory.
- Single runs are descriptive measurements, not evidence of statistical significance or causal proof that page straddling has been eliminated.

## LLC anomaly validation and telemetry correction

The 128 MiB dependent-load sanity test counted 17,635,923 generic LLC loads and zero generic LLC misses, versus 16,787,645 model-specific retired L3 load misses in a separate run. Raw output is saved alongside cache_sanity.cpp. A supplementary full-size pass uses grouped mem_load_uops_retired.l3_hit and mem_load_uops_retired.l3_miss events per allocator. These semantics differ from generic LLC events, so the report presents a separate table rather than silently replacing the original counters.

PARE-TA telemetry was corrected to emit null for unmeasured trapped DRAM instead of the original hard-coded values, and to capture a separate post-warmup minor-fault interval. Allocation behavior is unchanged.
