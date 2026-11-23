---
name: benchmark-protocol
description: >-
  Design, pre-register, run (on CloudLab Linux), and validate memory-efficiency
  or allocator experiments in r_sp_alloc. Use before modifying benchmark harnesses
  or collecting new performance/utilization measurements.
---

# Benchmark Protocol Workflow

1. **Never overwrite existing raw data.** All existing directories under `results/` and `first_steps/bench/utilization/results/{validated,recheck,activity,bounded,screen}/` are immutable.
2. **Pre-register before running:**
   ```bash
   python3 scripts/research/new_experiment.py <slug> \
     --question "<specific research question>" \
     --hypothesis "<falsifiable claim>" \
     --gates "max_abs_error == 0; <explicit RAM/FPS/p99 cutoffs>" \
     --trials 5
   ```
3. **Run Linux C++ builds & tests ONLY on CloudLab Linux x86_64** (never rely on local macOS builds for `<malloc.h>`, `<sched.h>`, `mincore`, or ORT 1.20.1). Before benchmarking, run sanitized unit tests on CloudLab:
   ```bash
   g++ -O1 -g -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
     first_steps/bench/utilization/resource_test.cpp -o /tmp/resource_test && /tmp/resource_test
   ```
4. **Verify every output tensor:** Every run JSON must emit `max_abs_error == 0`, `command`/`_command`, and `binary_sha256`/`_binary_sha256`, and separate active inference timing from verification and `/proc` telemetry.
5. **Gate before promotion:**
   ```bash
   python3 scripts/research/validate_run.py <new_result_dir> --require-protocol --min-trials 5
   ```
6. Preserve failed or negative trials in place and record them accurately in `context/HISTORY_AND_DECISIONS.md` and `context/EVIDENCE.json`.
