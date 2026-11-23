/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
#include "synth.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define FS 8000

static const char *PAT[128];

static void init_pat(void)
{
    static int done;
    if (done) return;
    done = 1;
    const char *t[][2] = {
        {"A",".-"},{"B","-..."},{"C","-.-."},{"D","-.."},{"E","."},{"F","..-."},
        {"G","--."},{"H","...."},{"I",".."},{"J",".---"},{"K","-.-"},{"L",".-.."},
        {"M","--"},{"N","-."},{"O","---"},{"P",".--."},{"Q","--.-"},{"R",".-."},
        {"S","..."},{"T","-"},{"U","..-"},{"V","...-"},{"W",".--"},{"X","-..-"},
        {"Y","-.--"},{"Z","--.."},{"0","-----"},{"1",".----"},{"2","..---"},
        {"3","...--"},{"4","....-"},{"5","....."},{"6","-...."},{"7","--..."},
        {"8","---.."},{"9","----."},{".",".-.-.-"},{",","--..--"},{"?","..--.."},
        {"/","-..-."},{"=","-...-"},{"+",".-.-."},{"-","-....-"},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        PAT[(int)t[i][0][0]] = t[i][1];
}

static uint32_t rs;
static double urand(void)
{
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    return (rs + 0.5) / 4294967296.0;
}
static double grand(void)
{
    return sqrt(-2.0 * log(urand())) * cos(2.0 * M_PI * urand());
}

void synth_default(synth_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->wpm = 20; c->tone = 700; c->snr_db = 10; c->dash_ratio = 3;
    c->rise_ms = 5; c->qsb_period_s = 8; c->qrm_wpm = 25; c->lead_s = 1.0f;
    c->seed = 1;
}

/* List of durations (in units) alternating tone/silence, starting with tone. */
typedef struct { float *d; int n, cap; } seq_t;
static void push(seq_t *s, float v)
{
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 256; s->d = realloc(s->d, s->cap * sizeof(float)); }
    s->d[s->n++] = v;
}
static float jit(float v, float j)
{
    float r = v + j * (float)grand();
    return r < 0.3f * v ? 0.3f * v : r;
}

static seq_t keying(const char *text, float dash_ratio, float jitter, float lgap, float wgap)
{
    init_pat();
    seq_t s = {0};
    int first = 1;
    float pending_gap = 0;
    for (const char *p = text; *p; p++) {
        int ch = toupper((unsigned char)*p);
        if (ch == ' ') { if (!first) pending_gap = wgap; continue; }
        const char *pat = (ch < 128) ? PAT[ch] : NULL;
        if (!pat) continue;
        if (!first) push(&s, jit(pending_gap ? pending_gap : lgap, jitter));
        for (const char *e = pat; *e; e++) {
            if (e != pat) push(&s, jit(1, jitter));
            push(&s, jit(*e == '-' ? dash_ratio : 1, jitter));
        }
        first = 0;
        pending_gap = 0;
    }
    /* s.d[0] is tone, then they alternate (even indices = tone) */
    return s;
}

/* fills env[] (0..1) from the sequence, starting at sample `start` */
static void render_env(float *env, int n, int start, const seq_t *s, float unit_s, float rise_ms)
{
    int rise = (int)(rise_ms * FS / 1000.0f);
    if (rise < 1) rise = 1;
    double t = start;
    for (int i = 0; i < s->n; i++) {
        double len = s->d[i] * unit_s * FS;
        if ((i & 1) == 0) {     /* tone */
            int a = (int)t, b = (int)(t + len);
            for (int k = a; k < b + rise && k < n; k++) {
                if (k < 0) continue;
                float g;
                if (k - a < rise) g = 0.5f - 0.5f * cosf((float)M_PI * (k - a) / rise);
                else g = 1.0f;
                if (k >= b) g *= 0.5f + 0.5f * cosf((float)M_PI * (k - b) / rise);
                if (g > env[k]) env[k] = g;
            }
        }
        t += len;
    }
}

static double seq_len(const seq_t *s) { double t = 0; for (int i = 0; i < s->n; i++) t += s->d[i]; return t; }

