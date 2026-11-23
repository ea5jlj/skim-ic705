/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
#include "words.h"
#include "callsign.h"

#define TOKMAX 80              /* longest token that is examined */

static int is_al(char c) { return c >= 'A' && c <= 'Z'; }
static int is_dg(char c) { return c >= '0' && c <= '9'; }

/* Vocabulary: S = strong (on its own it shows the token is glued CW text),
 * W = weak (common, but could also be part of a plain-language word). */
#define S 1
#define W 0
static const struct { const char *w; char strong; } vocab[] = {
    {"CQ",S}, {"DE",S}, {"TEST",S}, {"TU",S}, {"73",S}, {"72",S}, {"88",S},
    {"RST",S}, {"QTH",S}, {"NAME",S}, {"QSL",S}, {"QRZ",S}, {"QRL",S}, {"QSO",S},
    {"QRM",S}, {"QRN",S}, {"QSB",S}, {"QSY",S}, {"QRP",S}, {"QRO",S}, {"QRS",S},
    {"QRQ",S}, {"QRU",S}, {"QRV",S}, {"QRT",S}, {"QSK",S}, {"TNX",S}, {"TKS",S},
    {"PSE",S}, {"AGN",S}, {"CFM",S}, {"CPY",S}, {"CUL",S}, {"BCNU",S}, {"SRI",S},
    {"HIHI",S}, {"GL",S}, {"BK",S}, {"KN",S}, {"SK",S}, {"FB",S}, {"GM",S}, {"GA",S},
    {"GE",S}, {"GN",S}, {"WX",S}, {"PWR",S}, {"ANT",S}, {"RIG",S}, {"UR",S}, {"HR",S},
    {"HW",S}, {"VY",S}, {"OM",S}, {"YL",S}, {"XYL",S}, {"DX",S}, {"NR",S}, {"TNK",S},
    {"EE",S}, {"ABT",S}, {"RPT",S},
    {"ES",W}, {"FER",W}, {"OP",W}, {"AR",W}, {"TEE",W}, {"HI",W}, {"OK",W},
    {"INFO",W}, {"DR",W}, {"GD",W}, {"BEST",W}, {"CALL",W},
    {"NW",W}, {"NIL",W}, {"ALL",W}, {"WATTS",W}, {"WATT",W}, {"MY",W}, {"IS",W},
    {"THE",W}, {"AND",W}, {"FOR",W}, {"TO",W}, {"IN",W}, {"ON",W}, {"SUN",W},
    {"K",W}, {"R",W},
    {0,0}
};
#undef S
#undef W

/* strength of a piece: -1 = not recognized, 0 = weak, 1 = strong */
static int vocab_find(const char *p, int n)
{
    for (int i = 0; vocab[i].w; i++) {
        const char *w = vocab[i].w;
        int k = 0;
        while (k < n && w[k] && w[k] == p[k]) k++;
        if (k == n && !w[k]) return vocab[i].strong;
    }
    return -1;
}

static int is_report(const char *p, int n)   /* 5NN, 599, 579… */
{
    return n == 3 && p[0] == '5' && (p[1] == 'N' || is_dg(p[1])) && (p[2] == 'N' || is_dg(p[2]));
}

static int is_number(const char *p, int n)
{
    if (n < 1 || n > 4) return 0;
    for (int i = 0; i < n; i++) if (!is_dg(p[i])) return 0;
    return 1;
}

static int same(const char *p, int n, const char *k)
{
    int i = 0;
    for (; i < n; i++) if (k[i] != p[i]) return 0;
    return k[n] == 0;
}

static int known_find(const char *p, int n, const char *const *known, int nk)
{
    for (int i = 0; i < nk; i++) if (known[i] && known[i][0] && same(p, n, known[i])) return 1;
    return 0;
}

static int call_ok(const char *p, int n)
{
    char b[TOKMAX + 1];
    if (n > TOKMAX) return 0;
    for (int i = 0; i < n; i++) b[i] = p[i];
    b[n] = 0;
    return cs_is_call(b);
}

/* as a piece of a glued token: a suffix of 5–6 letters (HA40EUDXF) is only
 * taken when the call is known, or "GM5NN73TUEE" would give N73TUEE */
static int call_piece(const char *p, int n)
{
    int sl = 0;
    while (sl < n && is_al(p[n - 1 - sl])) sl++;
    return sl <= 4 && call_ok(p, n);
}

/* cost of using p[0..n) as one piece (lower is better); <0 = not allowed.
 * *strong: 2 = callsign or report, 1 = strong word, 0 = weak word or number */
