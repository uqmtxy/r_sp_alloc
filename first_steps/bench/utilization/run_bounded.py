from pathlib import Path
import json, random, statistics, subprocess, os, hashlib
here = Path(__file__).resolve().parent
out = here/'results/bounded'
rng = random.Random(20261119)
env = {k:v for k,v in os.environ.items() if not k.startswith(('MALLOC_', 'LD_PRELOAD', 'GLIBC_TUNABLES'))}
def run(tag, budget, pause, frames=120, burst=10):
    cmd = [str(here/'activity_bench'), '--policy', 'bfc' if budget < 0 else 'pool_idle', '--budget-mib', str(max(0,budget)), '--pause-ms',str(pause),'--burst',str(burst),'--frames',str(frames)]
    r = subprocess.run(cmd,cwd=here.parents[2],env=env,check=True,capture_output=True,text=True)
    d = json.loads(r.stdout)
    assert d['max_abs_error']==0
    d.update(command=cmd, binary_sha256=hashlib.sha256((here/'activity_bench').read_bytes()).hexdigest())
    (out/f'{tag}.json').write_text(json.dumps(d,indent=2))
    print(tag, d['fps'], d['p99_ms'], flush=True)
    return d
def main():
    order=[-1,0,8,16,24]
    rng.shuffle(order)
    screen={b:run(f'screen_{b}',b,1000) for b in order}
    b=min((b for b in (8,16,24) if screen[b]['idle_private_bytes']<=screen[-1]['idle_private_bytes']/2),key=lambda b:screen[b]['p99_ms'])
    (out/'selection.txt').write_text(str(b))
    for pause in [0,1000]:
        first=rng.choice([True,False])
        for i in range(5):
            order=[-1,b] if (i%2==0)==first else [b,-1]
            for budget in order: run(f'confirm_{pause}_{i}_{budget}',budget,pause)
    for budget in [-1,b]: run(f'endurance_{budget}',budget,1000,1000)

if __name__ == "__main__":
    main()
