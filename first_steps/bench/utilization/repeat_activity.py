import subprocess,json,random,hashlib
from pathlib import Path
here=Path(__file__).resolve().parent
jobs=[(p,1000) for p in ['bfc','pool_idle'] for repeat in range(5)]
random.Random(20261109).shuffle(jobs)
for index,(p,pause) in enumerate(jobs):
 command=[str(here/'activity_bench'),'--policy',p,'--pause-ms',str(pause),'--frames','120','--burst','30']
 r=subprocess.run(command,capture_output=True,text=True,check=True,timeout=300)
 d=json.loads(r.stdout);assert d['max_abs_error']==0
 d['command']=command;d['binary_sha256']=hashlib.sha256((here/'activity_bench').read_bytes()).hexdigest()
 (here/'results/activity'/f'repeat_{index}_{p}_{pause}.json').write_text(json.dumps(d,indent=2))
 print(d,flush=True)
