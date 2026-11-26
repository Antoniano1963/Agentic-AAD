#!/bin/bash
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -pthread -o solver solver.cpp -Wall -Wextra 2>&1 | head -50
ls -la solver
