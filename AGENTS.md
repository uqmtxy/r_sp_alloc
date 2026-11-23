# AGENTS.md — `r_sp_alloc` Shared Research Instructions (Antigravity / Neon & Codex)

> **Research North Star:** Reduce live physical RAM consumption (`mincore()` residency, private dirty/clean bytes, RSS) in dedicated edge computer vision inference (fixed-shape FP32 `640x640` YOLO26n on ONNX Runtime CPU EP) while bounding tail latency (`p99`) and throughput (`FPS`) with **zero numerical error** (`max_abs_error == 0`).

---

## 1. Mandatory Fast-Boot Sequence (Run First in Every Session)

Before planning, coding, or reporting results, read these three files in order:
1. [context/STATE.md](context/STATE.md) — current goal, active frontier, TODOs, blockers, paper status.
2. [context/EVIDENCE.md](context/EVIDENCE.md) — verified claims, exact metrics, status tags (`VALIDATED`, `FAILED_GATE`, `STALE_DRAFT`).
3. [context/CODE_INDEX.md](context/CODE_INDEX.md) — auto-generated map of active vs. archived C++/Python files.

Then run the repository state and integrity check:
```bash
python3 scripts/research/check_state.py
```
For literature details or historical failures, read [context/PAPERS.md](context/PAPERS.md) and [context/HISTORY_AND_DECISIONS.md](context/HISTORY_AND_DECISIONS.md).

---

## 2. Directory Truth Map (Active vs. Archived vs. Stale)

| Directory / File | Status | Purpose |
|---|---|---|
| [first_steps/bench/utilization/](first_steps/bench/utilization/) | **ACTIVE FRONTIER** | `PageReuseResource`, `utilization_bench.cpp`, `activity_bench.cpp`, bounded retention (`1fd7ede`) |
| [context/](context/) | **ACTIVE MEMORY** | Canonical project state, verified evidence registry, papers, codebase index, session logs |
| [scripts/research/](scripts/research/) | **ACTIVE TOOLING** | `check_state.py`, `check_evidence.py`, `validate_run.py`, `reindex.py`, `new_experiment.py` |
| [.agents/](.agents/) | **ACTIVE AGENT OS** | Progressive workflow skills (`skills/*/SKILL.md`) & immutable-data guard hook (`hooks.json`) |
| [assignment/latex/main.tex](assignment/latex/main.tex) | **SYNCED REPORT (NOV 19)** | Graduate English ACM `acmart` report reflecting validated Nov 5–19 results |
| [first_steps/paper/MANUSCRIPT.md](first_steps/paper/MANUSCRIPT.md) & [first_steps/paper/latex/main.tex](first_steps/paper/latex/main.tex) | **STALE DRAFT (DO NOT CITE)** | Oct 15/19 pre-CloudLab draft containing superseded claims (`18,842 FPS`, `0.0 KB trapped DRAM`, wrong 2023/2024 citations) |
| [first_steps/bench/approach_01_exact_fit_slab/](first_steps/bench/approach_01_exact_fit_slab/) .. `06`, `pare_ta_arena/`, [first_steps/src/dynalloc.cpp](first_steps/src/dynalloc.cpp), [results/cloudlab_20261021/](results/cloudlab_20261021/), [talking_steps/](talking_steps/) | **ARCHIVED / HISTORICAL** | Early prototypes & Oct 21 CloudLab baseline; preserve for provenance, do not confuse with active `utilization/` frontier |

---

## 3. Platform Split & Build Reality

- **Local host (macOS arm64):** Used for code edits, static inspection, Python analysis/plotting, and `scripts/research/*.py` checks.
- **Experiment host (CloudLab `m510` Linux `x86_64`):** Intel Xeon D-1548 (8c/16t, 64 GiB DRAM), Ubuntu Linux `6.8.0-147-generic`, 4 KiB pages, THP `[never]`, ONNX Runtime `1.20.1` x86_64.
- **Do NOT waste time running local `clangd` / Linux C++ builds on macOS:** Active benchmarks use Linux-only headers (`<malloc.h>`, `<sched.h>`, `mincore`, `cpu_set_t`). Run C++ builds, ASan/UBSan (`resource_test.cpp`), and benchmarks only on CloudLab Linux.

---

## 4. Non-Negotiable Scientific Guardrails

