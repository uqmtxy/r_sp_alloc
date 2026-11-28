#!/usr/bin/env python3
"""Generate publication-quality PDF + PNG figures from verified raw JSONs."""
import json
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
FIG_DIR = ROOT / "first_steps/paper/figures"
M2_AUTO = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle"
M2_MC = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam"
M2_HANDOFF = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff"
M2_SKI = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski"
CL_BOUNDED = ROOT / "first_steps/bench/utilization/results/bounded"
THEORY = json.loads((ROOT / "context/THEORY_VERIFICATION.json").read_text())

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
    "font.size": 9,
    "axes.titlesize": 10,
    "axes.labelsize": 9,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "legend.fontsize": 8,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "axes.spines.top": False,
    "axes.spines.right": False,
})

# Colorblind-safe Okabe-Ito palette
C_BLUE = "#0072B2"
C_ORANGE = "#E69F00"
C_GREEN = "#009E73"
C_VERM = "#D55E00"
C_PURPLE = "#CC79A7"
C_GRAY = "#6C757D"


def save_fig(fig, stem: str):
    fig.savefig(FIG_DIR / f"{stem}.pdf", bbox_inches="tight")
    fig.savefig(FIG_DIR / f"{stem}.png", bbox_inches="tight", dpi=300)
    plt.close(fig)


