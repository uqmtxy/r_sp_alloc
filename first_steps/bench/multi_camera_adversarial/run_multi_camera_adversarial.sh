#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

BIN="${SCRIPT_DIR}/multi_camera_adversarial_bench"
MODEL="${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

echo "=========================================================="
echo " COMPILING MULTI-CAMERA ADVERSARIAL BENCHMARK"
echo "=========================================================="

g++ -O3 -std=c++17 -pthread \
    "${SCRIPT_DIR}/multi_camera_adversarial_bench.cpp" \
    -o "${BIN}" \
    -I "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/include" \
    -L "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib" \
    -lonnxruntime \
    -Wl,-rpath,'$ORIGIN/../yolo26n_pipeline/onnxruntime/lib' \
    -Wl,-rpath,"${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib"

echo "Build successful: ${BIN}"

# Detect system allocators
JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)

echo "Discovered Allocators:"
echo "  jemalloc : ${JEMALLOC_LIB:-'NOT FOUND'}"
echo "  tcmalloc : ${TCMALLOC_LIB:-'NOT FOUND'}"
echo "  mimalloc : ${MIMALLOC_LIB:-'NOT FOUND'}"
echo "  dynalloc : ${DYNALLOC_LIB}"

THREADS=4
FRAMES=50 # 50 frames per thread = 200 total concurrent frames
WARMUP=5
ADV_LEVELS=(0 1 3 10)

echo ""
echo "=========================================================="
echo " RUNNING 4-CAMERA CONCURRENT ADVERSARIAL BENCHMARK"
echo " (4 Concurrent Streams, YOLO26n C++ ONNX Runtime)"
echo "=========================================================="

run_multi_adv() {
    local name="$1"
    local preload="$2"
    local env_var="$3"

    echo "--- Testing Allocator: ${name} ---"
    for adv in "${ADV_LEVELS[@]}"; do
        local out_json="${RESULTS_DIR}/${name}_adv${adv}.json"
        echo -n "  -> Adv${adv} ... "
        if [ -n "${preload}" ]; then
            env ${env_var} LD_PRELOAD="${preload}" "${BIN}" \
                --model "${MODEL}" \
                --threads "${THREADS}" \
                --frames "${FRAMES}" \
                --warmup "${WARMUP}" \
                --adv "${adv}" \
                --name "${name}" \
                --json > "${out_json}"
        else
            env ${env_var} "${BIN}" \
                --model "${MODEL}" \
                --threads "${THREADS}" \
                --frames "${FRAMES}" \
                --warmup "${WARMUP}" \
                --adv "${adv}" \
                --name "${name}" \
                --json > "${out_json}"
        fi
        local fps=$(grep '"aggregate_fps"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        local p99=$(grep '"latency_p99_ms"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        local peak_rss=$(grep '"peak_rss_mb"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        local settled_rss=$(grep '"settled_rss_mb"' "${out_json}" | awk -F': ' '{print $2}' | tr -d ',')
        echo "FPS: ${fps} | p99: ${p99} ms | Peak RSS: ${peak_rss} MB | Settled RSS: ${settled_rss} MB"
    done
}

# 1. Generic Ubuntu Default (glibc)
run_multi_adv "glibc" "" ""

# 2. Meta jemalloc 5.3 Default
if [ -n "${JEMALLOC_LIB}" ]; then
    run_multi_adv "jemalloc" "${JEMALLOC_LIB}" ""
fi

# 3. Google TCMalloc
if [ -n "${TCMALLOC_LIB}" ]; then
    run_multi_adv "tcmalloc" "${TCMALLOC_LIB}" ""
fi

# 4. Microsoft mimalloc 2.1
if [ -n "${MIMALLOC_LIB}" ]; then
    run_multi_adv "mimalloc" "${MIMALLOC_LIB}" ""
fi

# 5. Page-Grid DynAlloc (libdynalloc.so)
run_multi_adv "dynalloc" "${DYNALLOC_LIB}" ""

echo ""
echo "=========================================================="
echo " 4-CAMERA ADVERSARIAL CONCURRENCY EXPERIMENTS COMPLETE"
echo " Results saved to ${RESULTS_DIR}"
echo "=========================================================="
