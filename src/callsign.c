/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* callsign — see callsign.h */
#include "callsign.h"

#define SHOW_MIN 2.5f           /* minimum score to show a callsign */
#ifndef CS_DECAY
#define CS_DECAY 0.95f          /* per word: what was seen ~20 words ago weighs a third */
#endif
#ifndef CS_HYST
#define CS_HYST 0.75f           /* margin needed to change the callsign shown */
#endif

static int is_al(char c) { return c >= 'A' && c <= 'Z'; }
static int is_dg(char c) { return c >= '0' && c <= '9'; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scpy(char *d, const char *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }

/* base without '/': prefix (L, LL, LD or DL) + digits + 1–4 letters
 * (special-event calls with 2+ digits may have up to 6: HA40EUDXF) */
static int is_base(const char *s, int n)
{
    for (int pl = 1; pl <= 2; pl++) {
        if (pl + 2 > n) break;
        if (pl == 1) {
            const char *one = "BFGIKMNRW";
            int ok = 0;
            for (const char *q = one; *q; q++) if (*q == s[0]) ok = 1;
            if (!ok) continue;
        } else {
            if (!(is_al(s[0]) || is_al(s[1]))) continue;
            if (!(is_al(s[0]) || is_dg(s[0])) || !(is_al(s[1]) || is_dg(s[1]))) continue;
            if (is_dg(s[1]) && !is_al(s[0])) continue;
            /* T0, T1: not allocated; in contests "T16" is a cut serial number (016) */
            if (s[0] == 'T' && (s[1] == '0' || s[1] == '1')) continue;
        }
        /* 1 to 4 digits: special-event calls have several (GB100ABC, EA25XYZ) */
        int nd = 0;
        while (pl + nd < n && is_dg(s[pl + nd])) nd++;
        if (nd < 1 || nd > 4) continue;
        int sl = n - pl - nd;
        if (sl < 1 || sl > (nd >= 2 ? 6 : 4)) continue;
        int ok = 1;
        for (int i = pl + nd; i < n; i++) if (!is_al(s[i])) ok = 0;
        if (ok) return 1;
    }
    return 0;
}

int cs_is_call(const char *w)
{
    int n = slen(w);
    if (n < 3 || n > CS_LEN) return 0;
    if (w[0] == 'Q') return 0;                       /* Q series: unallocated */
    for (int i = 0; i < n; i++) if (!(is_al(w[i]) || is_dg(w[i]) || w[i] == '/')) return 0;
    for (int i = 0; i + 2 < n; i++) if (w[i] == '5' && w[i + 1] == 'N' && w[i + 2] == 'N') return 0;
    /* with '/': the longest part is the base (EA4XYZ/P, F/EA4XYZ) */
    int best_a = 0, best_n = 0, a = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || w[i] == '/') {
            if (i - a > best_n) { best_n = i - a; best_a = a; }
            a = i + 1;
        }
    }
    return is_base(w + best_a, best_n);
}

static int is_report(const char *w)
{
    /* 5NN, 599, 579, 5NN7… (a report, maybe with something stuck to it) */
    if (w[0] == '5' && (w[1] == 'N' || is_dg(w[1])) && (w[2] == 'N' || is_dg(w[2]))) return 1;
    if (seq(w, "RST")) return 1;
    return 0;
}

static void bump(cs_t *s, const char *call, float d)
{
    int k = -1, worst = 0;
    for (int i = 0; i < CS_CANDS; i++) {
        if (s->c[i].call[0] && seq(s->c[i].call, call)) { k = i; break; }
        if (s->c[i].score < s->c[worst].score || !s->c[i].call[0]) worst = i;
    }
    if (k < 0) {
        if (d <= 0) return;
        k = worst;
        scpy(s->c[k].call, call, CS_LEN + 1);
        s->c[k].score = 0;
    }
    s->c[k].score += d;
}