def make_fig1():
    # Panel A: M2 Pro single-stream auto_idle idle_private_bytes across regimes
    regimes = ["continuous", "burst", "mixed"]
    policies = ["bfc", "pool_explicit", "pool_auto"]
    labels = ["BFC (default)", "Pool Explicit", "Pool Auto (0 MiB)"]
    colors = [C_GRAY, C_ORANGE, C_BLUE]

    means = {p: [] for p in policies}
    stds = {p: [] for p in policies}
    for reg in regimes:
        for pol in policies:
            files = sorted(M2_AUTO.glob(f"confirm_{reg}_*_{pol}.json"))
            if not files:
                means[pol].append(0.0)
                stds[pol].append(0.0)
            else:
                vals = [json.loads(f.read_text())["idle_private_bytes"] / (1024.0 * 1024.0) for f in files]
                means[pol].append(statistics.mean(vals))
                stds[pol].append(statistics.stdev(vals) if len(vals) > 1 else 0.0)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.8, 3.3), gridspec_kw={"width_ratios": [1.25, 1.0]})

    x = np.arange(len(regimes))
    w = 0.25
    for idx, (pol, lab, col) in enumerate(zip(policies, labels, colors)):
        offset = (idx - 1) * w
        vals = means[pol]
        errs = stds[pol]
        # Skip missing pool_explicit in continuous
        plot_x = [xi + offset for xi, v in zip(x, vals) if v > 0]
        plot_v = [v for v in vals if v > 0]
        plot_e = [e for v, e in zip(vals, errs) if v > 0]
        bars = ax1.bar(plot_x, plot_v, width=w * 0.92, yerr=plot_e, capsize=3,
                       label=lab, color=col, edgecolor="black", linewidth=0.5)
        for bx, bv in zip(plot_x, plot_v):
            ax1.text(bx, bv + 2.0, f"{bv:.1f}", ha="center", va="bottom", fontsize=7.5)

    ax1.set_xticks(x)
    ax1.set_xticklabels(["Continuous\n(0 false rec.)", "Burst\n(1000 ms gap)", "Mixed\n(500 ms gap)"])
    ax1.set_ylabel("Idle Physical Footprint (MiB)")
    ax1.set_ylim(0, 145)
    ax1.set_title("(a) M2 Pro (16 KiB Pages): Single-Stream Footprint")
    ax1.legend(frameon=False, loc="upper right", ncol=2, fontsize=7.5)
    ax1.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B: XNU 16 KiB probe (32 MiB allocated, 16 MiB freed, 16 MiB live neighbor)
    mechs = ["MADV_\nDONTNEED\n(XNU)", "MADV_\nFREE\n(XNU)", "MADV_FREE_\nREUSABLE\n(XNU)", "mmap(\nMAP_FIXED)\n(XNU)"]
    phys_after = [32.0, 32.0, 16.0, 16.0]
    mincore_after = [32.0, 32.0, 32.0, 16.0]
    xb = np.arange(len(mechs))
    wb = 0.34
    ax2.bar(xb - wb / 2, phys_after, width=wb, label="phys_footprint (MiB)", color=C_BLUE, edgecolor="black", linewidth=0.5)
    ax2.bar(xb + wb / 2, mincore_after, width=wb, label="mincore() resident (MiB)", color=C_VERM, edgecolor="black", linewidth=0.5)
    ax2.axhline(16.0, color=C_GREEN, linestyle="--", linewidth=1.0, label="Live neighbor target (16 MiB)")
    ax2.set_xticks(xb)
    ax2.set_xticklabels(mechs, fontsize=7.5)
    ax2.set_ylabel("Residual Memory (MiB)")
    ax2.set_ylim(0, 40)
    ax2.set_title("(b) macOS XNU 16 KiB VM Reclamation Probe")
    ax2.legend(frameon=False, loc="upper right", fontsize=7.5)
    ax2.grid(axis="y", linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig1_platform_mechanisms")


def make_fig2():
    # CloudLab m510 4 KiB bounded retention (confirm_1000_*_-1 and _24) + endurance
    ret_1000 = [json.loads(p.read_text()) for p in sorted(CL_BOUNDED.glob("confirm_1000_*_-1.json"))]
    b24_1000 = [json.loads(p.read_text()) for p in sorted(CL_BOUNDED.glob("confirm_1000_*_24.json"))]
    ret_0 = [json.loads(p.read_text()) for p in sorted(CL_BOUNDED.glob("confirm_0_*_-1.json"))]
    b24_0 = [json.loads(p.read_text()) for p in sorted(CL_BOUNDED.glob("confirm_0_*_24.json"))]
    end_ret = json.loads((CL_BOUNDED / "endurance_-1.json").read_text())
    end_b24 = json.loads((CL_BOUNDED / "endurance_24.json").read_text())

    idle_ratio = statistics.median(b["idle_private_bytes"] / r["idle_private_bytes"] for b, r in zip(b24_1000, ret_1000))
    bs_ratio = statistics.median(b["estimated_private_byte_seconds"] / r["estimated_private_byte_seconds"] for b, r in zip(b24_1000, ret_1000))
    p99_ratio_burst = statistics.median(b["p99_ms"] / r["p99_ms"] for b, r in zip(b24_1000, ret_1000))
    p99_ratio_cont = statistics.median(b["p99_ms"] / r["p99_ms"] for b, r in zip(b24_0, ret_0))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.6, 3.2))

    metrics = ["Idle Private\nRSS Ratio", "Private\nByte-Seconds", "Burst p99\nLatency Ratio", "Continuous p99\nLatency Ratio"]
    ratios = [idle_ratio, bs_ratio, p99_ratio_burst, p99_ratio_cont]
    cols = [C_GREEN, C_BLUE, C_ORANGE, C_GRAY]
    x = np.arange(len(metrics))
    bars = ax1.bar(x, ratios, width=0.55, color=cols, edgecolor="black", linewidth=0.6)
    ax1.axhline(1.0, color="black", linestyle="--", linewidth=1.0, label="Retain baseline (1.00x)")
    for b, v in zip(bars, ratios):
        ax1.text(b.get_x() + b.get_width() / 2, v + 0.03, f"{v:.3f}x", ha="center", va="bottom", fontsize=8)
    ax1.set_xticks(x)
    ax1.set_xticklabels(metrics, fontsize=8)
    ax1.set_ylabel("Normalized Ratio vs. Pool Retain")
    ax1.set_ylim(0, 1.28)
    ax1.set_title("(a) CloudLab m510 (4 KiB): 24 MiB Bounded Pool")
    ax1.legend(frameon=False, loc="upper left")
    ax1.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B: 200-frame endurance
    end_labels = ["Pool Retain", "Pool Bounded (24 MiB)"]
    end_rss = [end_ret["idle_private_bytes"] / (1024.0 * 1024.0), end_b24["idle_private_bytes"] / (1024.0 * 1024.0)]
    end_fps = [end_ret["fps"], end_b24["fps"]]
    xb = np.arange(2)
    bars2 = ax2.bar(xb, end_rss, width=0.45, color=[C_GRAY, C_BLUE], edgecolor="black", linewidth=0.6)
    for b, r_val, f_val in zip(bars2, end_rss, end_fps):
        ax2.text(b.get_x() + b.get_width() / 2, r_val + 2.5,
                 f"{r_val:.2f} MiB\n({f_val:.3f} FPS)", ha="center", va="bottom", fontsize=8)
    ax2.set_xticks(xb)
    ax2.set_xticklabels(end_labels)
    ax2.set_ylabel("Endurance Idle Private RSS (MiB)")
    ax2.set_ylim(0, 148)
    ax2.set_title("(b) CloudLab 1,000-Frame Endurance")
    ax2.grid(axis="y", linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig2_cloudlab_pareto")


def make_fig3():
    t4 = THEORY["theorem_4"]
    cl_x = np.array(t4["cloudlab_4kib"]["budgets_mib"])
    cl_y = np.array(t4["cloudlab_4kib"]["minor_faults"]) / 1000.0
    cl_reg = t4["cloudlab_4kib"]["regression"]

    m2_x = np.array(t4["m2pro_16kib"]["budgets_mib"])
    m2_y = np.array(t4["m2pro_16kib"]["minor_faults"]) / 1000.0
    m2_reg = t4["m2pro_16kib"]["regression"]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.6, 3.2))

    # CloudLab 4 KiB
    xf1 = np.linspace(0, 24, 100)
    yf1 = (cl_reg["slope_faults_per_mib"] * xf1 + cl_reg["intercept_faults"]) / 1000.0
    ax1.scatter(cl_x, cl_y, color=C_BLUE, s=45, zorder=3, label="Measured runs")
    ax1.plot(xf1, yf1, color=C_VERM, linestyle="--", linewidth=1.6,
             label=f"OLS fit: {cl_reg['slope_faults_per_mib']:.1f} faults/MiB\n($R^2 = {cl_reg['r_squared']:.7f}$)")
    ax1.set_xlabel("Retained Free-Body Budget B (MiB)")
    ax1.set_ylabel("Minor Page Faults (thousands)")
    ax1.set_title("(a) CloudLab m510 (4 KiB Pages, 11 bursts)")
    ax1.legend(frameon=False, loc="upper right")
    ax1.grid(True, linestyle=":", alpha=0.5)

    # M2 Pro 16 KiB
    xf2 = np.linspace(0, 32, 100)
    yf2 = (m2_reg["slope_faults_per_mib"] * xf2 + m2_reg["intercept_faults"]) / 1000.0
    ax2.scatter(m2_x, m2_y, color=C_GREEN, s=45, zorder=3, label="Measured runs")
    ax2.plot(xf2, yf2, color=C_BLUE, linestyle="--", linewidth=1.6,
             label=f"OLS fit: {m2_reg['slope_faults_per_mib']:.1f} faults/MiB\n($R^2 = {m2_reg['r_squared']:.6f}$)")
    ax2.set_xlabel("Retained Free-Body Budget B (MiB)")
    ax2.set_ylabel("Minor Page Faults (thousands)")
    ax2.set_title("(b) Apple M2 Pro (16 KiB Pages, 12 bursts)")
    ax2.legend(frameon=False, loc="upper right")
    ax2.grid(True, linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig3_fault_budget_law")


def make_fig4():
    rows = [json.loads(p.read_text()) for p in sorted(M2_MC.glob("n*_k*.json"))]
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["streams"], r["concurrent"], r["policy"])].append(r)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.5), gridspec_kw={"width_ratios": [1.35, 1.0]})

    # Panel A: Grouped bar chart of mean phys_footprint for N=4 K=1 and N=8 K=2
    policies = ["bfc", "pool_retain", "pool_auto_0", "pool_coop"]
    p_labels = ["BFC (default)", "Pool Retain", "Pool Auto (0 MiB)", "Pool Coop (Arbiter)"]
    p_colors = [C_GRAY, C_ORANGE, C_GREEN, C_BLUE]

    configs = [(4, 1, "N=4, K=1\n(25% duty)"), (8, 2, "N=8, K=2\n(25% duty)")]
    x = np.arange(len(configs))
    w = 0.19
    for idx, (pol, lab, col) in enumerate(zip(policies, p_labels, p_colors)):
        vals, errs = [], []
        for (N, K, _) in configs:
            g = grouped[(N, K, pol)]
            m = statistics.mean(r["phys_footprint_mib"]["mean"] for r in g)
            s = statistics.stdev(r["phys_footprint_mib"]["mean"] for r in g)
            vals.append(m)
            errs.append(s)
        pos = x + (idx - 1.5) * w
        ax1.bar(pos, vals, width=w * 0.9, yerr=errs, capsize=2.5,
                label=lab, color=col, edgecolor="black", linewidth=0.5)
        for px, pv in zip(pos, vals):
            ax1.text(px, pv + 10.0, f"{pv:.0f}", ha="center", va="bottom", fontsize=7.5)

    # Overlay Theorem 3 predicted horizontal markers for N=4 and N=8
    t3 = THEORY["theorem_3"]["configs"]
    for i, key in enumerate(["N4_K1", "N8_K2"]):
        pred = t3[key]["r_pred_mib"]
        ax1.plot([i - 0.42, i + 0.42], [pred, pred], color=C_VERM, linestyle="--", linewidth=1.4,
                 label="Theorem 3 $R_{base}+K\\cdot M_{arena}$" if i == 0 else None)

    ax1.set_xticks(x)
    ax1.set_xticklabels([c[2] for c in configs])
    ax1.set_ylabel("Mean phys_footprint (MiB)")
    ax1.set_ylim(0, 740)
    ax1.set_title("(a) Multi-Camera Mean Physical Footprint Scaling")
    ax1.legend(frameon=False, loc="upper left", fontsize=7.5)
    ax1.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B: Minor page faults comparison (Pool Auto vs Pool Coop)
    cats = ["N=4, K=1", "N=8, K=2"]
    auto_mf = [statistics.mean(r["minor_faults_total"] for r in grouped[(N, K, "pool_auto_0")]) / 1000.0 for (N, K, _) in configs]
    coop_mf = [statistics.mean(r["minor_faults_total"] for r in grouped[(N, K, "pool_coop")]) / 1000.0 for (N, K, _) in configs]
    xb = np.arange(len(cats))
    wb = 0.30
    b1 = ax2.bar(xb - wb / 2, auto_mf, width=wb, label="Pool Auto (0 MiB)", color=C_GREEN, edgecolor="black", linewidth=0.5)
    b2 = ax2.bar(xb + wb / 2, coop_mf, width=wb, label="Pool Coop (-12.4%)", color=C_BLUE, edgecolor="black", linewidth=0.5)
    for bar_grp in (b1, b2):
        for b in bar_grp:
            ax2.text(b.get_x() + b.get_width() / 2, b.get_height() + 0.6, f"{b.get_height():.1f}k",
                     ha="center", va="bottom", fontsize=7.5)
    ax2.set_xticks(xb)
    ax2.set_xticklabels(cats)
    ax2.set_ylabel("Total Minor Page Faults (thousands)")
    ax2.set_ylim(0, 39)
    ax2.set_title("(b) Refault Reduction via Cooperative Handoff")
    ax2.legend(frameon=False, loc="upper left", fontsize=7.5)
    ax2.grid(axis="y", linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig4_multicam_scaling")


def make_fig5():
    fig, ax = plt.subplots(figsize=(8.6, 3.0))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 4.2)
    ax.axis("off")

    def box(x, y, w, h, text, fc, ec="black", lw=1.0, fs=8):
        p = patches.FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.08,rounding_size=0.12",
                                   facecolor=fc, edgecolor=ec, linewidth=lw)
        ax.add_patch(p)
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs)

    # Top row: N ORT Camera Streams
    for i, label in enumerate(["Stream 0\n(YOLO26n)", "Stream 1\n(YOLO26n)", "...", "Stream N-1\n(YOLO26n)"]):
        box(0.3 + i * 2.4, 3.05, 1.9, 0.8, label, "#E8F4FA", C_BLUE)

    # Middle row: Per-stream Controller + PageReuseResource
    box(0.3, 1.65, 4.3, 0.9, "Per-Stream IdleReclamationController\n(mark_frame_start/end, 2-Competitive Timer,\nPageReuseResource: head/body/tail)", "#EAF7F0", C_GREEN)
    box(5.4, 1.65, 4.3, 0.9, "GlobalBudgetArbiter (Multi-Stream)\n(notify_burst_start -> LRU idle victim selection,\npressure_reclaim(B_G) without touching active K)", "#FFF4E5", C_ORANGE)

    # Bottom row: OS VM Reclamation Backends
    box(0.3, 0.2, 4.3, 0.9, "Linux x86_64 Backend (4 KiB Pages)\nmadvise(MADV_DONTNEED) on interior pages", "#F3F4F6", C_GRAY)
    box(5.4, 0.2, 4.3, 0.9, "macOS Apple Silicon Backend (16 KiB Pages)\nmmap(MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0)", "#FCEAE6", C_VERM)

    # Arrows
    arrow_kw = dict(arrowstyle="-|>", color="black", lw=1.1, mutation_scale=11)
    for i in range(2):
        ax.annotate("", xy=(1.25 + i * 2.4, 2.63), xytext=(1.25 + i * 2.4, 3.0), arrowprops=arrow_kw)
    ax.annotate("", xy=(8.45, 2.63), xytext=(8.45, 3.0), arrowprops=arrow_kw)
    ax.annotate("", xy=(5.35, 2.1), xytext=(4.65, 2.1), arrowprops=dict(arrowstyle="<->", color="black", lw=1.1, mutation_scale=11))
    ax.annotate("", xy=(2.45, 1.18), xytext=(2.45, 1.6), arrowprops=arrow_kw)
    ax.annotate("", xy=(7.55, 1.18), xytext=(7.55, 1.6), arrowprops=arrow_kw)

    ax.set_title("Cooperative Idle- and Pressure-Aware Arena Reclamation Architecture", fontsize=10, pad=6)
    save_fig(fig, "fig5_architecture_arbiter")


