#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mapfile -d '' sources < <(find src -type f -name '*.cpp' -print0 | sort -z)
g++ -O3 -pthread -fopenmp -std=c++17 "${sources[@]}" -o solver
