#!/usr/bin/env python3
"""
Publication Figure Generator for ACM EuroSys / ISMM / USENIX ATC
Generates publication-quality vector charts (PDF + high-res PNG).
"""

import os
import json
import numpy as np
import matplotlib.pyplot as plt

# Matplotlib publication formatting
plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': ['Helvetica', 'DejaVu Sans', 'Arial'],
    'font.size': 10,
    'axes.labelsize': 11,
    'axes.titlesize': 12,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'legend.fontsize': 9,
    'figure.titlesize': 13,
    'pdf.fonttype': 42,
    'ps.fonttype': 42,
    'grid.alpha': 0.3,
    'grid.linestyle': '--'
})

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

# Color palette: ColorBrewer / Accessible Systems Palette
PALETTE = {
    'glibc': '#7f7f7f',       # Grey
    'jemalloc': '#d62728',    # Red
    'tcmalloc': '#ff7f0e',    # Orange
    'mimalloc': '#9467bd',    # Purple
    'dynalloc': '#1f77b4',    # Strong Blue (Our Work)
    'fd_arena': '#2ca02c',    # Emerald Green (Dedicated Arena)
}

def generate_figure1_adversarial_rss():
    """Fig 1: Physical Memory Inflation under Adversarial Heap Fragmentation (Single Stream)"""
    fig, ax = plt.subplots(figsize=(7.5, 3.8))
    
    allocators = ['glibc', 'jemalloc', 'tcmalloc', 'mimalloc', 'dynalloc', 'fd_arena']
    labels = ['glibc', 'jemalloc 5.3', 'TCMalloc', 'mimalloc 2.1', 'DynAlloc (Ours)', 'FD-Arena (Ours)']
    
    # Real empirical data from ADVERSARIAL_ANALYSIS.md
    adv0_rss = [92.2, 108.0, 104.2, 104.1, 91.5, 100.1]
    adv3_rss = [100.8, 112.5, 108.5, 115.8, 96.0, 108.6]
    adv10_rss = [112.5, 129.8, 122.5, 127.8, 109.9, 119.5]
    
    x = np.arange(len(allocators))
    width = 0.25
    
    r1 = ax.bar(x - width, adv0_rss, width, label='Adv0 (Clean Heap)', color='#aec7e8', edgecolor='black', linewidth=0.5)
    r2 = ax.bar(x, adv3_rss, width, label='Adv3 (ISMM \'24 Standard)', color='#1f77b4', edgecolor='black', linewidth=0.5)
    r3 = ax.bar(x + width, adv10_rss, width, label='Adv10 (High Fragmentation)', color='#d62728', edgecolor='black', linewidth=0.5)
    
    ax.set_ylabel('Peak Physical Memory (RSS in MB)')
    ax.set_title('Figure 1: Physical RAM Inflation Under Adversarial Heap Fragmentation (YOLO26n)')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15, ha='right')
    ax.legend(loc='upper left', framealpha=0.9)
    ax.grid(axis='y')
    ax.set_ylim(0, 150)
    
    # Add savings annotation
    ax.annotate('DynAlloc saves 20.0 MB\nvs jemalloc under Adv10', 
                xy=(4 + width, 109.9), xytext=(3.2, 135),
                arrowprops=dict(facecolor='black', shrink=0.05, width=1, headwidth=5),
                fontsize=8.5, bbox=dict(boxstyle="round,pad=0.3", fc="#e6f2ff", ec="#1f77b4", lw=1))
    
    plt.tight_layout()
    pdf_path = os.path.join(OUTPUT_DIR, 'fig1_adversarial_rss_bloat.pdf')
    png_path = os.path.join(OUTPUT_DIR, 'fig1_adversarial_rss_bloat.png')
    plt.savefig(pdf_path, dpi=300)
    plt.savefig(png_path, dpi=300)
    plt.close()
    print(f"Generated: {pdf_path}")

