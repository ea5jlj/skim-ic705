/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
#ifndef WAV_H
#define WAV_H
#include <stdint.h>
int      wav_write(const char *path, const int16_t *x, int n, int rate);
/* Reads any WAV and returns it as mono int16 at 8000 Hz (malloc). */
int16_t *wav_read_8k(const char *path, int *nout);
#endif
