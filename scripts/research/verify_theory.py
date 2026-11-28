#!/usr/bin/env python3
"""Verify formal Theorems 1-4 against observed C++ tests and raw benchmark JSONs."""
import json
import math
import statistics
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
OUT_PATH = ROOT / "context/THEORY_VERIFICATION.json"


def ols_regression(xs, ys):
    n = len(xs)
    mx = statistics.mean(xs)
    my = statistics.mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx if sxx > 0 else 0.0
    intercept = my - slope * mx
    ss_res = sum((y - (slope * x + intercept)) ** 2 for x, y in zip(xs, ys))
    r2 = 1.0 - (ss_res / syy) if syy > 0 else 1.0
    return {
        "n": n,
        "slope_faults_per_mib": slope,
        "intercept_faults": intercept,
        "r_squared": r2,
    }


def verify_theorem_1():
    # Compile & run resource_test on 16 KiB M2 Pro and check budget drop on screen_0 vs screen_-1.
    bin_path = Path("/tmp/resource_test_theory")
    subprocess.run(
        [
            "clang++", "-O2", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "first_steps/bench/utilization/resource_test.cpp"),
            "-o", str(bin_path),
        ],
        check=True,
    )
    res = subprocess.run([str(bin_path)], capture_output=True, text=True, check=True)
    m2_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle"
    s0 = json.loads((m2_dir / "screen_0.json").read_text())
    s_ret = json.loads((m2_dir / "screen_-1.json").read_text())
    idle_drop_mib = (s_ret["idle_private_bytes"] - s0["idle_private_bytes"]) / (1024.0 * 1024.0)
    passed = ("PASS" in res.stdout) and (idle_drop_mib > 50.0)
    return {
        "name": "Theorem 1: Exact Page-Residency Conservation & Boundary-Limited Retention Waste",
        "passed": passed,
        "resource_test_stdout": res.stdout.strip(),
        "m2pro_retain_idle_mib": s_ret["idle_private_bytes"] / (1024.0 * 1024.0),
        "m2pro_budget0_idle_mib": s0["idle_private_bytes"] / (1024.0 * 1024.0),
        "reclaimed_body_mib": idle_drop_mib,
    }


def verify_theorem_2():
    m2_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle"
    cont_auto = [json.loads(p.read_text()) for p in sorted(m2_dir.glob("confirm_continuous_*_pool_auto.json"))]
    burst_auto = [json.loads(p.read_text()) for p in sorted(m2_dir.glob("confirm_burst_*_pool_auto.json"))]
    burst_bfc = [json.loads(p.read_text()) for p in sorted(m2_dir.glob("confirm_burst_*_bfc.json"))]
    mixed_auto = [json.loads(p.read_text()) for p in sorted(m2_dir.glob("confirm_mixed_*_pool_auto.json"))]

    cont_false = sum(r["false_reclaims"] for r in cont_auto)
    cont_reclaims = sum(r["reclaim_calls"] for r in cont_auto)
    burst_false = sum(r["false_reclaims"] for r in burst_auto)
    mixed_false = sum(r["false_reclaims"] for r in mixed_auto)
    burst_reclaims = [r["reclaim_calls"] for r in burst_auto]
    p99_ratio = statistics.mean(r["p99_ms"] for r in burst_auto) / statistics.mean(r["p99_ms"] for r in burst_bfc)

    # Stochastic ski-rental oracle verification
    ski_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski"
    ski_files = sorted(ski_dir.glob("*_t*.json"))
    ski_summary = {}
    ski_ok = False
    if ski_files:
        tau_star = 250.0
        by_lab = defaultdict(list)
        for p in ski_files:
            d = json.loads(p.read_text())
            by_lab[d["label"]].append(d)
        for lab, grp in by_lab.items():
            if lab in ("bfc", "pool_retain"):
                tau_p = None
            elif lab == "pool_explicit":
                tau_p = 0.0
            else:
                tau_p = float(grp[0]["idle_threshold_ms"])
            ratios = []
            for r in grp:
                gaps = r["realized_gaps_ms"]
                opt = sum(min(1.0, g / tau_star) for g in gaps)
                if tau_p is None:
                    alg = sum(g / tau_star for g in gaps)
                elif tau_p <= 0:
                    alg = float(len(gaps))
                else:
                    alg = sum(min(1.0 + tau_p / tau_star, g / tau_star) if g >= tau_p else (g / tau_star) for g in gaps)
                ratios.append(alg / opt)
            ski_summary[lab] = {
                "competitive_ratio": statistics.mean(ratios),
                "mean_phys_mib": statistics.mean(r["mean_phys_mib"] for r in grp),
                "minor_faults": statistics.mean(r["minor_faults"] for r in grp),
            }
        ski_ok = (
            len(ski_files) == 40
            and all(json.loads(p.read_text())["max_abs_error"] == 0 for p in ski_files)
            and ski_summary["pool_auto_250"]["competitive_ratio"] <= 2.0
            and ski_summary["pool_auto_250"]["competitive_ratio"] < ski_summary["pool_retain"]["competitive_ratio"]
            and ski_summary["pool_auto_150"]["competitive_ratio"] < ski_summary["pool_explicit"]["competitive_ratio"]
        )

    passed = (
        len(cont_auto) == 5
        and len(burst_auto) == 5
        and len(mixed_auto) == 5
        and cont_false == 0
        and cont_reclaims == 0
        and burst_false == 0
        and mixed_false == 0
        and all(c == 12 for c in burst_reclaims)
        and p99_ratio <= 1.05
        and ski_ok
    )
    return {
        "name": "Theorem 2: 2-Competitive Online Idle-Threshold Reclamation",
        "passed": passed,
        "continuous_false_reclaims": cont_false,
        "continuous_reclaims": cont_reclaims,
        "burst_false_reclaims": burst_false,
        "mixed_false_reclaims": mixed_false,
        "burst_reclaim_calls": burst_reclaims,
        "burst_p99_ratio_vs_bfc": p99_ratio,
        "stochastic_ski": ski_summary,
    }