/* one edit away (one character changed, inserted or deleted)? */
static int near1(const char *a, const char *b)
{
    int na = slen(a), nb = slen(b);
    if (na == nb) {
        int d = 0;
        for (int i = 0; i < na; i++) d += a[i] != b[i];
        return d == 1;
    }
    if (na - nb == 1 || nb - na == 1) {
        const char *l = na > nb ? a : b, *sh = na > nb ? b : a;
        int i = 0, j = 0, skip = 0;
        while (l[i]) {
            if (l[i] == sh[j]) { i++; j++; }
            else if (!skip) { skip = 1; i++; }
            else return 0;
        }
        return 1;
    }
    return 0;
}

/* a is b cut short by 2–3 letters (HA40EU of HA40EUDXF: the end was lost) */
static int cut_of(const char *a, const char *b)
{
    int na = slen(a), nb = slen(b);
    if (nb - na < 2 || nb - na > 3) return 0;
    for (int i = 0; i < na; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* score plus support from close variants (decodes with one error, or cut short) */
static float support(const cs_t *s, int k)
{
    float v = s->c[k].score;
    for (int i = 0; i < CS_CANDS; i++)
        if (i != k && s->c[i].call[0] && s->c[i].score > 0 &&
            (near1(s->c[i].call, s->c[k].call) || cut_of(s->c[i].call, s->c[k].call)))
            v += 0.5f * s->c[i].score;
    return v;
}

static void decide(cs_t *s, const char *forced)
{
    if (forced) { scpy(s->best, forced, CS_LEN + 1); return; }
    int k = -1; float bk = 0;
    for (int i = 0; i < CS_CANDS; i++) {
        if (!s->c[i].call[0]) continue;
        float v = support(s, i);
        if (k < 0 || v > bk) { k = i; bk = v; }
    }
    if (k >= 0 && bk >= SHOW_MIN) {
        /* the callsign shown only changes if another one clearly beats it */
        if (!s->best[0]) scpy(s->best, s->c[k].call, CS_LEN + 1);
        else {
            float cur = 0;
            for (int i = 0; i < CS_CANDS; i++) if (seq(s->c[i].call, s->best)) cur = support(s, i);
            if (bk > cur + CS_HYST) scpy(s->best, s->c[k].call, CS_LEN + 1);
        }
    }
}

/* one complete token with its context (tok[0] = the previous one) */
static void token(cs_t *s, const char *t0)
{
    const char *p1 = s->tok[0], *p2 = s->tok[1];
    const char *forced = 0;
    const char *t = t0;

    /* known callsign with an ending stuck to it: DL1ABCBK, EA4XYZK, F5XYZTU… */
    static const char *tails[] = { "BK", "KN", "AR", "SK", "TU", "K", 0 };
    char cut[CS_LEN + 4];
    for (int i = 0; i < CS_CANDS && t == t0; i++) {
        const char *c = s->c[i].call;
        int n = slen(c);
        if (!n || (s->c[i].score < 0.3f && !seq(c, s->best))) continue;
        int j = 0;
        while (j < n && t0[j] == c[j]) j++;
        if (j != n) continue;
        for (int k = 0; tails[k]; k++) if (seq(t0 + n, tails[k])) { scpy(cut, c, sizeof cut); t = cut; break; }
    }

    for (int i = 0; i < CS_CANDS; i++) s->c[i].score *= CS_DECAY;   /* forgetting: recent evidence wins */

    if (cs_is_call(t)) {
        bump(s, t, 1.0f);
        if (seq(p1, "DE")) { bump(s, t, 4.0f); forced = t; }
        else if (seq(p1, "CQ") || ((seq(p1, "TEST") || seq(p1, "DX") || seq(p1, "POTA") || seq(p1, "SOTA")) && seq(p2, "CQ"))) {
            bump(s, t, 3.0f); forced = t;
        }
        else if (seq(p1, "TU") || seq(p1, "TEST")) bump(s, t, 2.0f);
        else if (seq(p2, "CQ") || seq(p2, "DE")) bump(s, t, 2.0f);  /* CQ/DE with something misdecoded in between */
        if (seq(p1, t)) bump(s, t, 2.0f);                          /* repeated */
    } else if (seq(t, "CQ")) {
        s->cq = 1;                                                  /* calling CQ: no QSO yet */
        s->peer[0] = 0;
    } else if (seq(t, "TEST")) {
        s->cq = 1;                                                  /* contest CQ: "TEST X" or "X TEST" */
        s->peer[0] = 0;
        if (cs_is_call(p1)) bump(s, p1, 2.0f);                      /* "X TEST": X is calling in a contest */
    } else if (seq(t, "DE") && cs_is_call(p1)) {
        bump(s, p1, -3.0f);                                         /* "X DE …": X is the other station */
        if (seq(s->best, p1)) s->best[0] = 0;
        if (!seq(p2, "CQ")) { scpy(s->peer, p1, CS_LEN + 1); s->cq = 0; }
    } else if (is_report(t) && cs_is_call(p1)) {
        bump(s, p1, -2.0f);                                         /* "X 5NN": X is the other station */
        if (seq(s->best, p1)) s->best[0] = 0;
        scpy(s->peer, p1, CS_LEN + 1);
        s->cq = 0;
    } else if (is_report(t)) {
        s->cq = 0;                                                  /* sending reports: already in a QSO */
        /* "X GE 5NN", "X TU 599", "X R 5NN"…: X is the other station */
        static const char *mid[] = { "GE", "GM", "GA", "GD", "TU", "R", "UR", "RST", 0 };
        int is_mid = 0;
        for (int i = 0; mid[i]; i++) if (seq(p1, mid[i])) is_mid = 1;
        if (is_mid && cs_is_call(p2) && !seq(p2, s->best)) {
            bump(s, p2, -1.0f);
            scpy(s->peer, p2, CS_LEN + 1);
        }
    }
    if (cs_is_call(t) && seq(p1, "TU") && seq(s->best, t)) s->cq = 0;

    scpy(s->tok[2], s->tok[1], CS_LEN + 4);
    scpy(s->tok[1], s->tok[0], CS_LEN + 4);
    scpy(s->tok[0], t, CS_LEN + 4);
    decide(s, forced);
}

void cs_reset(cs_t *s)
{
    char *p = (char *)s;
    for (unsigned i = 0; i < sizeof *s; i++) p[i] = 0;
}

/* "DEEA4XYZ" or "CQEA4XYZ" (lost space): split into two tokens */
static void token_split(cs_t *s, const char *t)
{
    if (!cs_is_call(t) && slen(t) > 4 &&
        ((t[0] == 'D' && t[1] == 'E') || (t[0] == 'C' && t[1] == 'Q')) && cs_is_call(t + 2)) {
        char pre[3] = { t[0], t[1], 0 };
        token(s, pre);
        token(s, t + 2);
        return;
    }
    token(s, t);
}

void cs_feed(cs_t *s, const char *text)
{
    for (; *text; text++) {
        char c = *text;
        if (c == ' ' || c == '=' || c == ',' || c == '.') {
            if (s->cn) { s->cur[s->cn] = 0; token_split(s, s->cur); s->cn = 0; }
            if (c == '=') token(s, "=");
            continue;
        }
        if (s->cn < CS_LEN + 2) s->cur[s->cn++] = c;
        else s->cn = CS_LEN + 3;        /* too long: discarded when closed */
    }
}

const char *cs_best(const cs_t *s) { return s->best; }

const char *cs_peer(const cs_t *s)
{
    /* the other station is never the station itself, nor a misread of it
     * (OE9EBI next to OE9IBI) */
    return (s->peer[0] && !seq(s->peer, s->best) && !near1(s->peer, s->best)) ? s->peer : "";
}

int cs_cq(const cs_t *s) { return s->cq; }
