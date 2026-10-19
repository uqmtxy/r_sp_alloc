#!/usr/bin/env bash
# ==============================================================================
# PARE-TA Empirical Benchmark Evaluation Suite
# Compares Page-Aligned Region-Enclosed Tensor Arena against generic allocators
# under Clean (Adv0) vs Adversarial Fragmentation (Adv3, Adv10)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/pare_ta_bench"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

FRAMES=500
WARMUP=20
PIN_CORE=0

echo "=================================================================="
echo " Starting PARE-TA Empirical Benchmark Evaluation Suite"
echo " Target Frames: ${FRAMES} (Warmup: ${WARMUP})"
echo " Output Directory: ${RESULTS_DIR}"
echo "=================================================================="

# Compile if not present
if [ ! -f "${BIN}" ]; then
    echo "--> Compiling pare_ta_bench..."
    make -C "${SCRIPT_DIR}"
fi

# Detect available allocators on Linux
JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)

run_eval() {
    local name="$1"
    local mode="$2"
    local adv="$3"
    local preload="${4:-}"
    local env_extra="${5:-}"

    echo "--> Running: ${name} (Mode=${mode}, Adv=${adv})..."
    local json_out="${RESULTS_DIR}/${name}.json"

    local cmd="${BIN} --frames ${FRAMES} --warmup ${WARMUP} --mode ${mode} --adv ${adv} --name ${RESULTS_DIR}/${name} --json"

    if [ -n "${preload}" ]; then
        if [ -n "${env_extra}" ]; then
            env LD_PRELOAD="${preload}" ${env_extra} ${cmd}
        else
            env LD_PRELOAD="${preload}" ${cmd}
        fi
    else
        if [ -n "${env_extra}" ]; then
            env ${env_extra} ${cmd}
        else
            ${cmd}
        fi
    fi
}

# 1. Clean Heap Evaluations (Adv0)
echo ""
echo "=== Phase 1: Clean Heap Evaluations (Adv0) ==="
run_eval "pare_ta_adv0" "pare_ta" 0
run_eval "std_pmr_adv0" "std_pmr" 0
run_eval "malloc_default_adv0" "default_malloc" 0

if [ -n "${JEMALLOC_LIB}" ]; then
    run_eval "jemalloc_default_adv0" "default_malloc" 0 "${JEMALLOC_LIB}"
    run_eval "jemalloc_tuned_adv0" "default_malloc" 0 "${JEMALLOC_LIB}" "MALLOC_CONF=dirty_decay_ms:0,muzzy_decay_ms:0"
fi
if [ -n "${TCMALLOC_LIB}" ]; then
    run_eval "tcmalloc_adv0" "default_malloc" 0 "${TCMALLOC_LIB}"
fi
if [ -n "${MIMALLOC_LIB}" ]; then
    run_eval "mimalloc_adv0" "default_malloc" 0 "${MIMALLOC_LIB}"
fi

# 2. Adversarial Fragmentation Evaluations (Adv3 - ISMM '26 Standard)
echo ""
echo "=== Phase 2: Adversarial Fragmentation Evaluations (Adv3 - ISMM '26 Standard) ==="
run_eval "pare_ta_adv3" "pare_ta" 3
run_eval "std_pmr_adv3" "std_pmr" 3
run_eval "malloc_default_adv3" "default_malloc" 3

if [ -n "${JEMALLOC_LIB}" ]; then
    run_eval "jemalloc_default_adv3" "default_malloc" 3 "${JEMALLOC_LIB}"
fi
if [ -n "${TCMALLOC_LIB}" ]; then
    run_eval "tcmalloc_adv3" "default_malloc" 3 "${TCMALLOC_LIB}"
fi
if [ -n "${MIMALLOC_LIB}" ]; then
    run_eval "mimalloc_adv3" "default_malloc" 3 "${MIMALLOC_LIB}"
fi

# 3. Adversarial Stress Evaluations (Adv10 - Extreme Fragmentation)
echo ""
echo "=== Phase 3: Adversarial Stress Evaluations (Adv10 - Extreme Fragmentation) ==="
run_eval "pare_ta_adv10" "pare_ta" 10
run_eval "std_pmr_adv10" "std_pmr" 10
run_eval "malloc_default_adv10" "default_malloc" 10

if [ -n "${JEMALLOC_LIB}" ]; then
    run_eval "jemalloc_default_adv10" "default_malloc" 10 "${JEMALLOC_LIB}"
fi

echo ""
echo "=================================================================="
echo " PARE-TA Benchmark Execution Completed Successfully!"
echo " Telemetry results saved under: ${RESULTS_DIR}"
echo "=================================================================="
