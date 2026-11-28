#!/usr/bin/env python3
import json
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
EXP_DIR = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff"


def mean_std(vals):
    m = statistics.mean(vals)
    s = statistics.stdev(vals) if len(vals) > 1 else 0.0
    return m, s


def main() -> int:
    rows = [json.loads(p.read_text()) for p in sorted(EXP_DIR.glob("n8_k2_gap*.json"))]
    if not rows:
        raise SystemExit("No run JSONs found")

    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["burst_gap_ms"], r["policy"])].append(r)

    max_err = max(r["max_abs_error"] for r in rows)
    verified = all(r.get("outputs_verified") and r.get("retained_outputs_verified") for r in rows)

    summary = {}
    lines = [
        "# Multi-Camera Handoff & Allocator Baselines (`N=8, K=2`, MacBook Pro M2 Pro, 16 KiB Pages)",
        "",
        f"- **Total runs:** {len(rows)}",
        f"- **OS page size:** {rows[0]['os_page_size']} B",
        f"- **Global `max_abs_error`:** `{max_err}` (verified={verified})",
        "- **Config:** `streams=8, concurrent=2, frames_per_burst=8, cycles=2, idle_ms=200, shed_warmup=True`",
        "",
    ]

    orders = {
        0: ["bfc", "direct_malloc", "direct_jemalloc", "pool_retain", "pool_auto_0", "pool_coop", "pool_hybrid"],
        350: ["pool_auto_0", "pool_coop", "pool_hybrid"],
    }
    for gap_ms in [0, 350]:
        lines.append(f"## Regime: `burst_gap_ms = {gap_ms}`")
        lines.append("")
        lines.append("| Policy | Trials | Mean `phys_footprint` (MiB) | Peak `phys_footprint` (MiB) | Final `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | p50 / p99 Latency (ms) | Wall (s) / FPS | Minor Faults | Auto / Press Rec. |")
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        for pol in orders[gap_ms]:
            grp = grouped[(gap_ms, pol)]
            mp_m, mp_s = mean_std([r["phys_footprint_mib"]["mean"] for r in grp])
            pk_m, pk_s = mean_std([r["phys_footprint_mib"]["peak"] for r in grp])
            fn_m, _ = mean_std([r["phys_footprint_mib"]["final"] for r in grp])
            bs_m, _ = mean_std([r["phys_footprint_mib"]["byte_seconds_gib_s"] for r in grp])
            p50_m, _ = mean_std([r["latency_ms"]["p50"] for r in grp])
            p99_m, p99_s = mean_std([r["latency_ms"]["p99"] for r in grp])
            wall_m, _ = mean_std([r["wall_sec"] for r in grp])
            fps_m, _ = mean_std([r["throughput_fps"] for r in grp])
            mf_m = statistics.mean(r["minor_faults_total"] for r in grp)
            ar_m = statistics.mean(r["auto_reclaims_total"] for r in grp)
            pr_m = statistics.mean(r["pressure_reclaims_total"] for r in grp)
            summary[(gap_ms, pol)] = {
                "mean_phys": mp_m,
                "peak_phys": pk_m,
                "final_phys": fn_m,
                "p50": p50_m,
                "p99": p99_m,
                "minor_faults": mf_m,
            }
            lines.append(
                f"| `{pol}` | {len(grp)} | {mp_m:.2f} ± {mp_s:.2f} | {pk_m:.2f} ± {pk_s:.2f} | "
                f"{fn_m:.2f} | {bs_m:.3f} | {p50_m:.2f} / {p99_m:.2f} ± {p99_s:.2f} | "
                f"{wall_m:.2f} / {fps_m:.2f} | {mf_m:,.0f} | {ar_m:.1f} / {pr_m:.1f} |"
            )
        lines.append("")

    g1 = (max_err == 0.0) and verified
    g2 = (
        summary[(0, "pool_hybrid")]["peak_phys"] < summary[(0, "pool_auto_0")]["peak_phys"]
        < summary[(0, "pool_retain")]["peak_phys"] < summary[(0, "bfc")]["peak_phys"]
    )
    g3 = (
        summary[(350, "pool_hybrid")]["mean_phys"] < summary[(350, "pool_coop")]["mean_phys"]
        and summary[(350, "pool_hybrid")]["final_phys"] < summary[(350, "pool_coop")]["final_phys"]
    )
    gates_pass = g1 and g2 and g3
    lines.extend([
        "## Acceptance Gates",
        "",
        f"- **Gate 1 (`max_abs_error == 0` & verified):** `{g1}`",
        f"- **Gate 2 (`gap=0` peak: `pool_hybrid` < `pool_auto_0` < `pool_retain` < `bfc`):** `{g2}` "
        f"({summary[(0,'pool_hybrid')]['peak_phys']:.2f} < {summary[(0,'pool_auto_0')]['peak_phys']:.2f} < "
        f"{summary[(0,'pool_retain')]['peak_phys']:.2f} < {summary[(0,'bfc')]['peak_phys']:.2f} MiB)",
        f"- **Gate 3 (`gap=350` mean/final: `pool_hybrid` < `pool_coop`):** `{g3}` "
        f"(mean {summary[(350,'pool_hybrid')]['mean_phys']:.2f} < {summary[(350,'pool_coop')]['mean_phys']:.2f}; "
        f"final {summary[(350,'pool_hybrid')]['final_phys']:.2f} < {summary[(350,'pool_coop')]['final_phys']:.2f} MiB)",
        f"- **`gates_pass`:** `{gates_pass}`",
        "",
    ])
    (EXP_DIR / "REPORT.md").write_text("\n".join(lines))
    print(f"Wrote {EXP_DIR / 'REPORT.md'} (gates_pass={gates_pass})")
    return 0 if gates_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
