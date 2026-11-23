/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* analyze — mean spectrum of a WAV (100–3000 Hz) and its main peaks.
 * usage: analyze f.wav            → peaks
 *        analyze f.wav tone_hz    → ASCII envelope at that tone (10 ms/column) */
#include "wav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double gpow(const int16_t *x, int n, double f)
{
    double c = 2 * cos(2 * M_PI * f / 8000), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        double s0 = x[i] / 32768.0 * w + c * s1 - s2; s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main(int argc, char **argv)
{
    int n; int16_t *x = wav_read_8k(argv[1], &n);
    if (!x) return 1;
    if (argc < 3) {
        int nb = 291; static double p[300], pk[300];
        int N = 512, frames = 0;
        for (int off = 0; off + N <= n; off += N / 2, frames++)
            for (int b = 0; b < nb; b++) {
                double v = gpow(x + off, N, 100 + 10 * b);
                p[b] += v; if (v > pk[b]) pk[b] = v;
            }
        double med = 0; for (int b = 0; b < nb; b++) med += p[b]; med /= nb;
        printf("mean spectrum (dB above average) and per-frame peak:\n");
        for (int b = 0; b < nb; b++) {
            int loc = (b == 0 || p[b] >= p[b-1]) && (b == nb-1 || p[b] >= p[b+1]);
            double db = 10 * log10(p[b] / med);
            if (loc && db > 3) printf("  %5d Hz  mean %+5.1f dB   peak %+5.1f dB\n", 100 + 10 * b, db, 10 * log10(pk[b] * frames / (p[b] > 0 ? p[b] : 1)) + db);
        }
    } else {
        double f = atof(argv[2]);
        int N = 80;  /* 10 ms */
        double mx = 0; int nc = n / N; double *e = malloc(nc * sizeof(double));
        for (int k = 0; k < nc; k++) { e[k] = sqrt(gpow(x + k * N, N * 2 < n - k * N ? N * 2 : N, f)); if (e[k] > mx) mx = e[k]; }
        const char *lv = " .:-=+*#%@";
        for (int k = 0; k < nc; k++) {
            if (k % 100 == 0) printf("\n%5.1fs ", k / 100.0);
            int q = (int)(9.99 * e[k] / mx); putchar(lv[q]);
        }
        putchar('\n');
    }
    return 0;
}
