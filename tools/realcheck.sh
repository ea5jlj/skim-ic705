#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
# usage: tools/realcheck.sh "-DX=1" ...  → CER of each build variant against your own
#        reference transcripts. For every REF_DIR/NAME.txt it decodes AUDIO_DIR/NAME.wav.
#   AUDIO_DIR (default ./audio), REF_DIR (default ./refs), ARGS (cwfile options, e.g. ARGS=-V)
cd "$(dirname "$0")/.."
AUDIO_DIR=${AUDIO_DIR:-audio}
REF_DIR=${REF_DIR:-refs}
shopt -s nullglob
mkdir -p build
for f in "$@"; do
  ${CC:-cc} -O2 -std=c99 $f tools/cwfile.c src/cwdec.c src/callsign.c tools/wav.c -lm -o build/cwfile_rc 2>/dev/null || { echo "$f => build failed"; continue; }
  out=""
  for r in "$REF_DIR"/*.txt; do
    w="$AUDIO_DIR/$(basename "$r" .txt).wav"
    [ -f "$w" ] || { echo "missing audio for $r: $w" >&2; continue; }
    c=$(./build/cwfile_rc "$w" $ARGS -r "$r" | tail -1 | awk '{print $2}')
    out="$out $(basename "$r" .txt)=$c"
  done
  echo "$f =>$out"
done
