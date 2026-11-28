#!/usr/bin/env python3
import hashlib
import json
import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
OUT_DIR = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam"
RUNS_DIR = OUT_DIR
SRC = ROOT / "first_steps/bench/utilization/multicam_staggered_bench.cpp"
BIN = Path("/tmp/multicam_staggered_bench")
MODEL = ROOT / "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"
REF = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle/reference_m2pro.bin"


def compile_binary() -> str:
    ort_prefix = subprocess.check_output(["brew", "--prefix", "onnxruntime"], text=True).strip()
    cmd = [
        "clang++", "-O3", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
        str(SRC), "-o", str(BIN),
        f"-I{ort_prefix}/include/onnxruntime",
        f"-L{ort_prefix}/lib", "-lonnxruntime",
        f"-Wl,-rpath,{ort_prefix}/lib",
    ]
    subprocess.run(cmd, check=True)
    return hashlib.sha256(BIN.read_bytes()).hexdigest()


def main() -> int:
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    bin_sha = compile_binary()

    specs = []
    for policy in ["bfc", "pool_retain", "pool_auto_0", "pool_auto_warm", "pool_coop"]:
        for trial in range(1, 6):
            specs.append((4, 1, policy, trial))
    for policy in ["bfc", "pool_retain", "pool_auto_0", "pool_coop"]:
        for trial in range(1, 6):
            specs.append((8, 2, policy, trial))

    rng = random.Random(20261128)
    rng.shuffle(specs)

    total = len(specs)
    for idx, (streams, conc, policy, trial) in enumerate(specs, 1):
        run_id = f"n{streams}_k{conc}_{policy}_t{trial}"
        out_path = RUNS_DIR / f"{run_id}.json"
        if out_path.exists():
            print(f"[{idx}/{total}] SKIP {run_id} (exists)")
            continue

        cmd = [
            str(BIN),
            "--model", str(MODEL),
            "--reference", str(REF),
            "--policy", policy,
            "--streams", str(streams),
            "--concurrent", str(conc),
            "--frames-per-burst", "8",
            "--cycles", "2",
            "--burst-gap-ms", "350",
            "--idle-ms", "200",
            "--poll-ms", "20",
            "--sample-ms", "25",
            "--global-budget-mib", "0.0",
            "--warm-budget-mib", "24.0",
            "--run-id", run_id,
            "--out", str(out_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(out_path.read_text())
        data["trial"] = trial
        data["binary_sha256"] = bin_sha
        if isinstance(data.get("command"), str):
            import shlex
            data["command"] = shlex.split(data["command"])
        out_path.write_text(json.dumps(data, indent=2) + "\n")
        print(f"[{idx}/{total}] {res.stdout.strip()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
