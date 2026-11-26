#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -O3 -march=native -funroll-loops -o solver src/tsp_solver.cpp
