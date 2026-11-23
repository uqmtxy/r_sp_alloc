#!/usr/bin/env python3
"""Generate publication-quality figures for the Graduate English assignment report
directly from validated November 2026 benchmark artifacts in first_steps/bench/utilization."""

import json
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
BOUNDED_DIR = ROOT / "first_steps" / "bench" / "utilization" / "results" / "bounded"
OUT_DIR = Path(__file__).resolve().parent / "figures"
OUT_DIR.mkdir(parents=True, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 9.2,
    "axes.titlesize": 10.2,
    "axes.titleweight": "bold",
    "axes.labelsize": 9.2,
    "xtick.labelsize": 8.2,
    "ytick.labelsize": 8.2,
    "legend.fontsize": 7.8,
    "figure.dpi": 300,
    "axes.spines.top": False,
    "axes.spines.right": False,
})

C_BLUE = "#1f6feb"
C_TEAL = "#0e8a7d"
C_ORANGE = "#d95f02"
C_RED = "#c92a2a"
C_SLATE = "#4a5568"
C_GREEN = "#1b7f3b"


def read_json(name: str) -> dict:
    return json.loads((BOUNDED_DIR / f"{name}.json").read_text())


def make_fig1_architecture():
    fig, (ax_top, ax_bot) = plt.subplots(
        2, 1, figsize=(11.2, 6.2), gridspec_kw={"height_ratios": [1.14, 1.04]}
    )
    fig.subplots_adjust(hspace=0.25)

    # --- TOP PANEL: Virtual Pool vs 4 KiB OS Page Boundaries & Hole Taxonomy ---
    ax = ax_top
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 50)
    ax.axis("off")
    ax.set_title(
        "A. PageReuseResource: Aligning Free-Hole Coalescing with 4 KiB OS Page Reclamation (Fistanto et al., PLOS '26)",
        loc="left", pad=6
    )

    pool_y = 22.0
    pool_h = 12.0
    page_w = 23.5
    start_x = 3.0

    # 4 pages from x=3.0 to x=97.0 (each 23.5 wide)
    # Page 0: 3.0 .. 26.5
    # Page 1: 26.5 .. 50.0
    # Page 2: 50.0 .. 73.5
    # Page 3: 73.5 .. 97.0
    segments = [
        (3.0, 8.8, "#2b6cb0", "Live Tensor A", "white"),
        (11.8, 4.4, "#f6ad55", "sub-\npage", "#1a202c"),
        (16.2, 5.5, "#2b6cb0", "Live B", "white"),
        (21.7, 4.8, "#fbd38d", "head\nhole", "#1a202c"),
        (26.5, 47.0, "#c6f6d5", "Coalesced Free Body Pages (2 × 4 KiB)\nEligible for MADV_DONTNEED (or retained budget)", "#1b4332"),
        (73.5, 6.2, "#fbd38d", "tail\nhole", "#1a202c"),
        (79.7, 11.8, "#2c5282", "Caller-Retained\nOutput C", "white"),
        (91.5, 5.5, "#fed7d7", "tail", "#742a2a"),
    ]
    for sx, sw, col, txt, tcol in segments:
        rect = patches.Rectangle((sx, pool_y), sw, pool_h, facecolor=col, edgecolor="#2d3748", linewidth=1.0)
        ax.add_patch(rect)
        ax.text(sx + sw / 2, pool_y + pool_h / 2, txt, ha="center", va="center",
                fontsize=7.3 if len(txt) < 25 else 8.1, color=tcol, fontweight="bold" if tcol == "white" else "normal")

    for idx in range(5):
        px = start_x + idx * page_w
        ax.plot([px, px], [pool_y - 2.0, pool_y + pool_h + 2.5], color="#1a202c", linestyle="--", linewidth=1.1)
        if idx < 4:
            ax.text(px + page_w / 2, pool_y + pool_h + 3.6, f"4 KiB OS Page {idx}",
                    ha="center", va="bottom", fontsize=8.2, fontweight="bold", color="#2d3748")

    # Left box: 2.0 to 47.5 (width 45.5)
    meta_box = patches.FancyBboxPatch(
        (2.0, 2.0), 45.5, 13.0, boxstyle="round,pad=0.6",
        facecolor="#ebf8ff", edgecolor="#3182ce", linewidth=1.2
    )
    ax.add_patch(meta_box)
    ax.text(3.3, 8.5,
            "Out-of-Band Metadata (outside purgeable mmap):\n"
            "• live_ map: {offset -> (req, 64B-rounded)}\n"
            "• free_ map: coalesced holes; mincore() checks\n"
            "  resident = payload + body + head + tail + sub",
            ha="left", va="center", fontsize=7.2, color="#1a365d", linespacing=1.42)

    # Right box: 50.5 to 98.0 (width 47.5)
    kern_box = patches.FancyBboxPatch(
        (50.5, 2.0), 47.5, 13.0, boxstyle="round,pad=0.6",
        facecolor="#f0fff4", edgecolor="#2f855a", linewidth=1.2
    )
    ax.add_patch(kern_box)
    ax.text(51.8, 8.5,
            "Safe Kernel Reclamation (purge / purge_to_budget):\n"
            "• Pages holding live or retained tensors (0 & 3)\n"
            "  are NEVER evicted.\n"
            "• Only excess free body pages -> MADV_DONTNEED",
            ha="left", va="center", fontsize=7.2, color="#1c4532", linespacing=1.42)

    ax.annotate("", xy=(61.0, 15.8), xytext=(50.0, 21.8),
                arrowprops=dict(arrowstyle="->", lw=1.6, color="#2f855a"))
    ax.text(56.5, 18.8, "excess body pages", fontsize=7.4, color="#2f855a", fontweight="bold")

    # --- BOTTOM PANEL: Policy Comparison Flow ---
    ax2 = ax_bot
    ax2.set_xlim(0, 100)
    ax2.set_ylim(0, 44)
    ax2.axis("off")
    ax2.set_title(
        "B. Evolution of Pause-Boundary Reclamation Across Recent Commits (YOLO26n FP32, Single Stream)",
        loc="left", pad=6
    )

    cards = [
        (1.5, 30.8, "#fff5f5", C_RED,
         "1. Default ORT BFC Arena",
         "• Retains all freed chunks forever\n"
         "• 0 purge calls; 2 minor faults\n"
         "• Idle private: 118.8 MiB (1-cam)\n"
         "• 4-cam live idle: 390.8 MiB"),
        (34.6, 30.8, "#fffaf0", C_ORANGE,
         "2. Full Purge (Commit c9828c5)",
         "• pool_idle (0 MiB) at 1 s pauses\n"
         "• Idle private: 34.3 MiB (-71.1%)\n"
         "• 27k–101k resume minor faults\n"
         "• Burst p99: +11.1% (FAILS gate)"),
        (67.7, 30.8, "#f0fff4", C_GREEN,
         "3. Bounded 24 MiB (Commit 1fd7ede)",
         "• purge_to_budget(24 MiB)\n"
         "• Idle private: 58.4 MiB (-50.8%)\n"
         "• -67% fewer faults vs 0 MiB\n"
         "• Burst p99 ratio: 1.0108 (PASS)"),
    ]
    for cx, cw, bg, edge, title, body in cards:
        box = patches.FancyBboxPatch(
            (cx, 3.0), cw, 34.0, boxstyle="round,pad=0.8",
            facecolor=bg, edgecolor=edge, linewidth=1.6
        )
        ax2.add_patch(box)
        ax2.text(cx + cw / 2, 33.2, title, ha="center", va="center",
                 fontsize=8.1, fontweight="bold", color=edge)
        ax2.plot([cx + 1.2, cx + cw - 1.2], [29.6, 29.6], color=edge, lw=0.8, alpha=0.5)
        ax2.text(cx + 1.5, 16.5, body, ha="left", va="center",
                 fontsize=7.6, color="#1a202c", linespacing=1.62)

    for ax_x in (32.7, 65.8):
        ax2.annotate("", xy=(ax_x + 1.6, 20.0), xytext=(ax_x - 0.2, 20.0),
                     arrowprops=dict(arrowstyle="->", lw=2.0, color=C_SLATE))

    fig.savefig(OUT_DIR / "fig1_architecture_holes.png", dpi=300, bbox_inches="tight")
    plt.close(fig)


