#!/usr/bin/env bash
# Separate full-size pass for model-specific retired-load L3 events.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="$(cd "${SCRIPT_DIR}/../../bench/yolo26n_pipeline" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT="${BENCH_DIR}/results_baremetal"
mkdir -p "$OUT"

run() {
    local name="$1"
    local preload="$2"
    local mode="${3:-bfc_arena}"
    shift 3 || true
    local env_args=("$@")
    if [[ -n "$preload" ]]; then env_args+=("LD_PRELOAD=$preload"); fi
    echo "Profiling retired L3 loads: $name (mode: $mode, 1000 frames + 50 warmup)"
    LC_ALL=C perf stat -e '{mem_load_uops_retired.l3_hit,mem_load_uops_retired.l3_miss}' \
        -o "$OUT/perf_l3_${name}.txt" \
        taskset -c 2 env "${env_args[@]}" "$BENCH_DIR/yolo26_bench" \
        --model "$BENCH_DIR/models/yolo26n.onnx" --frames 1000 --warmup 50 \
        --name "$name" --mode "$mode" --json > "$OUT/l3_${name}.json"
}

# Standard BFC Arena Baseline
run glibc_default '' bfc_arena
run glibc_tuned '' bfc_arena MALLOC_TRIM_THRESHOLD_=4096 MALLOC_MMAP_THRESHOLD_=65536
run jemalloc_default /usr/lib/x86_64-linux-gnu/libjemalloc.so bfc_arena
run jemalloc_tuned /usr/lib/x86_64-linux-gnu/libjemalloc.so bfc_arena MALLOC_CONF=dirty_decay_ms:0,muzzy_decay_ms:0
run tcmalloc /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so bfc_arena
run mimalloc /usr/lib/x86_64-linux-gnu/libmimalloc.so bfc_arena
run dynalloc "$ROOT/first_steps/src/libdynalloc.so" bfc_arena
run pare_ta '' pare_ta

# Direct Tensor Allocation Pass (No BFC Arena)
run direct_glibc '' direct_tensor
run direct_jemalloc /usr/lib/x86_64-linux-gnu/libjemalloc.so direct_tensor
run direct_tcmalloc /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so direct_tensor
run direct_mimalloc /usr/lib/x86_64-linux-gnu/libmimalloc.so direct_tensor
run direct_dynalloc "$ROOT/first_steps/src/libdynalloc.so" direct_tensor

python3 "$SCRIPT_DIR/analyze_perf_results.py"
