#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -O2 -march=native -std=c++17 src/solver2.cpp -o solver