int16_t *synth_render(const synth_cfg_t *c, const char *text, const char *qrm_text, int *nout)
{
    rs = c->seed ? c->seed * 2654435761u : 1;
    for (int i = 0; i < 10; i++) urand();

    float unit = 1.2f / c->wpm;
    seq_t s = keying(text, c->dash_ratio, c->jitter,
                     c->letter_gap > 0 ? c->letter_gap : 3, c->word_gap > 0 ? c->word_gap : 7);
    if (c->clip_ms > 0) {           /* slow TX/RX switching eats the start of the first element */
        float cut = c->clip_ms / 1000.0f / unit;
        for (int i = 0; i < s.n; i += 2) {
            if (i > 0 && s.d[i - 1] < 15.0f) continue;
            float k = cut < s.d[i] - 0.2f ? cut : s.d[i] - 0.2f;
            s.d[i] -= k;
            if (i > 0) s.d[i - 1] += k;
        }
    }
    int lead = (int)(c->lead_s * FS);
    int n = lead + (int)(seq_len(&s) * unit * FS) + FS;  /* +1 s tail */
    float *env = calloc(n, sizeof(float));
    render_env(env, n, lead, &s, unit, c->rise_ms);

    float *env2 = NULL;
    seq_t s2 = {0};
    if (c->qrm_tone > 0 && qrm_text) {
        s2 = keying(qrm_text, 3, 0.05f, 3, 7);
        env2 = calloc(n, sizeof(float));
        render_env(env2, n, lead / 2, &s2, 1.2f / c->qrm_wpm, 4);
    }

    /* unit-amplitude signal → power 0.5; noise in 2500 Hz = 0.5/snr */
    double pn2500 = 0.5 / pow(10.0, c->snr_db / 10.0);
    double sigma = sqrt(pn2500 * (FS / 2.0) / 2500.0);
    double scale = 0.25 / sqrt(0.5 + sigma * sigma);
    double ph = 0, ph2 = 0;
    double a2 = pow(10.0, c->qrm_db / 20.0);

    int16_t *out = malloc(n * sizeof(int16_t));
    for (int k = 0; k < n; k++) {
        double tt = (double)k / FS;
        double f = c->tone + c->drift_hz * tt / ((double)n / FS);
        ph += 2 * M_PI * f / FS;
        double g = 1.0;
        if (c->qsb_db > 0) {
            double db = -c->qsb_db * 0.5 * (1 - cos(2 * M_PI * tt / c->qsb_period_s));
            g = pow(10.0, db / 20.0);
        }
        double v = env[k] * g * sin(ph);
        if (env2) { ph2 += 2 * M_PI * c->qrm_tone / FS; v += env2[k] * a2 * sin(ph2); }
        v += sigma * grand();
        v *= scale * 32767.0;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[k] = (int16_t)lrint(v);
    }
    free(env); free(env2); free(s.d); free(s2.d);
    *nout = n;
    return out;
}

void synth_text(char *buf, int len, uint32_t seed)
{
    static const char *W[] = {
        "CQ","DE","K","UR","RST","599","579","NAME","QTH","TNX","FB","OM","73",
        "ANT","RIG","WX","HR","ES","GM","GE","PSE","AGN","QSL","BK","TU","5NN",
        "DIPOLE","100W","5W","CLOUDY","SUNNY","MADRID","PABLO","JOHN","IC705",
        "QRP","EFHW","VERTICAL","GL","CUL","SRI","QRM","QSB","HW","CPY","?",
    };
    static const char *PFX[] = {"EA","EB","EC","DL","F","G","I","W","K","VE","JA","OH","SM","LU","PY","ON"};
    rs = seed * 2246822519u + 7;
    for (int i = 0; i < 10; i++) urand();
    int n = 0;
    buf[0] = 0;
    while (n < len) {
        char w[32];
        double r = urand();
        if (r < 0.18) {
            const char *p = PFX[(int)(urand() * 16)];
            snprintf(w, sizeof w, "%s%d%c%c%c", p, (int)(urand() * 10),
                     'A' + (int)(urand() * 26), 'A' + (int)(urand() * 26), 'A' + (int)(urand() * 26));
        } else if (r < 0.28) {
            snprintf(w, sizeof w, "%d", (int)(urand() * 1000));
        } else {
            snprintf(w, sizeof w, "%s", W[(int)(urand() * (sizeof W / sizeof W[0]))]);
        }
        int l = (int)strlen(w);
        if (n + l + 1 >= len + 16) break;
        if (n) buf[n++] = ' ';
        memcpy(buf + n, w, l + 1);
        n += l;
    }
}
