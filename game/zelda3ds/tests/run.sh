#!/bin/bash
# Builds and runs the host-side unit tests for the pure map-grid logic
# (zelda3ds/source/LiveMapGeometry.cpp). Plain clang++, no framework/CMake
# target -- see test_livemap_geometry.cpp's header comment for why.
set -euo pipefail
cd "$(dirname "$0")"

${CXX:-c++} -std=c++14 -Wall -Wextra \
    -o "${TMPDIR:-/tmp}/zelda3ds_test_livemap_geometry" \
    test_livemap_geometry.cpp ../source/LiveMapGeometry.cpp

"${TMPDIR:-/tmp}/zelda3ds_test_livemap_geometry"
