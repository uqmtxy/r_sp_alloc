#!/usr/bin/env python3
"""Verify every claim in context/EVIDENCE.json against raw JSON files and optionally render context/EVIDENCE.md."""

import argparse
import glob
import json
import math
import re
import sys
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[2]
JSON_PATH = ROOT / "context" / "EVIDENCE.json"
MD_PATH = ROOT / "context" / "EVIDENCE.md"

BRACE_RE = re.compile(r"\{(\d+)\.\.(\d+)\}")


def expand_pattern(pattern: str) -> list[Path]:
    m = BRACE_RE.search(pattern)
    if m:
        start, end = int(m.group(1)), int(m.group(2))
        paths = []
        for idx in range(start, end + 1):
            expanded = pattern[: m.start()] + str(idx) + pattern[m.end() :]
            paths.extend(expand_pattern(expanded))
        return paths
    return [ROOT / pattern]


def expand_files(spec: dict, key_prefix: str = "") -> list[Path]:
    files_key = f"{key_prefix}files" if key_prefix else "files"
    glob_key = f"{key_prefix}files_glob" if key_prefix else "files_glob"
    if files_key in spec:
        out = []
        for pat in spec[files_key]:
            out.extend(expand_pattern(pat))
        return out
    if glob_key in spec:
        return [Path(p) for p in sorted(glob.glob(str(ROOT / spec[glob_key])))]
    raise ValueError(f"Missing {files_key}/{glob_key} in check {spec}")


def extract_key(data: dict, dotted_key: str):
    cur = data
    for part in dotted_key.split("."):
        if not isinstance(cur, dict) or part not in cur:
            raise KeyError(f"Missing key '{dotted_key}' (stopped at '{part}')")
        cur = cur[part]
    return cur


def eval_check(chk: dict) -> tuple[bool, str]:
    ctype = chk["type"]
    scale = float(chk.get("scale", 1.0))
    tol = float(chk.get("tol", 0.0))

    if ctype == "value":
        fp = ROOT / chk["file"]
        if not fp.exists():
            return False, f"missing file {chk['file']}"
        val = extract_key(json.loads(fp.read_text()), chk["key"])
        if not isinstance(val, bool) and isinstance(val, (int, float)):
            val = float(val) * scale
            ok = math.isclose(val, float(chk["expected"]), abs_tol=tol, rel_tol=0.0)
            return ok, f"{chk['file']}:{chk['key']}={val:.6g} (exp {chk['expected']}±{tol})"
        ok = type(val) is type(chk["expected"]) and val == chk["expected"]
        return ok, f"{chk['file']}:{chk['key']}={val!r} (exp {chk['expected']!r})"

    if ctype == "all_zero":
        files = expand_files(chk)
        if not files:
            return False, f"no files matched for {chk}"
        bad = []
        for fp in files:
            if not fp.exists():
                bad.append(f"missing:{fp.name}")
                continue
            val = extract_key(json.loads(fp.read_text()), chk["key"])
            if isinstance(val, bool) or val != 0:
                bad.append(f"{fp.name}:{val}")
        if bad:
            return False, f"non-zero {chk['key']}: {', '.join(bad[:5])}"
        return True, f"all_zero {chk['key']} across {len(files)} files"

    if ctype == "median":
        files = expand_files(chk)
        if not files:
            return False, f"no files matched for {chk}"
        vals = []
        for fp in files:
            if not fp.exists():
                return False, f"missing {fp.relative_to(ROOT)}"
            vals.append(float(extract_key(json.loads(fp.read_text()), chk["key"])) * scale)
        obs = median(vals)
        ok = math.isclose(obs, float(chk["expected"]), abs_tol=tol, rel_tol=0.0)
        return ok, f"median({len(vals)}):{chk['key']}={obs:.6g} (exp {chk['expected']}±{tol})"

    if ctype == "paired_ratio_median":
        nums = expand_files(chk, "num_")
        dens = expand_files(chk, "den_")
        if not nums or len(nums) != len(dens):
            return False, f"mismatched paired files ({len(nums)} vs {len(dens)})"
        ratios = []
        for nf, df in zip(nums, dens):
            if not nf.exists() or not df.exists():
                return False, f"missing pair {nf.name}/{df.name}"
            nv = float(extract_key(json.loads(nf.read_text()), chk["key"]))
            dv = float(extract_key(json.loads(df.read_text()), chk["key"]))
            ratios.append(nv / dv)
        obs = median(ratios)
        ok = math.isclose(obs, float(chk["expected"]), abs_tol=tol, rel_tol=0.0)
        return ok, f"paired_ratio_median({len(ratios)}):{chk['key']}={obs:.4f} (exp {chk['expected']}±{tol})"

    return False, f"unknown check type {ctype}"


