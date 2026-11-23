/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* cwdec — see cwdec.h. No libc: everything needed is here. */
#include "cwdec.h"
#ifdef CWD_DEBUG
extern void cwd_dbg(const char *what, float a, float b, float c, float e);
#define DBG(w,a,b,c,e) cwd_dbg(w,a,b,c,e)
#else
#define DBG(w,a,b,c,e)
#endif

#define PI_F   3.14159265358979f
#define TWO_PI 6.28318530717959f
#ifndef FIRST_DASH_V
#define FIRST_DASH_V 0.5f
#endif
#define FIRST_DASH   FIRST_DASH_V       /* first element of a transmission: dash above dot + this × unit */
#define FIRST_GAP_MS 800.0f     /* silence before it: at least this long... */
#define FIRST_GAP_U  12.0f      /* ...and this many units */
#define GFRAME 256              /* samples per Goertzel frame (32 ms) */
#define NEG    (-1e30f)

/* ---- tunable parameters (at build time: -DNAME=value) ---- */
#ifndef SP_PCT
#define SP_PCT 0.15f            /* space percentile used to seed k-means */
#endif
#ifndef MK_LO
#define MK_LO 0.2f
#endif
#ifndef MK_HI
#define MK_HI 0.85f
#endif
#ifndef GL_FRAC
#define GL_FRAC 0.4f            /* debounce: fraction of the shortest element */
#endif
#ifndef GL_MIN
#define GL_MIN 4.0f
#endif
#ifndef GL_MAX
#define GL_MAX 15.0f
#endif
#ifndef SQ_TONE
#define SQ_TONE 2.5f            /* spectral squelch: tone / floor */
#endif
#ifndef TAU_MAX
#define TAU_MAX 14.0f
#endif
#ifndef ENV_SQ
#define ENV_SQ 2.5f             /* envelope squelch: signal / noise */
#endif
#ifndef UNIT_RATE
#define UNIT_RATE 0.15f         /* maximum change of the unit per element */
#endif
#ifndef PROF_JUMP
#define PROF_JUMP 2.0f          /* level change (×, = 6 dB) after a word gap that means another operator */
#endif
#ifndef PROF_RUN
#define PROF_RUN 3               /* or this many marks in a row with that jump */
#endif
#ifndef PROF_MATCH
#define PROF_MATCH 1.6f         /* a saved operator matches within this level ratio (≈ 4 dB) */
#endif
#ifndef PROF_TRACK
#define PROF_TRACK 0.1f         /* how much a level outlier moves the operator level */
#endif
#ifndef PROF_FIT
#define PROF_FIT 0.8f           /* share of new marks fitting the last timing to keep it */
#endif
#ifndef WARM_N
#define WARM_N 12
#endif
#ifndef FILT_DIV
#define FILT_DIV 9.0f           /* filter tau = unit / FILT_DIV */
#endif
#ifndef FLOOR_PCT
#define FLOOR_PCT 0.5f
#endif
#ifndef MULTI_OPEN
#define MULTI_OPEN 5.0f        /* multi mode: spectral squelch to decode */
#endif
#ifndef MULTI_REPL
#define MULTI_REPL 2.0f
#endif
#ifndef XT_HZ
#define XT_HZ 250.0f
#endif
#ifndef XT_LEAK
#define XT_LEAK 0.3f            /* mute when a neighbour's leak reaches this share of our level */
#endif
#ifndef AUTO_SNR
#define AUTO_SNR -4.0f          /* AUTO: below this (dB in 2500 Hz) Viterbi wins */
#endif
#ifndef AUTO_WPM
#define AUTO_WPM 26.0f          /* AUTO: above this the threshold detector wins */
#endif
#ifndef AUTO_OFS
#define AUTO_OFS 21.5f
#endif
#ifndef AUTO_WSLOPE
#define AUTO_WSLOPE 0.21f
#endif
#ifndef FLOOR_K
#define FLOOR_K 1.2f            /* the moving minimum sits a little below the mean noise */
#endif
#ifndef FLOOR_W
#define FLOOR_W 6               /* noise floor: moving minimum over ±6 bins (±94 Hz) */
#endif
#ifndef PASS_FRAC
#define PASS_FRAC 0.05f
#endif
#ifndef WGAP_MIN
#define WGAP_MIN 4.0f           /* standard word-gap threshold: gap + 4 units (= 5 units) */
#endif
#ifndef WGAP_LOW
#define WGAP_LOW 2.5f           /* lowest it can go for tight spacing / heavy weighting */
#endif
#ifndef WG_RATIO
#define WG_RATIO 1.6f           /* and word gaps this many times longer */
#endif
#ifndef WGAP_MAX
#define WGAP_MAX 9.0f           /* most it can grow for widely spaced operators */
#endif
#ifndef MULTI_PK
#define MULTI_PK 3.0f           /* multi mode: a peak is a station if > MULTI_PK × floor */
#endif
#ifndef MULTI_SEP
#define MULTI_SEP 4             /* minimum channel spacing (15.6 Hz bins) */
#endif
#ifndef MULTI_IDLE
#define MULTI_IDLE 300          /* frames (~10 s) unseen → channel freed */
#endif
/* Viterbi */
#ifndef V_LSCALE
#define V_LSCALE 0.35f          /* the envelope is correlated: do not trust each tick */
#endif
#ifndef V_LCLIP
#define V_LCLIP 3.0f
#endif
#ifndef V_LOGEMIS
#define V_LOGEMIS 0             /* 1: likelihood on log(envelope) */
#endif
#ifndef V_SIG_MARK
#define V_SIG_MARK 0.30f        /* (log) spread of mark durations */
#endif
#ifndef V_SIG_GAP
#define V_SIG_GAP 0.40f
#endif
#ifndef V_SIG_LUP
#define V_SIG_LUP 0.35f        /* long tail of the letter gap */
#endif
#ifndef V_SIG_WDN
#define V_SIG_WDN 0.30f
#endif
#ifndef V_SIG_WUP
#define V_SIG_WUP 0.60f
#endif
#ifndef V_FILT_DIV
#define V_FILT_DIV 16.0f
#endif
#ifndef V_FILT_ADAPT
#define V_FILT_ADAPT 1
#endif
#ifndef V_LAG
#define V_LAG 15.0f             /* decision delay in units */
#endif

#define VT_MS 4                 /* ms per Viterbi tick */

/* ---------------------------------------------------------------- utilities */

static float cw_sqrt(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }

static float cw_sin(float x)
{
    float k = x / TWO_PI;
    int   r = (int)(k + (k >= 0.0f ? 0.5f : -0.5f));
    x -= (float)r * TWO_PI;
    if (x >  PI_F / 2) x =  PI_F - x;
    if (x < -PI_F / 2) x = -PI_F - x;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f *
               (1.0f - x2 / 72.0f))));
}
static float cw_cos(float x) { return cw_sin(x + PI_F / 2); }

static float cw_log(float x)
{
    union { float f; uint32_t i; } u;
    u.f = x > 1e-30f ? x : 1e-30f;
    int e = (int)((u.i >> 23) & 255u) - 127;
    u.i = (u.i & 0x7FFFFFu) | 0x3F800000u;
    float m = u.f, t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    float lm = 2.0f * t * (1.0f + t2 * (1.0f / 3 + t2 * (1.0f / 5 + t2 * (1.0f / 7 + t2 / 9))));
    return lm + (float)e * 0.69314718f;
}

static float cw_exp(float x)
{
    if (x < -80.0f) return 0.0f;
    if (x > 80.0f) x = 80.0f;
    float kf = x / 0.69314718f;
    int k = (int)(kf + (kf >= 0 ? 0.5f : -0.5f));
    float r = x - (float)k * 0.69314718f;
    float er = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120 + r / 720)))));
    union { float f; uint32_t i; } u;
    if (k < -126) return 0.0f;
    u.i = (uint32_t)(k + 127) << 23;
    return er * u.f;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void zero(void *p, unsigned n)
{
    char *c = (char *)p;
    for (unsigned i = 0; i < n; i++) c[i] = 0;
}

