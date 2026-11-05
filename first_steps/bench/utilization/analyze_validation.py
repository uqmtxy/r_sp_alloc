#!/usr/bin/env python3
"""Report physical utilization and keep claims scoped to observed phases."""
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
DATA = HERE / 'results/validated'
CONFIGS = ['bfc', 'bfc_trim', 'direct', 'direct_jemalloc', 'direct_mimalloc', 'direct_trim',
           'pool_retain_trim', 'pool_boundary_trim', 'pool_eager_trim']
MIB = 1024**2


def load(name):
    d = json.loads((DATA / f'{name}.json').read_text())
    assert d['outputs_verified'] and d['retained_outputs_verified'] and d['max_abs_error'] == 0, name
    return d


def table(header, rows):
    return ['| ' + ' | '.join(header) + ' |', '|' + '|'.join(['---'] * len(header)) + '|'] + [
        '| ' + ' | '.join(str(v) for v in row) + ' |' for row in rows]


def spread(items, getter):
    values = [getter(d) / MIB for d in items]
    return f'{statistics.median(values):.2f} [{min(values):.2f}, {max(values):.2f}]'


def main():
    single = {c: [load(f'single_{c}_r{r}') for r in range(5)] for c in CONFIGS}
    rows = []
    for c, records in single.items():
        rows.append([c, spread(records, lambda d: d['startup_peak_rss_bytes']),
                     spread(records, lambda d: d['active_peak_rss_bytes']),
                     spread(records, lambda d: d['live_session_idle']['rss_bytes']),
                     spread(records, lambda d: d['live_session_idle']['private_bytes'])])
    median = lambda c, field: statistics.median(d[field] for d in single[c])
    selected = 'direct_trim'
    chosen_peak = median(selected, 'active_peak_rss_bytes')
    bfc_peak = median('bfc', 'active_peak_rss_bytes')
    conventional = [c for c in CONFIGS if not c.startswith('pool_')]
    best_baseline = min(conventional, key=lambda c: median(c, 'active_peak_rss_bytes'))
    best_peak = median(best_baseline, 'active_peak_rss_bytes')
    custom_peak = median('pool_eager_trim', 'active_peak_rss_bytes')
    historical_path = HERE.parent / 'yolo26n_pipeline/results_baremetal/pare_ta.json'
    old_peak = json.loads(historical_path.read_text())['peak_rss_mb'] * MIB
    lines = [
        '# Physical RAM utilization validation', '',
        'Objective: execute identical FP32 YOLO26n inference with less resident RAM. FPS is diagnostic only.', '',
        'Five fresh processes per configuration; 60 measured + 10 warmup frames; CPU 2; '
        'eight distinct deterministic inputs; complete outputs compared against conventional BFC reference tensors. '
        'Outputs are intentionally retained across later frames. All accepted comparisons are bit-exact (max absolute error 0).', '',
        'All sizes below are MiB. Entries are median [minimum, maximum]. '
        'Startup includes construction and warmup. Active peak is the kernel RSS high-water mark reset after warmup. Idle samples retain the model, session, and allocator. '
        'RSS includes shared mappings; private resident memory is also reported. No teardown-only sample establishes success.', '',
        '## Single-stream footprint', '',
    ]
    lines += table(['Configuration', 'Startup/warmup peak RSS', 'Active peak RSS', 'Live-session idle RSS', 'Live-session idle private'], rows)
    lines += ['', '## Selection and claims', '',
              f'- Selected deployment policy: `{selected}` (disable CPU arena and trim free glibc pages after outputs are released).',
              f'- Relative to default BFC, median active peak decreases by {100*(1-chosen_peak/bfc_peak):.2f}%.',
              f'- Relative to historical PARE-TA ({old_peak/MIB:.2f} MiB), median active peak decreases by {100*(1-chosen_peak/old_peak):.2f}%. '
              'Historical inputs/output-lifetime tests differ, so the new repeated BFC comparison is the controlled evidence.',
              f'- Best measured conventional peak: `{best_baseline}` at {best_peak/MIB:.2f} MiB. '
              f'The custom eager pool is {100*(custom_peak/best_peak-1):.2f}% relative to it. '
              'Do not claim that a custom allocator beats the best tuned conventional allocator.',
              '- The custom pool can reduce startup peaks relative to direct glibc trimming; compare both startup and active columns. '
              'No single policy is claimed to dominate every phase. The active-peak reset excludes model-loading transients; '
              'it does not erase or relabel them.',
              '', '## Measured allocator-local utilization', '',
              'The numerator is resident bytes covered by outstanding ORT allocator requests. '
              'It is not last-use tensor liveness within ORT memory-pattern blocks. '
              'The denominator covers only pool-owned mappings; it is not whole-process useful-byte utilization.', '']
    page_rows = []
    for c in ['pool_retain_trim', 'pool_boundary_trim', 'pool_eager_trim']:
        records = [d['pages_idle'] for d in single[c]]
        assert len({p['live_requested_bytes'] for p in records}) == 1
        page_rows.append([c, f"{statistics.median(p['live_requested_bytes'] for p in records)/MIB:.5f}",
                          f"{statistics.median(p['resident_bytes'] for p in records)/MIB:.5f}",
                          f"{statistics.median(p['resident_body_bytes'] for p in records)/MIB:.5f}",
                          f"{statistics.median(p['resident_waste_bytes'] for p in records):.0f}",
                          f"{100*statistics.median(p['payload_utilization'] for p in records):.4f}%"])
    assert len({d['peak_requested_bytes'] for c in ['pool_retain_trim','pool_boundary_trim','pool_eager_trim'] for d in single[c]}) == 1
    lines += table(['Pool policy', 'Live requested MiB', 'Resident MiB', 'Resident free bodies MiB', 'Resident waste bytes', 'Payload utilization'], page_rows)
    lines += ['', '## Endurance', '']
    endurance_rows = []
    for c in ['direct_trim', 'pool_eager_trim']:
        d = load(f'endurance_{c}'); assert d['frames'] == 1000
        endurance_rows.append([c, d['frames'], f"{d['active_peak_rss_bytes']/MIB:.2f}",
                               f"{d['live_session_idle']['private_bytes']/MIB:.2f}", d['max_abs_error']])
    lines += table(['Configuration', 'Measured frames', 'Active peak RSS MiB', 'Live-session idle private MiB', 'Max output error'], endurance_rows)
    lines += ['', '## Four live camera sessions', '',
              'Five fresh processes, 30 measured + 5 warmup frames per camera. '
              'Workers are pinned to CPUs 0–3. Sessions, allocators, and worker threads remain alive at the idle sample.', '']
    multi_rows = []
    for c in ['bfc_trim', 'direct_trim', 'pool_eager_trim']:
        ds = [load(f'cameras4_{c}_r{r}') for r in range(5)]
        multi_rows.append([c, spread(ds, lambda d:d['active_peak_rss_bytes']),
                           spread(ds, lambda d:d['live_session_idle']['rss_bytes']),
                           spread(ds, lambda d:d['live_session_idle']['private_bytes'])])
    lines += table(['Configuration', 'Active peak RSS MiB', 'Live-session idle RSS MiB', 'Live-session idle private MiB'], multi_rows)
    lines += ['', '## Synthetic preconditioning sensitivity', '',
              'These stress runs use the existing empirical size histogram and fixed peak-count proxy (20000). '
              'Occupancies follow Adv1=0.33, Adv3=0.66, Adv10=0.80, but this is explicitly an adaptation, '
              'not a process-wide profile-driven reproduction of ISMM. Live anchor allocations are useful payload, not fragmentation. '
              'One run per setting; no variance or universality claim is made.', '']
    stress_rows = []
    for adv in [1,3,10]:
        for c in ['bfc_trim','direct_trim','pool_eager_trim']:
            d=load(f'cameras4_adv{adv}_{c}')
            stress_rows.append([c,adv,d['occupancy'],f"{d['active_peak_rss_bytes']/MIB:.2f}",f"{d['live_session_idle']['private_bytes']/MIB:.2f}"])
    lines += table(['Configuration','Adv','Occupancy','Active peak RSS MiB','Live-session idle private MiB'],stress_rows)
    lines += ['', '## Limits and rejected approaches', '',
              '- This validates the supplied fixed-shape 640x640 model on this CPU/ORT build. It does not establish dynamic-shape or other-model behavior.',
              '- Trimming may increase page faults, syscall work, and latency. Zero faults and higher FPS are not success criteria for this goal.',
              '- Input/output/precision, session thread counts, graph optimization, and ORT memory-pattern settings are held constant across accepted comparisons.',
              '- See REJECTED_APPROACHES.md for exploratory crashes and rejected policies. Failed modes are excluded from the supported final harness.',
              '- mincore measures resident virtual pages. No physical contiguity, global system-wide utilization, or unique-PFN accounting is claimed.',
              '- The custom pool reserves 256 MiB of virtual address space per camera. Reservation is excluded from physical consumption and reported separately.',
              '- Repeated results, exact commands, model/reference/library/binary/source hashes, and all failures are preserved under results/.', '',
              '## Source-grounded plan', '',
              'See ../../../utilization_improvement_plan.md for the predeclared acceptance rule, paper corrections, and experiment order.', '']
    success = (all(d['active_peak_rss_bytes'] < .75 * old_peak for d in single[selected]) and chosen_peak < .9*bfc_peak)
    assert success, 'predeclared footprint improvement not met'
    lines += ['## Outcome', '',
              'SUCCESS for the utilization objective on this workload: repeated live-process RAM reduction with exact inference outputs. '
              'The chosen implementation uses conventional allocation plus explicit return of free pages. '
              'The custom pool independently demonstrates resident-hole reclamation, but does not establish superiority over the strongest tuned baseline.', '']
    (HERE/'results/UTILIZATION_REPORT.md').write_text('\n'.join(lines))
    print('\n'.join(lines))

if __name__ == '__main__':
    main()
