#!/bin/bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
g++ -O2 -std=c++17 -pthread -o "$DIR/solver" "$DIR/solver.cpp"