/* ---------------------------------------------------------------- Morse table */

static const struct { const char *pat, *txt; } MORSE[] = {
    {".-","A"},{"-...","B"},{"-.-.","C"},{"-..","D"},{".","E"},{"..-.","F"},
    {"--.","G"},{"....","H"},{"..","I"},{".---","J"},{"-.-","K"},{".-..","L"},
    {"--","M"},{"-.","N"},{"---","O"},{".--.","P"},{"--.-","Q"},{".-.","R"},
    {"...","S"},{"-","T"},{"..-","U"},{"...-","V"},{".--","W"},{"-..-","X"},
    {"-.--","Y"},{"--..","Z"},
    {"-----","0"},{".----","1"},{"..---","2"},{"...--","3"},{"....-","4"},
    {".....","5"},{"-....","6"},{"--...","7"},{"---..","8"},{"----.","9"},
    {".-.-.-","."},{"--..--",","},{"..--..","?"},{"-..-.","/"},{"-...-","="},
    {".-.-.","+"},{"-....-","-"},{"-.--.","("},{"-.--.-",")"},{".--.-.","@"},
    {"---...",":"},{".----.","'"},{".-..-.","\""},{"-.-.--","!"},
    {"...-.-","<SK>"},{".-...","<AS>"},{"-.-.-","<KA>"},{"...-.","<SN>"},
};

static void build_table(cwd_t *d)
{
    for (int i = 0; i < 256; i++) d->table[i] = 0;
    for (unsigned k = 0; k < sizeof MORSE / sizeof MORSE[0]; k++) {
        int idx = 1;
        for (const char *p = MORSE[k].pat; *p; p++)
            idx = idx * 2 + (*p == '-');
        if (idx < 256) d->table[idx] = MORSE[k].txt;
    }
}

/* ---------------------------------------------------------------- output */

static void emit(cwd_chan_t *c, const char *s)
{
    while (*s && c->outn < CWD_OUT - 1) c->out[c->outn++] = *s++;
}

static int is_auto(const cwd_t *d) { return d->cfg.detector == CWD_DET_AUTO; }

static float chan_snr_db(const cwd_chan_t *c);

/* end of character for detector `k` (0 threshold, 1 Viterbi); t_end in ms */
static void tb_char(const cwd_t *d, cwd_chan_t *c, int k, uint32_t t_end)
{
    cwd_tb_t *b = &c->tb[k];
    if (b->nel > 0) {
        const char *s = (b->code < 256) ? d->table[b->code] : 0;
        if (!s) s = "*";
        if (is_auto(d)) {
            if (b->wn == 0) { b->w_t0 = t_end; b->w_snr = chan_snr_db(c); b->w_wpm = 1200.0f / c->unit; }
            while (*s && b->wn < CWD_WLEN - 1) b->w[b->wn++] = *s++;
            b->w_t1 = t_end;
        } else {
            emit(c, s);
        }
        b->char_since_space = 1;
        b->word_done = 0;
    }
    b->code = 1;
    b->nel = 0;
}

static void merge_words(const cwd_t *d, cwd_chan_t *c, int final);

static void tb_word(const cwd_t *d, cwd_chan_t *c, int k)
{
    cwd_tb_t *b = &c->tb[k];
    if (b->char_since_space && !b->word_done) {
        if (is_auto(d)) {
            if (b->wn > 0) {
                if (c->qn[k] == CWD_WQ) merge_words(d, c, 1);   /* should not happen */
                cwd_word_t *w = &c->q[k][c->qn[k]++];
                for (int i = 0; i < b->wn; i++) w->txt[i] = b->w[i];
                w->txt[b->wn] = 0;
                w->t0 = b->w_t0; w->t1 = b->w_t1; w->snr = b->w_snr; w->wpm = b->w_wpm;
                b->wn = 0;
            }
        } else {
            emit(c, " ");
        }
        b->word_done = 1;
        b->char_since_space = 0;
    }
}

static void tb_elem(cwd_chan_t *c, int k, int is_dash)
{
    cwd_tb_t *b = &c->tb[k];
    if (b->nel < 7) { b->code = b->code * 2 + is_dash; b->nel++; }
    else b->code = 999;                          /* too long → error */
}

/* ---------------------------------------------------------------- AUTO mode */

static float chan_snr_db(const cwd_chan_t *c)
{
    float r = c->noise > 0 ? c->sig / c->noise : 1.0f;
    return 8.6859f * cw_log(r > 1e-6f ? r : 1e-6f);
}

/* Which detector wins for this word? The envelope SNR depends on the filter
 * width (i.e. on speed), so it is converted to an equivalent SNR in 2500 Hz. */
static int auto_pref(const cwd_word_t *w)
{
    float est = w->snr - AUTO_OFS + AUTO_WSLOPE * (w->wpm - 12.0f);
    return (est < AUTO_SNR && w->wpm < AUTO_WPM) ? 1 : 0;
}

static int w_overlap(const cwd_word_t *a, const cwd_word_t *b, float tol)
{
    return (float)a->t0 <= (float)b->t1 + tol && (float)b->t0 <= (float)a->t1 + tol;
}

static void q_pop(cwd_chan_t *c, int k)
{
    for (int i = 1; i < c->qn[k]; i++) c->q[k][i - 1] = c->q[k][i];
    c->qn[k]--;
}

static void out_word(cwd_chan_t *c, const cwd_word_t *w)
{
    emit(c, w->txt);
    emit(c, " ");
}

/* Merges the two detectors' word queues in time order. Each stretch comes from
 * the preferred detector; the other one is used only where the preferred one
 * has nothing. */
static void merge_words(const cwd_t *d, cwd_chan_t *c, int final)
{
    (void)d;
    float tol = 2.0f * c->unit;
    float prog[2];
    {
        const cwd_tb_t *b = &c->tb[0];
        float p0 = (float)c->t - 3.0f * c->unit;
        if (b->wn > 0 && (float)b->w_t0 < p0) p0 = (float)b->w_t0;
        if (c->warm && c->nev > 0 && (float)c->warm_t0 < p0) p0 = (float)c->warm_t0;
        prog[0] = p0;
        const cwd_tb_t *v = &c->tb[1];
        float p1 = c->v ? (float)(c->v->committed * VT_MS) : p0;
        if (v->wn > 0 && (float)v->w_t0 < p1) p1 = (float)v->w_t0;
        prog[1] = p1;
    }
    for (;;) {
        int k;
        if (c->qn[0] && c->qn[1]) k = c->q[0][0].t0 <= c->q[1][0].t0 ? 0 : 1;
        else if (c->qn[0]) k = 0;
        else if (c->qn[1]) k = 1;
        else break;
        int o = 1 - k;
        cwd_word_t *w = &c->q[k][0];
        if ((float)w->t0 <= (float)c->suppress[k]) { q_pop(c, k); continue; }
        int pref = auto_pref(w);
        if (k == pref) {
            out_word(c, w);
            uint32_t sup = w->t1 + (uint32_t)tol;
            if (sup > c->suppress[o]) c->suppress[o] = sup;
            q_pop(c, k);
            continue;
        }
        int ov = 0;
        for (int j = 0; j < c->qn[o]; j++)
            if (w_overlap(w, &c->q[o][j], tol)) { ov = 1; break; }
        if (ov) { q_pop(c, k); continue; }
        if (final || prog[o] > (float)w->t1 + tol) {
            /* filler from the non-preferred detector: only if it looks like a word */
            int len = 0, bad = 0;
            for (const char *q = w->txt; *q; q++) { len++; bad |= *q == '*'; }
            if (len < 2 || bad) { q_pop(c, k); continue; }
            out_word(c, w);
            uint32_t sup = w->t1 + (uint32_t)tol;
            if (sup > c->suppress[o]) c->suppress[o] = sup;
            q_pop(c, k);
            continue;
        }
        break;
    }
}

