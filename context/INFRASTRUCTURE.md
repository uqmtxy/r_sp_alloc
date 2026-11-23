# Research OS Infrastructure (`r_sp_alloc`)

Designed as a **local-first, anti-stupidity** research workspace compatible with both **Antigravity (Neon)** and **Codex**.

## Design Rationale (Why This Shape vs. Heavyweight Alternatives)

- **Lean Context (`context/`):** The indexed source tree spans **63 files (10,329 LOC)** across **42 C/C++ files (8,002 LOC)** and **21 Python files (2,327 LOC)**, with the active frontier concentrated in **4 C++ files and 8 Python scripts** inside [../first_steps/bench/utilization/](../first_steps/bench/utilization/). Six Markdown specs plus one JSON registry (`STATE.md`, `EVIDENCE.json` + auto-rendered `EVIDENCE.md`, `CODE_INDEX.md`, `PAPERS.md`, `HISTORY_AND_DECISIONS.md`, `INFRASTRUCTURE.md`) replace 18+ overlapping prompt files to avoid context drift.
- **Zero-Daemon Code Indexing (`reindex.py`) as Primary Navigator:** Active benchmarks include Linux-only headers (`<malloc.h>`, `<sched.h>`, `mincore`) and link x86_64 ORT on CloudLab while editing happens on macOS arm64; running local `clangd`/Serena on macOS produces broken diagnostics without a Linux cross-sysroot. `scripts/research/reindex.py` deterministically indexes active vs. archived C++/Python symbols with Python stdlib in <1 s. (Pre-existing entries in `.agents/mcp_config.json` for `serena`, `gitnexus`, and `context7` remain untouched for optional use, but are not required by the fast-boot workflow.)
- **Programmatic Truth Verification (`check_evidence.py`, `validate_run.py`, `check_state.py`):** Numbers in `context/EVIDENCE.md` are generated from `context/EVIDENCE.json` and continuously asserted directly against immutable JSON files on disk.
- **Native Antigravity Hooks & Skills (`.agents/`):**
  - [../.agents/hooks.json](../.agents/hooks.json) runs [../.agents/hooks/guard_immutable.py](../.agents/hooks/guard_immutable.py) on `PreToolUse` (`write_file`, `edit_file`, `write_to_file`, `replace_file_content`, `multi_replace_file_content`, `run_command`) to block accidental writes or deletions inside immutable historical result directories.
  - [../.agents/skills/](../.agents/skills/) provides progressive workflows (`research-boot`, `benchmark-protocol`, `independent-audit`, `session-handoff`) loaded automatically by Antigravity and referenced in [../AGENTS.md](../AGENTS.md) for Codex.

## Parallel Work & Git Worktree Discipline

- Run parallel Builder tasks in isolated git branches/worktrees (`git worktree add ../alloc-<task> -b feat/<task>`), with non-overlapping file ownership.
- **Never** write shared mutable raw result files from parallel workers; write new runs under a fresh `first_steps/bench/utilization/results/<date>_<slug>/` folder created via `scripts/research/new_experiment.py`.
- Merge only after an **Independent Auditor** (separate subagent or fresh reviewer) runs `scripts/research/check_state.py`, `scripts/research/validate_run.py`, and the [independent-audit](../.agents/skills/independent-audit/SKILL.md) checklist.
