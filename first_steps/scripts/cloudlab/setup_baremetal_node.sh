#!/usr/bin/env bash
# ==============================================================================
# CloudLab Bare-Metal Node Setup Script
# Configures physical Linux hardware for noise-free systems memory benchmarking
# ==============================================================================
set -euo pipefail

echo "=================================================================="
echo " CloudLab Bare-Metal Environment Configuration"
echo "=================================================================="

ARCH="$(uname -m)"
echo "[1/6] Detected Hardware Architecture: ${ARCH}"

# 1. Install Essential Compilers & Profiling Tools
echo "[2/6] Installing system packages, compilers, and hardware PMU tools..."
sudo apt-get update -y
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    numactl \
    procps \
    python3 \
    python3-pip \
    libjemalloc-dev \
    libgoogle-perftools-dev \
    libmimalloc-dev \
    linux-tools-common \
    linux-tools-generic \
    "linux-tools-$(uname -r)" || true

# 2. Kernel Hardware PMU & Access Configuration
echo "[3/6] Unlocking Linux Hardware Performance Counters (PMU)..."
sudo sysctl -w kernel.perf_event_paranoid=-1
sudo sysctl -w kernel.kptr_restrict=0

# 3. Disable Transparent Huge Pages (THP) to evaluate strict 4 KiB page dynamics
echo "[4/6] Disabling Transparent Hugepages (THP) for pure 4 KiB page testing..."
if [ -d "/sys/kernel/mm/transparent_hugepage" ]; then
    echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
    echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
fi

# 4. Lock CPU Clock Governor to Performance (Prevent DVFS Frequency Jitter)
echo "[5/6] Locking CPU frequency scaling governor to 'performance'..."
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    if [ -f "$gov" ]; then
        echo performance | sudo tee "$gov" > /dev/null
    fi
done

# 5. Download ONNX Runtime matching host architecture
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="$(cd "${SCRIPT_DIR}/../../bench/yolo26n_pipeline" && pwd)"
ORT_DIR="${BENCH_DIR}/onnxruntime"
MODELS_DIR="${BENCH_DIR}/models"

mkdir -p "${ORT_DIR}" "${MODELS_DIR}"

if [ ! -f "${ORT_DIR}/lib/libonnxruntime.so" ]; then
    echo "[6/6] Downloading ONNX Runtime C++ release for ${ARCH}..."
    if [ "${ARCH}" = "x86_64" ]; then
        ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz"
    elif [ "${ARCH}" = "aarch64" ]; then
        ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-aarch64-1.20.1.tgz"
    else
        echo "Error: Unsupported architecture ${ARCH}"
        exit 1
    fi
    curl -sL "${ORT_URL}" | tar -xz -C "${ORT_DIR}" --strip-components=1
fi

if [ ! -f "${MODELS_DIR}/yolo26n.onnx" ]; then
    echo "Downloading official Ultralytics YOLO26n ONNX model (9.5 MB)..."
    curl -sL "https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n.onnx" -o "${MODELS_DIR}/yolo26n.onnx"
fi

# Build libdynalloc.so
SRC_DIR="$(cd "${SCRIPT_DIR}/../../src" && pwd)"
echo "Compiling libdynalloc.so..."
make -C "${SRC_DIR}" clean all

# Compile yolo26_bench
echo "Compiling native yolo26_bench harness..."
g++ -O3 -std=c++17 \
    "${BENCH_DIR}/yolo26_bench.cpp" \
    -o "${BENCH_DIR}/yolo26_bench" \
    -I "${ORT_DIR}/include" \
    -L "${ORT_DIR}/lib" \
    -lonnxruntime \
    -Wl,-rpath,'$ORIGIN/onnxruntime/lib' \
    -Wl,-rpath,"${ORT_DIR}/lib"

echo ""
echo "=================================================================="
echo " CloudLab Bare-Metal Node Setup Successfully Completed!"
echo "=================================================================="