/* ---------------------------------------------------------------- NCO / filter */

static void set_tone(cwd_chan_t *c, float f)
{
    c->tone = f;
    float w = TWO_PI * f / CWD_RATE;
    c->wr = cw_cos(w);
    c->wi = cw_sin(w);
}

static void set_filter(cwd_chan_t *c)
{
    /* until there are statistics, a wide filter (works at any speed) */
    float u = c->nmarks < CWD_HIST ? clampf(c->unit, 25.0f, 35.0f) : c->unit;
    float div = c->fdiv;
#if V_FILT_ADAPT
    /* Viterbi: fast filter on strong signals (sharp edges), narrow on weak ones */
    if (div > FILT_DIV) {
        float r = c->noise > 0 ? c->sig / c->noise : 0;
        float k = clampf((r - 6.0f) / 6.0f, 0.0f, 1.0f);
        div = FILT_DIV + k * (div - FILT_DIV);
    }
#endif
    float tau_ms = clampf(u / div, 1.0f, TAU_MAX);
    c->alpha = 1.0f / (tau_ms * (CWD_RATE / 1000.0f) + 1.0f);
    c->tau_ms = tau_ms;
}

/* ---------------------------------------------------------------- Viterbi */

/* states */
enum { S_DOT, S_DASH, S_EGAP, S_LGAP, S_WGAP };

static const float LT_M_EGAP = -0.4463f;   /* ln 0.64 */
static const float LT_M_LGAP = -1.3093f;   /* ln 0.27 */
static const float LT_M_WGAP = -2.4079f;   /* ln 0.09 */
static const float LT_G_MARK = -0.6931f;   /* ln 0.5  (gap → dot or dash) */
static const float LT_W_MARK = -0.9163f;   /* ln 0.4  (word gap → dot or dash) */
static const float LT_W_W    = -1.6094f;   /* ln 0.2  (word gap continues) */

static void vit_reset(cwd_vit_t *v)
{
    zero(v, sizeof *v);
    for (int i = 0; i <= CWD_VDMAX; i++) {
        for (int s = 0; s < CWD_VS; s++) v->delta[i][s] = NEG;
        v->aggG[i] = v->aggM[i] = NEG;
    }
    v->delta[0][S_WGAP] = 0.0f;
    v->aggG[0] = LT_W_MARK; v->argG[0] = S_WGAP;
}

/* duration log-probability table: normalized discrete log-normal */
static void vit_durations(cwd_vit_t *v, const cwd_chan_t *c)
{
    float u = c->unit;
    float mu[CWD_VS] = {
        c->dot, c->dash, c->gap, c->gap + 2.0f * u, c->gap + 6.0f * u
    };
    float sdn[CWD_VS] = { V_SIG_MARK, V_SIG_MARK, V_SIG_GAP, V_SIG_GAP, V_SIG_WDN };
    float sup[CWD_VS] = { V_SIG_MARK, V_SIG_MARK * 1.3f, V_SIG_GAP, V_SIG_LUP, V_SIG_WUP };
    for (int s = 0; s < CWD_VS; s++) {
        float m = clampf(mu[s] / VT_MS, 1.5f, (float)CWD_VDMAX);
        v->mu[s] = m;
        float lm = cw_log(m), mx = NEG;
        for (int k = 1; k <= CWD_VDMAX; k++) {
            float z = cw_log((float)k) - lm;
            float sg = z < 0 ? sdn[s] : sup[s];
            float l = -0.5f * z * z / (sg * sg) - cw_log((float)k);
            v->dur[s][k] = l;
            if (l > mx) mx = l;
        }
        float sum = 0.0f;
        for (int k = 1; k <= CWD_VDMAX; k++) sum += cw_exp(v->dur[s][k] - mx);
        float ln = mx + cw_log(sum);
        int dm = 1;
        for (int k = 1; k <= CWD_VDMAX; k++) {
            v->dur[s][k] -= ln;
            if (v->dur[s][k] > -25.0f) dm = k;
        }
        v->dmax[s] = dm;
        v->dur[s][0] = NEG;
    }
}

static void vit_apply(const cwd_t *d, cwd_chan_t *c, int s, uint32_t t0_tick)
{
    uint32_t t = t0_tick * VT_MS;              /* start of the gap = end of the character */
    switch (s) {
    case S_DOT:  tb_elem(c, 1, 0); break;
    case S_DASH: tb_elem(c, 1, 1); break;
    case S_EGAP: break;
    case S_LGAP: tb_char(d, c, 1, t); break;
    case S_WGAP: tb_char(d, c, 1, t); tb_word(d, c, 1); break;
    }
}

/* traces back and emits the segments that end before t − lag */
static void vit_commit(const cwd_t *d, cwd_chan_t *c, uint32_t lag)
{
    cwd_vit_t *v = c->v;
    uint32_t t = v->t;
    if (t <= v->committed + lag) return;
    const int R = CWD_VDMAX + 1;
    int s = 0;
    float best = NEG;
    for (int k = 0; k < CWD_VS; k++)
        if (v->delta[t % R][k] > best) { best = v->delta[t % R][k]; s = k; }

    static uint32_t se[256], st[256];
    static uint8_t ss[256];
    int n = 0;
    uint32_t tt = t;
    while (tt > v->committed && n < 256) {
        int dd = v->bpd[tt % CWD_VRING][s];
        int ps = v->bps[tt % CWD_VRING][s];
        if (dd <= 0) break;
        se[n] = tt; st[n] = tt > (uint32_t)dd ? tt - (uint32_t)dd : 0; ss[n] = (uint8_t)s; n++;
        if ((uint32_t)dd >= tt) break;
        tt -= (uint32_t)dd;
        s = ps;
    }
    for (int i = n - 1; i >= 0; i--) {
        if (se[i] + lag > t) break;
        if (se[i] <= v->committed) continue;
        vit_apply(d, c, ss[i], st[i]);
        v->committed = se[i];
    }
}

