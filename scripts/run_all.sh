#!/usr/bin/env bash
set -euo pipefail

for allocator in glibc jemalloc tcmalloc mimalloc; do
  echo "== ${allocator} =="
  ./scripts/run_one.sh "${allocator}"
  echo
done
