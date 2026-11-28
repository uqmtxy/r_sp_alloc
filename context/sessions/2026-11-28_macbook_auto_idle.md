# Session Handoff: Autonomous Idle Detector & Apple M2 Pro (`16 KiB`) Validation

- **Date:** 2026-11-28
- **Base Commit:** `91300b3` (`feat r_sp_alloc: init context system`)
- **Platform:** MacBook Pro (`Mac14,10`, Apple M2 Pro, 32 GiB RAM, Darwin `25.6.0`, `16 KiB` pages, Homebrew `onnxruntime 1.32.0`, `clang++ 23.1.2`)

## Key Findings & Changes
- **XNU VM Empirical Probe:** On macOS `16 KiB` pages, `madvise(..., MADV_DONTNEED)` and `MADV_FREE` produced `0 MiB` drop in both `mincore()` and `phys_footprint`; `MADV_FREE_REUSABLE` dropped `phys_footprint` (`32->16 MiB`) but left `mincore()` bits set (`32->32 MiB`). `mmap(..., MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0)` dropped **both** `mincore()` and `phys_footprint` (`32->16 MiB`) immediately while preserving live neighbor bytes (`neighbors_ok=1`).
- **Allocator Generalization:** Updated [../../first_steps/bench/utilization/page_reuse_resource.hpp](../../first_steps/bench/utilization/page_reuse_resource.hpp) (`4 KiB` & `16 KiB` pages, `#ifdef __APPLE__` `MAP_FIXED` discard) and [../../first_steps/bench/utilization/resource_test.cpp](../../first_steps/bench/utilization/resource_test.cpp); passed ASan + UBSan.
- **Autonomous Idle Detector (`T1a`):** Built [../../first_steps/bench/utilization/platform_memory.hpp](../../first_steps/bench/utilization/platform_memory.hpp), [../../first_steps/bench/utilization/idle_reclaimer.hpp](../../first_steps/bench/utilization/idle_reclaimer.hpp), [../../first_steps/bench/utilization/auto_idle_test.cpp](../../first_steps/bench/utilization/auto_idle_test.cpp) (ASan/UBSan PASS), [../../first_steps/bench/utilization/auto_idle_bench.cpp](../../first_steps/bench/utilization/auto_idle_bench.cpp), [../../first_steps/bench/utilization/run_auto_idle_mac.py](../../first_steps/bench/utilization/run_auto_idle_mac.py), and [../../first_steps/bench/utilization/analyze_auto_idle.py](../../first_steps/bench/utilization/analyze_auto_idle.py).
- **46-Run Pre-Registered Campaign:** Saved in `first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/` ([REPORT.md](../../first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/REPORT.md)). `pool_auto` (`idle_ms=250`, screened `0 MiB` budget) achieved **0 false reclaims** across all continuous & `80 ms` jitter gaps, cut idle `phys_footprint` by `60.1%` (`42.25` vs `105.80 MiB` burst; `42.31` vs `105.73 MiB` mixed), halved `mixed` minor faults vs `pool_explicit` (`12,114` vs `24,229`), and passed all `FPS` / `p99` median gates with `max_abs_error == 0`.

## Next Recommended Action
- **T1b:** Replicate `auto_idle_bench.cpp` on CloudLab `m510` Linux (`4 KiB` pages, `sched_setaffinity`, `MADV_DONTNEED`, ORT `1.20.1`), then proceed to **T2** (staggered 4-camera bounded retention).
