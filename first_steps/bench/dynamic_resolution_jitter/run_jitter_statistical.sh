#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

BIN="${SCRIPT_DIR}/jitter_workload"
if [ ! -f "${BIN}" ]; then
    g++ -O3 -std=c++17 -pthread "${SCRIPT_DIR}/jitter_workload.cpp" -o "${BIN}"
fi

JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

TRIALS=5
FRAMES=1000
WARMUP=20
CORE=0

echo "=========================================================="
echo " 5-TRIAL STATISTICAL DYNAMIC RESOLUTION JITTER BENCHMARK"
echo " (1,000 Frames x 5 Trials, Core 0 Pinned, 20 Warmup)"
echo "=========================================================="

run_trials() {
    local name="$1"
    local mode="$2"
    local preload="$3"
    local env_var="$4"

    echo "Running 5 trials for: ${name}..."
    for t in $(seq 1 "${TRIALS}"); do
        local out_json="${RESULTS_DIR}/${name}_trial${t}.json"
        if [ -n "${preload}" ] && [ -n "${env_var}" ]; then
            env "${env_var}" LD_PRELOAD="${preload}" "${BIN}" \
                --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
                --name "${name}" --json > "${out_json}"
        elif [ -n "${preload}" ]; then
            LD_PRELOAD="${preload}" "${BIN}" \
                --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
                --name "${name}" --json > "${out_json}"
        elif [ -n "${env_var}" ]; then
            env "${env_var}" "${BIN}" \
                --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
                --name "${name}" --json > "${out_json}"
        else
            "${BIN}" \
                --mode "${mode}" --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
                --name "${name}" --json > "${out_json}"
        fi
    done
}

run_trials "glibc_default" "dynamic" "" ""
run_trials "glibc_tuned" "dynamic" "" "MALLOC_TRIM_THRESHOLD_=4096"

if [ -n "${JEMALLOC_LIB}" ]; then
    run_trials "jemalloc_default" "dynamic" "${JEMALLOC_LIB}" ""
    run_trials "jemalloc_tuned" "dynamic" "${JEMALLOC_LIB}" "MALLOC_CONF=dirty_decay_ms:0,muzzy_decay_ms:0"
fi

if [ -n "${TCMALLOC_LIB}" ]; then
    run_trials "tcmalloc" "dynamic" "${TCMALLOC_LIB}" ""
fi

if [ -n "${MIMALLOC_LIB}" ]; then
    run_trials "mimalloc" "dynamic" "${MIMALLOC_LIB}" ""
fi

if [ -f "${DYNALLOC_LIB}" ]; then
    run_trials "dynalloc_preload" "dynamic" "${DYNALLOC_LIB}" ""
fi

run_trials "page_aware_arena" "page_aware" "" ""

echo "All 5-trial runs finished."
