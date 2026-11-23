#!/usr/bin/env python3
"""Scaffold a pre-registered experiment directory with PROTOCOL.md and manifest.json."""

import argparse
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASE = ROOT / "first_steps" / "bench" / "utilization" / "results"


def git_sha() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Pre-register a new CloudLab benchmark directory")
    parser.add_argument("slug", help="Short experiment slug (e.g., idle_detector_v1)")
    parser.add_argument("--question", required=True, help="Research question being tested")
    parser.add_argument("--hypothesis", required=True, help="Falsifiable hypothesis")
    parser.add_argument(
        "--gates",
        default="max_abs_error == 0; idle_private_bytes <= 0.50 * BFC; continuous_fps >= 0.97 * BFC; burst_p99 <= 1.05 * BFC",
        help="Prespecified acceptance gates",
    )
    parser.add_argument("--trials", type=int, default=5, help="Planned independent fresh-process trials per config")
    parser.add_argument("--out-dir", type=Path, default=None, help="Optional explicit target directory")
    parser.add_argument("--dry-run", action="store_true", help="Print manifest & protocol without writing files")
    args = parser.parse_args()

    today = dt.date.today().isoformat()
    exp_id = f"{today}_{args.slug}"
    target = (args.out_dir if args.out_dir else DEFAULT_BASE / exp_id).resolve()

    if target.exists() and any(target.iterdir()):
        print(f"FAIL: target directory already exists and is non-empty: {target}", file=sys.stderr)
        return 1

    manifest = {
        "experiment_id": exp_id,
        "status": "PREREGISTERED",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "git_commit_sha": git_sha(),
        "git_dirty_worktree": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        "research_question": args.question,
        "hypothesis": args.hypothesis,
        "acceptance_gates": args.gates,
        "planned_trials": args.trials,
        "hardware_host": "TBD (record on CloudLab before promoting)",
        "model_sha256": "TBD",
        "binary_sha256": "TBD",
    }

    protocol = "\n".join([
        f"# Pre-Registered Experiment Protocol: {exp_id}",
        "",
        f"- **Git SHA:** `{manifest['git_commit_sha']}`",
        f"- **Research Question:** {args.question}",
        f"- **Hypothesis:** {args.hypothesis}",
        f"- **Planned Trials:** {args.trials} fresh processes per configuration in paired/shuffled order",
        f"- **Acceptance Gates:** {args.gates}",
        "",
        "## Validation Before Promotion",
        f"1. Run `python3 scripts/research/validate_run.py {target.relative_to(ROOT) if target.is_relative_to(ROOT) else target} --require-protocol --min-trials {args.trials}`",
        "2. Preserve negative/failed trials in this folder without rewriting `PROTOCOL.md`.",
        "3. Request an independent audit (`.agents/skills/independent-audit/SKILL.md`) before adding to `context/EVIDENCE.json`.",
        "",
    ])

    if args.dry_run:
        print(f"[DRY RUN] Would create {target}")
        print(json.dumps(manifest, indent=2))
        print(protocol)
        return 0

    target.mkdir(parents=True, exist_ok=True)
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (target / "PROTOCOL.md").write_text(protocol, encoding="utf-8")
    print(f"Created pre-registered experiment scaffold: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
