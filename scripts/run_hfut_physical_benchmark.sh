#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark_directory="${IFCN_HFUT_SIM_BENCHMARK_DIR:-${repository_root}/../hfut-sim/benchmark}"
output_directory="${IFCN_PHYSICAL_BENCHMARK_OUTPUT:-${repository_root}/experiments/physical_hfut_benchmark}"
benchmark_executable="${IFCN_PHYSICAL_BENCHMARK_EXECUTABLE:-${repository_root}/build-release/ifcn_physical_benchmark}"

if [[ ! -d "${benchmark_directory}" ]]; then
    echo "hfut-sim benchmark directory not found: ${benchmark_directory}" >&2
    exit 2
fi

exec python3 "${repository_root}/scripts/benchmark_physical_simulators.py" \
    "${benchmark_directory}" \
    --benchmark-executable "${benchmark_executable}" \
    --output-directory "${output_directory}" \
    --model both \
    --repetitions 10 \
    --warmup 1 \
    --require-equivalent \
    "$@"
