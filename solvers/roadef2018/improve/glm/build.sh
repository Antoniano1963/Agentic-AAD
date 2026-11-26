#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
src="$PWD/source"
deps="$PWD/../../../../problems/roadef2018/deps"
mkdir -p .build_include
ln -sfn "$src" .build_include/roadef2018
ln -sfn "$(echo "$deps"/optimizationtools-*)" .build_include/benchtools
g++ -include string -std=gnu++14 -O3 -DNDEBUG -pthread -I.build_include -I"$deps" "$src"/lib/{main,instance,solution}.cpp "$src"/ub_*/*.cpp .build_include/benchtools/tools.cpp -lboost_program_options -o solver
