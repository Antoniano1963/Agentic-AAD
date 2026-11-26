#!/usr/bin/env bash
# Rebuild this candidate exactly as it was built for evaluation.
set -euo pipefail
DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
g++ -std=c++17 -O2 -march=native -funroll-loops -DNDEBUG -DPFSP_TEMP=0.6 -DPFSP_TEMP_SCALE=0.5 -DPFSP_WIN=16 -DPFSP_BLOCK=2 -DPFSP_BLOCK_PERIOD=8 -DPFSP_POLISH=0.06 $@ -o solver solver.cpp
chmod +x solver
./solver --selftest