static void vit_tick(const cwd_t *d, cwd_chan_t *c, float e)
{
    cwd_vit_t *v = c->v;
    const int R = CWD_VDMAX + 1;

    /* mark and space levels, from the threshold detector */
    if (!v->lv_init) {
        v->ms = c->sig; v->ms2 = c->sig * c->sig * 1.05f;
        v->ss = c->noise; v->ss2 = c->noise * c->noise * 1.3f;
        v->lv_init = 1;
    }
    if (c->conf) { v->ms += (e - v->ms) * 0.03f; v->ms2 += (e * e - v->ms2) * 0.03f; }
    else         { v->ss += (e - v->ss) * 0.01f; v->ss2 += (e * e - v->ss2) * 0.01f; }

    float L;
    if (!c->open) {
        L = -V_LCLIP;
    } else {
#if V_LOGEMIS
        float le = cw_log(e > 1e-9f ? e : 1e-9f);
        float lm = cw_log(v->ms > 1e-9f ? v->ms : 1e-9f), ls = cw_log(v->ss > 1e-9f ? v->ss : 1e-9f);
        const float sm = 0.45f, s0 = 0.6f;
        L = -0.5f * (le - lm) * (le - lm) / (sm * sm) + 0.5f * (le - ls) * (le - ls) / (s0 * s0)
            - cw_log(sm) + cw_log(s0);
#else
        float vs = v->ss2 - v->ss * v->ss, vm = v->ms2 - v->ms * v->ms;
        float fs = 0.25f * v->ss, fm = 0.15f * v->ms;
        if (vs < fs * fs) vs = fs * fs;
        if (vm < fm * fm) vm = fm * fm;
        if (vm < vs) vm = vs;
        L = -0.5f * (e - v->ms) * (e - v->ms) / vm + 0.5f * (e - v->ss) * (e - v->ss) / vs
            - 0.5f * cw_log(vm) + 0.5f * cw_log(vs);
#endif
        L = clampf(L * V_LSCALE, -V_LCLIP, V_LCLIP);
    }

    if ((v->t & 63u) == 0) vit_durations(v, c);

    uint32_t t = ++v->t;
    v->psum[t % R] = v->psum[(t - 1) % R] + L;
    float pt = v->psum[t % R];

    for (int s = 0; s < CWD_VS; s++) {
        int mark = s <= S_DASH;
        int maxd = v->dmax[s];
        if ((uint32_t)maxd > t) maxd = (int)t;
        float best = NEG; int bd = 1, bs = S_WGAP;
        for (int k = 1; k <= maxd; k++) {
            int tp = (int)((t - (uint32_t)k) % (uint32_t)R);
            float seg = pt - v->psum[tp];
            float sc = v->dur[s][k] + (mark ? seg : -seg);
            float prev; int pst;
            if (mark) { prev = v->aggG[tp]; pst = v->argG[tp]; }
            else {
                float lt = s == S_EGAP ? LT_M_EGAP : (s == S_LGAP ? LT_M_LGAP : LT_M_WGAP);
                prev = v->aggM[tp] + lt; pst = v->argM[tp];
                if (s == S_WGAP && v->delta[tp][S_WGAP] + LT_W_W > prev) {
                    prev = v->delta[tp][S_WGAP] + LT_W_W; pst = S_WGAP;
                }
            }
            if (prev <= NEG * 0.5f) continue;
            sc += prev;
            if (sc > best) { best = sc; bd = k; bs = pst; }
        }
        v->delta[t % R][s] = best;
        v->bpd[t % CWD_VRING][s] = (uint16_t)bd;
        v->bps[t % CWD_VRING][s] = (uint8_t)bs;
    }

    /* aggregates for segments starting at t */
    float *dl = v->delta[t % R];
    float g1 = dl[S_EGAP] + LT_G_MARK, g2 = dl[S_LGAP] + LT_G_MARK, g3 = dl[S_WGAP] + LT_W_MARK;
    int ag = S_EGAP; float bg = g1;
    if (g2 > bg) { bg = g2; ag = S_LGAP; }
    if (g3 > bg) { bg = g3; ag = S_WGAP; }
    v->aggG[t % R] = bg; v->argG[t % R] = (uint8_t)ag;
    if (dl[S_DOT] >= dl[S_DASH]) { v->aggM[t % R] = dl[S_DOT]; v->argM[t % R] = S_DOT; }
    else                         { v->aggM[t % R] = dl[S_DASH]; v->argM[t % R] = S_DASH; }

    /* renormalize so long sessions keep precision */
    if ((t & 255u) == 0) {
        float mx = NEG;
        for (int s = 0; s < CWD_VS; s++) if (dl[s] > mx) mx = dl[s];
        float p0 = v->psum[t % R];
        for (int i = 0; i < R; i++) {
            for (int s = 0; s < CWD_VS; s++) if (v->delta[i][s] > NEG * 0.5f) v->delta[i][s] -= mx;
            if (v->aggG[i] > NEG * 0.5f) v->aggG[i] -= mx;
            if (v->aggM[i] > NEG * 0.5f) v->aggM[i] -= mx;
            v->psum[i] -= p0;
        }
    }

    if ((t & 7u) == 0) {
        float lag = V_LAG * c->unit / VT_MS;
        vit_commit(d, c, (uint32_t)clampf(lag, 150.0f, 600.0f));
    }
}

/* ---------------------------------------------------------------- timing (threshold) */

static void kmeans(const float *v, int n, float *c, int k, float clip)
{
    for (int it = 0; it < 5; it++) {
        float sum[3] = {0, 0, 0}; int cnt[3] = {0, 0, 0};
        for (int i = 0; i < n; i++) {
            if (v[i] > clip) continue;
            int best = 0;
            float bd = v[i] > c[0] ? v[i] - c[0] : c[0] - v[i];
            for (int j = 1; j < k; j++) {
                float dd = v[i] > c[j] ? v[i] - c[j] : c[j] - v[i];
                if (dd < bd) { bd = dd; best = j; }
            }
            sum[best] += v[i]; cnt[best]++;
        }
        for (int j = 0; j < k; j++) if (cnt[j]) c[j] = sum[j] / cnt[j];
    }
}

static float pct(const float *v, int n, float q)
{
    float t[CWD_SHIST];
    for (int i = 0; i < n; i++) {
        float x = v[i]; int j = i;
        while (j > 0 && t[j - 1] > x) { t[j] = t[j - 1]; j--; }
        t[j] = x;
    }
    int k = (int)(q * (n - 1) + 0.5f);
    return t[k];
}

/* Dot, dash, gap and unit from recent history. Marks come out longer (+b)
 * and spaces shorter (−b): combining both cancels b. */
static void update_timing(cwd_chan_t *c)
{
    int have_gap = c->nspaces >= 3;
    float gap = c->gap;
    if (have_gap) {
        float mn = pct(c->spaces, c->nspaces, SP_PCT);
        float k3[3] = {mn, 3.0f * mn, 7.0f * mn};
        kmeans(c->spaces, c->nspaces, k3, 3, 1e9f);
        gap = k3[0];
    }
    float u = c->unit;
    if (c->nmarks >= 3) {
        float mn = pct(c->marks, c->nmarks, MK_LO), mx = pct(c->marks, c->nmarks, MK_HI);
        if (mx > 6.0f * mn) mx = 4.0f * mn;
        float k2[2] = {mn, mx};
        kmeans(c->marks, c->nmarks, k2, 2, 6.0f * mn);
        if (k2[1] > 2.0f * k2[0]) {
            c->dot = k2[0]; c->dash = k2[1];
            u = 0.5f * (k2[1] - k2[0]);
            if (have_gap) u = 0.5f * (u + 0.5f * (k2[0] + gap));
        } else {
            float m = 0.5f * (k2[0] + k2[1]);
            if (have_gap) {
                if (m > 2.2f * gap) { u = 0.25f * (m + gap); c->dash = m; c->dot = m - 2.0f * u; }
                else                { u = 0.5f * (m + gap);  c->dot = m;  c->dash = m + 2.0f * u; }
            } else if (m < 0.5f * (c->dot + c->dash)) {
                c->dot = m; c->dash = m + 2.0f * u;
            } else {
                c->dash = m; c->dot = m - 2.0f * u;
            }
        }
    }
    u = clampf(u, (1.0f - UNIT_RATE) * c->unit, (1.0f + UNIT_RATE) * c->unit);
    c->unit = clampf(u, 24.0f, 400.0f);
    c->gap = clampf(gap, 0.3f * c->unit, 1.5f * c->unit);
    if (c->dot < 0.3f * c->unit) c->dot = 0.3f * c->unit;
    set_filter(c);
}

/* Word-gap threshold from this station's own measured spaces, not from the
 * estimated unit: heavy keyer weighting (long dashes) inflates the unit and
 * glues words together ("CQCQDEIK4PLU"), and widely spaced operators split
 * every letter ("C Q D E"). Recent spaces are split in three groups (element,
 * letter and word gaps). When letter and word gaps are clearly apart, the
 * threshold goes between them; otherwise it goes back to the standard 5 units.
 * It depends only on measured gaps, never on the decoded text. */
