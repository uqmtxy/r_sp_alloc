from pathlib import Path
from statistics import median
import json
out=Path(__file__).resolve().parent/'results/bounded'
budget=int((out/'selection.txt').read_text())
def read(name): return json.loads((out/(name+'.json')).read_text())
lines=['# Bounded retention experiment','',f'Screening selected {budget} MiB. Five fresh pairs per regime. Values below are candidate/BFC ratios calculated within each pair.','', '| Regime | Metric | Median | Min | Max |','|---|---|---:|---:|---:|']
passed=True
for pause in [0,1000]:
    pairs=[(read(f'confirm_{pause}_{i}_{budget}'),read(f'confirm_{pause}_{i}_-1')) for i in range(5)]
    keys=['fps','p99_ms'] + (['idle_private_bytes','estimated_private_byte_seconds','resume_median_ms'] if pause else [])
    for k in keys:
        ratios=[a[k]/b[k] for a,b in pairs]
        m=median(ratios)
        lines.append(f'| {"burst" if pause else "continuous"} | {k} | {m:.4f} | {min(ratios):.4f} | {max(ratios):.4f} |')
        if pause==0 and k=='fps': passed &= m>=.97
        if pause and k=='p99_ms': passed &= m<=1.05
        if pause and k=='idle_private_bytes': passed &= m<=.50
        if pause and k=='estimated_private_byte_seconds': passed &= m<1
lines+=['',f'Prespecified median gates: {"PASS" if passed else "FAIL"}.','', 'Endurance results from 1000 frames and 100 scheduled pauses per policy.','', '| Policy | Active FPS | p99 ms | Resume median ms | Idle private MiB | Faults |','|---|---:|---:|---:|---:|---:|']
for b in [-1,budget]:
 d=read(f'endurance_{b}');lines.append(f'| {b if b>=0 else "BFC"} | {d["fps"]:.3f} | {d["p99_ms"]:.2f} | {d["resume_median_ms"]:.2f} | {d["idle_private_bytes"]/2**20:.2f} | {d["minor_faults"]} |')
lines+=['','Full outputs and caller-retained outputs were checked against saved FP32 references. The runner required zero numerical error. Each process used CPU 2 and the same model. Reclamation time during scheduled pauses is recorded separately. FPS excludes scheduled sleep, validation and telemetry. Estimated byte-seconds uses frame-boundary private residency and can undercount transient peaks. Idle RAM is the final live pause sample. Five pairs on one host characterize this workload. The 5% p99 margin is an assumed research threshold.']
(out/'REPORT.md').write_text('\n'.join(lines)+'\n')
if (out/f'original_4_{budget}.json').exists() and len(list(out.glob('original_*.json')))==15:
    lines+=['','## Original 30-frame bursts','','Five shuffled blocks of BFC, full purge and bounded retention. Each row reports paired ratios against BFC.', '', '| Budget MiB | Metric | Median | Min | Max |','|---:|---|---:|---:|---:|']
    for b in (0,budget):
        for k in ['fps','p99_ms','idle_private_bytes','estimated_private_byte_seconds','resume_median_ms']:
            values=[read(f'original_{i}_{b}')[k]/read(f'original_{i}_-1')[k] for i in range(5)]
            lines.append(f'| {b} | {k} | {median(values):.4f} | {min(values):.4f} | {max(values):.4f} |')
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n')
