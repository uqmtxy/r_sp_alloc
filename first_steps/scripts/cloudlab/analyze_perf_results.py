#!/usr/bin/env python3
"""Summarize measured CloudLab baremetal telemetry cleanly and accurately."""
import json
import re
from pathlib import Path


def parse_perf_txt(file_path):
    metrics = {}
    path = Path(file_path)
    if path.exists():
        for line in path.read_text().splitlines():
            match = re.match(r"^\s*([\d,]+)\s+([a-zA-Z0-9_.-]+)", line)
            if match:
                metrics[match[2]] = int(match[1].replace(',', ''))
    return metrics


def ratio(metrics, numerator, denominator, scale=1):
    if numerator not in metrics or not metrics.get(denominator):
        return 'N/A'
    return f'{scale * metrics[numerator] / metrics[denominator]:.3f}'


def value(data, key, digits=2):
    return f'{data[key]:.{digits}f}' if key in data else 'N/A'


def format_table(header, rows):
    col_widths = [len(h) for h in header]
    for row in rows:
        for i, cell in enumerate(row):
            if i < len(col_widths):
                col_widths[i] = max(col_widths[i], len(str(cell)))
            else:
                col_widths.append(len(str(cell)))

    def fmt_row(items):
        return '| ' + ' | '.join(str(item).rjust(col_widths[i]) if i > 0 else str(item).ljust(col_widths[i]) for i, item in enumerate(items)) + ' |'

    def fmt_sep():
        return '|' + '|'.join('-' * (w + 2) for w in col_widths) + '|'

    lines = [fmt_row(header), fmt_sep()]
    for r in rows:
        lines.append(fmt_row(r))
    return lines


def hypothesis_rows(results, multi_dir):
    def load(directory, name):
        return json.loads((directory / f'{name}.json').read_text())

    try:
        dyn = load(results, 'dynalloc')
        pare = load(results, 'pare_ta')
        direct_pare = load(results, 'direct_pare_ta')
        direct = [load(results, name) for name in ('direct_glibc', 'direct_jemalloc')]
        l3 = []
        for name in ('pare_ta', 'direct_glibc', 'direct_jemalloc'):
            metrics = parse_perf_txt(results / f'perf_l3_{name}.txt')
            hits = metrics['mem_load_uops_retired.l3_hit']
            misses = metrics['mem_load_uops_retired.l3_miss']
            l3.append(100 * misses / (hits + misses))
        better = all(direct_pare['achieved_fps'] > d['achieved_fps'] and
                     direct_pare['latency']['p99_ms'] < d['latency']['p99_ms']
                     for d in direct) and all(l3[0] < pct for pct in l3[1:])
        multi = [load(multi_dir, f'{name}_adv10') for name in ('pare_ta', 'tcmalloc', 'mimalloc')]
    except (OSError, ValueError, KeyError, ZeroDivisionError):
        return [['Evaluation', 'INCOMPLETE', 'Missing or invalid required measurements.']]

    return [
        ['DynAlloc steady minor faults <= 5', 'PASS' if dyn['minor_faults'] <= 5 else 'FAIL', str(dyn['minor_faults'])],
        ['DynAlloc throughput >= 6.70 FPS', 'PASS' if dyn['achieved_fps'] >= 6.70 else 'FAIL', f"{dyn['achieved_fps']:.5f} FPS"],
        ['PARE-TA steady minor faults = 0', 'PASS' if pare['minor_faults'] == 0 else 'FAIL', str(pare['minor_faults'])],
        ['PARE-TA trapped DRAM < 4 KiB; zero body holes', 'UNVERIFIED', 'Inference field is hardcoded; no physical-hole measurement.'],
        ['PARE-TA beats direct glibc/jemalloc in FPS, p99, L3 miss %', 'PASS' if better else 'FAIL',
         f"Retired L3 miss %: PARE-TA {l3[0]:.3f}, direct glibc {l3[1]:.3f}, direct jemalloc {l3[2]:.3f}."],
        ['Adv10 bounded live PARE-TA DRAM; TCMalloc/mimalloc retain >400 MiB', 'PARTIAL',
         'Post-teardown settled MiB (PARE-TA/TCMalloc/mimalloc): ' +
         '/'.join(f"{d['settled_rss_mb']:.2f}" for d in multi) +
         '; live bound not measured, mimalloc >400 MiB not reproduced.'],
    ]


