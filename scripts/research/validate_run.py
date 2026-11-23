#!/usr/bin/env python3
"""Gatekeeper validating benchmark result directories before acceptance."""

import argparse
import json
import math
import sys
from pathlib import Path

IGNORED_JSONS = {"ENVIRONMENT.json", "SHA256SUMS.json", "REFERENCE_VARIANTS.json", "manifest.json"}
NUMERIC_METRIC_KEYS = (
    "fps",
    "fps_diagnostic",
    "p99_ms",
    "idle_private_bytes",
    "active_peak_rss_bytes",
    "estimated_private_byte_seconds",
    "minor_faults",
)


def is_valid_hex(val: object) -> bool:
    return isinstance(val, str) and len(val) >= 32 and all(c in "0123456789abcdefABCDEF" for c in val)


def validate_directory(result_dir: Path, min_trials: int, require_protocol: bool) -> list[str]:
    errors: list[str] = []
    if not result_dir.is_dir():
        return [f"Not a directory: {result_dir}"]

    if require_protocol:
        if not ((result_dir / "PROTOCOL.md").exists() or (result_dir / "manifest.json").exists()):
            errors.append(f"Missing PROTOCOL.md or manifest.json in {result_dir}")

    run_files = sorted(p for p in result_dir.glob("*.json") if p.name not in IGNORED_JSONS)
    if not run_files:
        errors.append(f"No benchmark run JSON files found in {result_dir}")
        return errors

    if min_trials > 0 and len(run_files) < min_trials:
        errors.append(f"Found only {len(run_files)} run JSONs (< --min-trials {min_trials})")

    for fp in run_files:
        try:
            data = json.loads(fp.read_text(encoding="utf-8"))
        except Exception as exc:
            errors.append(f"{fp.name}: invalid JSON ({exc})")
            continue

        if not isinstance(data, dict) or not data:
            errors.append(f"{fp.name}: empty or non-object JSON")
            continue

        if "max_abs_error" not in data:
            errors.append(f"{fp.name}: missing max_abs_error")
        elif isinstance(data["max_abs_error"], bool) or data["max_abs_error"] != 0:
            errors.append(f"{fp.name}: max_abs_error={data['max_abs_error']!r} (expected numeric 0)")

        for flag in ("outputs_verified", "retained_outputs_verified"):
            if flag in data and data[flag] is not True:
                errors.append(f"{fp.name}: {flag}={data[flag]!r} (expected true)")

        cmd = data.get("command") or data.get("_command")
        if not isinstance(cmd, list) or not cmd:
            errors.append(f"{fp.name}: missing provenance command/_command")

        bsha = data.get("binary_sha256") or data.get("_binary_sha256")
        if not is_valid_hex(bsha):
            errors.append(f"{fp.name}: missing or invalid binary_sha256/_binary_sha256")

        for k in NUMERIC_METRIC_KEYS:
            if k in data:
                v = data[k]
                if v is None or isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(float(v)) or float(v) < 0:
                    errors.append(f"{fp.name}: non-finite or negative metric {k}={v!r}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate utilization/activity benchmark JSON outputs")
    parser.add_argument("result_dir", type=Path, help="Directory containing benchmark JSON files")
    parser.add_argument("--min-trials", type=int, default=1, help="Minimum number of valid run JSON files")
    parser.add_argument("--require-protocol", action="store_true", help="Require PROTOCOL.md or manifest.json")
    args = parser.parse_args()

    errors = validate_directory(args.result_dir.resolve(), args.min_trials, args.require_protocol)
    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        return 1

    count = len([p for p in args.result_dir.glob("*.json") if p.name not in IGNORED_JSONS])
    print(f"PASS: {count} benchmark JSON runs validated cleanly in {args.result_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
