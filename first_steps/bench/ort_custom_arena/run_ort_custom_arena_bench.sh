#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

BIN="${SCRIPT_DIR}/ort_custom_arena_bench"
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

echo "=========================================================="
echo " COMPILING ORT CUSTOM ARENA BENCHMARK"
echo "=========================================================="

g++ -O3 -std=c++17 -pthread \
    "${SCRIPT_DIR}/ort_custom_arena_bench.cpp" \
    -o "${BIN}" \
    -I "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/include" \
    -L "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib" \
    -lonnxruntime \
    -Wl,-rpath,'$ORIGIN/../yolo26n_pipeline/onnxruntime/lib' \
    -Wl,-rpath,"${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib"

echo "Build successful: ${BIN}"

FRAMES=100
WARMUP=10
CORE=0
ADV_LEVELS=(0 1 3 10)

echo ""
echo "=========================================================="
echo " RUNNING NATIVE ORT ARENA VS DEFAULT ALLOCATORS"
echo " (YOLO26n C++ ONNX Runtime, Core 0, Frames 200, Warmup 15)"
echo "=========================================================="

run_bench() {
    local name="$1"
    local mode="$2"
    local preload="$3"

    echo "--- Testing: ${name} (Mode: ${mode}) ---"
    for adv in "${ADV_LEVELS[@]}"; do
        local out_json="${RESULTS_DIR}/${name}_adv${adv}.json"
        echo -n "  -> Adv${adv} ... "
        if [ -n "${preload}" ]; then
            LD_PRELOAD="${preload}" "${BIN}" \
                --frames "${FRAMES}" \
                --warmup "${WARMUP}" \
                --core "${CORE}" \
                --adv "${adv}" \
                --name "${name}" \
                --mode "${mode}" \
                --json > "${out_json}"
        else
            "${BIN}" \
                --frames "${FRAMES}" \
                --warmup "${WARMUP}" \
                --core "${CORE}" \
                --adv "${adv}" \
                --name "${name}" \
                --mode "${mode}" \
                --json > "${out_json}"
        fi
        local lat=$(grep '"latency_mean_ms"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        local p99=$(grep '"latency_p99_ms"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        local rss=$(grep '"peak_rss_mb"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        echo "Mean: ${lat} ms | p99: ${p99} ms | Peak RSS: ${rss} MB"
    done
}

# 1. Default ONNX Runtime (Default glibc)
run_bench "glibc_default" "default" ""

# 2. Transparent Interposition with DynAlloc (Page-Grid Slabs)
run_bench "dynalloc_preload" "default" "${DYNALLOC_LIB}"

# 3. Native ONNX Runtime Double-Buffered Arena (OrtDoubleBufferedArena)
run_bench "ort_custom_arena" "ort_custom_arena" ""

# 4. Unified Hybrid: Native OrtDoubleBufferedArena + Page-Grid DynAlloc
run_bench "hybrid_dynalloc_arena" "ort_custom_arena" "${DYNALLOC_LIB}"

echo ""
echo "=========================================================="
echo " NATIVE ORT ARENA EXPERIMENTS COMPLETE"
echo " Results saved to ${RESULTS_DIR}"
echo "=========================================================="
