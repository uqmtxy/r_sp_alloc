#!/usr/bin/env python3
"""Unified state, immutable-data, evidence, code-index, and markdown-link verifier."""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

IMMUTABLE_PREFIXES = (
    "results/cloudlab_20261021/",
    "results/20260611",
    "results/pre_cloudlab_20261021/",
    "first_steps/bench/utilization/results/validated/",
    "first_steps/bench/utilization/results/recheck/",
    "first_steps/bench/utilization/results/activity/",
    "first_steps/bench/utilization/results/bounded/",
    "first_steps/bench/utilization/results/screen/",
)

IMMUTABLE_MUST_EXIST = (
    "results/cloudlab_20261021/SUMMARY.md",
    "results/20260611T120659Z-glibc/stdout.txt",
    "first_steps/bench/utilization/results/UTILIZATION_REPORT.md",
    "first_steps/bench/utilization/results/validated/single_pool_eager_trim_r0.json",
    "first_steps/bench/utilization/results/recheck/RECHECK_REPORT.md",
    "first_steps/bench/utilization/results/activity/ACTIVITY_REPORT.md",
    "first_steps/bench/utilization/results/bounded/REPORT.md",
)

LINK_RE = re.compile(r"\[[^\]]+\]\(([^)#]+)(?:#[^)]*)?\)")


def run_git(args: list[str]) -> str:
    # Preserve leading spaces on porcelain lines (e.g., " D path" or " M path").
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).rstrip("\n")


def check_immutable_paths() -> list[str]:
    status_lines = run_git(["status", "--porcelain"]).splitlines()
    violations = []
    for req in IMMUTABLE_MUST_EXIST:
        if not (ROOT / req).exists():
            violations.append(f"Immutable artifact missing: {req}")
    for line in status_lines:
        if len(line) < 4:
            continue
        code = line[:2]
        path = line[3:].strip().strip('"')
        if " -> " in path:
            path = path.split(" -> ")[-1].strip('"')
        if any(path.startswith(pref) for pref in IMMUTABLE_PREFIXES):
            violations.append(f"Immutable raw result touched ({code}): {path}")
    return violations


def check_markdown_links() -> list[str]:
    targets = [
        ROOT / "AGENTS.md",
        ROOT / "README.md",
        *sorted((ROOT / "context").glob("*.md")),
        *sorted((ROOT / "context" / "sessions").glob("*.md")),
        *sorted((ROOT / ".agents" / "skills").glob("*/SKILL.md")),
    ]
    broken = []
    for md in targets:
        if not md.exists():
            continue
        text = md.read_text(encoding="utf-8", errors="ignore")
        # Strip fenced code blocks so sample snippets aren't treated as links
        text = re.sub(r"```.*?```", "", text, flags=re.S)
        for m in LINK_RE.finditer(text):
            raw_target = m.group(1).strip()
            if not raw_target or "://" in raw_target or raw_target.startswith("mailto:"):
                continue
            resolved = (md.parent / raw_target).resolve()
            if not resolved.exists():
                broken.append(f"{md.relative_to(ROOT)} -> broken link '{raw_target}'")
    return broken


def summarize_state() -> None:
    state_file = ROOT / "context" / "STATE.md"
    if not state_file.exists():
        return
    print("\n--- Active TODOs & Paper Status (from context/STATE.md) ---")
    for line in state_file.read_text(encoding="utf-8").splitlines():
        if line.startswith("- [ ]") or line.startswith("- [x]") or "`SYNCED_" in line or "`STALE_" in line:
            print(line)


def main() -> int:
    branch = run_git(["rev-parse", "--abbrev-ref", "HEAD"])
    head = run_git(["log", "-1", "--oneline"])
    dirty = len([l for l in run_git(["status", "--porcelain"]).splitlines() if l.strip()])
    print(f"Git: branch={branch} | HEAD={head} | dirty/untracked_entries={dirty}")

    failures: list[str] = []
    failures.extend(check_immutable_paths())
    failures.extend(check_markdown_links())

    for cmd, label in [
        ([sys.executable, str(ROOT / "scripts/research/check_evidence.py"), "--check-render"], "evidence"),
        ([sys.executable, str(ROOT / "scripts/research/reindex.py"), "--check"], "code_index"),
    ]:
        res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if res.returncode != 0:
            failures.append(f"{label} failed: {(res.stderr or res.stdout).strip()}")
        else:
            print(res.stdout.strip())

    summarize_state()

    if failures:
        print("\nSTATE CHECK FAILED:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print("\nSTATE CHECK PASSED: immutable data intact, evidence verified, code index synced, links valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