static void adapt_word_gap(cwd_chan_t *c)
{
    float target = WGAP_MIN;
    int n = c->nspaces;
    if (n >= 12) {
        float e = pct(c->spaces, n, SP_PCT), l = 0, w = 0;
        for (int i = 0; i < n; i++) {                  /* start: shortest and longest long space */
            float x = c->spaces[i];
            if (x < 1.8f * e) continue;
            if (l == 0 || x < l) l = x;
            if (x > w) w = x;
        }
        if (l == 0) l = 3.0f * e;
        if (w <= l) w = 2.0f * l;
        int ne = 0, nl = 0, nw = 0;
        for (int it = 0; it < 8; it++) {               /* 3-means, boundaries on the log scale */
            float b1 = cw_sqrt(e * l), b2 = cw_sqrt(l * w), se = 0, sl = 0, sw = 0;
            ne = nl = nw = 0;
            for (int i = 0; i < n; i++) {
                float x = c->spaces[i];
                if (x < b1) { se += x; ne++; } else if (x < b2) { sl += x; nl++; }
                else { sw += x < 3.0f * l ? x : 3.0f * l; nw++; }   /* pauses between overs count as plain word gaps */
            }
            if (ne) e = se / ne;
            if (nl) l = sl / nl;
            if (nw) w = sw / nw;
        }
        if (ne >= 3 && nl >= 3 && nw >= 2 && l >= 2.2f * e && w >= WG_RATIO * l) {
            float thr = cw_sqrt(l * w);                /* ms */
            target = clampf((thr - c->gap) / c->unit, WGAP_LOW, WGAP_MAX);
        }
    }
    c->wmul += (target - c->wmul) * 0.3f;
}

static void add_element(cwd_chan_t *c, float m)
{
    /* = 2 units + bias. The first element of a transmission is often cut short
     * by the sender's TX switching (semi break-in): "TEST" arrives as "EEST".
     * For it, a mark clearly longer than a dot is already a dash. */
    float th = c->first_el ? c->dot + FIRST_DASH * c->unit : c->dot + c->unit;
    c->first_el = 0;
    tb_elem(c, 0, m > th);
}

/* gap of duration s that started at t0 (ms) */
static void add_space(const cwd_t *d, cwd_chan_t *c, float s, uint32_t t0)
{
    if (c->tb[0].nel > 0 && s > c->gap + c->unit) tb_char(d, c, 0, t0);
    if (s > c->gap + c->wmul * c->unit) tb_word(d, c, 0);
}

/* ------------------------------------------------ operator profiles
 * Several operators often alternate on one frequency (a QSO, a contest run,
 * a pile-up answer). Each keeps its own timing, so mixing them in one history
 * glues or splits words. A channel keeps up to CWD_NPROF profiles: when a new
 * transmission starts (long silence, or a clear level jump after a word gap),
 * the current timing is saved, and once the first elements are buffered the
 * profile that matches their level and speed is loaded (or a new one starts). */

static float ratio_hi(float a, float b) { return a > b ? a / b : b / a; }

static void prof_save(cwd_chan_t *c)
{
    c->prev = c->cur;
    if (c->cur < 0) return;
    cwd_prof_t *p = &c->prof[c->cur];
    for (int i = 0; i < CWD_HIST; i++) p->marks[i] = c->marks[i];
    for (int i = 0; i < CWD_SHIST; i++) p->spaces[i] = c->spaces[i];
    p->nmarks = c->nmarks; p->mpos = c->mpos;
    p->nspaces = c->nspaces; p->spos = c->spos;
    p->dot = c->dot; p->dash = c->dash; p->gap = c->gap; p->unit = c->unit; p->wmul = c->wmul;
    p->level = c->lvl;
    p->last = c->t;
    p->used = 1;
}

static void push_mark(cwd_chan_t *c, float m)
{
    c->marks[c->mpos] = m;
    c->mpos = (c->mpos + 1) % CWD_HIST;
    if (c->nmarks < CWD_HIST) c->nmarks++;
}

static void push_space(cwd_chan_t *c, float s)
{
    c->spaces[c->spos] = s;
    c->spos = (c->spos + 1) % CWD_SHIST;
    if (c->nspaces < CWD_SHIST) c->nspaces++;
}

/* share of the buffered marks that fit a dot or a dash of this timing */
static float timing_fit(const cwd_chan_t *c, float dot, float dash)
{
    int n = 0, ok = 0;
    for (int i = 0; i < c->nev; i++) {
        float m = c->ev[i];
        if (m <= 0) continue;
        n++;
        float a = m > dot ? (m - dot) / dot : (dot - m) / dot;
        float b = m > dash ? (m - dash) / dash : (dash - m) / dash;
        if (a < 0.35f || b < 0.35f) ok++;
    }
    return n ? (float)ok / (float)n : 0.0f;
}

/* end of warm-up: pick the operator profile for the buffered elements */
static void prof_select(cwd_chan_t *c)
{
    float lv = 0, mk[CWD_SHIST]; int nl = 0, nm = 0;
    for (int i = 0; i < c->nev; i++) {
        if (c->ev[i] <= 0) continue;
        lv += c->evl[i]; nl++;
        if (nm < CWD_SHIST) mk[nm++] = c->ev[i];
    }
    if (!nl) return;
    lv /= (float)nl;
    float sdot = pct(mk, nm, 0.2f);             /* short marks: about a dot */
    int best = -1; float bd = 0;
    for (int k = 0; k < CWD_NPROF; k++) {
        const cwd_prof_t *p = &c->prof[k];
        if (!p->used || p->level <= 0 || p->dot <= 0) continue;
        float rl = ratio_hi(lv, p->level), rd = ratio_hi(sdot, p->dot);
        if (rl > PROF_MATCH || rd > 1.7f) continue;
        float dist = rl * rd;
        if (best < 0 || dist < bd) { best = k; bd = dist; }
    }
    if (best >= 0 && best != c->prev) {  /* another known operator: its history + the new elements */
        const cwd_prof_t *p = &c->prof[best];
        for (int i = 0; i < CWD_HIST; i++) c->marks[i] = p->marks[i];
        for (int i = 0; i < CWD_SHIST; i++) c->spaces[i] = p->spaces[i];
        c->nmarks = p->nmarks; c->mpos = p->mpos;
        c->nspaces = p->nspaces; c->spos = p->spos;
        c->dot = p->dot; c->dash = p->dash; c->gap = p->gap; c->unit = p->unit; c->wmul = p->wmul;
        for (int i = 0; i < c->nev; i++) {
            if (c->ev[i] > 0) push_mark(c, c->ev[i]);
            else if (-c->ev[i] <= 3000.0f) push_space(c, -c->ev[i]);
        }
        update_timing(c);
        adapt_word_gap(c);
        c->cur = best;
    } else if (c->prev >= 0 && (best == c->prev || timing_fit(c, c->prof[c->prev].dot, c->prof[c->prev].dash) >= PROF_FIT)) {
        /* the operator just heard (same level), or new elements that fit its
         * rhythm anyway (a fade, or a similar operator): nothing to change,
         * the history already holds that timing plus the new elements */
        c->cur = c->prev;
    } else {                             /* a new operator: only the new elements */
        int slot = 0;
        for (int k = 0; k < CWD_NPROF; k++) {
            if (!c->prof[k].used) { slot = k; break; }
            if (c->prof[k].last < c->prof[slot].last) slot = k;
        }
        c->prof[slot].used = 0;
        c->cur = slot;
        c->nmarks = c->mpos = 0;
        c->nspaces = c->spos = 0;
        for (int i = 0; i < c->nev; i++) {
            if (c->ev[i] > 0) push_mark(c, c->ev[i]);
            else if (-c->ev[i] <= 3000.0f) push_space(c, -c->ev[i]);
        }
        update_timing(c);
        c->wmul = WGAP_MIN;              /* its own word gap, from its own spaces */
        for (int i = 0; i < 12; i++) adapt_word_gap(c);
    }
    c->lvl = lv;
}

