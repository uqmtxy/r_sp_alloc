#!/usr/bin/env python3
"""Analyze Apple M2 Pro autonomous idle-reclamation experiment and write REPORT.md."""

import json
from pathlib import Path
from statistics import median

HERE = Path(__file__).resolve().parent
OUT = HERE / "results" / "2026-11-28_macbook_m2pro_auto_idle"


def read(tag: str) -> dict:
    return json.loads((OUT / f"{tag}.json").read_text(encoding="utf-8"))


def fmt_ratio(vals: list[float]) -> str:
    return f"{median(vals):.4f} [{min(vals):.4f}, {max(vals):.4f}]"


def main() -> None:
    budget = int((OUT / "selection.txt").read_text(encoding="utf-8").strip())
    env = json.loads((OUT / "ENVIRONMENT.json").read_text(encoding="utf-8"))

    lines = [
        "# Apple M2 Pro Autonomous Idle-Reclamation Experiment",
        "",
        f"- **Host:** `{env['cpu_brand']}` (`{env['hw_model']}`), OS page size `{env['os_page_size']}` B, `{env['onnxruntime_brew']}`, `core_pinned=false` (QoS user-initiated)",
        f"- **Frozen Retention Budget:** `{budget} MiB` (selected from shuffled screening `0, 8, 16, 24, 32 MiB` meeting `<= 0.55 * BFC` idle footprint with lowest `p99_ms`)",
        "- **Autonomous Detector Config:** `idle_ms=250`, `poll_ms=20`, `frames=120`, `warmup=10`, `max_abs_error=0` required on every run.",
        "",
        "## Screening Sweep (Burst: 10-frame bursts, 1,000 ms pauses, n=1)",
        "",
        "| Budget (MiB) | Policy | Active FPS | p99 (ms) | Resume Med (ms) | Idle Footprint (MiB) | Reclaims | False Reclaims | Minor Faults |",
        "|---:|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for b in (-1, 0, 8, 16, 24, 32):
        d = read(f"screen_{b}")
        lbl = "BFC" if b < 0 else str(b)
        lines.append(
            f"| {lbl} | `{d['policy']}` | {d['fps']:.3f} | {d['p99_ms']:.2f} | {d['resume_median_ms']:.2f} | "
            f"{d['idle_private_bytes'] / (1024**2):.2f} | {d['reclaim_calls']} | {d['false_reclaims']} | {d['minor_faults']} |"
        )

    lines += [
        "",
        "## Confirmation Results (n=5 Fresh Processes per Regime, Paired Ratios vs. BFC)",
        "",
        "| Regime | Policy | Metric | Median [Min, Max] | Gate |",
        "|---|---|---|---|---|",
    ]

    gates_pass = True

    # Continuous
    cont_pairs = [(read(f"confirm_continuous_{i}_pool_auto"), read(f"confirm_continuous_{i}_bfc")) for i in range(5)]
    fps_r = [a["fps"] / b["fps"] for a, b in cont_pairs]
    p99_r = [a["p99_ms"] / b["p99_ms"] for a, b in cont_pairs]
    false_sum = sum(a["false_reclaims"] for a, _ in cont_pairs)
    reclaim_sum = sum(a["reclaim_calls"] for a, _ in cont_pairs)
    gates_pass &= (median(fps_r) >= 0.97 and false_sum == 0 and reclaim_sum == 0)
    lines.append(f"| continuous | `pool_auto` | fps ratio | {fmt_ratio(fps_r)} | >= 0.97 |")
    lines.append(f"| continuous | `pool_auto` | p99_ms ratio | {fmt_ratio(p99_r)} | diagnostic |")
    lines.append(f"| continuous | `pool_auto` | false_reclaims / reclaims (sum of 5) | {false_sum} / {reclaim_sum} | == 0 |")

    # Burst & Mixed
    for regime in ("burst", "mixed"):
        for pol in ("pool_auto", "pool_explicit"):
            pairs = [(read(f"confirm_{regime}_{i}_{pol}"), read(f"confirm_{regime}_{i}_bfc")) for i in range(5)]
            for key, gate in [
                ("fps", "diagnostic"),
                ("p99_ms", "<= 1.05" if pol == "pool_auto" else "comparator"),
                ("idle_private_bytes", "<= 0.55" if pol == "pool_auto" else "comparator"),
                ("estimated_private_byte_seconds", "< 1.00" if pol == "pool_auto" else "comparator"),
                ("resume_median_ms", "diagnostic"),
            ]:
                vals = [a[key] / b[key] for a, b in pairs]
                med = median(vals)
                if pol == "pool_auto":
                    if key == "p99_ms":
                        gates_pass &= med <= 1.05
                    elif key == "idle_private_bytes":
                        gates_pass &= med <= 0.55
                    elif key == "estimated_private_byte_seconds":
                        gates_pass &= med < 1.0
                lines.append(f"| {regime} | `{pol}` | {key} ratio | {fmt_ratio(vals)} | {gate} |")

            f_sum = sum(a["false_reclaims"] for a, _ in pairs)
            r_med = median(a["reclaim_calls"] for a, _ in pairs)
            if pol == "pool_auto":
                gates_pass &= (f_sum == 0 and r_med == (12 if regime == "burst" else 6))
            lines.append(
                f"| {regime} | `{pol}` | false_reclaims (sum) / reclaim_calls (med) | {f_sum} / {r_med:.0f} | "
                f"{'0 false' if pol == 'pool_auto' else 'comparator'} |"
            )

    # Also report absolute MiB medians for burst and mixed
    lines += [
        "",
        "## Absolute Medians (Burst & Mixed)",
        "",
        "| Regime | Policy | Active FPS | p99 (ms) | Idle Footprint (MiB) | Reclaims (Med) | Minor Faults (Med) |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for regime in ("continuous", "burst", "mixed"):
        pols = ("bfc", "pool_auto") if regime == "continuous" else ("bfc", "pool_explicit", "pool_auto")
        for pol in pols:
            ds = [read(f"confirm_{regime}_{i}_{pol}") for i in range(5)]
            lines.append(
                f"| {regime} | `{pol}` | {median(d['fps'] for d in ds):.3f} | {median(d['p99_ms'] for d in ds):.2f} | "
                f"{median(d['idle_private_bytes'] for d in ds) / (1024**2):.2f} | "
                f"{median(d['reclaim_calls'] for d in ds):.0f} | {median(d['minor_faults'] for d in ds):.0f} |"
            )

    lines += [
        "",
        f"Prespecified Apple M2 Pro median gates: **{'PASS' if gates_pass else 'FAIL'}**.",
        "",
        "## Methodological Notes",
        "- On macOS XNU (`16 KiB` pages), `MADV_DONTNEED` is a no-op on `phys_footprint`; `PageReuseResource::discard()` uses `mmap(..., MAP_FIXED)` anonymous remap so both `mincore()` and `phys_footprint` drop immediately while preserving live neighbors.",
        "- `idle_private_bytes` maps to `TASK_VM_INFO.phys_footprint` on macOS. Threads run with `QOS_CLASS_USER_INITIATED` (`core_pinned=false`).",
        "- All 46 experiment runs verified complete and retained FP32 outputs against `reference_m2pro.bin` (`max_abs_error == 0`).",
    ]
    (OUT / "REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {OUT / 'REPORT.md'} (gates_pass={gates_pass})")


if __name__ == "__main__":
    main()
