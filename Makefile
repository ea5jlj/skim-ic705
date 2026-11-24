# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
# the core is built as on the radio: no libc
CORE_FLAGS = -ffreestanding -fno-builtin -fno-math-errno -fno-stack-protector

BIN = build
all: $(BIN)/cwgen $(BIN)/cwfile $(BIN)/bench $(BIN)/analyze $(BIN)/wordfix

$(BIN):
	mkdir -p $(BIN)

$(BIN)/cwdec.o: src/cwdec.c src/cwdec.h | $(BIN)
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@

$(BIN)/%.o: tools/%.c src/cwdec.h src/callsign.h src/words.h tools/synth.h tools/wav.h | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN)/cwgen: $(BIN)/cwgen.o $(BIN)/synth.o $(BIN)/wav.o
	$(CC) $^ -lm -o $@
$(BIN)/callsign.o: src/callsign.c src/callsign.h | $(BIN)
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@

$(BIN)/words.o: src/words.c src/words.h src/callsign.h src/dict.h | $(BIN)
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@

$(BIN)/pskr.o: src/pskr.c src/pskr.h | $(BIN)
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@

$(BIN)/dict.o: src/dict.c src/dict.h | $(BIN)
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@

$(BIN)/cwfile: $(BIN)/cwfile.o $(BIN)/cwdec.o $(BIN)/callsign.o $(BIN)/words.o $(BIN)/dict.o $(BIN)/wav.o
	$(CC) $^ -lm -o $@
$(BIN)/bench: $(BIN)/bench.o $(BIN)/cwdec.o $(BIN)/synth.o
	$(CC) $^ -lm -o $@

# check that the core does not depend on libc
check-freestanding: $(BIN)/cwdec.o $(BIN)/callsign.o $(BIN)/words.o $(BIN)/dict.o $(BIN)/pskr.o
	@nm -u $^ | grep -v ':$$' | grep . | sort -u > $(BIN)/undef.txt; \
	 nm -g $^ | awk 'NF == 3 && $$2 != "U" { print $$3 }' | sort -u > $(BIN)/def.txt; \
	 comm -23 $(BIN)/undef.txt $(BIN)/def.txt | grep . && echo "^ external symbols" || echo "core has no external dependencies"

bench: $(BIN)/bench
	./$(BIN)/bench

clean:
	rm -rf $(BIN)

.PHONY: all bench clean check-freestanding
$(BIN)/analyze: $(BIN)/analyze.o $(BIN)/wav.o
	$(CC) $^ -lm -o $@
$(BIN)/wordfix: $(BIN)/wordfix.o $(BIN)/words.o $(BIN)/dict.o $(BIN)/callsign.o
	$(CC) $^ -o $@
