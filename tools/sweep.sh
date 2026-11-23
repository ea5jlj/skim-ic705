#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
# usage: tools/sweep.sh "-DX=1 -DY=2" ...  → bench summary for each build variant
#   BARGS: bench options (e.g. BARGS=-V for Viterbi); CC: C compiler (default cc)
cd "$(dirname "$0")/.."
mkdir -p build
for f in "$@"; do
  # $f and $BARGS are split into words on purpose (several -D options)
  ${CC:-cc} -O2 -std=c99 $f tools/bench.c src/cwdec.c tools/synth.c -lm -o build/bench_sw 2>/dev/null || { echo "$f => build failed"; continue; }
  r=$(./build/bench_sw $BARGS | tail -1 | sed "s/Mean CER (cells capped at 100%)/CER/")
  echo "$f => $r"
done