def make_fig6():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.2, 3.4), gridspec_kw={"width_ratios": [1.15, 1.15]})

    # Panel A: 25 ms time-series traces (gap=0, N=8, K=2)
    trace_specs = [
        ("bfc", "BFC (default)", C_GRAY, "-"),
        ("pool_retain", "Pool Retain", C_ORANGE, "-."),
        ("pool_auto_0", "Pool Auto (overlap spikes)", C_GREEN, "--"),
        ("direct_jemalloc", "Direct jemalloc (259k faults)", C_PURPLE, ":"),
        ("pool_hybrid", "Pool Hybrid (flat ceiling)", C_BLUE, "-"),
    ]
    traces_dir = M2_HANDOFF / "traces"
    for pol, lab, col, ls in trace_specs:
        tp = traces_dir / f"trace_gap0_{pol}.json"
        if not tp.exists():
            continue
        td = json.loads(tp.read_text())
        ts = [s["t_sec"] for s in td["samples"]]
        ys = [s["phys_mib"] for s in td["samples"]]
        ax1.plot(ts, ys, label=lab, color=col, linestyle=ls, linewidth=1.6 if pol == "pool_hybrid" else 1.2)

    ax1.set_xlabel("Elapsed Time (s)")
    ax1.set_ylabel("phys_footprint (MiB)")
    ax1.set_ylim(140, 720)
    ax1.set_title("(a) N=8, K=2 Back-to-Back Handoff Trace (gap=0 ms)")
    ax1.legend(frameon=False, loc="upper right", fontsize=7.0, ncol=2)
    ax1.grid(True, linestyle=":", alpha=0.5)

    # Panel B: Peak vs Mean across all 7 policies (gap=0)
    rows = [json.loads(p.read_text()) for p in sorted(M2_HANDOFF.glob("n8_k2_gap0_*.json"))]
    by_pol = defaultdict(list)
    for r in rows:
        by_pol[r["policy"]].append(r)
    pols = ["bfc", "pool_retain", "pool_auto_0", "direct_malloc", "direct_jemalloc", "pool_coop", "pool_hybrid"]
    plabs = ["BFC", "Pool\nRetain", "Pool\nAuto", "Direct\nmalloc", "Direct\njemalloc", "Pool\nCoop", "Pool\nHybrid"]
    means = [statistics.mean(r["phys_footprint_mib"]["mean"] for r in by_pol[p]) for p in pols]
    peaks = [statistics.mean(r["phys_footprint_mib"]["peak"] for r in by_pol[p]) for p in pols]

    x = np.arange(len(pols))
    w = 0.36
    ax2.bar(x - w / 2, means, width=w, label="Mean footprint", color=C_BLUE, edgecolor="black", linewidth=0.5)
    ax2.bar(x + w / 2, peaks, width=w, label="Peak footprint", color=C_VERM, edgecolor="black", linewidth=0.5)
    for xi, pk in zip(x, peaks):
        ax2.text(xi + w / 2, pk + 8, f"{pk:.0f}", ha="center", va="bottom", fontsize=7.0)
    ax2.set_xticks(x)
    ax2.set_xticklabels(plabs, fontsize=7.2)
    ax2.set_ylabel("phys_footprint (MiB)")
    ax2.set_ylim(0, 740)
    ax2.set_title("(b) N=8, K=2 Multi-Allocator Comparison (gap=0 ms)")
    ax2.legend(frameon=False, loc="upper right", fontsize=7.5)
    ax2.grid(axis="y", linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig6_handoff_timeseries")


def make_fig7():
    rows = [json.loads(p.read_text()) for p in sorted(M2_SKI.glob("*_t*.json"))]
    by_lab = defaultdict(list)
    for r in rows:
        by_lab[r["label"]].append(r)

    tau_star = 250.0
    order = ["pool_explicit", "pool_auto_50", "pool_auto_150", "pool_auto_250", "pool_auto_500", "pool_auto_1000", "pool_retain"]
    xlabels = ["0\n(Explicit)", "50", "150", "250\n(τ*)", "500", "1000", "∞\n(Retain)"]

    crs, bs_vals, mf_vals = [], [], []
    for lab in order:
        grp = by_lab[lab]
        tau_p = None if lab == "pool_retain" else (0.0 if lab == "pool_explicit" else float(grp[0]["idle_threshold_ms"]))
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
        crs.append(statistics.mean(ratios))
        bs_vals.append(statistics.mean(r["byte_seconds_gib_s"] for r in grp))
        mf_vals.append(statistics.mean(r["minor_faults"] for r in grp) / 1000.0)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.8, 3.2))

    # Panel A: Empirical competitive ratio
    x = np.arange(len(order))
    cols = [C_BLUE if lab == "pool_auto_250" else (C_GRAY if lab in ("pool_explicit", "pool_retain") else C_GREEN) for lab in order]
    bars = ax1.bar(x, crs, width=0.58, color=cols, edgecolor="black", linewidth=0.5)
    ax1.axhline(2.0, color=C_VERM, linestyle="--", linewidth=1.3, label="Theorem 2 bound (2.00x)")
    for b, v in zip(bars, crs):
        ax1.text(b.get_x() + b.get_width() / 2, v + 0.05, f"{v:.2f}x", ha="center", va="bottom", fontsize=7.5)
    ax1.set_xticks(x)
    ax1.set_xticklabels(xlabels, fontsize=7.5)
    ax1.set_xlabel("Idle Threshold τ (ms)")
    ax1.set_ylabel("Empirical Competitive Ratio (ALG / OPT)")
    ax1.set_ylim(0, max(max(crs) * 1.18, 2.4))
    ax1.set_title("(a) Stochastic Ski-Rental Competitive Ratio")
    ax1.legend(frameon=False, loc="upper left", fontsize=7.5)
    ax1.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B: Byte-seconds vs minor faults Pareto curve
    ax2.plot(mf_vals, bs_vals, color=C_GRAY, linestyle="-", linewidth=1.0, zorder=1)
    for lab, mf, bs, col in zip(order, mf_vals, bs_vals, cols):
        ax2.scatter([mf], [bs], color=col, s=55 if lab == "pool_auto_250" else 38, edgecolor="black", zorder=3)
    short_names = ["τ=0", "50ms", "150ms", "τ*=250ms", "500ms", "1000ms", "Retain"]
    for name, mf, bs in zip(short_names, mf_vals, bs_vals):
        dy = 0.014 if name != "150ms" else -0.022
        ax2.text(mf + 0.4, bs + dy, name, fontsize=7.2)
    ax2.set_xlabel("Minor Page Faults (thousands)")
    ax2.set_ylabel("Memory-Time Integral (GiB·s)")
    ax2.set_xlim(-1.5, 34.5)
    ax2.set_ylim(0.71, 1.02)
    ax2.set_title("(b) Memory-Time vs. Refault Pareto Frontier")
    ax2.grid(True, linestyle=":", alpha=0.5)

    fig.tight_layout()
    save_fig(fig, "fig7_ski_rental_competitive")


def main():
    FIG_DIR.mkdir(parents=True, exist_ok=True)
    make_fig1()
    make_fig2()
    make_fig3()
    make_fig4()
    make_fig5()
    make_fig6()
    make_fig7()
    print("Generated all 7 PDF+PNG figure pairs cleanly.")


if __name__ == "__main__":
    main()
