#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/src"
gcc -O2 -c bouknap.c -o bouknap.o
g++ -std=c++17 -O2 -fopenmp -fpermissive -w -include climits renault.cpp modelo.cpp item.cpp kosaraju.cpp leer_datos.cpp parameters.cpp Problema.cpp Subproblema.cpp truck.cpp utilidades.cpp bouknap.o relaxfix.cpp BusquedaLocal.cpp -o ../s46_open