def main():
    bench = Path(__file__).resolve().parents[2] / 'bench'
    results = bench / 'yolo26n_pipeline/results_baremetal'
    results.mkdir(parents=True, exist_ok=True)

    lines = [
        '# CloudLab Bare-Metal YOLO26n Evaluation Report',
        '',
        'Target Hardware: CloudLab Utah m510 (Intel Xeon D-1548, 8 cores / 16 threads, 64 GB DRAM).',
        'Workload: Ultralytics YOLO26n ONNX Runtime C++ Pipeline (640x640, FP32).',
        'All single-stream runs executed on pinned logical CPU 2 (1000 measured frames + 50 warmup frames).',
        '',
        '## 1. Primary ONNX Runtime Inference Evaluation',
        '',
        'Full-process perf counts include startup and warmup; JSON minor faults measure post-warmup steady state.',
        'Memory units are MiB. N/A means metric was uncounted or denominator was zero.',
        ''
    ]

    header_primary = [
        'Allocator / Mode', 'Mode', 'Peak RSS', 'Settled RSS', 'Priv Dirty', 'IPC',
        'L1 Miss %', 'dTLB Load Miss %', 'Minor Faults (Steady)', 'p99 ms', 'FPS'
    ]

    all_cfgs = [
        'glibc_default', 'glibc_tuned', 'jemalloc_default', 'jemalloc_tuned',
        'tcmalloc', 'mimalloc', 'dynalloc', 'pare_ta',
        'direct_glibc', 'direct_jemalloc', 'direct_tcmalloc', 'direct_mimalloc',
        'direct_dynalloc', 'direct_pare_ta'
    ]

    rows_primary = []
    for cfg in all_cfgs:
        path = results / f'{cfg}.json'
        if not path.exists():
            continue
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            rows_primary.append([cfg, 'error', 'incomplete'] + ['N/A'] * (len(header_primary) - 3))
            continue

        perf = parse_perf_txt(results / f'perf_{cfg}.txt')
        mode = data.get('mode', 'bfc_arena')

        row = [
            cfg,
            mode,
            value(data, 'peak_rss_mb'),
            value(data, 'settled_rss_mb'),
            value(data, 'private_dirty_mb'),
            ratio(perf, 'instructions', 'cycles'),
            ratio(perf, 'L1-dcache-load-misses', 'L1-dcache-loads', 100),
            ratio(perf, 'dTLB-load-misses', 'dTLB-loads', 100),
            str(data.get('minor_faults', 'N/A')),
            value(data.get('latency', {}), 'p99_ms'),
            value(data, 'achieved_fps')
        ]
        rows_primary.append(row)

    if rows_primary:
        lines.extend(format_table(header_primary, rows_primary))
    else:
        lines.append('No primary inference results found yet.')

    # 2. Retired L3 Cache Validation
    lines += [
        '',
        '## 2. Model-Specific Retired-Load L3 Cache Validation',
        '',
        'Uses grouped un-multiplexed events `{mem_load_uops_retired.l3_hit, mem_load_uops_retired.l3_miss}`.',
        'Miss % is computed as `misses / (hits + misses) * 100`.',
        ''
    ]

    header_l3 = ['Allocator / Mode', 'Retired L3 Hits', 'Retired L3 Misses', 'Retired L3 Miss %']
    rows_l3 = []
    for cfg in all_cfgs:
        perf = parse_perf_txt(results / f'perf_l3_{cfg}.txt')
        hits = perf.get('mem_load_uops_retired.l3_hit')
        misses = perf.get('mem_load_uops_retired.l3_miss')
        if hits is None or misses is None:
            continue
        total = hits + misses
        pct = f'{100.0 * misses / total:.3f}' if total > 0 else 'N/A'
        rows_l3.append([cfg, f'{hits:,}', f'{misses:,}', pct])

    if rows_l3:
        lines.extend(format_table(header_l3, rows_l3))
    else:
        lines.append('No retired L3 validation logs found.')

    # 3. PARE-TA Deep Telemetry
    lines += [
        '',
        '## 3. PARE-TA Custom Arena Architecture Telemetry',
        '',
        'Page-Aligned Region-Enclosed Tensor Arena runtime counters under ONNX Runtime inference.',
        ''
    ]
    pare_json = results / 'pare_ta.json'
    if pare_json.exists():
        try:
            pdata = json.loads(pare_json.read_text())
            lines += [
                f"- **Model**: YOLO26n ONNX (640x640 FP32)",
                f"- **Persistent Weights Arena**: {pdata.get('persistent_allocated_bytes', 0) / (1024*1024):.2f} MiB",
                f"- **Peak Transient Frame Activation**: {pdata.get('transient_peak_frame_bytes', 0) / (1024*1024):.2f} MiB",
                f"- **Total Allocations Handled**: {pdata.get('total_allocations', 0):,}",
                f"- **Eliminated Individual Frees (O(1) No-ops)**: {pdata.get('noop_deallocations', 0):,}",
                f"- **Reported Trapped DRAM**: {pdata.get('trapped_dram_kb', 0.0)} KiB (hardcoded by the harness; not a physical-hole measurement)",
                f"- **Post-Warmup Steady-State Minor Page Faults**: {pdata.get('minor_faults', 0)}"
            ]
        except Exception:
            lines.append('Error parsing PARE-TA telemetry.')
    else:
        lines.append('PARE-TA full inference run has not executed yet.')

    # 4. Multi-Camera Concurrency
    lines += [
        '',
        '## 4. Multi-Camera Concurrent Stream Evaluation',
        '',
        '4 Concurrent Camera Streams executing YOLO26n on pinned logical CPUs 0–3 under adversarial heap fragmentation.',
        ''
    ]

    header_multi = ['Configuration', 'Adv Level', 'Aggregate FPS', 'p99 ms', 'Peak RSS MiB', 'Settled RSS MiB', 'Priv Dirty MiB', 'Minor Faults']
    rows_multi = []
    multi_dir = bench / 'multi_camera_adversarial/results'
    if multi_dir.exists():
        for path in sorted(multi_dir.glob('*.json')):
            try:
                data = json.loads(path.read_text())
                rows_multi.append([
                    path.stem,
                    str(data.get('adv_level', 0)),
                    value(data, 'aggregate_fps'),
                    value(data, 'latency_p99_ms'),
                    value(data, 'peak_rss_mb'),
                    value(data, 'settled_rss_mb'),
                    value(data, 'private_dirty_mb'),
                    str(data.get('minor_faults', 'N/A'))
                ])
            except Exception:
                continue

    if rows_multi:
        lines.extend(format_table(header_multi, rows_multi))
    else:
        lines.append('No multi-camera results found.')

    lines += ['', '## 5. Hypothesis Verification', '']
    lines.extend(format_table(['Criterion', 'Status', 'Evidence'], hypothesis_rows(results, multi_dir)))
    lines += [
        '',
        'The performance criterion uses direct_pare_ta primary FPS/p99 and the pare_ta grouped L3 pass (both use native pare_ta mode). These are separate process runs.',
        '',
        'Standalone arena results are supplemental endpoint-touch microbenchmarks; they do not replace full-inference physical-hole validation.',
        '',
        '## 6. Measurement Limits',
        '',
        '- CPU affinity pins single-stream inference to logical CPU 2 and camera workers to CPUs 0–3; affinity alone does not provide kernel or IRQ isolation.',
        '- Primary perf events are collected together and may be multiplexed. Consult raw perf logs for running percentages; ratios across differently scheduled events need caution.',
        '- Retired L3 events are grouped in a separate pass and include initialization and warmup. They do not isolate steady-state tensor accesses.',
        '- The inference harness emits a literal zero for trapped_dram_kb. This does not establish zero physical body holes or a measured <4 KiB bound.',
        '- Arena mappings provide contiguous virtual addresses; physical DRAM contiguity is not established.',
        '- Zero minor faults do not by themselves establish zero syscalls. No syscall trace is collected by these scripts.',
        '- Multi-camera settled RSS is sampled after workers join and destroy their sessions and arenas; it measures process retention after teardown, not live arena residency.',
        ''
    ]

    report = results / 'BAREMETAL_REPORT.md'
    report.write_text('\n'.join(lines))
    print(f'[+] Bare-metal measurement report written to {report}')


if __name__ == '__main__':
    main()
