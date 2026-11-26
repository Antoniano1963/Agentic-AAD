#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -O2 -pipe -pthread -std=c++17 solver.cpp -o solver
