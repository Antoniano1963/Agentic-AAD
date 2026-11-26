#!/bin/bash
set -e
cd "$(dirname "$0")"
g++ -O2 -w -o jikken main.cpp cross.cpp env.cpp evaluator.cpp indi.cpp kopt.cpp rand.cpp sort.cpp 2> build.log
