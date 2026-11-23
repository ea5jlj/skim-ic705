/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* bench — SNR × WPM sweep under several conditions; measures the character
 * error rate (CER = Levenshtein distance / length of the true text).
 * usage: bench            full table
 *        bench -c N       condition N only
 *        bench -1 cond snr wpm seed   one case: prints true and decoded text
 *        bench -V ...     same with the Viterbi detector
 *        bench -A ...     same with the AUTO detector */
#include "../src/cwdec.h"
#include "synth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { const char *name; float jitter, ratio, qsb, drift, qrm_tone, qrm_db; } cond_t;
static const cond_t CONDS[] = {
    {"machine (perfect keyer)",           0.00f, 3.0f, 0,  0,  0,   0},
    {"hand (15% jitter, dash 3.4)",      0.15f, 3.4f, 0,  0,  0,   0},
    {"QSB 10 dB / 8 s",                   0.05f, 3.0f, 10, 0,  0,   0},
    {"40 Hz drift + QRM at 150 Hz -6 dB", 0.05f, 3.0f, 0, 40,  850, -6},
};
#define NCOND (int)(sizeof CONDS / sizeof CONDS[0])
static const float SNRS[] = {20, 0, -5, -8, -10, -12, -14};
static const float WPMS[] = {12, 20, 28, 36};
#define NSEED 3

static void norm(const char *in, char *out)
{
    int n = 0, sp = 1;
    for (; *in; in++) {
        int c = toupper((unsigned char)*in);
        if (c == ' ') { if (!sp) { out[n++] = ' '; sp = 1; } }
        else { out[n++] = c; sp = 0; }
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static int lev(const char *a, const char *b)
{
    int la = strlen(a), lb = strlen(b);
    int *p = malloc((lb + 1) * sizeof(int)), *q = malloc((lb + 1) * sizeof(int));
    for (int j = 0; j <= lb; j++) p[j] = j;
    for (int i = 1; i <= la; i++) {
        q[0] = i;
        for (int j = 1; j <= lb; j++) {
            int c = p[j - 1] + (a[i - 1] != b[j - 1]);
            if (p[j] + 1 < c) c = p[j] + 1;
            if (q[j - 1] + 1 < c) c = q[j - 1] + 1;
            q[j] = c;
        }
        int *t = p; p = q; q = t;
    }
    int r = p[lb];
    free(p); free(q);
    return r;
}

static int DET = CWD_DET_THRESHOLD;

static void decode_all(const int16_t *x, int n, char *raw, int rmax)
{
    static cwd_t d;
    cwd_cfg_t cfg; cwd_default_cfg(&cfg);
    cfg.detector = DET;
    cwd_init(&d, &cfg);
    char out[CWD_OUT];
    int rn = 0;
    for (int i = 0; i <= n; i += 160) {
        int k = n - i < 160 ? n - i : 160;
        if (k > 0) cwd_process(&d, x + i, k); else cwd_flush(&d);
        int m = cwd_take(&d, 0, out, sizeof out);
        if (m && rn + m < rmax - 1) { memcpy(raw + rn, out, m); rn += m; }
    }
    cwd_flush(&d);
    int m = cwd_take(&d, 0, out, sizeof out);
    if (m && rn + m < rmax - 1) { memcpy(raw + rn, out, m); rn += m; }
    raw[rn] = 0;
}

static float run(int ci, float snr, float wpm, int seed, char *dec, char *truth)
{
    const cond_t *c = &CONDS[ci];
    synth_cfg_t s; synth_default(&s);
    s.wpm = wpm; s.snr_db = snr; s.jitter = c->jitter; s.dash_ratio = c->ratio;
    s.qsb_db = c->qsb; s.drift_hz = c->drift; s.qrm_tone = c->qrm_tone; s.qrm_db = c->qrm_db;
    s.qrm_wpm = 25; s.seed = seed;
    s.tone = 600 + 37 * seed;                    /* different tone for each seed */
    if (s.qrm_tone) s.qrm_tone = s.tone + 150;
    char text[400], qrm[400];
    synth_text(text, 220, seed * 31 + ci);
    synth_text(qrm, 220, seed * 77 + 5);
    int n;
    int16_t *x = synth_render(&s, text, qrm, &n);
    char raw[2048];
    decode_all(x, n, raw, sizeof raw);
    free(x);
    norm(raw, dec);
    norm(text, truth);
    return (float)lev(dec, truth) / (float)strlen(truth);
}

int main(int argc, char **argv)
{
    char dec[2048], truth[512];
    if (argc >= 2 && !strcmp(argv[1], "-V")) { DET = CWD_DET_VITERBI; argv++; argc--; }
    else if (argc >= 2 && !strcmp(argv[1], "-A")) { DET = CWD_DET_AUTO; argv++; argc--; }
    if (argc >= 6 && !strcmp(argv[1], "-1")) {
        float cer = run(atoi(argv[2]), atof(argv[3]), atof(argv[4]), atoi(argv[5]), dec, truth);
        printf("real: %s\ndeco: %s\nCER %.1f%%\n", truth, dec, 100 * cer);
        return 0;
    }
    int only = -1;
    if (argc >= 3 && !strcmp(argv[1], "-c")) only = atoi(argv[2]);

    int nw = sizeof WPMS / sizeof WPMS[0], ns = sizeof SNRS / sizeof SNRS[0];
    double total = 0; int cnt = 0;
    for (int ci = 0; ci < NCOND; ci++) {
        if (only >= 0 && ci != only) continue;
        printf("\n[%d] %s — CER %% (mean of %d seeds)\n  SNR dB |", ci, CONDS[ci].name, NSEED);
        for (int w = 0; w < nw; w++) printf(" %4.0f WPM", WPMS[w]);
        printf("\n  -------+");
        for (int w = 0; w < nw; w++) printf("---------");
        printf("\n");
        for (int si = 0; si < ns; si++) {
            printf("  %6.0f |", SNRS[si]);
            for (int w = 0; w < nw; w++) {
                double acc = 0;
                for (int sd = 1; sd <= NSEED; sd++) acc += run(ci, SNRS[si], WPMS[w], sd, dec, truth);
                acc /= NSEED;
                total += acc > 1.0 ? 1.0 : acc; cnt++;
                printf(" %7.1f ", 100 * acc);
                fflush(stdout);
            }
            printf("\n");
        }
    }
    /* pure noise: anything the decoder outputs is garbage */
    int garbage = 0;
    float mins = 0;
    for (int sd = 1; sd <= NSEED; sd++) {
        synth_cfg_t s; synth_default(&s);
        s.snr_db = -60; s.seed = 100 + sd; s.lead_s = 60;
        int n; int16_t *x = synth_render(&s, "E", NULL, &n);
        static char raw[8192];
        decode_all(x, n, raw, sizeof raw);
        for (char *q = raw; *q; q++) garbage += *q != ' ';
        mins += (float)n / 8000.0f / 60.0f;
        free(x);
    }
    printf("\nMean CER (cells capped at 100%%): %.2f%% | garbage on pure noise: %.0f chars/min\n",
           100 * total / cnt, garbage / mins);
    return 0;
}
