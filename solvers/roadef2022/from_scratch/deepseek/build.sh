#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
g++ -O2 -std=c++17 -march=native -o "$DIR/solver" "$DIR/solver.cpp" -pthread
