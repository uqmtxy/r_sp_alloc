#!/usr/bin/env bash
set -euo pipefail

allocator="${1:-glibc}"
duration="${DURATION:-60}"
threads="${THREADS:-4}"
live_objects="${LIVE_OBJECTS:-200000}"
min_size="${MIN_SIZE:-16}"
max_size="${MAX_SIZE:-4096}"
churn_batch="${CHURN_BATCH:-512}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="results/${timestamp}-${allocator}"

mkdir -p "${out_dir}"
make >/dev/null

case "${allocator}" in
  glibc)
    preload=""
    ;;
  jemalloc)
    preload="$(ldconfig -p | awk '/libjemalloc\.so\.2 / { print $NF; exit }')"
    ;;
  tcmalloc)
    preload="$(ldconfig -p | awk '/libtcmalloc_minimal\.so\.4 / { print $NF; exit }')"
    ;;
  mimalloc)
    preload="$(ldconfig -p | awk '/libmimalloc\.so\.2 / { print $NF; exit }')"
    ;;
  *)
    echo "unknown allocator: ${allocator}" >&2
    echo "valid options: glibc jemalloc tcmalloc mimalloc" >&2
    exit 2
    ;;
esac

if [[ "${allocator}" != "glibc" && -z "${preload}" ]]; then
  echo "could not locate preload library for ${allocator}" >&2
  exit 3
fi

cmd=(
  ./bench/alloc_workload
  --threads "${threads}"
  --seconds "${duration}"
  --live-objects "${live_objects}"
  --min-size "${min_size}"
  --max-size "${max_size}"
  --churn-batch "${churn_batch}"
)

{
  echo "allocator=${allocator}"
  echo "duration=${duration}"
  echo "threads=${threads}"
  echo "live_objects=${live_objects}"
  echo "min_size=${min_size}"
  echo "max_size=${max_size}"
  echo "churn_batch=${churn_batch}"
  echo "ld_preload=${preload:-none}"
  echo
} > "${out_dir}/metadata.txt"

if [[ -n "${preload}" ]]; then
  /usr/bin/time -v env LD_PRELOAD="${preload}" "${cmd[@]}" \
    > "${out_dir}/stdout.txt" \
    2> "${out_dir}/time.txt"
else
  /usr/bin/time -v "${cmd[@]}" \
    > "${out_dir}/stdout.txt" \
    2> "${out_dir}/time.txt"
fi

cat "${out_dir}/metadata.txt"
cat "${out_dir}/stdout.txt"
grep -E "Maximum resident set size|User time|System time|Elapsed|Minor|Major" "${out_dir}/time.txt" || true
echo "result_dir=${out_dir}"
