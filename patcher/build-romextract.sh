#!/bin/sh
# Builds patcher/romextract: runs the ROM extractor on a PC and writes the
# extracted files to a folder, or compares them with an existing one.
# Useful for checking a ROM or for working on the extractor.
set -e
cd "$(dirname "$0")"
${CXX:-c++} -std=c++14 -O2 -Wall -Isrc/extract -o romextract src/romextract-cli.cpp src/extract/RomExtract.cpp
echo "Built patcher/romextract"
