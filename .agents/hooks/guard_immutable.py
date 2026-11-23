#!/usr/bin/env python3
"""Antigravity PreToolUse hook protecting immutable experimental data from accidental edits or deletion."""

import json
import re
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

PARENT_IMMUTABLE_DIRS = (
    "results",
    "first_steps/bench/utilization/results",
)

MUTATION_RE = re.compile(
    r"(?:^|[;&|]\s*|\s)(?:rm|mv|cp|truncate|chmod|unlink|touch|ln|dd|tee)\b"
    r"|\bsed\s+-i\b"
    r"|\bgit\s+(?:checkout|restore|clean|rm)\b"
    r"|\.(?:unlink|write_text|write_bytes|rmtree|rename|replace)\("
    r"|(?:^|[^-])>>?"
)


def rel_repo_path(raw_path: str, cwd: str) -> str:
    p = Path(raw_path)
    if not p.is_absolute():
        p = (Path(cwd) if cwd else ROOT) / p
    try:
        return p.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return raw_path.replace("\\", "/")


def is_protected_path(rel_path: str) -> bool:
    norm = rel_path.strip().lstrip("./")
    return any(norm.startswith(pref) or norm == pref.rstrip("/") for pref in IMMUTABLE_PREFIXES)


def deny(reason: str) -> int:
    print(json.dumps({"decision": "deny", "reason": reason}))
    return 0


def allow() -> int:
    print(json.dumps({"decision": "allow"}))
    return 0


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return allow()

    tool = payload.get("toolCall", {})
    name = tool.get("name", "")
    args = tool.get("args", {}) or {}
    cwd = str(payload.get("cwd") or ROOT)

    if name in {"write_file", "edit_file", "write_to_file", "replace_file_content", "multi_replace_file_content"}:
        raw_target = (
            args.get("Path")
            or args.get("TargetFile")
            or args.get("path")
            or args.get("target_file")
            or args.get("file_path")
            or ""
        )
        rel = rel_repo_path(str(raw_target), cwd)
        if is_protected_path(rel):
            return deny(
                f"Blocked by guard_immutable: '{rel}' is inside an immutable experimental result directory. "
                "Create a new directory via scripts/research/new_experiment.py instead."
            )
        return allow()

    if name == "run_command":
        cmd = str(args.get("CommandLine") or args.get("command") or "")
        # Ignore harmless fd/null redirects before scanning for file-mutating redirects
        cleaned_cmd = re.sub(r"(?:[012]?>>?\s*/dev/null|[012]?>&\s*[012])", "", cmd)
        cwd_rel = rel_repo_path(".", cwd)
        in_protected_cwd = is_protected_path(cwd_rel)
        mentions_protected = any(pref.rstrip("/") in cmd for pref in IMMUTABLE_PREFIXES)
        mentions_parent_rm = any(
            re.search(rf"\b(?:rm|mv|chmod|rmtree)\b[^\n;]*\b{re.escape(p)}\b", cmd)
            for p in PARENT_IMMUTABLE_DIRS
        )
        if MUTATION_RE.search(cleaned_cmd) and (in_protected_cwd or mentions_protected or mentions_parent_rm):
            return deny(
                "Blocked by guard_immutable: shell command appears to mutate or delete an immutable raw result directory."
            )
        return allow()

    return allow()


if __name__ == "__main__":
    raise SystemExit(main())
