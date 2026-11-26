#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -pthread src/solver.cpp -o solver
