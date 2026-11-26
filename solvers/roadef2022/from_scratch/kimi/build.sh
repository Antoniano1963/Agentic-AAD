#!/bin/bash
set -e
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
python3 -m py_compile "$DIR/solver.py"
