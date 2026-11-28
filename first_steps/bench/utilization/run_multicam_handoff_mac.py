#!/usr/bin/env python3
import hashlib
import json
import random
import shlex
import subprocess
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
OUT_DIR = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff"
TRACES_DIR = OUT_DIR / "traces"
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
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    TRACES_DIR.mkdir(parents=True, exist_ok=True)
    bin_sha = compile_binary()

    specs = []
    # Regime 1: Back-to-back continuous handoff (burst_gap_ms = 0), 7 policies x 5 = 35 runs
    for pol in ["bfc", "direct_malloc", "direct_jemalloc", "pool_retain", "pool_auto_0", "pool_coop", "pool_hybrid"]:
        for trial in range(1, 6):
            specs.append((0, pol, trial))
    # Regime 2: Sparse bursts (burst_gap_ms = 350), 3 policies x 5 = 15 runs
    for pol in ["pool_auto_0", "pool_coop", "pool_hybrid"]:
        for trial in range(1, 6):
            specs.append((350, pol, trial))

    rng = random.Random(2026112802)
    rng.shuffle(specs)

    total = len(specs)
    for idx, (gap_ms, pol, trial) in enumerate(specs, 1):
        run_id = f"n8_k2_gap{gap_ms}_{pol}_t{trial}"
        out_path = OUT_DIR / f"{run_id}.json"
        if out_path.exists():
            print(f"[{idx}/{total}] SKIP {run_id}")
            continue

        cmd = [
            str(BIN),
            "--model", str(MODEL),
            "--reference", str(REF),
            "--policy", pol,
            "--streams", "8",
            "--concurrent", "2",
            "--frames-per-burst", "8",
            "--cycles", "2",
            "--burst-gap-ms", str(gap_ms),
            "--idle-ms", "200",
            "--poll-ms", "20",
            "--sample-ms", "25",
            "--global-budget-mib", "0.0",
            "--shed-warmup",
            "--run-id", run_id,
            "--out", str(out_path),
        ]
        if trial == 1:
            cmd.extend(["--save-trace", str(TRACES_DIR / f"trace_gap{gap_ms}_{pol}.json")])

        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(out_path.read_text())
        data["trial"] = trial
        data["shed_warmup"] = True
        data["binary_sha256"] = bin_sha
        if isinstance(data.get("command"), str):
            data["command"] = shlex.split(data["command"])
        out_path.write_text(json.dumps(data, indent=2) + "\n")
        print(f"[{idx}/{total}] gap={gap_ms} {res.stdout.strip()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
