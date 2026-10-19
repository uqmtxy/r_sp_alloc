#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

MODEL="${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"
BIN="${SCRIPT_DIR}/adversarial_runner"

echo "=========================================================="
echo " COMPILING ISMM '24 ADVERSARIAL YOLO26n BENCHMARK"
echo "=========================================================="

g++ -O3 -std=c++17 -pthread \
    "${SCRIPT_DIR}/adversarial_runner.cpp" \
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
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

echo "Discovered Allocators:"
echo "  jemalloc : ${JEMALLOC_LIB:-'NOT FOUND'}"
echo "  tcmalloc : ${TCMALLOC_LIB:-'NOT FOUND'}"
echo "  mimalloc : ${MIMALLOC_LIB:-'NOT FOUND'}"
echo "  dynalloc : ${DYNALLOC_LIB}"

FRAMES=200
WARMUP=15
CORE=0

# Levels: 0 (Adv0: Clean), 1 (Adv1: Light), 3 (Adv3: Standard), 10 (Adv10: Stress)
ADV_LEVELS=(0 1 3 10)

echo ""
echo "=========================================================="
echo " RUNNING ISMM '24 ADVERSARIAL ALLOCATION BENCHMARKS"
echo " (YOLO26n C++ ONNX Runtime, Core 0, Warmup 15, Frames 200)"
echo "=========================================================="

run_adv_suite() {
    local name="$1"
    local mode="$2"
    local preload="$3"
    local env_var="$4"

    echo "--- Testing Allocator: ${name} (Mode: ${mode}) ---"
    for adv in "${ADV_LEVELS[@]}"; do
        local out_json="${RESULTS_DIR}/${name}_adv${adv}.json"
        echo "  [Adv${adv}] Running ${name}..."
        if [ -n "${preload}" ] && [ -n "${env_var}" ]; then
            env "${env_var}" LD_PRELOAD="${preload}" "${BIN}" \
                --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" \
                --core "${CORE}" --adv "${adv}" --name "${name}" --json > "${out_json}"
        elif [ -n "${preload}" ]; then
            LD_PRELOAD="${preload}" "${BIN}" \
                --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" \
                --core "${CORE}" --adv "${adv}" --name "${name}" --json > "${out_json}"
        elif [ -n "${env_var}" ]; then
            env "${env_var}" "${BIN}" \
                --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" \
                --core "${CORE}" --adv "${adv}" --name "${name}" --json > "${out_json}"
        else
            "${BIN}" \
                --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" \
                --core "${CORE}" --adv "${adv}" --name "${name}" --json > "${out_json}"
        fi
    done
}

# 1. glibc default
run_adv_suite "glibc_default" "dynamic" "" ""

# 2. jemalloc default
if [ -n "${JEMALLOC_LIB}" ]; then
    run_adv_suite "jemalloc_default" "dynamic" "${JEMALLOC_LIB}" ""
fi

# 3. TCMalloc
if [ -n "${TCMALLOC_LIB}" ]; then
    run_adv_suite "tcmalloc" "dynamic" "${TCMALLOC_LIB}" ""
fi

# 4. mimalloc
if [ -n "${MIMALLOC_LIB}" ]; then
    run_adv_suite "mimalloc" "dynamic" "${MIMALLOC_LIB}" ""
fi

# 5. DynAlloc (LD_PRELOAD interposition)
if [ -f "${DYNALLOC_LIB}" ]; then
    run_adv_suite "dynalloc_preload" "dynamic" "${DYNALLOC_LIB}" ""
fi

# 6. Native FD-Arena (Double-Buffered Region Allocator)
run_adv_suite "fd_arena" "fd_arena" "" ""

echo "All Adversarial experiments completed successfully."
