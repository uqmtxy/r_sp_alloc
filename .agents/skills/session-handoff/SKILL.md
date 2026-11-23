---
name: session-handoff
description: >-
  Close out a research or development session cleanly so the next Antigravity or
  Codex session can resume immediately. Use before wrapping up any substantial task.
---

# Session Handoff Workflow

1. **Refresh derived artifacts (if code or evidence changed):**
   ```bash
   python3 scripts/research/reindex.py
   python3 scripts/research/check_evidence.py --render
   ```
2. **Update [context/STATE.md](../../../context/STATE.md):** check off completed tasks, add new TODOs/blockers, update Paper/Deliverable status, and link the new session note.
3. **Update [context/HISTORY_AND_DECISIONS.md](../../../context/HISTORY_AND_DECISIONS.md)** if an architectural decision, rejected mode, or failed experiment occurred.
4. **Write a concise session note** under `context/sessions/YYYY-MM-DD_<topic>.md` covering:
   - Goal & git branch/commit
   - Files changed & why
   - Exact verification commands run & observed pass/fail output
   - Open blockers & single best next action
5. **Final gate (must exit 0):**
   ```bash
   python3 scripts/research/check_state.py
   ```
