#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
# Builds the SKIM app for the radio (build/SKIM.APP) and the desktop simulator (build/icapsim).
#   sh build.sh          both
#   sh build.sh radio    SKIM.APP only   (needs arm-none-eabi-gcc and python3)
#   sh build.sh sim      simulator only  (host C compiler: $CC or cc)
#   sh build.sh release  like `all`, then copies SKIM.APP and SKIM.CFG into ../app/
# The SQ6EMM SDK is taken from $ICAP_SDK (default ~/dev/icap-sdk).
set -e
cd "$(dirname "$0")"
SDK=${ICAP_SDK:-$HOME/dev/icap-sdk}
T=$SDK/tools
B=build
STEP=${1:-all}
mkdir -p $B
if [ "$STEP" = all ] || [ "$STEP" = radio ] || [ "$STEP" = release ]; then
  GCC=${GCC:-arm-none-eabi-gcc}
  # same options as the SDK's reference decoders (digi-common/flags.sh)
  CF="-mcpu=cortex-a9 -marm -mfpu=neon-vfpv3 -mfloat-abi=hard -O2 -ffreestanding -nostdlib -fno-builtin \
      -fno-pic -mword-relocations -fno-common -fno-tree-loop-distribute-patterns -fno-section-anchors -Wall -I$SDK"
  DF="$CF -fbuiltin -fno-math-errno"
  LF="-nostdlib -T $T/app.ld -Wl,--emit-relocs -Wl,--no-dynamic-linker"
  $GCC $CF -c $T/icap_start.S -o $B/icap_start.o
  $GCC $DF -c ../src/cwdec.c -o $B/cwdec.o
  $GCC $DF -c ../src/callsign.c -o $B/callsign.o
  $GCC $DF -c ../src/words.c -o $B/words.o
  $GCC $DF -c skim.c -o $B/skim.o
  $GCC $CF $LF $B/icap_start.o $B/skim.o $B/cwdec.o $B/callsign.o $B/words.o -lgcc -o $B/skim.elf
  # nothing external: everything must be inside the app
  if ${GCC%gcc}nm -u $B/skim.elf | grep -q .; then
    echo "ERROR: unresolved symbols:"; ${GCC%gcc}nm -u $B/skim.elf; exit 1
  fi
  ${GCC%gcc}size $B/skim.elf
  # a single app: registered as decoder mode "SKIM" on the RTTY button (":keep" = the radio's
  # mode is left alone, so the key keeps working) and shown on the native DECODE screen
  python3 $T/mkapp.py $B/skim.elf $B/SKIM.APP --name SKIM --models 3 --stack 16384 --abi 3 --background --mode SKIM:keep
fi
if [ "$STEP" = all ] || [ "$STEP" = sim ] || [ "$STEP" = release ]; then
  ${CC:-cc} -O2 -std=c99 -DSKIM_HOST -Wall -I$SDK -o $B/icapsim icapsim.c skim.c ../src/cwdec.c ../src/callsign.c ../src/words.c ../tools/wav.c -lm
fi
if [ "$STEP" = release ]; then
  mkdir -p ../app
  cp $B/SKIM.APP SKIM.CFG ../app/
  echo "app/ updated:"
  (cd ../app && (shasum -a 256 SKIM.APP 2>/dev/null || sha256sum SKIM.APP))
fi
