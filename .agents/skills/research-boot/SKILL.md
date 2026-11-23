---
name: research-boot
description: >-
  Initialize a research or coding session on r_sp_alloc. Use at the start of any
  conversation to reconstruct project goals, verified metrics, active TODOs, and
  codebase boundaries without relying on stale drafts.
---

# Research Boot Workflow

1. Read [AGENTS.md](../../../AGENTS.md) at the repository root.
2. Read the three canonical live files in order:
   - [context/STATE.md](../../../context/STATE.md)
   - [context/EVIDENCE.md](../../../context/EVIDENCE.md)
   - [context/CODE_INDEX.md](../../../context/CODE_INDEX.md)
3. Run the deterministic verification script from repository root:
   ```bash
   python3 scripts/research/check_state.py
   ```
4. If working on literature or methodology, read [context/PAPERS.md](../../../context/PAPERS.md) and [context/HISTORY_AND_DECISIONS.md](../../../context/HISTORY_AND_DECISIONS.md).
5. **Do not cite** `first_steps/paper/MANUSCRIPT.md`, `first_steps/paper/latex/main.tex`, or `first_steps/walkthrough.md`—they are archived pre-CloudLab drafts.
6. Report git status, active frontier, and next TODO before editing any code.