/* At the start of a transmission the first elements are buffered and decoded
 * in one go once there are speed statistics. */
/* a pause long enough for the sender's transmitter to drop back to receive */
static int long_pause(const cwd_chan_t *c, float s) { return s > FIRST_GAP_MS && s > FIRST_GAP_U * c->unit; }

static void replay(const cwd_t *d, cwd_chan_t *c)
{
    prof_select(c);
    uint32_t t = c->warm_t0;
    for (int i = 0; i < c->nev; i++) {
        if (c->ev[i] > 0) { add_element(c, c->ev[i]); t += (uint32_t)c->ev[i]; }
        else              { add_space(d, c, -c->ev[i], t); t += (uint32_t)(-c->ev[i]);
                            if (long_pause(c, -c->ev[i])) c->first_el = 1; }
    }
    c->nev = 0;
    c->warm = 0;
}

static void start_warm(cwd_chan_t *c)
{
    c->warm = 1; c->warm_marks = 0; c->nev = 0; c->jump_n = 0;
    /* the timing history is kept while the first elements are buffered: the
     * profile choice at the end of the warm-up decides what to keep */
}

static int use_thr(const cwd_t *d) { return d->cfg.detector != CWD_DET_VITERBI; }
static int use_vit(const cwd_t *d) { return d->cfg.detector != CWD_DET_THRESHOLD; }

static void on_mark(const cwd_t *d, cwd_chan_t *c, float m)
{
    float ml = c->mk_n ? c->mk_sum : c->env;      /* peak envelope of the mark */
    /* a clear level jump is another operator taking over: at once after a
     * pause longer than a letter gap, or after 3 such marks in a row */
    int jump = 0;
    if (use_thr(d) && !c->warm && c->cur >= 0 && c->lvl > 0 && ratio_hi(ml, c->lvl) > PROF_JUMP) {
        jump = 1;
        if (c->last_space > c->gap + 2.5f * c->unit || ++c->jump_n >= PROF_RUN) {
            prof_save(c);
            start_warm(c);
        }
    }
    if (!jump) c->jump_n = 0;
    push_mark(c, m);
    update_timing(c);
    if (!use_thr(d)) return;
    if (!c->warm) c->lvl += (ml - c->lvl) * (jump ? PROF_TRACK : 0.2f);   /* outliers move it slowly (fades) */
    if (c->warm) {
        if (c->nev == 0) c->warm_t0 = c->edge_t;
        c->evl[c->nev] = ml;
        c->ev[c->nev++] = m;
        if (++c->warm_marks >= WARM_N || c->nev >= CWD_EV - 1) replay(d, c);
        return;
    }
    add_element(c, m);
}

static void on_space(const cwd_t *d, cwd_chan_t *c, float s)
{
    c->last_space = s;
    if (s > 3000.0f) {                 /* long silence: new transmission */
        if (use_thr(d)) {
            if (c->warm) replay(d, c);
            add_space(d, c, s, c->edge_t);
            prof_save(c);
            start_warm(c);
        }
        c->first_el = 1;
        return;
    }
    if (!c->warm && long_pause(c, s)) c->first_el = 1;   /* while warm: replay() sees the space */
    push_space(c, s);
    update_timing(c);
    adapt_word_gap(c);
    if (use_thr(d) && c->warm) c->ev[c->nev++] = -s;
}

static void space_timeout(const cwd_t *d, cwd_chan_t *c, float elapsed)
{
    if (!use_thr(d)) return;
    if (c->warm) {
        if (c->nev > 0 && elapsed > 1500.0f && elapsed > 10.0f * c->unit) replay(d, c);
        else return;
    }
    add_space(d, c, elapsed, c->edge_t);
}

/* ---------------------------------------------------------------- envelope (1 ms) */

static void env_step(const cwd_t *d, cwd_chan_t *c, float e)
{
    c->t++;
    c->env = e;
    if (!c->init) {
        c->noise = e > 1e-9f ? e : 1e-9f;
        c->sig = c->noise;
        c->init = 1;
    }

    if (c->conf) {
        c->sig += (e - c->sig) * (e > c->sig ? 0.05f : 0.01f);
    } else {
        c->noise += (e - c->noise) * (e < c->noise ? 0.02f : 0.004f);
        c->sig += (c->noise - c->sig) * 0.0005f;
    }
    if (c->sig < c->noise) c->sig = c->noise;
    if (c->noise < 1e-9f) c->noise = 1e-9f;

    c->open = c->locked && c->sig > d->cfg.squelch * c->noise &&
              c->tone_snr > (d->nch > 1 ? MULTI_OPEN : SQ_TONE);
    /* crosstalk: a much stronger neighbour keying leaks through the filter */
    if (c->open && d->nch > 1) {
        for (int k = 0; k < d->nch; k++) {
            const cwd_chan_t *o = &d->ch[k];
            if (o == c || !o->active || !o->conf) continue;
            float df = o->tone - c->tone;
            if (df < 0) df = -df;
            if (df >= XT_HZ) continue;
            /* what of the neighbour gets through this channel's filter (4 poles),
             * with margin for keying sidebands, compared with this station's own
             * level (not with the envelope now, near zero in its own spaces) */
            float w = 6.2832f * 0.7f * df * c->tau_ms * 0.001f;
            float h = 1.0f / ((1.0f + w * w) * (1.0f + w * w));
            if (o->env * h > XT_LEAK * c->sig) { c->open = 0; break; }
        }
    }
    float range = c->sig - c->noise;
    float lo = c->noise + 0.40f * range;
    float hi = c->noise + 0.55f * range;
    if (hi < 1.8f * c->noise) hi = 1.8f * c->noise;
    if (lo < 1.5f * c->noise) lo = 1.5f * c->noise;
    c->raw = c->conf ? (e > lo) : (e > hi);

    float shortest = c->dot < c->gap ? c->dot : c->gap;
    int glitch = (int)clampf(GL_FRAC * shortest, GL_MIN, GL_MAX);
    if (c->raw != c->conf) {
        if (++c->pend >= glitch) {
            uint32_t edge = c->t - (uint32_t)c->pend;
            float dur = (float)(edge - c->edge_t);
            if (c->open) {
                if (c->conf) on_mark(d, c, dur);
                else         on_space(d, c, dur);
            } else if (use_thr(d)) {
                c->tb[0].code = 1; c->tb[0].nel = 0;
            }
            c->conf = c->raw;
            c->edge_t = edge;
            c->pend = 0;
            if (c->conf) { c->mk_sum = 0; c->mk_n = 0; }   /* mk_sum holds the peak */
        }
    } else {
        c->pend = 0;
    }

    if (c->conf) { if (e > c->mk_sum) c->mk_sum = e; c->mk_n++; }
    if (!c->conf && c->open) space_timeout(d, c, (float)(c->t - c->edge_t));
    if (is_auto(d) && (c->t & 15u) == 0) merge_words(d, c, 0);

    if (use_vit(d)) {
        c->v->acc += e;
        if (++c->v->accn == VT_MS) {
            vit_tick(d, c, c->v->acc / VT_MS);
            c->v->acc = 0; c->v->accn = 0;
        }
    }
}

