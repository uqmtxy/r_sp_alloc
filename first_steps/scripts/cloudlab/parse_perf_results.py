#!/usr/bin/env python3
"""
Hardware PMU Perf Stat Log Parser
Parses perf stat output into structured markdown tables and CSV.
"""

import os
import re
import sys
import glob

def parse_perf_file(filepath):
    metrics = {}
    with open(filepath, 'r') as f:
        for line in f:
            # Pattern matching for perf stat outputs
            # e.g., "  1,234,567  L1-dcache-load-misses  #  12.34% of all L1-dcache hits"
            m = re.search(r'^\s*([0-9,.<>]+|\<not supported\>|\<not counted\>)\s+([a-zA-Z0-9_\-]+)', line)
            if m:
                val_str = m.group(1).replace(',', '')
                event = m.group(2)
                try:
                    metrics[event] = float(val_str)
                except ValueError:
                    metrics[event] = None
    return metrics

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    results_dir = os.path.join(script_dir, '../../bench/adversarial_yolo26_bench/results_perf')
    if not os.path.exists(results_dir):
        print(f"Results directory not found: {results_dir}")
        return

    files = glob.glob(os.path.join(results_dir, '*_perf.txt'))
    if not files:
        print(f"No perf log files found in {results_dir}")
        return

    data = {}
    for f in files:
        fname = os.path.basename(f)
        parts = fname.replace('_perf.txt', '').split('_')
        allocator = parts[0]
        adv = parts[1]
        metrics = parse_perf_file(f)
        if allocator not in data:
            data[allocator] = {}
        data[allocator][adv] = metrics

    print("\n# Hardware PMU Performance Counter Comparison (Bare-Metal)\n")
    print("| Allocator | Adv Level | L1-dcache Misses | LLC Misses | dTLB Misses | Instructions | IPC |")
    print("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")

    for alloc in sorted(data.keys()):
        for adv in ['adv0', 'adv3']:
            if adv in data[alloc]:
                m = data[alloc][adv]
                l1 = f"{m.get('L1-dcache-load-misses', 0):,.0f}" if m.get('L1-dcache-load-misses') else "N/A"
                llc = f"{m.get('LLC-load-misses', 0):,.0f}" if m.get('LLC-load-misses') else "N/A"
                dtlb = f"{m.get('dTLB-load-misses', 0):,.0f}" if m.get('dTLB-load-misses') else "N/A"
                inst = f"{m.get('instructions', 0):,.0f}" if m.get('instructions') else "N/A"
                cycles = m.get('cycles', 0)
                ipc = f"{(m.get('instructions', 0) / cycles):.2f}" if (cycles and m.get('instructions')) else "N/A"
                print(f"| {alloc} | {adv.upper()} | {l1} | {llc} | {dtlb} | {inst} | {ipc} |")

if __name__ == '__main__':
    main()
