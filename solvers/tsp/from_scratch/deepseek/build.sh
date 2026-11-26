#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
g++ -O3 -std=c++17 -march=native tsp_solver.cpp -o bin/tsp_solver
