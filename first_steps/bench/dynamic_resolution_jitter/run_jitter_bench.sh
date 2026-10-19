#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"

echo "=========================================================="
echo " COMPILING DYNAMIC RESOLUTION JITTER BENCHMARK"
echo "=========================================================="

g++ -O3 -std=c++17 -pthread \
    "${SCRIPT_DIR}/jitter_workload.cpp" \
    -o "${SCRIPT_DIR}/jitter_workload"

echo "Build successful: ${SCRIPT_DIR}/jitter_workload"

# Rebuild libdynalloc.so if needed
if [ ! -f "${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so" ]; then
    echo "Building libdynalloc.so..."
    make -C "${WORKSPACE_ROOT}/first_steps/src"
fi

# Locate allocators in Ubuntu
JEMALLOC_LIB=$(find /usr/lib -name "libjemalloc.so*" 2>/dev/null | grep -E "libjemalloc\.so(\.[0-9]+)?$" | head -n1 || true)
TCMALLOC_LIB=$(find /usr/lib -name "libtcmalloc_minimal.so*" 2>/dev/null | grep -E "libtcmalloc_minimal\.so(\.[0-9]+)?$" | head -n1 || true)
MIMALLOC_LIB=$(find /usr/lib -name "libmimalloc.so*" 2>/dev/null | grep -E "libmimalloc\.so(\.[0-9]+)?$" | head -n1 || true)
DYNALLOC_LIB="${WORKSPACE_ROOT}/first_steps/src/libdynalloc.so"

echo "Allocators detected:"
echo "  jemalloc : ${JEMALLOC_LIB:-'NOT FOUND'}"
echo "  tcmalloc : ${TCMALLOC_LIB:-'NOT FOUND'}"
echo "  mimalloc : ${MIMALLOC_LIB:-'NOT FOUND'}"
echo "  dynalloc : ${DYNALLOC_LIB}"

FRAMES=1000
WARMUP=20
CORE=0

echo ""
echo "=========================================================="
echo " RUNNING DYNAMIC RESOLUTION JITTER EXPERIMENTS"
echo " (1,000 Frames, Non-Stationary Jitter, Physical Core 0)"
echo "=========================================================="

echo "[1/8] Running Generic glibc Default..."
"${SCRIPT_DIR}/jitter_workload" \
    --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
    --name "glibc_default" --json > "${RESULTS_DIR}/glibc_default.json"

echo "[2/8] Running Tuned glibc (M_TRIM_THRESHOLD=4096)..."
MALLOC_TRIM_THRESHOLD_=4096 "${SCRIPT_DIR}/jitter_workload" \
    --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
    --name "glibc_tuned" --json > "${RESULTS_DIR}/glibc_tuned.json"

if [ -n "${JEMALLOC_LIB}" ]; then
    echo "[3/8] Running Meta jemalloc Default (10s decay)..."
    LD_PRELOAD="${JEMALLOC_LIB}" "${SCRIPT_DIR}/jitter_workload" \
        --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
        --name "jemalloc_default" --json > "${RESULTS_DIR}/jemalloc_default.json"

    echo "[4/8] Running Tuned jemalloc (dirty_decay_ms:0)..."
    LD_PRELOAD="${JEMALLOC_LIB}" MALLOC_CONF="dirty_decay_ms:0,muzzy_decay_ms:0" "${SCRIPT_DIR}/jitter_workload" \
        --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
        --name "jemalloc_tuned" --json > "${RESULTS_DIR}/jemalloc_tuned.json"
fi

if [ -n "${TCMALLOC_LIB}" ]; then
    echo "[5/8] Running Google TCMalloc..."
    LD_PRELOAD="${TCMALLOC_LIB}" "${SCRIPT_DIR}/jitter_workload" \
        --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
        --name "tcmalloc" --json > "${RESULTS_DIR}/tcmalloc.json"
fi

if [ -n "${MIMALLOC_LIB}" ]; then
    echo "[6/8] Running Microsoft mimalloc..."
    LD_PRELOAD="${MIMALLOC_LIB}" "${SCRIPT_DIR}/jitter_workload" \
        --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
        --name "mimalloc" --json > "${RESULTS_DIR}/mimalloc.json"
fi

if [ -f "${DYNALLOC_LIB}" ]; then
    echo "[7/8] Running Drop-in Page-Grid DynAlloc (libdynalloc.so)..."
    LD_PRELOAD="${DYNALLOC_LIB}" "${SCRIPT_DIR}/jitter_workload" \
        --mode dynamic --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
        --name "dynalloc_preload" --json > "${RESULTS_DIR}/dynalloc_preload.json"
fi

echo "[8/8] Running Dedicated Page-Grid Arena (page_aware)..."
"${SCRIPT_DIR}/jitter_workload" \
    --mode page_aware --frames "${FRAMES}" --warmup "${WARMUP}" --core "${CORE}" \
    --name "page_aware_arena" --json > "${RESULTS_DIR}/page_aware_arena.json"

echo "All experiments completed successfully. Results saved to ${RESULTS_DIR}/"