static void demod(const cwd_t *d, cwd_chan_t *c, float s)
{
    float in_i = s * c->pr, in_q = s * c->pi;
    float a = c->alpha;
    c->fi[0] += (in_i - c->fi[0]) * a;  c->fq[0] += (in_q - c->fq[0]) * a;
    c->fi[1] += (c->fi[0] - c->fi[1]) * a; c->fq[1] += (c->fq[0] - c->fq[1]) * a;
    c->fi[2] += (c->fi[1] - c->fi[2]) * a; c->fq[2] += (c->fq[1] - c->fq[2]) * a;
    c->fi[3] += (c->fi[2] - c->fi[3]) * a; c->fq[3] += (c->fq[2] - c->fq[3]) * a;

    float nr = c->pr * c->wr - c->pi * c->wi;
    c->pi = c->pr * c->wi + c->pi * c->wr;
    c->pr = nr;
    if (++c->renorm == 128) {
        c->renorm = 0;
        float g = 1.5f - 0.5f * (c->pr * c->pr + c->pi * c->pi);
        c->pr *= g; c->pi *= g;
    }

    if (++c->decim == CWD_RATE / 1000) {
        c->decim = 0;
        env_step(d, c, cw_sqrt(c->fi[3] * c->fi[3] + c->fq[3] * c->fq[3]));
    }
}

/* ---------------------------------------------------------------- channels */

static void chan_reset(const cwd_t *d, cwd_chan_t *c, float tone)
{
    zero(c, sizeof *c);
    c->pr = 1.0f;
    c->fdiv = d->cfg.detector == CWD_DET_VITERBI ? V_FILT_DIV : FILT_DIV;
    set_tone(c, tone);
    c->unit = 1200.0f / (d->cfg.wpm_init > 4.0f ? d->cfg.wpm_init : 20.0f);
    c->dot = c->unit; c->dash = 3.0f * c->unit; c->gap = c->unit;
    c->wmul = WGAP_MIN;
    c->cur = c->prev = -1;
    set_filter(c);
    c->tb[0].code = c->tb[1].code = 1;
    c->warm = 1;
    /* Viterbi state only for the first CWD_MAXVIT channels (it is most of the
     * memory); with the threshold detector no channel needs it */
    int k = (int)(c - d->ch);
    c->v = use_vit(d) && k < CWD_MAXVIT ? (cwd_vit_t *)&d->vit[k] : 0;
    if (c->v) {
        vit_reset(c->v);
        vit_durations(c->v, c);
    }
}

/* flushes a channel (end of input or channel freed) */
static void chan_flush(const cwd_t *d, cwd_chan_t *c)
{
    if (use_thr(d)) {
        if (c->warm) replay(d, c);
        tb_char(d, c, 0, c->t);
        if (is_auto(d)) tb_word(d, c, 0);
    }
    if (use_vit(d)) {
        vit_commit(d, c, 0);
        tb_char(d, c, 1, c->v->t * VT_MS);
        if (is_auto(d)) tb_word(d, c, 1);
    }
    if (is_auto(d)) merge_words(d, c, 1);
}

static int bin_of(const cwd_t *d, float f)
{
    int b = (int)((f - d->bin_f0) / d->bin_step + 0.5f);
    return b < 0 ? 0 : (b >= d->nbins ? d->nbins - 1 : b);
}

static float peak_near(const cwd_t *d, int b, int w)
{
    float p = 0.0f;
    for (int k = b - w; k <= b + w; k++)
        if (k >= 0 && k < d->nbins && d->spec[k] > p) p = d->spec[k];
    return p;
}

static float interp_freq(const cwd_t *d, int pk)
{
    float f = d->bin_f0 + pk * d->bin_step;
    if (pk > 0 && pk < d->nbins - 1) {
        float a = d->spec[pk - 1], b = d->spec[pk], c = d->spec[pk + 1];
        float den = a - 2.0f * b + c;
        if (den < 0.0f) f += clampf(0.5f * (a - c) / den, -0.5f, 0.5f) * d->bin_step;
    }
    return f;
}

/* single mode: one channel following the dominant signal */
static void track_single(cwd_t *d, int pk)
{
    cwd_chan_t *c = &d->ch[0];
    c->tone_snr = peak_near(d, bin_of(d, c->tone), 1) / d->floor;
    if (d->cfg.tone_fixed_hz > 0.0f) return;
    if (d->spec[pk] < 2.0f * d->floor) return;
    float f = interp_freq(d, pk);
    if (!c->locked) { set_tone(c, f); c->locked = 1; c->active = 1; return; }
    float df = f - c->tone;
    if (df < 0) df = -df;
    if (df <= 15.0f) set_tone(c, c->tone + (f - c->tone) * 0.25f);
    else if (d->spec[pk] > 2.0f * d->spec[bin_of(d, c->tone)]) set_tone(c, f);
}

/* multi mode: every clear, separate peak gets a channel */
static void track_multi(cwd_t *d)
{
    int nb = d->nbins;
    int used[CWD_NBINS_MAX];
    for (int b = 0; b < nb; b++) used[b] = 0;

    /* 1) active channels follow their peak */
    for (int i = 0; i < d->nch; i++) {
        cwd_chan_t *c = &d->ch[i];
        if (!c->active) continue;
        int cb = bin_of(d, c->tone), pk = cb;
        for (int k = cb - 1; k <= cb + 1; k++)
            if (k >= 0 && k < nb && d->spec[k] > d->spec[pk]) pk = k;
        c->tone_snr = d->spec[pk] / d->floor;
        c->best_snr = c->tone_snr > c->best_snr ? c->tone_snr : c->best_snr * 0.997f;
        /* only fine-tune on a true local maximum, and slowly */
        int is_peak = pk > 0 && pk < nb - 1 && d->spec[pk] >= d->spec[pk - 1] && d->spec[pk] >= d->spec[pk + 1];
        if (c->tone_snr > MULTI_PK && is_peak) {
            c->last_seen = d->frames;
            float f = interp_freq(d, pk);
            float df = clampf(f - c->tone, -4.0f, 4.0f);
            set_tone(c, c->tone + df * 0.25f);
        }
        /* two channels on the same signal: keep the older one */
        for (int j = 0; j < i; j++) {
            cwd_chan_t *o = &d->ch[j];
            if (!o->active) continue;
            int ob = bin_of(d, o->tone);
            if (ob - cb <= MULTI_SEP / 2 && cb - ob <= MULTI_SEP / 2) {
                chan_flush(d, c);
                c->active = 0;
                break;
            }
        }
        if (!c->active) continue;
        for (int k = cb - MULTI_SEP; k <= cb + MULTI_SEP; k++)
            if (k >= 0 && k < nb) used[k] = 1;
        if (d->frames - c->last_seen > MULTI_IDLE && !c->open) {
            chan_flush(d, c);
            c->active = 0;                  /* pending text can still be read */
        }
    }

    /* 2) new peaks → free channels, strongest first */
    for (;;) {
        int pk = -1;
        for (int b = 1; b < nb - 1; b++) {
            if (used[b]) continue;
            if (d->spec[b] < MULTI_PK * d->floor) continue;
            if (d->spec[b] < d->spec[b - 1] || d->spec[b] < d->spec[b + 1]) continue;
            if ((b > 1 && d->spec[b] < d->spec[b - 2]) || (b < nb - 2 && d->spec[b] < d->spec[b + 2])) continue;
            if (pk < 0 || d->spec[b] > d->spec[pk]) pk = b;
        }
        if (pk < 0) break;
        for (int k = pk - MULTI_SEP; k <= pk + MULTI_SEP; k++)
            if (k >= 0 && k < nb) used[k] = 1;
        int slot = -1;
        for (int i = 0; i < d->nch; i++) if (!d->ch[i].active && d->ch[i].outn == 0) { slot = i; break; }
        if (slot < 0)
            for (int i = 0; i < d->nch; i++) if (!d->ch[i].active) { slot = i; break; }
        if (slot < 0) {
            /* all busy: replace the weakest station that is not decoding,
             * if the new one is clearly stronger */
            int w = -1;
            for (int i = 0; i < d->nch; i++) {
                if (d->ch[i].open) continue;
                if (w < 0 || d->ch[i].best_snr < d->ch[w].best_snr) w = i;
            }
            if (w < 0 || d->spec[pk] < MULTI_REPL * d->ch[w].best_snr * d->floor) continue;
            chan_flush(d, &d->ch[w]);
            slot = w;
        }
        cwd_chan_t *c = &d->ch[slot];
        char keep[CWD_OUT]; int kn = c->outn;           /* do not lose unread text */
        for (int k = 0; k < kn; k++) keep[k] = c->out[k];
        chan_reset(d, c, interp_freq(d, pk));
        for (int k = 0; k < kn; k++) c->out[k] = keep[k];
        c->outn = kn;
        c->active = 1; c->locked = 1; c->last_seen = d->frames;
        c->tone_snr = c->best_snr = d->spec[pk] / d->floor;
    }
}