def render_markdown(reg: dict) -> str:
    lines = [
        "# Verified Evidence Registry (`r_sp_alloc`)",
        "",
        "<!-- AUTO-GENERATED from context/EVIDENCE.json via `python3 scripts/research/check_evidence.py --render`. Do not hand-edit. -->",
        "",
        "Every quantitative claim below is programmatically checked against raw JSON artifacts on disk.",
        "",
        "## Summary Matrix",
        "",
        "| ID | Status | Commit | Report | Scope & Caveats |",
        "|---|---|---|---|---|",
    ]
    for e in reg["entries"]:
        rp = e["report_path"]
        lines.append(f"| `{e['id']}` | **{e['status']}** | `{e['commit']}` | [{rp}](../{rp}) | {e['scope_limits']} |")
    lines.append("")
    lines.append("## Machine-Verified Assertions")
    lines.append("")
    for e in reg["entries"]:
        lines.append(f"### `{e['id']}` — {e['title']} (`{e['status']}`)")
        lines.append(f"- **Commit:** `{e['commit']}` | **Report:** [{e['report_path']}](../{e['report_path']})")
        lines.append(f"- **Scope / Caveats:** {e['scope_limits']}")
        if e["checks"]:
            for chk in e["checks"]:
                ok, msg = eval_check(chk)
                badge = "PASS" if ok else "FAIL"
                lines.append(f"  - `{badge}`: `{msg}`")
        else:
            lines.append("  - *(No numeric assertions promoted — explicitly flagged non-citable)*")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate EVIDENCE.json against raw JSONs and optionally render EVIDENCE.md")
    parser.add_argument("--render", action="store_true", help="Write context/EVIDENCE.md after checking")
    parser.add_argument("--check-render", action="store_true", help="Fail if context/EVIDENCE.md is out of sync")
    args = parser.parse_args()

    reg = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    failures = []
    total_checks = 0

    for entry in reg["entries"]:
        eid = entry["id"]
        rp = ROOT / entry["report_path"]
        if not rp.exists():
            failures.append(f"{eid}: missing report_path {entry['report_path']}")
        for rf in entry.get("raw_files", []):
            if not (ROOT / rf).exists():
                failures.append(f"{eid}: missing raw_file {rf}")
        for chk in entry.get("checks", []):
            total_checks += 1
            ok, msg = eval_check(chk)
            if not ok:
                failures.append(f"{eid}: {msg}")

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1

    rendered = render_markdown(reg)
    if args.render:
        MD_PATH.write_text(rendered, encoding="utf-8")
        print(f"OK: verified {total_checks} assertions across {len(reg['entries'])} entries and rendered {MD_PATH.relative_to(ROOT)}")
        return 0

    if args.check_render:
        if not MD_PATH.exists():
            print(f"FAIL: missing {MD_PATH.relative_to(ROOT)}", file=sys.stderr)
            return 1
        if MD_PATH.read_text(encoding="utf-8") != rendered:
            print(f"FAIL: {MD_PATH.relative_to(ROOT)} differs from EVIDENCE.json; run `python3 scripts/research/check_evidence.py --render`", file=sys.stderr)
            return 1

    print(f"OK: {total_checks} evidence assertions verified across {len(reg['entries'])} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