static float piece_cost(const char *p, int n, const char *const *known, int nk, int *strong)
{
    *strong = 0;
    if (known_find(p, n, known, nk)) { *strong = 2; return 0.6f; }
    int v = vocab_find(p, n);
    if (v >= 0) { *strong = v; return n == 1 ? 1.5f : 1.0f; }
    if (is_report(p, n)) { *strong = 2; return 1.0f; }
    if (n >= 4 && call_piece(p, n)) {
        /* rarer shapes cost a little more: 4-letter suffix, several digits */
        int sl = 0, nd = 0;
        while (sl < n && is_al(p[n - 1 - sl])) sl++;
        for (int i = 0; i < n; i++) if (is_dg(p[i])) nd++;
        *strong = 2;
        return 1.2f + (sl >= 4 ? 0.3f : 0.0f) + (nd > 1 ? 1.0f : 0.0f)
                    + (known_find(p, n - 1, known, nk) ? 1.5f : 0.0f);   /* EA5JLJK next to EA5JLJ */
    }
    if (is_number(p, n)) return 1.4f;
    /* stray dots stuck to a word (IIICQDX): a piece of its own, so the
     * noise filter can drop it; it does not count as evidence */
    if (n >= 2 && n <= 4) {
        int dots = 1;
        for (int i = 0; i < n; i++) if (p[i] != 'I') dots = 0;
        if (dots) { *strong = -1; return 1.0f; }
    }
    return -1.0f;
}

/* a token that stands on its own and is left as it is */
static int recognized(const char *p, int n, const char *const *known, int nk)
{
    return known_find(p, n, known, nk) || vocab_find(p, n) >= 0 || is_report(p, n) ||
           is_number(p, n) || call_ok(p, n);
}

/* Best split of a token into recognized pieces. Writes it to o (pieces
 * separated by spaces) and returns its length, or 0 if it is left alone. */
static int split(const char *p, int n, char *o, const char *const *known, int nk, int next)
{
    /* only callsigns known from outside the line protect a whole token */
    if (n < 4 || n > TOKMAX || known_find(p, n, known, next)) return 0;
    int as_call = call_ok(p, n);          /* may still be a known call + a word: OS5ZK */
    if (!as_call && recognized(p, n, known, nk)) return 0;
    for (int i = 0; i < n; i++) if (!is_al(p[i]) && !is_dg(p[i])) return 0;
    /* DP over (position, last piece was DE/CQ): a callsign right after DE or
     * CQ is cheaper, so "K5KVDEW6SFM" → "K5KV DE W6SFM", not "K5KVD EW6SFM" */
    float bc[TOKMAX + 1][2];
    int from[TOKMAX + 1][2], fs[TOKMAX + 1][2], top[TOKMAX + 1][2], weak[TOKMAX + 1][2];
    int kn[TOKMAX + 1][2], pcs[TOKMAX + 1][2];
    /* never cut inside a report: "GM5NN73TUEE" is not "GM5N N73TUEE" */
    char nocut[TOKMAX + 1] = {0};
    for (int i = 0; i + 2 < n; i++)
        if (is_report(p + i, 3)) nocut[i + 1] = nocut[i + 2] = 1;
    for (int i = 0; i <= n; i++) bc[i][0] = bc[i][1] = -1;
    bc[0][0] = 0; top[0][0] = weak[0][0] = kn[0][0] = pcs[0][0] = 0; from[0][0] = -1;
    for (int i = 1; i <= n; i++) {
        for (int j = (i > 12 ? i - 12 : 0); j < i; j++) {
            if (as_call && j == 0 && i == n) continue;      /* not the whole token as is */
            int st;
            float c = piece_cost(p + j, i - j, known, nk, &st);
            if (c < 0 || nocut[j] || nocut[i]) continue;
            int m = i - j;
            int dc = m == 2 && ((p[j] == 'D' && p[j + 1] == 'E') || (p[j] == 'C' && p[j + 1] == 'Q'));
            int callp = st == 2 && !is_report(p + j, m);
            for (int s0 = 0; s0 < 2; s0++) {
                if (bc[j][s0] < 0) continue;
                float t = bc[j][s0] + c - (s0 && callp ? 1.2f : 0.0f);
                if (bc[i][dc] < 0 || t < bc[i][dc]) {
                    bc[i][dc] = t; from[i][dc] = j; fs[i][dc] = s0; pcs[i][dc] = pcs[j][s0] + 1;
                    top[i][dc] = st > top[j][s0] ? st : top[j][s0];
                    weak[i][dc] = weak[j][s0] + (st == 0);
                    kn[i][dc] = kn[j][s0] | known_find(p + j, m, known, nk);
                }
            }
        }
    }
    int e = (bc[n][1] >= 0 && (bc[n][0] < 0 || bc[n][1] < bc[n][0])) ? 1 : 0;
    if (bc[n][e] < 0 || pcs[n][e] < 2) return 0;
    if (top[n][e] < 2 && weak[n][e] > 0) return 0;
    if (as_call && !kn[n][e]) return 0;
    int cut[TOKMAX + 1], nc = 0;
    for (int i = n, st = e; i > 0; ) { cut[nc++] = i; int j = from[i][st]; st = fs[i][st]; i = j; }
    int on = 0, a = 0;
    for (int c = nc - 1; c >= 0; c--) {
        if (on) o[on++] = ' ';
        for (int i = a; i < cut[c]; i++) o[on++] = p[i];
        a = cut[c];
    }
    return on;
}