def generate_figure2_tail_latency():
    """Fig 2: Tail Latency (p99) Degradation across Adversarial Fragmentation Levels"""
    fig, ax = plt.subplots(figsize=(7.0, 3.8))
    
    levels = ['Adv0 (Clean)', 'Adv1 (Light)', 'Adv3 (Standard)', 'Adv10 (Stress)']
    x = np.arange(len(levels))
    
    # Real empirical p99 latencies (ms) from ADVERSARIAL_ANALYSIS.md
    glibc_p99 = [85.87, 86.35, 86.86, 86.50]
    jemalloc_p99 = [85.23, 86.02, 86.15, 84.75]
    mimalloc_p99 = [84.60, 86.82, 86.25, 87.47]
    dynalloc_p99 = [87.38, 84.86, 85.41, 85.94]
    arena_p99 = [88.60, 85.76, 85.77, 87.77]
    
    ax.plot(x, glibc_p99, marker='s', label='glibc default', color=PALETTE['glibc'], linewidth=1.5)
    ax.plot(x, jemalloc_p99, marker='^', label='jemalloc 5.3', color=PALETTE['jemalloc'], linewidth=1.5)
    ax.plot(x, mimalloc_p99, marker='D', label='mimalloc 2.1', color=PALETTE['mimalloc'], linewidth=1.5)
    ax.plot(x, dynalloc_p99, marker='o', label='DynAlloc (Ours)', color=PALETTE['dynalloc'], linewidth=2.5)
    ax.plot(x, arena_p99, marker='*', markersize=8, label='FD-Arena (Ours)', color=PALETTE['fd_arena'], linewidth=2.0, linestyle='--')
    
    ax.set_ylabel('99th Percentile Latency (p99 in ms)')
    ax.set_title('Figure 2: Tail Latency (p99) Stability Across Fragmentation Levels')
    ax.set_xticks(x)
    ax.set_xticklabels(levels)
    ax.legend(loc='lower left', framealpha=0.9)
    ax.grid(True)
    ax.set_ylim(83.5, 90.0)
    
    plt.tight_layout()
    pdf_path = os.path.join(OUTPUT_DIR, 'fig2_tail_latency_jitter.pdf')
    png_path = os.path.join(OUTPUT_DIR, 'fig2_tail_latency_jitter.png')
    plt.savefig(pdf_path, dpi=300)
    plt.savefig(png_path, dpi=300)
    plt.close()
    print(f"Generated: {pdf_path}")

def generate_figure3_multicamera_memory():
    """Fig 3: Active Peak RSS vs Settled RSS at Rest across 4 Concurrent Cameras"""
    fig, ax = plt.subplots(figsize=(7.5, 3.8))
    
    allocators = ['glibc', 'jemalloc (10s)', 'jemalloc (ms:0)', 'TCMalloc', 'mimalloc', 'DynAlloc (Ours)']
    peak_rss = [309.4, 408.9, 358.5, 345.5, 387.7, 305.6]
    settled_rss = [72.0, 48.7, 45.8, 345.5, 342.7, 33.8]
    
    x = np.arange(len(allocators))
    width = 0.35
    
    r1 = ax.bar(x - width/2, peak_rss, width, label='Active Peak RSS (4 Cameras)', color='#1f77b4', edgecolor='black', linewidth=0.5)
    r2 = ax.bar(x + width/2, settled_rss, width, label='Settled RSS (At Rest)', color='#ff7f0e', edgecolor='black', linewidth=0.5)
    
    ax.set_ylabel('Physical Memory (RSS in MB)')
    ax.set_title('Figure 3: Multi-Camera Memory Scaling & Post-Inference Hoarding')
    ax.set_xticks(x)
    ax.set_xticklabels(allocators, rotation=15, ha='right')
    ax.legend(loc='upper right', framealpha=0.9)
    ax.grid(axis='y')
    ax.set_ylim(0, 470)
    
    # Annotate permanent retention
    ax.annotate('TCMalloc & mimalloc permanently\nretain 340+ MB in user space', 
                xy=(3.5, 345), xytext=(2.2, 410),
                arrowprops=dict(facecolor='black', shrink=0.05, width=1, headwidth=5),
                fontsize=8.5, bbox=dict(boxstyle="round,pad=0.3", fc="#fff2e6", ec="#ff7f0e", lw=1))
    
    plt.tight_layout()
    pdf_path = os.path.join(OUTPUT_DIR, 'fig3_multicamera_retention.pdf')
    png_path = os.path.join(OUTPUT_DIR, 'fig3_multicamera_retention.png')
    plt.savefig(pdf_path, dpi=300)
    plt.savefig(png_path, dpi=300)
    plt.close()
    print(f"Generated: {pdf_path}")

