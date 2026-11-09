#!/usr/bin/env python3
"""Sequential utilization experiments; correctness failures stop accepted runs."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = HERE / 'results' / 'recheck'
BIN = HERE / 'utilization_bench'
MODEL = HERE.parent / 'yolo26n_pipeline/models/yolo26n.onnx'
OUT.mkdir(parents=True, exist_ok=True)
REFERENCE = OUT / 'reference.bin'
SOURCES = [HERE / 'utilization_bench.cpp', HERE / 'page_reuse_resource.hpp']
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source_hashes = {str(p.relative_to(ROOT)): sha(p) for p in SOURCES}
binary_hash = sha(BIN)
env_base = os.environ.copy()
for key in ['LD_PRELOAD', 'MALLOC_CONF', 'MALLOC_TRIM_THRESHOLD_', 'MALLOC_MMAP_THRESHOLD_', 'MALLOC_ARENA_MAX']:
    env_base.pop(key, None)
subprocess.run([str(BIN), '--mode', 'reference', '--reference', str(REFERENCE), '--model', str(MODEL)],
               env=env_base, check=True, stdout=subprocess.DEVNULL)
reference_hash = sha(REFERENCE)
configs = {
 'bfc': ('bfc', False, ''),
 'bfc_trim': ('bfc', True, ''),
 'direct_trim': ('direct', True, ''),
 'pool_eager_trim': ('pool_eager', True, ''),
}


def run(name, config, frames, warmup, cameras=1, adv=0, n_peak=20000):
    mode, trim, preload = configs[config]
    command = [str(BIN), '--mode', mode, '--trim', str(int(trim)), '--frames', str(frames),
               '--warmup', str(warmup), '--cameras', str(cameras), '--adv', str(adv),
               '--n-peak', str(n_peak), '--reference', str(REFERENCE), '--model', str(MODEL)]
    path = OUT / f'{name}.json'
    if path.exists():
        old = json.loads(path.read_text())
        if old['_command'] != command or old['_binary_sha256'] != binary_hash or old['_reference_sha256'] != reference_hash:
            raise RuntimeError(f'refusing to reuse incompatible result: {path}')
        print('Already validated:', name, flush=True)
        return old
    env = env_base.copy()
    if preload:
        env['LD_PRELOAD'] = preload
    begun = time.time()
    p = subprocess.run(command, env=env, capture_output=True, text=True, timeout=600)
    (OUT / f'{name}.stderr').write_text(p.stderr)
    if p.returncode:
        (OUT / f'{name}.failed.stdout').write_text(p.stdout)
        raise RuntimeError(f'{name}: exit {p.returncode}: {p.stderr[-2000:]}')
    d = json.loads(p.stdout)
    assert d['outputs_verified'] and d['retained_outputs_verified'], name
    assert d['max_abs_error'] == 0, (name, d['max_abs_error'])
    if cameras == 1:
        assert d['frames'] == frames
        if 'pages_idle' in d:
            a = d['pages_idle']
            assert a['resident_bytes'] == a['resident_payload_bytes'] + a['resident_waste_bytes']
    else:
        assert d['cameras'] == cameras and d['frames_per_camera'] == frames
        for a in d['camera_pages_idle']:
            assert a['resident_bytes'] == a['resident_payload_bytes'] + a['resident_waste_bytes']
    d.update(_config=config, _command=command, _binary_sha256=binary_hash,
             _reference_sha256=reference_hash, _model_sha256=sha(MODEL),
             _source_sha256=source_hashes, _preload=preload,
             _preload_sha256=sha(Path(preload)) if preload else None,
             _started_unix=begun, _wall_seconds=time.time() - begun)
    path.write_text(json.dumps(d, indent=2) + '\n')
    print(f"{name}: peak={d['active_peak_rss_bytes']/2**20:.2f} MiB, "
          f"live-session idle private={d['live_session_idle']['private_bytes']/2**20:.2f} MiB, full outputs exact", flush=True)
    return d


rng = random.Random(20261109)
for repeat in range(5):
    order = list(configs)
    rng.shuffle(order)
    for config in order:
        run(f'single_{config}_r{repeat}', config, 60, 10)

# The conventional minimum-footprint alternative is retained as an equal competitor.
for config in ['direct_trim', 'pool_eager_trim']:
    run(f'endurance_{config}', config, 1000, 50)

for repeat in range(5):
    order = ['bfc_trim', 'direct_trim', 'pool_eager_trim']
    rng.shuffle(order)
    for config in order:
        run(f'cameras4_{config}_r{repeat}', config, 30, 5, cameras=4)

# Explicit adaptation: existing workload size histogram and fixed peak proxy,
# not an exact reproduction of ISMM's process-wide malloc profiling.
for adv in [1, 3, 10]:
    for config in ['bfc_trim', 'direct_trim', 'pool_eager_trim']:
        run(f'cameras4_adv{adv}_{config}', config, 30, 5, cameras=4, adv=adv)
print('All utilization validation runs completed.', flush=True)
