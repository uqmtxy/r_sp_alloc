#!/usr/bin/env python3
"""Run screened + confirmed autonomous idle-reclamation experiments on Apple M2 Pro (macOS)."""

import datetime as dt
import hashlib
import json
import os
import random
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = HERE / "results" / "2026-11-28_macbook_m2pro_auto_idle"
BIN = Path("/tmp/auto_idle_bench")
MODEL = ROOT / "first_steps" / "bench" / "yolo26n_pipeline" / "models" / "yolo26n.onnx"
REF = OUT / "reference_m2pro.bin"
SRC_FILES = [
    HERE / "auto_idle_bench.cpp",
    HERE / "idle_reclaimer.hpp",
    HERE / "page_reuse_resource.hpp",
    HERE / "platform_memory.hpp",
]

RNG = random.Random(20261128)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_binary() -> str:
    ort_prefix = subprocess.check_output(["brew", "--prefix", "onnxruntime"], text=True).strip()
    cmd = [
        "clang++", "-O3", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
        str(HERE / "auto_idle_bench.cpp"),
        "-o", str(BIN),
        f"-I{ort_prefix}/include/onnxruntime",
        f"-L{ort_prefix}/lib", "-lonnxruntime",
        f"-Wl,-rpath,{ort_prefix}/lib",
    ]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return sha256(BIN)


def record_environment(bin_sha: str) -> None:
    env_info = {
        "recorded_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "uname": subprocess.check_output(["uname", "-a"], text=True).strip(),
        "hw_model": subprocess.check_output(["sysctl", "-n", "hw.model"], text=True).strip(),
        "cpu_brand": subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip(),
        "ncpu": int(subprocess.check_output(["sysctl", "-n", "hw.ncpu"], text=True).strip()),
        "memsize_bytes": int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True).strip()),
        "os_page_size": int(subprocess.check_output(["sysctl", "-n", "hw.pagesize"], text=True).strip()),
        "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
        "onnxruntime_brew": subprocess.check_output(["brew", "list", "--versions", "onnxruntime"], text=True).strip(),
        "model_sha256": sha256(MODEL),
        "reference_sha256": sha256(REF),
        "binary_sha256": bin_sha,
        "source_sha256": {p.relative_to(ROOT).as_posix(): sha256(p) for p in SRC_FILES},
    }
    (OUT / "ENVIRONMENT.json").write_text(json.dumps(env_info, indent=2) + "\n", encoding="utf-8")


def run_one(tag: str, policy: str, regime: str, budget_mib: int, bin_sha: str,
            frames: int = 120, warmup: int = 10, burst: int = 10, pause_ms: int = 1000,
            idle_ms: int = 250, poll_ms: int = 20) -> dict:
    cmd = [
        str(BIN),
        "--policy", policy,
        "--regime", regime,
        "--budget-mib", str(max(0, budget_mib)),
        "--frames", str(frames),
        "--warmup", str(warmup),
        "--burst", str(burst),
        "--pause-ms", str(pause_ms),
        "--idle-ms", str(idle_ms),
        "--poll-ms", str(poll_ms),
        "--model", str(MODEL),
        "--reference", str(REF),
    ]
    res = subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True)
    d = json.loads(res.stdout)
    assert d["max_abs_error"] == 0, f"{tag}: non-zero error {d['max_abs_error']}"
    d.update(
        tag=tag,
        command=cmd,
        binary_sha256=bin_sha,
        model_sha256=sha256(MODEL),
        reference_sha256=sha256(REF),
    )
    (OUT / f"{tag}.json").write_text(json.dumps(d, indent=2) + "\n", encoding="utf-8")
    print(
        f"{tag}: policy={policy} regime={regime} budget={budget_mib} "
        f"fps={d['fps']:.3f} p99={d['p99_ms']:.2f}ms "
        f"idle={d['idle_private_bytes'] / (1024**2):.2f}MiB "
        f"reclaims={d['reclaim_calls']} false={d['false_reclaims']}",
        flush=True,
    )
    return d


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    bin_sha = compile_binary()
    if not REF.exists():
        subprocess.run(
            [str(BIN), "--write-reference", str(REF), "--model", str(MODEL)],
            cwd=ROOT, check=True,
        )
    record_environment(bin_sha)

    # 1. Screening sweep (burst regime, shuffled order)
    budgets = [-1, 0, 8, 16, 24, 32]
    RNG.shuffle(budgets)
    screen = {}
    for b in budgets:
        pol = "bfc" if b < 0 else "pool_auto"
        screen[b] = run_one(f"screen_{b}", pol, "burst", max(0, b), bin_sha)

    bfc_idle = screen[-1]["idle_private_bytes"]
    eligible = [b for b in (0, 8, 16, 24, 32) if screen[b]["idle_private_bytes"] <= 0.55 * bfc_idle]
    if not eligible:
        eligible = [0]
    selected_budget = min(eligible, key=lambda b: screen[b]["p99_ms"])
    (OUT / "selection.txt").write_text(f"{selected_budget}\n", encoding="utf-8")
    print(f"SCREENING FROZEN BUDGET: {selected_budget} MiB (eligible={eligible})", flush=True)

    # 2. Confirmation trials (n=5 shuffled blocks per regime)
    for i in range(5):
        # Continuous: bfc vs pool_auto
        cont_pols = ["bfc", "pool_auto"]
        RNG.shuffle(cont_pols)
        for pol in cont_pols:
            run_one(f"confirm_continuous_{i}_{pol}", pol, "continuous", selected_budget, bin_sha)

        # Burst: bfc vs pool_explicit vs pool_auto
        burst_pols = ["bfc", "pool_explicit", "pool_auto"]
        RNG.shuffle(burst_pols)
        for pol in burst_pols:
            run_one(f"confirm_burst_{i}_{pol}", pol, "burst", selected_budget, bin_sha)

        # Mixed (80 ms jitter + 1000 ms pauses): bfc vs pool_explicit vs pool_auto
        mixed_pols = ["bfc", "pool_explicit", "pool_auto"]
        RNG.shuffle(mixed_pols)
        for pol in mixed_pols:
            run_one(f"confirm_mixed_{i}_{pol}", pol, "mixed", selected_budget, bin_sha)


if __name__ == "__main__":
    main()