def generate_figure4_plos_trapped_bytes():
    """Fig 4: Granularity Mismatch Trapped DRAM Breakdown (PLOS '23)"""
    fig, ax = plt.subplots(figsize=(7.0, 3.8))
    
    categories = ['Power-of-Two Bins', 'Generic Size Classes', 'Page-Grid Slabs', 'Page-Aware Arena']
    
    # In KB of trapped memory per 100 MB active allocation
    subpage_holes = [18400, 11200, 180, 0]
    head_tail_holes = [12600, 8900, 120, 0]
    reclaimable_clean = [69000, 79900, 99700, 100000]
    
    x = np.arange(len(categories))
    width = 0.45
    
    p1 = ax.bar(x, subpage_holes, width, label='Trapped Sub-Page Holes', color='#d62728', edgecolor='black', linewidth=0.5)
    p2 = ax.bar(x, head_tail_holes, width, bottom=subpage_holes, label='Trapped Head/Tail Page Holes', color='#ff7f0e', edgecolor='black', linewidth=0.5)
    
    ax.set_ylabel('Trapped Unreturnable Memory (KB)')
    ax.set_title('Figure 4: Granularity Mismatch Trapped DRAM (PLOS \'23 Replication)')
    ax.set_xticks(x)
    ax.set_xticklabels(categories)
    ax.legend(loc='upper right', framealpha=0.9)
    ax.grid(axis='y')
    ax.set_ylim(0, 35000)
    
    ax.annotate('0.0 KB Trapped DRAM\nby Page-Aware Construction', 
                xy=(3, 0), xytext=(2.2, 10000),
                arrowprops=dict(facecolor='black', shrink=0.05, width=1, headwidth=5),
                fontsize=8.5, bbox=dict(boxstyle="round,pad=0.3", fc="#e6ffe6", ec="#2ca02c", lw=1))
    
    plt.tight_layout()
    pdf_path = os.path.join(OUTPUT_DIR, 'fig4_plos_trapped_bytes.pdf')
    png_path = os.path.join(OUTPUT_DIR, 'fig4_plos_trapped_bytes.png')
    plt.savefig(pdf_path, dpi=300)
    plt.savefig(png_path, dpi=300)
    plt.close()
    print(f"Generated: {pdf_path}")

def generate_figure5_pare_ta_tradeoffs():
    """Fig 5: PARE-TA Throughput Speedup & 70.1% Memory Footprint Reduction"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.8))
    
    # 1. Left: Throughput across fragmentation levels (FPS)
    levels = ['Adv0 (Clean)', 'Adv3 (Standard)', 'Adv10 (Stress)']
    x = np.arange(len(levels))
    
    malloc_fps = [534.5, 493.0, 483.0]       # in thousands of FPS (kFPS)
    raw_pmr_fps = [7612.4, 7490.4, 7787.7]
    pare_ta_fps = [6856.8, 4312.2, 3922.1]
    
    ax1.plot(x, malloc_fps, marker='s', label='Generic malloc', color='#7f7f7f', linewidth=1.5)
    ax1.plot(x, raw_pmr_fps, marker='^', label='Raw std::pmr', color='#ff7f0e', linewidth=1.5, linestyle=':')
    ax1.plot(x, pare_ta_fps, marker='o', label='PARE-TA (Ours)', color='#1f77b4', linewidth=2.5)
    
    ax1.set_ylabel('Throughput (kFPS, Log Scale)')
    ax1.set_yscale('log')
    ax1.set_title('(a) Allocation Throughput Under Fragmentation')
    ax1.set_xticks(x)
    ax1.set_xticklabels(levels, fontsize=8.5)
    ax1.legend(loc='lower left', framealpha=0.9, fontsize=8.5)
    ax1.grid(True)
    
    # 2. Right: Peak RSS Footprint under Adv10 Stress
    configs = ['Generic malloc', 'Raw std::pmr', 'PARE-TA (Ours)']
    peak_rss = [27.20, 89.70, 26.80]
    colors = ['#7f7f7f', '#ff7f0e', '#1f77b4']
    
    bars = ax2.bar(configs, peak_rss, color=colors, width=0.45, edgecolor='black', linewidth=0.5)
    ax2.set_ylabel('Peak Physical Memory (RSS in MB)')
    ax2.set_title('(b) Peak DRAM Footprint under Adv10 Stress')
    ax2.grid(axis='y')
    ax2.set_ylim(0, 110)
    
    # Annotate 70.1% memory reduction
    ax2.annotate('PARE-TA cuts 62.9 MB (-70.1%)\nvs Raw PMR via Page-Grid', 
                 xy=(2, 26.80), xytext=(0.8, 70),
                 arrowprops=dict(facecolor='black', shrink=0.05, width=1, headwidth=5),
                 fontsize=8.0, bbox=dict(boxstyle="round,pad=0.3", fc="#e6f2ff", ec="#1f77b4", lw=1))
    
    plt.tight_layout()
    pdf_path = os.path.join(OUTPUT_DIR, 'fig5_pare_ta_tradeoffs.pdf')
    png_path = os.path.join(OUTPUT_DIR, 'fig5_pare_ta_tradeoffs.png')
    plt.savefig(pdf_path, dpi=300)
    plt.savefig(png_path, dpi=300)
    plt.close()
    print(f"Generated: {pdf_path}")

if __name__ == '__main__':
    print("Generating ACM publication vector charts...")
    generate_figure1_adversarial_rss()
    generate_figure2_tail_latency()
    generate_figure3_multicamera_memory()
    generate_figure4_plos_trapped_bytes()
    generate_figure5_pare_ta_tradeoffs()
    print("All vector figures successfully generated in PDF and PNG.")
