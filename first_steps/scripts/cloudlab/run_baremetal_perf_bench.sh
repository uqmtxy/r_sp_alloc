#!/usr/bin/env bash
# ==============================================================================
# CloudLab Bare-Metal Profiling Runner
# Executes YOLO26n under Linux perf hardware counters on an isolated CPU core
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="$(cd "${SCRIPT_DIR}/../../bench/yolo26n_pipeline" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${BENCH_DIR}/results_baremetal"
mkdir -p "${RESULTS_DIR}"

BIN="${BENCH_DIR}/yolo26_bench"
MODEL="${BENCH_DIR}/models/yolo26n.onnx"
FRAMES=1000
WARMUP=50
PIN_CORE=2

echo "=================================================================="
echo " Starting Bare-Metal Hardware Profiling Suite on Core ${PIN_CORE}"
echo " Model: YOLO26n (Ultralytics January 2026 SOTA)"
echo " Inference Frames: ${FRAMES} (Warmup: ${WARMUP})"
echo "=================================================================="

# Check binary
if [ ! -f "${BIN}" ]; then
    echo "Error: Binary not found at ${BIN}. Run setup_baremetal_node.sh first."
    exit 1
fi

# Locate allocators
JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

PERF_EVENTS="cycles,instructions,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,dTLB-loads,dTLB-load-misses,page-faults,minor-faults"

run_config() {
    local cfg_name="$1"
    local preload_lib="$2"
    local extra_env="$3"
    local mode="${4:-bfc_arena}"
    local adv="${5:-0}"

    echo "--> Profiling: ${cfg_name} (mode: ${mode}, adv: ${adv})..."
    local json_out="${RESULTS_DIR}/${cfg_name}.json"
    local perf_out="${RESULTS_DIR}/perf_${cfg_name}.txt"

    local cmd="taskset -c ${PIN_CORE} ${BIN} --model ${MODEL} --frames ${FRAMES} --warmup ${WARMUP} --name ${cfg_name} --mode ${mode} --adv ${adv} --json"

    if [ -n "${preload_lib}" ]; then
        if [ -n "${extra_env}" ]; then
            perf stat -e "${PERF_EVENTS}" -o "${perf_out}" env LD_PRELOAD="${preload_lib}" ${extra_env} ${cmd} > "${json_out}"
        else
            perf stat -e "${PERF_EVENTS}" -o "${perf_out}" env LD_PRELOAD="${preload_lib}" ${cmd} > "${json_out}"
        fi
    else
        if [ -n "${extra_env}" ]; then
            perf stat -e "${PERF_EVENTS}" -o "${perf_out}" env ${extra_env} ${cmd} > "${json_out}"
        else
            perf stat -e "${PERF_EVENTS}" -o "${perf_out}" ${cmd} > "${json_out}"
        fi
    fi
}

# ==============================================================================
# Suite 1: Standard ONNX Runtime Inference (BFC Arena Baseline)
# ==============================================================================
echo ""
echo "--- Suite 1: Standard ONNX Runtime (BFC Arena) ---"

# 1. Default glibc
run_config "glibc_default" "" "" "bfc_arena"

# 2. Tuned glibc
run_config "glibc_tuned" "" "MALLOC_TRIM_THRESHOLD_=4096 MALLOC_MMAP_THRESHOLD_=65536" "bfc_arena"

# 3. Default jemalloc
if [ -n "${JEMALLOC_LIB}" ]; then
    run_config "jemalloc_default" "${JEMALLOC_LIB}" "" "bfc_arena"
    run_config "jemalloc_tuned" "${JEMALLOC_LIB}" "MALLOC_CONF=dirty_decay_ms:0,muzzy_decay_ms:0" "bfc_arena"
fi

# 4. Google TCMalloc
if [ -n "${TCMALLOC_LIB}" ]; then
    run_config "tcmalloc" "${TCMALLOC_LIB}" "" "bfc_arena"
fi

# 5. Microsoft mimalloc
if [ -n "${MIMALLOC_LIB}" ]; then
    run_config "mimalloc" "${MIMALLOC_LIB}" "" "bfc_arena"
fi

# 6. Page-Grid DynAlloc (Recycling Pool & TLC)
if [ -f "${DYNALLOC_LIB}" ]; then
    run_config "dynalloc" "${DYNALLOC_LIB}" "" "bfc_arena"
fi

# 7. Native PARE-TA Custom Arena (Direct Integration)
run_config "pare_ta" "" "" "pare_ta"

# ==============================================================================
# Suite 2: Direct Tensor Allocation (DisableCpuMemArena)
# Stresses dynamic allocators on real edge CV intermediate tensor activations
# ==============================================================================
echo ""
echo "--- Suite 2: Direct Tensor Allocation (No BFC Arena) ---"

run_config "direct_glibc" "" "" "direct_tensor"

if [ -n "${JEMALLOC_LIB}" ]; then
    run_config "direct_jemalloc" "${JEMALLOC_LIB}" "" "direct_tensor"
fi

if [ -n "${TCMALLOC_LIB}" ]; then
    run_config "direct_tcmalloc" "${TCMALLOC_LIB}" "" "direct_tensor"
fi

if [ -n "${MIMALLOC_LIB}" ]; then
    run_config "direct_mimalloc" "${MIMALLOC_LIB}" "" "direct_tensor"
fi

if [ -f "${DYNALLOC_LIB}" ]; then
    run_config "direct_dynalloc" "${DYNALLOC_LIB}" "" "direct_tensor"
fi

run_config "direct_pare_ta" "" "" "pare_ta"

echo ""
echo "=================================================================="
echo " Generating Hardware Telemetry Matrix..."
echo "=================================================================="
python3 "${SCRIPT_DIR}/analyze_perf_results.py"

echo ""
echo "=================================================================="
echo " Profiling Run Completed. Report at: ${RESULTS_DIR}/BAREMETAL_REPORT.md"
echo "=================================================================="