def verify_theorem_3():
    mc_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam"
    files = sorted(mc_dir.glob("n*_k*.json"))
    if not files:
        return {"name": "Theorem 3: Multi-Stream Staggered Duty-Cycle Scaling", "passed": False, "reason": "missing runs"}

    grouped = defaultdict(list)
    max_err = 0.0
    for p in files:
        d = json.loads(p.read_text())
        grouped[(d["streams"], d["concurrent"], d["policy"])].append(d)
        max_err = max(max_err, d["max_abs_error"])

    summary = {}
    ok = (max_err == 0.0)
    for (N, K) in [(4, 1), (8, 2)]:
        bfc_m = statistics.mean(r["phys_footprint_mib"]["mean"] for r in grouped[(N, K, "bfc")])
        ret_m = statistics.mean(r["phys_footprint_mib"]["mean"] for r in grouped[(N, K, "pool_retain")])
        auto_m = statistics.mean(r["phys_footprint_mib"]["mean"] for r in grouped[(N, K, "pool_auto_0")])
        coop_m = statistics.mean(r["phys_footprint_mib"]["mean"] for r in grouped[(N, K, "pool_coop")])
        coop_p99 = statistics.mean(r["latency_ms"]["p99"] for r in grouped[(N, K, "pool_coop")])
        ret_p99 = statistics.mean(r["latency_ms"]["p99"] for r in grouped[(N, K, "pool_retain")])

        # Theorem 3 model: R_pred(N, K) = R_base(N) + K * M_arena
        r_base = statistics.mean(r["phys_footprint_mib"]["min"] for r in grouped[(N, K, "pool_auto_0")])
        m_arena = statistics.mean(r["page_accounting"]["resident_body_mib"] for r in grouped[(N, K, "pool_retain")]) / float(N)
        r_pred = r_base + float(K) * m_arena
        rel_err = abs(coop_m - r_pred) / r_pred if r_pred > 0 else 0.0

        summary[f"N{N}_K{K}"] = {
            "bfc_mean_mib": bfc_m,
            "retain_mean_mib": ret_m,
            "auto0_mean_mib": auto_m,
            "coop_mean_mib": coop_m,
            "r_base_mib": r_base,
            "m_arena_mib": m_arena,
            "r_pred_mib": r_pred,
            "pred_rel_error": rel_err,
            "coop_vs_retain": coop_m / ret_m,
            "coop_vs_bfc": coop_m / bfc_m,
            "auto0_vs_bfc": auto_m / bfc_m,
            "coop_p99_vs_retain": coop_p99 / ret_p99,
        }
        if not (coop_m < ret_m < bfc_m and auto_m < ret_m < bfc_m and coop_m / ret_m <= 0.60 and coop_p99 / ret_p99 <= 1.10 and rel_err < 0.02):
            ok = False

    return {
        "name": "Theorem 3: Multi-Stream Staggered Duty-Cycle Footprint Scaling & Zero-Lag Cooperative Bound",
        "passed": ok,
        "total_runs": len(files),
        "max_abs_error": max_err,
        "configs": summary,
    }