def make_fig2_screening():
    budgets = [-1, 0, 8, 16, 24]
    labels = ["BFC\n(Baseline)", "0 MiB\n(Full Purge)", "8 MiB\nBudget", "16 MiB\nBudget", "24 MiB\n(Selected)"]
    data = [read_json(f"screen_{b}") for b in budgets]

    idle_mib = [d["idle_private_bytes"] / (1024 ** 2) for d in data]
    faults_k = [d["minor_faults"] / 1000.0 for d in data]
    p99_ms = [d["p99_ms"] for d in data]
    resume_ms = [d["resume_median_ms"] for d in data]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.0, 4.2))
    fig.subplots_adjust(wspace=0.38)

    x = np.arange(len(labels))
    bar_colors = [C_SLATE, C_ORANGE, "#6baed6", "#4292c6", C_TEAL]

    bars = ax1.bar(x, idle_mib, width=0.54, color=bar_colors, edgecolor="#1a202c", linewidth=0.8, zorder=3)
    bars[-1].set_edgecolor("#0b5345")
    bars[-1].set_linewidth(2.0)

    cutoff_50 = idle_mib[0] * 0.5
    ax1.axhline(cutoff_50, color=C_RED, linestyle="--", linewidth=1.2, zorder=4,
                label=f"50% BFC Gate ({cutoff_50:.2f} MiB)")
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels)
    ax1.set_ylabel("Idle Private Memory (MiB)")
    ax1.set_ylim(0, 145)
    ax1.grid(axis="y", linestyle=":", alpha=0.5, zorder=0)
    ax1.set_title("A. Idle Memory vs. Page Faults (Screening)", loc="left", pad=8)

    for idx, val in enumerate(idle_mib):
        ax1.text(idx, val + 2.5, f"{val:.1f}", ha="center", va="bottom",
                 fontsize=7.9, fontweight="bold", color="#1a202c", zorder=6)

    ax1_r = ax1.twinx()
    ax1_r.spines["top"].set_visible(False)
    ax1_r.plot(x, faults_k, color=C_RED, marker="o", linewidth=2.0, markersize=6, zorder=5,
               label="Minor Faults (×1,000)")
    ax1_r.set_ylabel("Minor Page Faults (Thousands)", color=C_RED)
    ax1_r.tick_params(axis="y", labelcolor=C_RED)
    ax1_r.set_ylim(-8, 125)

    for idx, fk in enumerate(faults_k):
        txt = f"{int(round(fk * 1000)):,}" if idx == 0 else f"{fk:.1f}k"
        if idx == 4:
            # Place below marker cleanly so it never collides with 58.3 MiB bar label
            y_pos = fk - 7.5
            va = "top"
        elif idx == 0:
            y_pos = fk + 4.5
            va = "bottom"
        else:
            y_pos = fk + 4.8
            va = "bottom"
        ax1_r.text(
            idx, y_pos, txt, ha="center", va=va, fontsize=7.6,
            color=C_RED, fontweight="bold", zorder=7,
            bbox=dict(boxstyle="round,pad=0.14", facecolor="white", edgecolor="none", alpha=0.88)
        )

    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax1_r.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="upper right", frameon=True, facecolor="white", framealpha=0.92)

    w = 0.36
    b1 = ax2.bar(x - w / 2, p99_ms, width=w, color=C_BLUE, edgecolor="#1a202c", linewidth=0.7,
                 label="Frame p99 Latency (ms)", zorder=3)
    b2 = ax2.bar(x + w / 2, resume_ms, width=w, color="#9ecae1", edgecolor="#1a202c", linewidth=0.7,
                 label="Burst-Resume Median (ms)", zorder=3)

    gate_105 = p99_ms[0] * 1.05
    ax2.axhline(gate_105, color=C_RED, linestyle="--", linewidth=1.2, zorder=4,
                label=f"+5% BFC p99 Margin ({gate_105:.1f} ms)")
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels)
    ax2.set_ylabel("Latency (ms)  [y-axis zoomed: 260–336]")
    ax2.set_ylim(260, 336)
    ax2.grid(axis="y", linestyle=":", alpha=0.5, zorder=0)
    ax2.set_title("B. Tail & Resume Latency Across Retention Budgets", loc="left", pad=8)

    for rect in b1:
        h = rect.get_height()
        ax2.text(rect.get_x() + rect.get_width() / 2, h + 1.2, f"{h:.1f}",
                 ha="center", va="bottom", fontsize=7.0, fontweight="bold", color=C_BLUE)
    for rect in b2:
        h = rect.get_height()
        ax2.text(rect.get_x() + rect.get_width() / 2, h - 2.2, f"{h:.1f}",
                 ha="center", va="top", fontsize=6.9, fontweight="bold", color="#1a365d")

    ax2.legend(loc="upper right", frameon=True, facecolor="white", framealpha=0.92)

    fig.savefig(OUT_DIR / "fig2_bounded_screening.png", dpi=300, bbox_inches="tight")
    plt.close(fig)


