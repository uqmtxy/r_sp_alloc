#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORT="${SCRIPT_DIR}/../yolo26n_pipeline/onnxruntime"
g++ -O3 -std=c++17 -Wall -Wextra -pthread "${SCRIPT_DIR}/utilization_bench.cpp" -o "${SCRIPT_DIR}/utilization_bench" -I "${ORT}/include" -L "${ORT}/lib" -lonnxruntime '-Wl,-rpath,$ORIGIN/../yolo26n_pipeline/onnxruntime/lib'