def verify_theorem_4():
    # CloudLab 4 KiB budget sweep
    cl_dir = ROOT / "first_steps/bench/utilization/results/bounded"
    cl_xs, cl_ys = [], []
    for b in [0, 8, 16, 24]:
        d = json.loads((cl_dir / f"screen_{b}.json").read_text())
        cl_xs.append(float(b))
        cl_ys.append(float(d["minor_faults"]))
    cl_reg = ols_regression(cl_xs, cl_ys)

    # M2 Pro 16 KiB budget sweep
    m2_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle"
    m2_xs, m2_ys = [], []
    for b in [0, 8, 16, 24, 32]:
        d = json.loads((m2_dir / f"screen_{b}.json").read_text())
        m2_xs.append(float(b))
        m2_ys.append(float(d["minor_faults"]))
    m2_reg = ols_regression(m2_xs, m2_ys)

    passed = (cl_reg["r_squared"] > 0.99) and (m2_reg["r_squared"] > 0.99) and (cl_reg["slope_faults_per_mib"] < 0) and (m2_reg["slope_faults_per_mib"] < 0)
    return {
        "name": "Theorem 4: Linear Fault-Budget Tradeoff Law across 4 KiB and 16 KiB Pages",
        "passed": passed,
        "cloudlab_4kib": {"budgets_mib": cl_xs, "minor_faults": cl_ys, "regression": cl_reg},
        "m2pro_16kib": {"budgets_mib": m2_xs, "minor_faults": m2_ys, "regression": m2_reg},
    }


def verify_theorem_5():
    hd_dir = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff"
    files = sorted(hd_dir.glob("n8_k2_gap*.json"))
    if len(files) < 50:
        return {"name": "Theorem 5: Hybrid Cooperative + Online Idle Dominance Bound", "passed": False, "reason": f"found {len(files)} runs"}

    grouped = defaultdict(list)
    max_err = 0.0
    for p in files:
        d = json.loads(p.read_text())
        grouped[(d["burst_gap_ms"], d["policy"])].append(d)
        max_err = max(max_err, d["max_abs_error"])

    gap0 = {}
    for pol in ["bfc", "direct_malloc", "direct_jemalloc", "pool_retain", "pool_auto_0", "pool_coop", "pool_hybrid"]:
        grp = grouped[(0, pol)]
        gap0[pol] = {
            "mean_phys_mib": statistics.mean(r["phys_footprint_mib"]["mean"] for r in grp),
            "peak_phys_mib": statistics.mean(r["phys_footprint_mib"]["peak"] for r in grp),
            "minor_faults": statistics.mean(r["minor_faults_total"] for r in grp),
            "p99_ms": statistics.mean(r["latency_ms"]["p99"] for r in grp),
        }

    gap350 = {}
    for pol in ["pool_auto_0", "pool_coop", "pool_hybrid"]:
        grp = grouped[(350, pol)]
        gap350[pol] = {
            "mean_phys_mib": statistics.mean(r["phys_footprint_mib"]["mean"] for r in grp),
            "peak_phys_mib": statistics.mean(r["phys_footprint_mib"]["peak"] for r in grp),
            "final_phys_mib": statistics.mean(r["phys_footprint_mib"]["final"] for r in grp),
            "minor_faults": statistics.mean(r["minor_faults_total"] for r in grp),
        }

    passed = (
        max_err == 0.0
        and gap0["pool_hybrid"]["peak_phys_mib"] < gap0["pool_auto_0"]["peak_phys_mib"]
        and gap0["pool_auto_0"]["peak_phys_mib"] < gap0["pool_retain"]["peak_phys_mib"]
        and gap0["pool_retain"]["peak_phys_mib"] < gap0["bfc"]["peak_phys_mib"]
        and gap350["pool_hybrid"]["mean_phys_mib"] < gap350["pool_coop"]["mean_phys_mib"]
        and gap350["pool_hybrid"]["final_phys_mib"] < gap350["pool_coop"]["final_phys_mib"]
    )
    return {
        "name": "Theorem 5: Hybrid Cooperative + Online Idle Dominance Bound",
        "passed": passed,
        "total_runs": len(files),
        "max_abs_error": max_err,
        "gap0": gap0,
        "gap350": gap350,
    }


def main() -> int:
    report = {
        "theorem_1": verify_theorem_1(),
        "theorem_2": verify_theorem_2(),
        "theorem_3": verify_theorem_3(),
        "theorem_4": verify_theorem_4(),
        "theorem_5": verify_theorem_5(),
    }
    all_passed = all(v["passed"] for v in report.values())
    report["all_passed"] = all_passed
    OUT_PATH.write_text(json.dumps(report, indent=2) + "\n")
    for k in ["theorem_1", "theorem_2", "theorem_3", "theorem_4", "theorem_5"]:
        print(f"{k}: passed={report[k]['passed']} — {report[k]['name']}")
    print(f"Wrote {OUT_PATH} (all_passed={all_passed})")
    return 0 if all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
