#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -O3 -march=native -std=c++17 pfsp_solver.cpp -o pfsp_solver
