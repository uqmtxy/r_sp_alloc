#!/usr/bin/env python3
import json
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
EXP_DIR = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski"
TAU_STAR_MS = 250.0  # Break-even threshold normalizing refault cost = 1.0 per purge cycle


def ski_rental_costs(gaps_ms, tau_ms):
    # Normalized units: refault buy cost = 1.0, holding rate = 1.0 / TAU_STAR_MS per ms.
    opt = sum(min(1.0, g / TAU_STAR_MS) for g in gaps_ms)
    if tau_ms is None:  # retain forever (never buy)
        alg = sum(g / TAU_STAR_MS for g in gaps_ms)
    elif tau_ms <= 0:  # explicit (buy at start of every gap)
        alg = float(len(gaps_ms))
    else:
        alg = sum(min(1.0 + tau_ms / TAU_STAR_MS, g / TAU_STAR_MS) if g >= tau_ms else (g / TAU_STAR_MS) for g in gaps_ms)
    return alg, opt, alg / opt


def main() -> int:
    rows = [json.loads(p.read_text()) for p in sorted(EXP_DIR.glob("*_t*.json"))]
    if not rows:
        raise SystemExit("No run JSONs found")

    grouped = defaultdict(list)
    for r in rows:
        grouped[r["label"]].append(r)

    max_err = max(r["max_abs_error"] for r in rows)
    order = ["bfc", "pool_retain", "pool_explicit", "pool_auto_50", "pool_auto_150", "pool_auto_250", "pool_auto_500", "pool_auto_1000"]

    lines = [
        "# Stochastic Ski-Rental Oracle Benchmark (Apple M2 Pro, 16 KiB Pages)",
        "",
        f"- **Total runs:** {len(rows)}",
        f"- **Global `max_abs_error`:** `{max_err}`",
        f"- **Break-even calibration:** $\\tau^* = {TAU_STAR_MS:.0f}$ ms (normalized holding rate $1/\\tau^*$ vs. unit refault cost)",
        "",
        "| Policy | Trials | Mean `phys_footprint` (MiB) | Byte-Seconds (GiB·s) | Minor Faults | Reclaim Calls | p99 (ms) | Empirical Competitive Ratio (ALG/OPT) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    comp = {}
    for lab in order:
        grp = grouped[lab]
        mp = statistics.mean(r["mean_phys_mib"] for r in grp)
        bs = statistics.mean(r["byte_seconds_gib_s"] for r in grp)
        mf = statistics.mean(r["minor_faults"] for r in grp)
        rc = statistics.mean(r["reclaim_calls"] for r in grp)
        p99 = statistics.mean(r["p99_ms"] for r in grp)
        if lab == "bfc" or lab == "pool_retain":
            tau_param = None
        elif lab == "pool_explicit":
            tau_param = 0.0
        else:
            tau_param = float(grp[0]["idle_threshold_ms"])
        ratios = [ski_rental_costs(r["realized_gaps_ms"], tau_param)[2] for r in grp]
        cr = statistics.mean(ratios)
        comp[lab] = cr
        lines.append(f"| `{lab}` | {len(grp)} | {mp:.2f} | {bs:.3f} | {mf:,.0f} | {rc:.1f} | {p99:.2f} | {cr:.4f}x |")

    g1 = (max_err == 0)
    g2 = comp["pool_auto_250"] <= 2.0
    g3 = comp["pool_auto_250"] < comp["pool_explicit"] and comp["pool_auto_250"] < comp["pool_retain"]
    gates_pass = g1 and g2 and g3
    lines.extend([
        "",
        "## Acceptance Gates",
        "",
        f"- **Gate 1 (`max_abs_error == 0`):** `{g1}`",
        f"- **Gate 2 (`ALG(250 ms) / OPT <= 2.0`):** `{g2}` ({comp['pool_auto_250']:.4f}x)",
        f"- **Gate 3 (`ALG(250 ms) < ALG(tau=0)` and `< ALG(tau=inf)`):** `{g3}` "
        f"({comp['pool_auto_250']:.4f}x < {comp['pool_explicit']:.4f}x and {comp['pool_retain']:.4f}x)",
        f"- **`gates_pass`:** `{gates_pass}`",
        "",
    ])
    (EXP_DIR / "REPORT.md").write_text("\n".join(lines))
    print(f"Wrote {EXP_DIR / 'REPORT.md'} (gates_pass={gates_pass})")
    return 0 if gates_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