def make_fig3_confirmation_and_endurance():
    budget = int((BOUNDED_DIR / "selection.txt").read_text().strip())

    metrics_spec = [
        ("Cont.: Active FPS", 0, "fps", "ge", 0.97),
        ("Cont.: p99 Latency", 0, "p99_ms", None, None),
        ("Burst (10f): Active FPS", 1000, "fps", None, None),
        ("Burst (10f): p99 Latency", 1000, "p99_ms", "le", 1.05),
        ("Burst (10f): Resume Median", 1000, "resume_median_ms", None, None),
        ("Burst (10f): Idle Private RAM", 1000, "idle_private_bytes", "le", 0.50),
        ("Burst (10f): Est. Byte-Seconds", 1000, "estimated_private_byte_seconds", "lt", 1.00),
        ("Sensit. (30f): p99 Latency", "orig", "p99_ms", "le", 1.05),
        ("Sensit. (30f): Idle Private RAM", "orig", "idle_private_bytes", "le", 0.50),
    ]

    labels, all_ratios, meds, mins, maxs, gates = [], [], [], [], [], []
    for lbl, pause, key, op, thresh in metrics_spec:
        if pause == "orig":
            rs = [read_json(f"original_{i}_{budget}")[key] / read_json(f"original_{i}_-1")[key] for i in range(5)]
        else:
            rs = [read_json(f"confirm_{pause}_{i}_{budget}")[key] / read_json(f"confirm_{pause}_{i}_-1")[key] for i in range(5)]
        labels.append(lbl)
        all_ratios.append(rs)
        meds.append(median(rs))
        mins.append(min(rs))
        maxs.append(max(rs))
        gates.append((op, thresh))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.4, 4.7), gridspec_kw={"width_ratios": [1.38, 1.0]})
    fig.subplots_adjust(wspace=0.33, bottom=0.18)

    y = np.arange(len(labels))[::-1]
    ax1.axvline(1.0, color="#718096", linestyle="-", linewidth=1.0, zorder=1, label="BFC Parity (1.00)")

    for idx, (yi, rs, med, mn, mx, (op, thr)) in enumerate(zip(y, all_ratios, meds, mins, maxs, gates)):
        ax1.plot([mn, mx], [yi, yi], color="#a0aec0", linewidth=2.2, zorder=2)
        ax1.scatter(rs, [yi] * len(rs), color=C_BLUE, alpha=0.65, s=26, zorder=3,
                    label="Individual Pair (n=5)" if idx == 0 else "")
        ax1.scatter([med], [yi], color=C_TEAL, marker="D", s=46, edgecolor="#0b3b34", zorder=4,
                    label="Median Ratio" if idx == 0 else "")
        ax1.text(max(mx, med) + 0.022, yi, f"{med:.4f}", va="center", ha="left",
                 fontsize=7.8, fontweight="bold", color="#1a202c")
        if thr is not None:
            ax1.plot([thr, thr], [yi - 0.34, yi + 0.34], color=C_RED, linestyle="--", linewidth=1.4, zorder=3,
                     label="Prespecified Gate" if idx == 0 else "")

    ax1.set_yticks(y)
    ax1.set_yticklabels(labels)
    ax1.set_xlim(0.40, 1.44)
    ax1.set_xlabel("Candidate (24 MiB) / BFC Paired Ratio")
    ax1.grid(axis="x", linestyle=":", alpha=0.5)
    ax1.set_title("A. Confirmed Paired Ratios Across 5 Trials", loc="left", pad=8)
    ax1.legend(loc="upper center", bbox_to_anchor=(0.48, -0.15), ncol=2,
               fontsize=7.6, frameon=True, facecolor="white")

    # Right panel: 1,000-Frame Endurance (100 scheduled pauses)
    e_bfc = read_json("endurance_-1")
    e_24 = read_json(f"endurance_{budget}")

    e_labels = ["Idle Private\nRAM", "Est. Private\nByte-Sec", "Active\nFPS", "p99\nLatency"]
    bfc_raw = [
        f"{e_bfc['idle_private_bytes'] / (1024**2):.1f}\nMiB",
        f"{e_bfc['estimated_private_byte_seconds'] / 1e9:.2f}\nGB·s",
        f"{e_bfc['fps']:.3f}",
        f"{e_bfc['p99_ms']:.1f}\nms",
    ]
    c24_raw = [
        f"{e_24['idle_private_bytes'] / (1024**2):.1f}\nMiB",
        f"{e_24['estimated_private_byte_seconds'] / 1e9:.2f}\nGB·s",
        f"{e_24['fps']:.3f}",
        f"{e_24['p99_ms']:.1f}\nms",
    ]
    c24_pct = [
        100.0 * e_24["idle_private_bytes"] / e_bfc["idle_private_bytes"],
        100.0 * e_24["estimated_private_byte_seconds"] / e_bfc["estimated_private_byte_seconds"],
        100.0 * e_24["fps"] / e_bfc["fps"],
        100.0 * e_24["p99_ms"] / e_bfc["p99_ms"],
    ]
    bfc_pct = [100.0] * 4

    ex = np.arange(len(e_labels))
    w = 0.40
    b_bfc = ax2.bar(ex - w / 2, bfc_pct, width=w, color=C_SLATE, edgecolor="#1a202c", linewidth=0.7,
                    label="BFC (100%)", zorder=3)
    b_c24 = ax2.bar(ex + w / 2, c24_pct, width=w, color=C_TEAL, edgecolor="#1a202c", linewidth=0.7,
                    label="24 MiB Bounded", zorder=3)

    ax2.axhline(100.0, color="#718096", linestyle=":", linewidth=1.0, zorder=1)
    ax2.set_xticks(ex)
    ax2.set_xticklabels(e_labels)
    ax2.set_ylabel("Percentage Relative to BFC (%)")
    ax2.set_ylim(0, 128)
    ax2.grid(axis="y", linestyle=":", alpha=0.5, zorder=0)
    ax2.set_title("B. 1,000-Frame Endurance (100 Pauses)", loc="left", pad=8)

    for rect, raw in zip(b_bfc, bfc_raw):
        ax2.text(rect.get_x() + rect.get_width() / 2, 24.0, raw,
                 ha="center", va="center", fontsize=6.8, color="white", fontweight="bold", zorder=5)
    for rect, pct, raw in zip(b_c24, c24_pct, c24_raw):
        ax2.text(rect.get_x() + rect.get_width() / 2, rect.get_height() + 2.2,
                 f"{raw}\n({pct:.1f}%)", ha="center", va="bottom", fontsize=6.7,
                 fontweight="bold", color="#0b5345", zorder=5)

    ax2.legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), ncol=2,
               fontsize=7.6, frameon=True, facecolor="white")

    fig.savefig(OUT_DIR / "fig3_confirmation_endurance.png", dpi=300, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    make_fig1_architecture()
    make_fig2_screening()
    make_fig3_confirmation_and_endurance()
    print("Regenerated figures in", OUT_DIR)