1. **No eBPF on the `malloc`/`free` hot path:** Analyzed and rejected in [first_steps/02_ebpf_feasibility_analysis.md](first_steps/02_ebpf_feasibility_analysis.md); only out-of-band telemetry/supervision is suitable.
2. **Oct 21 CloudLab `DynAlloc` did not beat `glibc`:** In [results/cloudlab_20261021/SUMMARY.md](results/cloudlab_20261021/SUMMARY.md), `DynAlloc` had lower FPS (`5.62` vs `6.46` / `6.68`) and higher `p99` (`221.37` ms vs `159.32` ms `glibc_tuned`).
3. **Generic `LLC-load-misses` failed sanity checks:** Never cite raw generic LLC counters from Oct 21; only use the separate retired-load L3 validation (`13.254%` DynAlloc vs `12.646%` `glibc_tuned`) where appropriate, which showed no L3 locality gain.
4. **Preloaded `malloc` does not control all ORT allocations:** Primary ORT runs use ORT's internal arena unless `DisableCpuMemArena()` + `ObservedAllocator` are explicitly configured.
5. **PARE-TA was an allocation microbenchmark:** It did not run ONNX inference; its earlier `trapped_dram_kb` was hardcoded (`0` vs `18400`) before telemetry correction.
6. **Multi-camera 200 ms settled RSS with live anchors** cannot establish permanent allocator retention or isolate memory strictly to thread caches.
7. **Utilization criteria != historical allocator-comparison criteria:** Nov 5–19 tests optimize **physical residency (`mincore()`, idle private bytes, active peak RSS)** under `max_abs_error == 0`, not raw FPS/cache counters.
8. **Scope of Nov 19 (`1fd7ede`) bounded retention:** Validated ONLY for **single-stream, explicit scheduled pauses, fixed `640x640` FP32 YOLO26n** on one Broadwell-DE node. Do NOT extrapolate to staggered multi-camera bursts, automatic idle detection, or OS memory pressure.
9. **Be exact on metrics and baselines:** `99.9903%` is **allocator-local** idle payload utilization (`pool_eager_trim` mapping), NOT whole-process RAM. Never claim `PageReuseResource` dominates `direct_trim` in single-stream active peak RSS (`73.74` vs `73.39` MiB). Never cite `Fistanto et al.` (`PLOS '26`) as `2023` or `van Kempen & Berger` (`ISMM '26`) as `2024`/`Zheng et al.`.

---

## 5. Immutability & Benchmark Integrity Rules

- **IMMUTABLE DIRECTORIES (NEVER edit, overwrite, or delete):**
  - `results/cloudlab_20261021/`, `results/20260611*`, `results/pre_cloudlab_20261021/`
  - `first_steps/bench/utilization/results/validated/`, `recheck/`, `activity/`, `bounded/`, `screen/`
- **New experiments MUST pre-register protocol and provenance** via `python3 scripts/research/new_experiment.py` and pass `python3 scripts/research/validate_run.py <dir> --require-protocol --min-trials 5` before entering `context/EVIDENCE.json`.
- **Preserve negative results:** Failed runs and rejected modes (e.g., `c9828c5` `pool_idle` +11.1% p99 failure, `REJECTED_APPROACHES.md`) stay documented.

---

## 6. Multi-Agent Roles & Workflows (`.agents/skills/`)

| Role | Responsibility |
|---|---|
| **Lead / Orchestrator** (Neon / Codex) | Plans, sets pre-registered gates, owns `context/STATE.md` and session handoffs |
| **Builder** | Implements C++/Python changes in an isolated git branch or worktree |
| **Independent Auditor** (fresh subagent / separate reviewer) | **Never authors the change.** Runs `check_state.py`, `validate_run.py`, ASan/UBSan checks, and [independent-audit](.agents/skills/independent-audit/SKILL.md) before `APPROVE` |
| **Paper Steward** | Updates manuscripts/figures strictly from `VALIDATED` entries in [context/EVIDENCE.json](context/EVIDENCE.json) |

Available skills in `.agents/skills/`:
- [research-boot](.agents/skills/research-boot/SKILL.md) — fast context reconstruction & state check
- [benchmark-protocol](.agents/skills/benchmark-protocol/SKILL.md) — pre-registered experiment creation & CloudLab validation
- [independent-audit](.agents/skills/independent-audit/SKILL.md) — adversarial verification by an independent agent
- [session-handoff](.agents/skills/session-handoff/SKILL.md) — end-of-session state, index, and evidence synchronization
