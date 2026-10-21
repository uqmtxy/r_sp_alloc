#!/usr/bin/env python3
"""Summarize measured CloudLab telemetry without treating missing events as zero."""
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


def main():
    bench = Path(__file__).resolve().parents[2] / 'bench'
    results = bench / 'yolo26n_pipeline/results_baremetal'
    results.mkdir(parents=True, exist_ok=True)
    lines = ['# CloudLab bare-metal YOLO26n measurements', '',
             'Full-process perf counts include startup and warmup; JSON minor faults exclude warmup. '
             'PMU counts are multiplexed estimates: inspect raw perf files for coverage and unsupported events. '
             'N/A means absent, uncounted, or an invalid denominator. Memory units are MiB.', '',
             '| Allocator | Peak RSS | Settled RSS | Private dirty | IPC | L1 miss % | LLC miss % | dTLB load miss % | dTLB store misses | Process page faults | Process minor faults | Post-warmup minor faults | p99 ms | FPS |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for cfg in ['glibc_default', 'glibc_tuned', 'jemalloc_default', 'jemalloc_tuned', 'tcmalloc', 'mimalloc', 'dynalloc']:
        path = results / f'{cfg}.json'
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            lines.append(f'| {cfg} | incomplete |')
            continue
        perf = parse_perf_txt(results / f'perf_{cfg}.txt')
        cells = [cfg, value(data, 'peak_rss_mb'), value(data, 'settled_rss_mb'), value(data, 'private_dirty_mb'),
                 ratio(perf, 'instructions', 'cycles'), ratio(perf, 'L1-dcache-load-misses', 'L1-dcache-loads', 100),
                 ratio(perf, 'LLC-load-misses', 'LLC-loads', 100), ratio(perf, 'dTLB-load-misses', 'dTLB-loads', 100),
                 str(perf.get('dTLB-store-misses', 'N/A')), str(perf.get('page-faults', 'N/A')),
                 str(perf.get('minor-faults', 'N/A')), str(data.get('minor_faults', 'N/A')),
                 value(data.get('latency', {}), 'p99_ms'), value(data, 'achieved_fps')]
        lines.append('| ' + ' | '.join(cells) + ' |')
    lines += ['', '## Retired-load L3 validation pass', '',
              'The 128 MiB cache sanity test reported zero generic LLC-load-misses but 16.8 million '
              'mem_load_uops_retired.l3_miss events. Generic LLC zeros above are unreliable. '
              'This separate pass uses grouped retired L3 hit/miss events, 1000 measured frames plus 50 warmup, '
              'and the same allocator settings. Counts include startup and warmup. '
              'Its miss fraction is misses / (hits + misses), not the generic LLC event ratio.', '',
              '| Allocator | Retired L3 hits | Retired L3 misses | Retired L3 miss % |',
              '|---|---:|---:|---:|']
    for cfg in ['glibc_default', 'glibc_tuned', 'jemalloc_default', 'jemalloc_tuned', 'tcmalloc', 'mimalloc', 'dynalloc']:
        perf = parse_perf_txt(results / f'perf_l3_{cfg}.txt')
        hits = perf.get('mem_load_uops_retired.l3_hit')
        misses = perf.get('mem_load_uops_retired.l3_miss')
        if hits is None or misses is None or not hits + misses:
            lines.append(f'| {cfg} | incomplete |')
        else:
            lines.append(f'| {cfg} | {hits} | {misses} | {100 * misses / (hits + misses):.3f} |')
    lines += ['', '## PARE-TA allocation microbenchmark', '',
              'This benchmark touches the first and last element of each simulated tensor; it does not run ONNX inference. '
              'Total minor faults include warmup; the post-warmup column reports the separate steady-state interval. '
              'Previously hard-coded trapped DRAM values have been replaced with null (not measured).', '',
              '| Configuration | p99 ms | Peak RSS MiB | Private dirty MiB | Minor faults including warmup | Post-warmup minor faults |',
              '|---|---:|---:|---:|---:|---:|']
    for path in sorted((bench / 'pare_ta_arena/results').glob('*.json')):
        data = json.loads(path.read_text())
        lines.append(f"| {path.stem} | {value(data, 'latency_p99_ms', 6)} | {value(data, 'peak_rss_mb')} | {value(data, 'private_dirty_mb')} | {data.get('minor_faults', 'N/A')} | {data.get('steady_state_minor_faults', 'N/A')} |")
    lines += ['', '## Four-camera inference', '',
              'Settled RSS is sampled 200 ms after worker teardown. Adversarial live anchors remain allocated at this point. '
              'This cannot establish permanent retention or attribute retained bytes exclusively to thread caches.', '',
              '| Configuration | p99 ms | FPS | Peak RSS MiB | Settled RSS MiB | Private dirty MiB | Measured interval minor faults |',
              '|---|---:|---:|---:|---:|---:|---:|']
    for path in sorted((bench / 'multi_camera_adversarial/results').glob('*.json')):
        data = json.loads(path.read_text())
        cells = [path.stem, value(data, 'latency_p99_ms'), value(data, 'aggregate_fps'),
                 value(data, 'peak_rss_mb'), value(data, 'settled_rss_mb'), value(data, 'private_dirty_mb'), str(data.get('minor_faults', 'N/A'))]
        lines.append('| ' + ' | '.join(cells) + ' |')
    lines += ['', '## Interpretation limits', '',
              'CPU affinity does not provide kernel CPU isolation. CPU 2 shares a physical core with CPU 10. '
              'Single runs do not establish statistical significance. Zero LLC counts require independent validation. '
              'Cache and TLB counters alone cannot prove elimination of page straddling or trapped DRAM. '
              'The primary inference suite uses the ONNX Runtime arena, so preload allocators may not see every tensor allocation.', '']
    report = results / 'BAREMETAL_REPORT.md'
    report.write_text('\n'.join(lines))
    print(f'[+] Measurement report written to {report}')


if __name__ == '__main__':
    main()
