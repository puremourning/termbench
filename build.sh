#!/usr/bin/env bash

BaseFile=termbench.cpp
CLANGCompileFlags="-g -std=c++17 -nostdlib++"

echo -----------------
echo "Setting up tooling"
echo "" > compile_flags.txt
for word in $CLANGCompileFlags; do
  echo $word >> compile_flags.txt
done

echo "-----------------"
echo "Building debug:"
clang++ $CLANGCompileFlags $BaseFile -o termbench_debug_clang

echo -----------------
echo "Building release:"
clang++ -O3 $CLANGCompileFlags $BaseFile -o termbench_release_clang




