Run from first_steps/bench/utilization on the reference node. Preserve the current results/bounded directory before starting another campaign, since the scripts use fixed filenames.

```bash
ORT=../yolo26n_pipeline/onnxruntime
g++ -O3 -std=c++17 -pthread activity_bench.cpp -o activity_bench -I "$ORT/include" -L "$ORT/lib" -lonnxruntime '-Wl,-rpath,$ORIGIN/../yolo26n_pipeline/onnxruntime/lib'
python3 run_bounded.py
python3 analyze_bounded.py
python3 check_original_bursts.py
```

The binary accepts a budget in MiB for completely free resident page interiors per pool. The harness reclaims at an explicit pause boundary. Each camera in this experiment uses a single session pinned to CPU 2. One reference file supplies the same eight distinct input/output comparisons across runs.
