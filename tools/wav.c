/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* Minimal WAV: writes mono PCM16; reads PCM16/PCM24/float32, mono or stereo,
 * at any rate, and converts to mono 8 kHz. */
#include "wav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

int wav_write(const char *path, const int16_t *x, int n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, n * 2);
    for (int i = 0; i < n; i++) put16(f, (uint16_t)x[i]);
    fclose(f);
    return 0;
}

static uint32_t g32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t g16(const uint8_t *p) { return p[0] | p[1] << 8; }

int16_t *wav_read_8k(const char *path, int *nout)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    if (sz < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return NULL; }

    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL; uint32_t dlen = 0;
    for (long p = 12; p + 8 <= sz;) {
        uint32_t cl = g32(b + p + 4);
        if (!memcmp(b + p, "fmt ", 4)) {
            fmt = g16(b + p + 8); ch = g16(b + p + 10); rate = g32(b + p + 12); bits = g16(b + p + 22);
            if (fmt == 0xFFFE && cl >= 40) fmt = g16(b + p + 32);
        } else if (!memcmp(b + p, "data", 4)) {
            data = b + p + 8; dlen = cl;
            if ((long)(p + 8 + dlen) > sz) dlen = sz - p - 8;
        }
        p += 8 + cl + (cl & 1);
    }
    if (!data || ch < 1 || rate < 1000) { free(b); return NULL; }
    int bps = bits / 8;
    int nin = dlen / (bps * ch);
    float *mono = malloc(nin * sizeof(float));
    for (int i = 0; i < nin; i++) {
        float acc = 0;
        for (int c = 0; c < ch; c++) {
            const uint8_t *q = data + (i * ch + c) * bps;
            float v = 0;
            if (fmt == 1 && bits == 16) v = (int16_t)g16(q) / 32768.0f;
            else if (fmt == 1 && bits == 24) v = ((int32_t)(q[0] << 8 | q[1] << 16 | q[2] << 24) >> 8) / 8388608.0f;
            else if (fmt == 1 && bits == 8) v = (q[0] - 128) / 128.0f;
            else if (fmt == 3 && bits == 32) { float t; memcpy(&t, q, 4); v = t; }
            acc += v;
        }
        mono[i] = acc / ch;
    }
    free(b);

    /* resample to 8 kHz: FIR low-pass (windowed sinc) + linear interpolation */
    int n = (int)((double)nin * 8000 / rate);
    int16_t *out = malloc((n > 0 ? n : 1) * sizeof(int16_t));
    if (rate == 8000) {
        for (int i = 0; i < n; i++) out[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, mono[i])) * 32767);
    } else {
        double fc = 3600.0 / rate;     /* normalized cut-off */
        int taps = 127;
        float *h = malloc(taps * sizeof(float));
        double sum = 0;
        for (int k = 0; k < taps; k++) {
            int m = k - taps / 2;
            double s = m ? sin(2 * M_PI * fc * m) / (M_PI * m) : 2 * fc;
            double w = 0.42 - 0.5 * cos(2 * M_PI * k / (taps - 1)) + 0.08 * cos(4 * M_PI * k / (taps - 1));
            h[k] = (float)(s * w); sum += h[k];
        }
        for (int k = 0; k < taps; k++) h[k] /= (float)sum;
        for (int i = 0; i < n; i++) {
            double pos = (double)i * rate / 8000;
            int i0 = (int)pos; double fr = pos - i0;
            float v[2];
            for (int j = 0; j < 2; j++) {
                double acc = 0;
                int c = i0 + j;
                for (int k = 0; k < taps; k++) {
                    int idx = c + k - taps / 2;
                    if (idx >= 0 && idx < nin) acc += h[k] * mono[idx];
                }
                v[j] = (float)acc;
            }
            float y = (float)(v[0] * (1 - fr) + v[1] * fr);
            out[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, y)) * 32767);
        }
        free(h);
    }
    free(mono);
    *nout = n;
    return out;
}
