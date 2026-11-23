---
name: independent-audit
description: >-
  Perform an adversarial, zero-trust audit of code changes, benchmark results,
  context updates, or manuscript claims. Use via a fresh subagent or independent
  reviewer that did NOT author the implementation.
---

# Independent Audit Checklist (Red-Team Verification)

You are an **Independent Auditor**. Do not accept summary claims on faith—verify files and run scripts directly.

1. **Repository & Evidence Integrity:**
   ```bash
   python3 scripts/research/check_state.py
   ```
   Confirm zero immutable files were modified (`git status --porcelain`).
2. **Benchmark Provenance & Raw Data Check (if new results exist):**
   ```bash
   python3 scripts/research/validate_run.py <result_dir> --require-protocol --min-trials 5
   ```
   Spot-check at least two raw JSON files directly and confirm `max_abs_error == 0`, valid `binary_sha256`, and non-zero/non-hardcoded telemetry.
3. **Code & Accounting Audit (if C++/Python changed):**
   - Inspect `git diff` for silent shortcuts: hardcoded zeros, suppressed errors, loosened tolerances, or metadata placed inside `MADV_DONTNEED`-purgeable mappings.
   - Verify `mincore()` byte conservation (`resident == payload + body + head + tail + sub`) is preserved.
   - Check whether CloudLab ASan/UBSan (`resource_test.cpp`) output was captured if allocator code changed.
4. **Scientific Scope Audit (if docs/manuscripts changed):**
   - Ensure no stale Oct 15/19 claims (`18,842 FPS`, `0.0 KB trapped DRAM`, `2023/2024` dates, `Zheng et al.`).
   - Verify single-stream `24 MiB` bounded retention is NOT extrapolated to multi-camera staggered bursts or automatic idle detection without explicit runs.
   - Verify `99.9903%` is labeled **allocator-local** payload utilization (never process-wide RAM).
5. **Output Format:** Emit a structured verdict:
   - `VERDICT: APPROVE` or `VERDICT: BLOCK`
   - Exact checks executed + exit codes
   - Any defects, broken links, or unsupported claims with `file:line` citations.
