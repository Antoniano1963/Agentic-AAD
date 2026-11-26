#!/bin/bash
# Build the solver. Usage: bash build.sh [output-name]
set -e
cd "$(dirname "$0")"
OUT="${1:-solver}"
g++ -O2 -std=c++17 -pthread -o "$OUT" solver.cpp
echo "built $(pwd)/$OUT"
