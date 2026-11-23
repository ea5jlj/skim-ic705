/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* Synthetic CW generator for tests (desktop only, uses libm). */
#ifndef SYNTH_H
#define SYNTH_H
#include <stdint.h>

typedef struct {
    float wpm;          /* speed */
    float tone;         /* Hz */
    float snr_db;       /* signal (key down) / noise in 2500 Hz */
    float jitter;       /* std. deviation of each duration, in units (0 = machine) */
    float dash_ratio;   /* dash / dot (3 = perfect) */
    float rise_ms;      /* raised-cosine edges */
    float qsb_db;       /* fading depth (0 = none) */
    float qsb_period_s;
    float drift_hz;     /* total tone drift over the file */
    float qrm_tone;     /* 2nd signal: tone (0 = none) */
    float qrm_db;       /* 2nd signal relative to the main one */
    float qrm_wpm;
    float lead_s;       /* initial silence (noise only) */
    float letter_gap;   /* gap between letters in units (0 = standard 3) */
    float word_gap;     /* gap between words in units (0 = standard 7) */
    uint32_t seed;
} synth_cfg_t;

void synth_default(synth_cfg_t *c);
/* Returns int16 samples at 8000 Hz (malloc; release with free). */
int16_t *synth_render(const synth_cfg_t *c, const char *text, const char *qrm_text, int *nout);
/* Random QSO-like test text, ~len characters. */
void synth_text(char *buf, int len, uint32_t seed);

#endif
