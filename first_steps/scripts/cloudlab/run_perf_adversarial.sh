#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${WORKSPACE_ROOT}/first_steps/bench/adversarial_yolo26_bench/results_perf"
mkdir -p "${RESULTS_DIR}"

MODEL="${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"
BIN="${WORKSPACE_ROOT}/first_steps/bench/adversarial_yolo26_bench/adversarial_runner"

if [ ! -f "${BIN}" ]; then
    echo "Compiling adversarial_runner..."
    g++ -O3 -std=c++17 -pthread \
        "${WORKSPACE_ROOT}/first_steps/bench/adversarial_yolo26_bench/adversarial_runner.cpp" \
        -o "${BIN}" \
        -I "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/include" \
        -L "${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib" \
        -lonnxruntime \
        -Wl,-rpath,'$ORIGIN/../yolo26n_pipeline/onnxruntime/lib' \
        -Wl,-rpath,"${WORKSPACE_ROOT}/first_steps/bench/yolo26n_pipeline/onnxruntime/lib"
fi

JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

PERF_EVENTS="cycles,instructions,cache-misses,cache-references,L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads,dTLB-load-misses,dTLB-loads,minor-faults"

FRAMES=500
WARMUP=20
CORE=0

echo "=========================================================="
echo " BARE-METAL PERF STAT: ISMM '24 ADVERSARIAL ALLOCATION"
echo " (Adv0 Clean vs Adv3 Fragmented Heap, CPU Core 0)"
echo "=========================================================="

run_perf_trial() {
    local name="$1"
    local mode="$2"
    local adv="$3"
    local preload="$4"

    local log_file="${RESULTS_DIR}/${name}_adv${adv}_perf.txt"
    echo "Measuring ${name} [Adv${adv}] -> ${log_file}"

    if [ -n "${preload}" ]; then
        taskset -c "${CORE}" perf stat -e "${PERF_EVENTS}" \
            env LD_PRELOAD="${preload}" "${BIN}" \
            --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" \
            --warmup "${WARMUP}" --core "${CORE}" --adv "${adv}" \
            --name "${name}" 2> "${log_file}" > /dev/null
    else
        taskset -c "${CORE}" perf stat -e "${PERF_EVENTS}" \
            "${BIN}" \
            --model "${MODEL}" --mode "${mode}" --frames "${FRAMES}" \
            --warmup "${WARMUP}" --core "${CORE}" --adv "${adv}" \
            --name "${name}" 2> "${log_file}" > /dev/null
    fi
}

for adv in 0 3; do
    echo "=== Running Adversarial Level: Adv${adv} ==="
    run_perf_trial "glibc" "dynamic" "${adv}" ""
    if [ -n "${JEMALLOC_LIB}" ]; then
        run_perf_trial "jemalloc" "dynamic" "${adv}" "${JEMALLOC_LIB}"
    fi
    if [ -n "${TCMALLOC_LIB}" ]; then
        run_perf_trial "tcmalloc" "dynamic" "${adv}" "${TCMALLOC_LIB}"
    fi
    if [ -n "${MIMALLOC_LIB}" ]; then
        run_perf_trial "mimalloc" "dynamic" "${adv}" "${MIMALLOC_LIB}"
    fi
    if [ -f "${DYNALLOC_LIB}" ]; then
        run_perf_trial "dynalloc" "dynamic" "${adv}" "${DYNALLOC_LIB}"
    fi
    run_perf_trial "fd_arena" "fd_arena" "${adv}" ""
done

echo "Adversarial hardware profiling complete. Logs saved in ${RESULTS_DIR}/"
