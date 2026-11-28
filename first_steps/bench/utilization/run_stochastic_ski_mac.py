#!/usr/bin/env python3
import hashlib
import json
import random
import subprocess
from pathlib import Path

ROOT = Path("/Users/kenza/StudyProjects/SJTU/alloc")
OUT_DIR = ROOT / "first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski"
SRC = ROOT / "first_steps/bench/utilization/stochastic_ski_bench.cpp"
BIN = Path("/tmp/stochastic_ski_bench")
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
    bin_sha = compile_binary()

    configs = [
        ("bfc", 0, "bfc"),
        ("pool_retain", 999999, "pool_retain"),
        ("pool_explicit", 0, "pool_explicit"),
        ("pool_auto", 50, "pool_auto_50"),
        ("pool_auto", 150, "pool_auto_150"),
        ("pool_auto", 250, "pool_auto_250"),
        ("pool_auto", 500, "pool_auto_500"),
        ("pool_auto", 1000, "pool_auto_1000"),
    ]
    specs = [(pol, tau, label, t) for (pol, tau, label) in configs for t in range(1, 6)]
    random.Random(2026112803).shuffle(specs)

    for idx, (pol, tau, label, trial) in enumerate(specs, 1):
        run_id = f"{label}_t{trial}"
        out_path = OUT_DIR / f"{run_id}.json"
        if out_path.exists():
            print(f"[{idx}/{len(specs)}] SKIP {run_id}")
            continue
        cmd = [
            str(BIN),
            "--model", str(MODEL),
            "--reference", str(REF),
            "--policy", pol,
            "--idle-ms", str(tau),
            "--poll-ms", "10",
            "--sample-ms", "10",
            "--run-id", run_id,
            "--out", str(out_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
        d = json.loads(out_path.read_text())
        d["label"] = label
        d["trial"] = trial
        d["binary_sha256"] = bin_sha
        out_path.write_text(json.dumps(d, indent=2) + "\n")
        print(f"[{idx}/{len(specs)}] {res.stdout.strip()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
