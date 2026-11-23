# r_sp_alloc — Workload-Specific Physical Memory Efficiency for Edge CV

Research repository investigating physical RAM efficiency, user-kernel page reclamation (`4 KiB`), and bounded working-set retention for dedicated edge computer vision inference (fixed-shape `640x640` FP32 **YOLO26n** on **ONNX Runtime** CPU EP).

## Quickstart for Agents & Researchers

Read [AGENTS.md](AGENTS.md) and [context/STATE.md](context/STATE.md) first, then run:

```bash
# Check git state, immutable data integrity, evidence registry, code index, and links
python3 scripts/research/check_state.py

# Regenerate codebase symbol & file index -> context/CODE_INDEX.md
python3 scripts/research/reindex.py

# Validate any new experiment output directory before promoting results
python3 scripts/research/validate_run.py first_steps/bench/utilization/results/bounded --require-protocol --min-trials 5
```

## Validated Research Summary (Commits `20dc7b8`, `c9828c5`, `1fd7ede`)

Tested on bare-metal CloudLab `m510` (`Intel Xeon D-1548`, Ubuntu Linux `6.8.0-147-generic`, `4 KiB` pages, THP disabled) with bit-exact FP32 output verification (`max_abs_error == 0`):

1. **Live Utilization Baseline (`20dc7b8`, rechecked `c9828c5`):** Disabling ONNX Runtime's BFC CPU arena plus `glibc` trimming (`direct_trim`) cut single-stream active peak RSS by **40.27%** (`73.39` vs. `122.86 MiB`). Our out-of-band coalescing allocator (`PageReuseResource`, `pool_eager_trim`) matched steady active RSS (`73.74 MiB`), lowered startup peak (`73.50` vs. `78.99 MiB`), lifted allocator-local idle payload utilization from `20.80%` to `99.9903%`, and reduced 4-camera live idle private memory from `390.80 MiB` (`bfc_trim`) to `88.05 MiB`.
2. **Full Pause Purging (`c9828c5` — Failed Latency Gate):** Purging all free `body` pages (`0 MiB` budget) at 1 s scheduled pauses cut idle private RAM by **71.1%** (`34.34` vs. `118.70 MiB`) at `1.5%` FPS loss, but triggered ~27k minor resume faults and an **+11.1% p99 latency regression** (`298.87` vs. `268.92 ms`).
3. **Bounded 24 MiB Pause Retention (`1fd7ede` — Passed Prespecified Median Gates):** Keeping up to `24 MiB` of resident free pages warm via `mincore()` in `purge_to_budget()` cut idle private RAM by **50.8%–50.9%** (`0.4916` paired ratio; `58.37` vs. `118.79 MiB` in 1,000-frame endurance) and estimated private byte-seconds by **44.0%–44.3%**, while holding burst FPS (`0.9947`) and median burst p99 latency (`1.0108`, within the assumed 5% margin) on single-stream explicit pauses.

For exact per-claim evidence links and scope limits, see [context/EVIDENCE.md](context/EVIDENCE.md).

## Repository Layout

- **Active Research Frontier:** [first_steps/bench/utilization/](first_steps/bench/utilization/) (`page_reuse_resource.hpp`, `utilization_bench.cpp`, `activity_bench.cpp`, `run_bounded.py`)
- **Research Intelligence & Context:** [context/](context/) (`STATE.md`, `EVIDENCE.md`, `EVIDENCE.json`, `CODE_INDEX.md`, `PAPERS.md`, `HISTORY_AND_DECISIONS.md`)
- **Verification & Experiment Scripts:** [scripts/research/](scripts/research/)
- **Agent Workflows & Guard Hook:** [.agents/](.agents/)
- **Synced Assignment Report (Nov 19):** [assignment/latex/main.tex](assignment/latex/main.tex) and [assignment/GE6001-03300-M03_Erdeni_Khatunov.pdf](assignment/GE6001-03300-M03_Erdeni_Khatunov.pdf)
- **Archived / Stale:** [first_steps/paper/MANUSCRIPT.md](first_steps/paper/MANUSCRIPT.md) (stale Oct 15/19 draft), [results/cloudlab_20261021/](results/cloudlab_20261021/) (Oct 21 CloudLab baseline), `first_steps/bench/approach_01..06/`

## Building Active Benchmarks (Linux CloudLab Only)

Active C++ harnesses require Linux headers (`<malloc.h>`, `<sched.h>`, `mincore`) and x86_64 ONNX Runtime:

```bash
# Run sanitized allocator unit tests (ASan + UBSan)
g++ -O1 -g -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
  first_steps/bench/utilization/resource_test.cpp -o /tmp/resource_test && /tmp/resource_test

# Build and reproduce bounded retention benchmark (from first_steps/bench/utilization/)
cat first_steps/bench/utilization/results/bounded/REPRODUCE.md
```

---

## Historical Phase-1 Docker Lab (June 2026)

Original synthetic churn container scaffolding preserved under `bench/`, `scripts/run_*.sh`, `Dockerfile`, and `docker-compose.yml`:

```bash
docker compose build
docker compose run --rm allocator-lab
./scripts/run_all.sh
```
