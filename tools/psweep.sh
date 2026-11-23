#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
# like sweep.sh but in parallel (one variant per process)
cd "$(dirname "$0")/.."
mkdir -p build
i=0
for f in "$@"; do
  i=$((i+1))
  ( ${CC:-cc} -O2 -std=c99 $f tools/bench.c src/cwdec.c tools/synth.c -lm -o build/bench_p$i 2>/dev/null || { echo "$f => build failed"; exit; }
    r=$(./build/bench_p$i $BARGS | tail -1 | sed "s/Mean CER (cells capped at 100%)/CER/")
    echo "$f => $r" ) &
done
wait