/* "EA5" + "RJ": prefix and digits only, then letters only */
static int prefix_digits(const char *p, int n)
{
    for (int pl = 1; pl <= 2; pl++) {
        if (pl == 1) {
            const char *one = "BFGIKMNRW";
            int ok = 0;
            for (const char *q = one; *q; q++) if (*q == p[0]) ok = 1;
            if (!ok) continue;
        } else if (n < 3 || !is_al(p[0]) || !(is_al(p[1]) || is_dg(p[1]))) continue;
        int nd = 0;
        while (pl + nd < n && is_dg(p[pl + nd])) nd++;
        if (nd >= 1 && nd <= 2 && pl + nd == n) return 1;      /* 3+ digits: rare, keep apart */
    }
    return 0;
}

static int letters_only(const char *p, int n)
{
    if (n < 1 || n > 4) return 0;
    for (int i = 0; i < n; i++) if (!is_al(p[i])) return 0;
    return 1;
}

static int should_join(const char *a, int na, const char *b, int nb, const char *const *known, int nk)
{
    char t[TOKMAX + 1];
    if (na + nb > TOKMAX) return 0;
    for (int i = 0; i < na; i++) t[i] = a[i];
    for (int i = 0; i < nb; i++) t[na + i] = b[i];
    t[na + nb] = 0;
    if (known_find(t, na + nb, known, nk)) return 1;
    return prefix_digits(a, na) && letters_only(b, nb) && vocab_find(b, nb) < 0 && cs_is_call(t);
}

static int tokens(const char *in, const char **ws, int *wn, int max)
{
    int n = 0;
    while (*in && n < max) {
        while (*in == ' ') in++;
        if (!*in) break;
        ws[n] = in;
        while (*in && *in != ' ') in++;
        wn[n] = (int)(in - ws[n]);
        n++;
    }
    return n;
}

static int pass(const char *in, char *out, int max, const char *const *known, int nk, int next)
{
    const char *ws[64]; int wn[64];
    int n = tokens(in, ws, wn, 64);
    int on = 0;
    char buf[2 * TOKMAX + 2];
    for (int i = 0; i < n; i++) {
        const char *p = ws[i]; int m = wn[i];
        char joined[TOKMAX + 1];
        if (i + 1 < n && should_join(ws[i], wn[i], ws[i + 1], wn[i + 1], known, nk)) {
            for (int k = 0; k < wn[i]; k++) joined[k] = ws[i][k];
            for (int k = 0; k < wn[i + 1]; k++) joined[wn[i] + k] = ws[i + 1][k];
            p = joined; m = wn[i] + wn[i + 1];
            i++;
        }
        int sm = split(p, m, buf, known, nk, next);
        if (sm) { p = buf; m = sm; }
        if (on && on < max - 1) out[on++] = ' ';
        for (int k = 0; k < m && on < max - 1; k++) out[on++] = p[k];
    }
    if (max > 0) out[on] = 0;
    return on;
}

/* known list = the caller's callsigns + the callsigns standing alone in text */
static int add_line_calls(const char *text, const char **kl, int nkl, int maxk, char (*lc)[16], int maxlc)
{
    const char *ws[64]; int wn[64], nlc = 0;
    int n = tokens(text, ws, wn, 64);
    for (int i = 0; i < n && nlc < maxlc && nkl < maxk; i++) {
        if (wn[i] < 4 || wn[i] > 15 || !call_ok(ws[i], wn[i])) continue;
        if (known_find(ws[i], wn[i] - 1, kl, nkl)) continue;      /* EA5JLJK after EA5JLJ */
        for (int k = 0; k < wn[i]; k++) lc[nlc][k] = ws[i][k];
        lc[nlc][wn[i]] = 0;
        kl[nkl++] = lc[nlc++];
    }
    return nkl;
}

int wd_fix(const char *in, char *out, int max, const char *const *known, int nknown)
{
    const char *kl[64]; char lc[24][16];
    int next = nknown < 32 ? nknown : 32;
    for (int i = 0; i < next; i++) kl[i] = known[i];
    /* pass 1: callsigns already standing alone in the line */
    int nk = add_line_calls(in, kl, next, 64, lc, 24);
    char tmp[256];
    pass(in, tmp, sizeof tmp, kl, nk, next);
    /* pass 2: also the callsigns pass 1 found ("CQDEEA5JLJEA5JLJK") */
    nk = add_line_calls(tmp, kl, next, 64, lc, 24);
    return pass(in, out, max, kl, nk, next);
}