/* k-th smallest of a[0..n) (0-based); reorders a. Hoare's selection. */
static float kth(float *a, int n, int k)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        float piv = a[(lo + hi) >> 1];
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i] < piv) i++;
            while (a[j] > piv) j--;
            if (i <= j) { float t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else break;
    }
    return a[k];
}

static void tone_frame(cwd_t *d)
{
    int nb = d->nbins;
    for (int b = 0; b < nb; b++) {
        float s1 = d->g1[b], s2 = d->g2[b];
        float p = s1 * s1 + s2 * s2 - d->coef[b] * s1 * s2;
        d->spec[b] += (p - d->spec[b]) * 0.04f;
        d->g1[b] = d->g2[b] = 0.0f;
    }
    d->frames++;
    if (d->frames < 8) return;

    /* floor: FLOOR_PCT percentile of the spectrum (robust to peaks and to the
     * silent edges of the receiver passband) */
    int pk = 0;
    for (int b = 1; b < nb; b++) if (d->spec[b] > d->spec[pk]) pk = b;
    {
        /* CW signals are narrow peaks: a moving minimum (±FLOOR_W bins) removes
         * them and leaves the noise, even in a dense pile-up, where the plain
         * spectrum is mostly signals */
        float m[CWD_NBINS_MAX], w[CWD_NBINS_MAX];
        for (int i = 0; i < nb; i++) {
            float v = d->spec[i];
            for (int k = i - FLOOR_W; k <= i + FLOOR_W; k++)
                if (k >= 0 && k < nb && d->spec[k] < v) v = d->spec[k];
            m[i] = v;
        }
        /* only bins inside the passband count: those the radio's filter silences
         * (>13 dB below the noise there) are not "noise". Two order statistics
         * of m, found by selection instead of sorting (same values, less CPU) */
        for (int i = 0; i < nb; i++) w[i] = m[i];
        float pref = kth(w, nb, (int)(0.8f * (nb - 1)));
        int first = 0;
        for (int i = 0; i < nb; i++) if (m[i] < PASS_FRAC * pref) first++;
        if (first > nb - 1) first = nb - 1;
        for (int i = 0; i < nb; i++) w[i] = m[i];
        float fl = FLOOR_K * kth(w, nb, first + (int)(FLOOR_PCT * (nb - 1 - first)));   /* the minimum sits a little below the mean */
        d->floor = fl > 1e-12f ? fl : 1e-12f;
    }

    if (d->cfg.mode == CWD_MODE_MULTI && d->cfg.tone_fixed_hz <= 0.0f) track_multi(d);
    else track_single(d, pk);
}

/* ---------------------------------------------------------------- API */

void cwd_default_cfg(cwd_cfg_t *c)
{
    c->tone_min_hz = 200.0f;
    c->tone_max_hz = 2600.0f;
    c->tone_fixed_hz = 0.0f;
    c->squelch = ENV_SQ;
    c->wpm_init = 20.0f;
    c->mode = CWD_MODE_SINGLE;
    c->detector = CWD_DET_THRESHOLD;
    c->max_channels = CWD_MAXCH;
}

void cwd_init(cwd_t *d, const cwd_cfg_t *c)
{
    zero(d, sizeof *d);
    d->cfg = *c;
    build_table(d);

    d->bin_step = (float)CWD_RATE / GFRAME / 2.0f;     /* half a bin: 15.6 Hz */
    d->bin_f0 = c->tone_min_hz;
    d->nbins = (int)((c->tone_max_hz - c->tone_min_hz) / d->bin_step) + 1;
    if (d->nbins > CWD_NBINS_MAX) d->nbins = CWD_NBINS_MAX;
    if (d->nbins < 3) d->nbins = 3;
    for (int b = 0; b < d->nbins; b++)
        d->coef[b] = 2.0f * cw_cos(TWO_PI * (d->bin_f0 + b * d->bin_step) / CWD_RATE);
    d->floor = 1e-12f;
    for (int k = 0; k < GFRAME; k++) d->win[k] = 0.5f - 0.5f * cw_cos(TWO_PI * k / (GFRAME - 1));

    int multi = c->mode == CWD_MODE_MULTI && c->tone_fixed_hz <= 0.0f;
    int maxch = c->detector == CWD_DET_THRESHOLD ? CWD_MAXCH : CWD_MAXVIT;   /* Viterbi needs its state */
    d->nch = multi ? (c->max_channels < 1 ? 1 : (c->max_channels > maxch ? maxch : c->max_channels)) : 1;
    for (int i = 0; i < d->nch; i++) chan_reset(d, &d->ch[i], 0.5f * (c->tone_min_hz + c->tone_max_hz));
    if (!multi) {
        d->ch[0].active = 1;
        if (c->tone_fixed_hz > 0.0f) { set_tone(&d->ch[0], c->tone_fixed_hz); d->ch[0].locked = 1; }
    }
}

unsigned cwd_process(cwd_t *d, const int16_t *x, int n)
{
    for (int i = 0; i < n; i++) {
        float s = (float)x[i] * (1.0f / 32768.0f);
        float sw = s * d->win[d->gcount];            /* Hann window: less leakage */
        for (int b = 0; b < d->nbins; b++) {
            float g0 = sw + d->coef[b] * d->g1[b] - d->g2[b];
            d->g2[b] = d->g1[b];
            d->g1[b] = g0;
        }
        if (++d->gcount == GFRAME) { d->gcount = 0; tone_frame(d); }
        for (int k = 0; k < d->nch; k++)
            if (d->ch[k].active) demod(d, &d->ch[k], s);
    }
    unsigned mask = 0;
    for (int k = 0; k < d->nch; k++) if (d->ch[k].outn) mask |= 1u << k;
    return mask;
}

unsigned cwd_flush(cwd_t *d)
{
    unsigned mask = 0;
    for (int k = 0; k < d->nch; k++) {
        if (d->ch[k].active) chan_flush(d, &d->ch[k]);
        if (d->ch[k].outn) mask |= 1u << k;
    }
    return mask;
}

int cwd_take(cwd_t *d, int ch, char *out, int outmax)
{
    if (ch < 0 || ch >= d->nch || outmax < 1) return 0;
    cwd_chan_t *c = &d->ch[ch];
    int n = c->outn < outmax - 1 ? c->outn : outmax - 1;
    for (int i = 0; i < n; i++) out[i] = c->out[i];
    out[n] = 0;
    for (int i = n; i < c->outn; i++) c->out[i - n] = c->out[i];
    c->outn -= n;
    return n;
}

int cwd_channels(const cwd_t *d) { return d->nch; }

void cwd_status(const cwd_t *d, int ch, cwd_status_t *s)
{
    const cwd_chan_t *c = &d->ch[ch < 0 || ch >= d->nch ? 0 : ch];
    s->wpm = 1200.0f / c->unit;
    s->tone_hz = c->tone;
    s->snr = c->noise > 0 ? c->sig / c->noise : 0;
    s->active = c->active;
    s->open = c->open;
    s->mark = c->conf;
    s->held = use_thr(d) && c->warm && c->nev > 0;
}
