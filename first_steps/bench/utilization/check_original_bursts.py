from run_bounded import run, rng, out
budget=int((out/'selection.txt').read_text())
for i in range(5):
    order=[-1,0,budget]
    rng.shuffle(order)
    for b in order:
        run(f'original_{i}_{b}',b,1000,burst=30)
