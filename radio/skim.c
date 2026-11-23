/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * SKIM — ICAP app for the IC-705 / IC-9700 (SQ6EMM custom firmware, v1rc).
 * Receive-only multi-signal CW decoder: it never transmits.
 *
 * Output:
 *   - native DECODE screen: one line per decoded phrase,
 *     "7029.9 CQ EA4XYZ: CQ CQ DE EA4XYZ K";
 *   - the app's own 8-line panel (only when started from MENU > SET > Apps):
 *     status, text spectrum and text / one line per station;
 *   - SKIMnn.LOG in the Apps folder (one per session, the last `logs` kept): same as DECODE, plus time.
 *
 * Settings in SKIM.CFG (key=value, one per line, all optional):
 *   mode=multi|single     several stations (default) / one channel following the strongest signal
 *   det=thr|vit|auto      detector (thr works best on real audio)
 *   tmin=250 tmax=2500    tone search range (Hz); should cover the radio's filter
 *   tone=0                >0: fixed tone (e.g. the radio's CW pitch)
 *   wpm=20                initial speed
 *   chans=8               channels in multi mode (1..12; 1..4 with det=vit/auto)
 *   cols=30               panel line width
 *   dcols=40              DECODE and log line width
 *   pause=1500            ms of silence that close a DECODE line
 *   calls=1               show each station's detected callsign
 *   pitch=600             the radio's CW PITCH: each line then shows the station's
 *                         real frequency (dial − (tone − pitch)); 0 = show the tone
 *   cwr=0                 1 for CW-R, or if "CW Normal Side" is set to USB
 *   clean=1               remove typical noise from DECODE (stray E/T, *)
 *   decode=1 log=1        send to the DECODE screen / write the log
 *   spec=2                text spectrum line: 0 off, 2 digits 0-9 (fixed width),
 *                         3 ASCII .:-=+*#, 1 blocks (the IC-705 font lacks them: "?")
 *   fonttest=1            show a test line for 10 s to see which characters render
 *
 * The core (../src/cwdec.c) is freestanding C; its state (~157 KB) lives in the
 * app heap (icap_heap), like the SDK's reference decoders.
 */
#include "icap.h"
#include "../src/cwdec.h"
#include "../src/callsign.h"
#include "../src/words.h"

/* ------------------------------------------------ minimal libc (the compiler
 * may emit calls to these for struct copies). The desktop simulator (SKIM_HOST)
 * uses the system libc. */
#ifndef SKIM_HOST
void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, unsigned n)
{
    unsigned char *a = d; const unsigned char *b = s;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, unsigned n)
{
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}
int memcmp(const void *x, const void *y, unsigned n)
{
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
#endif

/* ------------------------------------------------ strings */

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char *scpy(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }

static char *sint(char *d, int v, int width)
{
    char t[12]; int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)-v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) t[n++] = '-';
    while (n < width) t[n++] = ' ';
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}

static char *s2(char *d, unsigned v) { *d++ = (char)('0' + v / 10 % 10); *d++ = (char)('0' + v % 10); *d = 0; return d; }

static int sval(const char *s) { int v = 0, neg = 0; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

static int keyis(const char *line, const char *key)
{
    while (*key) if (*line++ != *key++) return 0;
    return *line == '=';
}

/* ------------------------------------------------ settings */

static struct {
    int mode, det, chans, cols, dcols, decode, log, wpm;
    int tmin, tmax, tone, spec, fonttest, pause, calls, pitch, cwr, clean, words, logs;
} cfg = { CWD_MODE_MULTI, CWD_DET_THRESHOLD, 8, 30, 40, 1, 1, 20, 250, 2500, 0, 2, 0, 1500, 1, 600, 0, 1, 1, 10 };

static void read_cfg(void)
{
    int fd = icap_open("SKIM.CFG", ICAP_O_READ);
    if (fd < 0) return;
    static char buf[4096];                  /* SKIM.CFG with its comments is ~2.5 KB */
    int n = icap_read(fd, buf, sizeof buf - 1);
    icap_close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (char *p = buf; *p;) {
        char *e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        char c = *e; *e = 0;
        if (*p != '#') {
            const char *v = p;
            while (*v && *v != '=') v++;
            if (*v == '=') v++;
            if (keyis(p, "mode")) cfg.mode = (v[0] == 'm' || v[0] == 'M') ? CWD_MODE_MULTI : CWD_MODE_SINGLE;
            else if (keyis(p, "det")) cfg.det = (v[0] == 'v' || v[0] == 'V') ? CWD_DET_VITERBI :
                                                (v[0] == 'a' || v[0] == 'A') ? CWD_DET_AUTO : CWD_DET_THRESHOLD;
            else if (keyis(p, "tmin")) cfg.tmin = sval(v);
            else if (keyis(p, "tmax")) cfg.tmax = sval(v);
            else if (keyis(p, "tone")) cfg.tone = sval(v);
            else if (keyis(p, "wpm")) cfg.wpm = sval(v);
            else if (keyis(p, "chans")) cfg.chans = sval(v);
            else if (keyis(p, "cols")) cfg.cols = sval(v);
            else if (keyis(p, "dcols")) cfg.dcols = sval(v);
            else if (keyis(p, "spec")) cfg.spec = sval(v);
            else if (keyis(p, "fonttest")) cfg.fonttest = sval(v);
            else if (keyis(p, "pause")) cfg.pause = sval(v);
            else if (keyis(p, "calls")) cfg.calls = sval(v);
            else if (keyis(p, "pitch")) cfg.pitch = sval(v);
            else if (keyis(p, "cwr")) cfg.cwr = sval(v);
            else if (keyis(p, "clean")) cfg.clean = sval(v);
            else if (keyis(p, "words")) cfg.words = sval(v);
            else if (keyis(p, "decode")) cfg.decode = sval(v);
            else if (keyis(p, "log")) cfg.log = sval(v);
            else if (keyis(p, "logs")) cfg.logs = sval(v);
        }
        *e = c;
        p = e;
        while (*p == '\n' || *p == '\r') p++;
    }
    if (cfg.logs < 1) cfg.logs = 1;
    if (cfg.logs > 98) cfg.logs = 98;
    if (cfg.cols < 16) cfg.cols = 16;
    if (cfg.cols > 60) cfg.cols = 60;
    if (cfg.dcols < 20) cfg.dcols = 20;
    if (cfg.dcols > 72) cfg.dcols = 72;
    if (cfg.pause < 500) cfg.pause = 500;
    if (cfg.tmax - cfg.tmin > 2400) cfg.tmax = cfg.tmin + 2400;
    if (cfg.tmin < 100) cfg.tmin = 100;
    if (cfg.tmax <= cfg.tmin + 100) cfg.tmax = cfg.tmin + 100;
}

/* ------------------------------------------------ text per channel */

#define PL 6                    /* text lines in the panel (single mode) */
#define PW 64                   /* maximum panel line width */
static struct {
    char tail[PW + 1];  /* end of the text on one line (multi mode) */
    int  tn;
    char line[80];      /* phrase being built for DECODE / log */
    int  ln;
    uint32_t dial_hz;   /* dial frequency when the phrase started */
    float    tone;      /* audio tone of the phrase being built */
    uint32_t last_ms;   /* last character received */
} ch[CWD_MAXCH];

/* single-mode panel: word-wrapped lines */
static char pan[PL][PW + 1];
static int  pan_n;              /* length of the current (last) line */

static void pan_scroll(void)
{
    for (int i = 0; i < PL - 1; i++) scpy(pan[i], pan[i + 1]);
    pan[PL - 1][0] = 0;
    pan_n = 0;
}

static void pan_add(char c)
{
    int w = cfg.cols < PW ? cfg.cols : PW;
    char *L = pan[PL - 1];
    if (c == ' ' && (pan_n == 0 || L[pan_n - 1] == ' ')) return;
    if (pan_n >= w) {
        if (c == ' ') { pan_scroll(); return; }
        /* the current word moves whole to the next line */
        int sp = pan_n - 1;
        while (sp > 0 && L[sp] != ' ') sp--;
        char word[PW + 1]; int wn = 0;
        if (sp > 0) { for (int i = sp + 1; i < pan_n; i++) word[wn++] = L[i]; L[sp] = 0; }
        pan_scroll();
        for (int i = 0; i < wn; i++) pan[PL - 1][pan_n++] = word[i];
        L = pan[PL - 1];
    }
    L[pan_n++] = c;
    L[pan_n] = 0;
}

static int log_fd = -1;

static void log_name(char *name, int n)              /* SKIMnn.LOG */
{
    char *q = scpy(name, "SKIM");
    *q++ = (char)('0' + n / 10); *q++ = (char)('0' + n % 10);
    scpy(q, ".LOG");
}

/* mode code the firmware gives apps (measured on the IC-705, 2026-11-22):
 * 32 = CW and CW-R (not distinguished), 48 = RTTY and RTTY-R, 80 = decoder mode
 * selected with the RTTY button. Station frequencies only make sense in CW. */
#define RMODE_CW 32
static int radio_mode = RMODE_CW;

/* Stations are remembered by their REAL FREQUENCY (not by the audio tone, which
 * changes when the dial moves): each keeps its callsign, CQ and QSO state. A
 * decoder channel points to the station it is hearing right now. */
#define NSTN 16
#define STN_TOL 80                      /* Hz: same station if closer than this */
static struct { int32_t key; uint32_t last; int used; cs_t cs; } stn[NSTN];
static int ch_st[CWD_MAXCH];

#define DIAL_POLL_MS   100u     /* how often the dial is read */
#define DIAL_FAST_HZ   50       /* more than this in DIAL_POLL_MS: the dial is being turned */
#define DIAL_SETTLE_MS 400u     /* still for this long: decoding starts again */

static int32_t station_key(uint32_t dial, float tone)
{
    if (cfg.pitch <= 0 || !dial || radio_mode != RMODE_CW) return (int32_t)(tone + 0.5f);   /* no dial or not in CW: key by tone */
    float off = (float)cfg.pitch - tone;
    if (cfg.cwr) off = -off;
    return (int32_t)dial + (int32_t)(off >= 0 ? off + 0.5f : off - 0.5f);
}

static int station_for(int32_t key, uint32_t now)
{
    int best = -1, lru = 0;
    int32_t bd = STN_TOL + 1;
    for (int i = 0; i < NSTN; i++) {
        if (stn[i].used) {
            int32_t dd = stn[i].key - key;
            if (dd < 0) dd = -dd;
            if (dd < bd) { bd = dd; best = i; }
        }
        if (!stn[i].used || stn[i].last < stn[lru].last) lru = i;
    }
    if (best < 0) {                     /* new station: reuse the least recent slot */
        best = lru;
        stn[best].used = 1;
        stn[best].key = key;
        cs_reset(&stn[best].cs);
    } else {
        stn[best].key += (key - stn[best].key) / 4;   /* follow small drift */
    }
    stn[best].last = now;
    return best;
}

#define CS(k) (&stn[ch_st[k]].cs)
static const char *chan_call(int k) { return cfg.calls ? cs_best(CS(k)) : ""; }

/* Removes typical noise from a phrase: words made only of E, T and *, short
 * runs of stray dots (EI, IE, II…), and the * of unrecognized characters
 * inside words. */
static int junk_word(const char *w, int n)
{
    int only_et = 1;
    for (int i = 0; i < n; i++) if (w[i] != 'E' && w[i] != 'T' && w[i] != '*') only_et = 0;
    if (only_et) return 1;
    static const char *bad[] = { "EI", "IE", "II", "III", "EIE", "IEI", "EIT", "TIE", "TEI", "ETI", 0 };
    for (int b = 0; bad[b]; b++) {
        int m = slen(bad[b]);
        if (m != n) continue;
        int eq = 1;
        for (int i = 0; i < n; i++) if (w[i] != bad[b][i]) eq = 0;
        if (eq) return 1;
    }
    return 0;
}

static int clean_text(const char *in, char *out, int max)
{
    int on = 0;
    while (*in) {
        while (*in == ' ') in++;
        const char *w = in;
        while (*in && *in != ' ') in++;
        int n = (int)(in - w);
        if (!n || junk_word(w, n)) continue;
        char tmp[64]; int tn = 0;
        for (int i = 0; i < n && tn < 63; i++) if (w[i] != '*') tmp[tn++] = w[i];
        if (!tn) continue;
        if (on && on < max - 1) out[on++] = ' ';
        for (int i = 0; i < tn && on < max - 1; i++) out[on++] = tmp[i];
    }
    out[on] = 0;
    return on;
}

/* Station frequency in kHz with one decimal. In normal CW Icom receives on the
 * LSB side (factory "CW Normal Side"): a higher tone is a LOWER station →
 * station = dial − (tone − pitch). CW-R is the other way round (cwr=1). */
static char *sfreq_station(char *p, uint32_t dial, float tone)
{
    float off = (float)cfg.pitch - tone;
    if (cfg.cwr) off = -off;
    int32_t hz = (int32_t)dial + (int32_t)(off >= 0 ? off + 0.5f : off - 0.5f);
    if (hz < 0) hz = 0;
    uint32_t dhz = ((uint32_t)hz + 50u) / 100u;               /* tenths of kHz */
    p = sint(p, (int)(dhz / 10u), 0);
    *p++ = '.';
    *p++ = (char)('0' + dhz % 10u);
    *p = 0;
    return p;
}

static void send_line(int k, float tone)
{
    if (ch[k].ln == 0) return;
    while (ch[k].ln && ch[k].line[ch[k].ln - 1] == ' ') ch[k].ln--;
    ch[k].line[ch[k].ln] = 0;
    /* lines that are typical noise (only E T I S H M N A 5 and asterisks) are not sent */
    int real = 0, chars = 0;
    for (int i = 0; i < ch[k].ln; i++) {
        char c = ch[k].line[i];
        if (c == ' ') continue;
        chars++;
        const char *w = "ETISHMNA5*";
        int weak = 0;
        for (const char *q = w; *q; q++) if (*q == c) weak = 1;
        if (!weak) real++;
    }
    if (chars < 2 || real == 0) { ch[k].ln = 0; return; }

    char body[120];
    if (cfg.clean) { if (clean_text(ch[k].line, body, 80) < 2) { ch[k].ln = 0; return; } }
    else scpy(body, ch[k].line);
    if (cfg.words) {                    /* word spacing: "CQCQDEEA4XYZ" → "CQ CQ DE EA4XYZ" */
        const char *known[2 * NSTN]; int nk = 0;
        for (int i = 0; i < NSTN; i++) {
            if (!stn[i].used) continue;
            if (cs_best(&stn[i].cs)[0]) known[nk++] = cs_best(&stn[i].cs);
            if (cs_peer(&stn[i].cs)[0]) known[nk++] = cs_peer(&stn[i].cs);
        }
        char fixed[120];
        wd_fix(body, fixed, sizeof fixed, known, nk);
        if (cfg.clean) clean_text(fixed, body, 120);    /* noise the split freed: "III CQ DX" */
        else scpy(body, fixed);
    }
    /* the callsign logic reads the finished line, with its spacing fixed */
    cs_feed(CS(k), body);
    cs_feed(CS(k), " ");

    /* DECODE: "7029.9 CQ EA4XYZ: text" / "7029.9 EA4XYZ>DL1ABC: text" / "7029.9 text" */
    char out[180], *p = out;
    if (cfg.pitch > 0 && ch[k].dial_hz && radio_mode == RMODE_CW) p = sfreq_station(p, ch[k].dial_hz, tone);
    else p = sint(p, (int)(tone + 0.5f), 4);      /* not in CW: the tone, not a wrong frequency */
    *p++ = ' ';
    const char *call = chan_call(k);
    if (call[0]) {
        if (cfg.calls && cs_cq(CS(k))) p = scpy(p, "CQ ");    /* only with a callsign: a bare "CQ" reads as text */
        p = scpy(p, call);
        const char *peer = cfg.calls ? cs_peer(CS(k)) : "";
        if (peer[0]) { *p++ = '>'; p = scpy(p, peer); }
        *p++ = ':'; *p++ = ' ';
    }
    p = scpy(p, body);
    if (cfg.decode) icap_decode_out(out);
    if (log_fd >= 0) {                         /* log: time + line + uncleaned text */
        char lg[300], *q = lg;
        struct icap_time t;
        if (icap_clock_now(&t) == 0) { q = s2(q, t.hour); q = s2(q, t.min); q = s2(q, t.sec); }
        else q = scpy(q, "------");
        *q++ = ' ';
        q = scpy(q, out);
        if (cfg.clean || cfg.words) { q = scpy(q, "  | "); q = sint(q, (int)(tone + 0.5f), 0); *q++ = ' '; q = scpy(q, ch[k].line); }
        *q++ = '\r'; *q++ = '\n';
        icap_write(log_fd, lg, (uint32_t)(q - lg));
    }
    ch[k].ln = 0;
}

static void add_text(int k, const char *s, float tone, uint32_t now)
{
    {
        struct icap_radio rs;
        uint32_t dial = (icap_radio_state(&rs) == 0) ? rs.freq_hz : 0;
        int st = station_for(station_key(dial, tone), now);
        /* the channel moved to another station: close the old station's phrase
         * first, so text, frequency and callsign never mix two stations */
        if (st != ch_st[k] && ch[k].ln) send_line(k, ch[k].tone);
        ch_st[k] = st;
    }
    ch[k].tone = tone;
    /* prefix: "7029.9 CQ CALL>PEER: " */
    int pre = 7 + (chan_call(k)[0] ? slen(chan_call(k)) + 2 + (cs_cq(CS(k)) ? 3 : 0) : 0)
            + (cs_peer(CS(k))[0] ? slen(cs_peer(CS(k))) + 1 : 0);
    int maxl = cfg.dcols - pre;
    if (maxl < 12) maxl = 12;
    if (maxl > (int)sizeof ch[k].line - 1) maxl = (int)sizeof ch[k].line - 1;
    for (; *s; s++) {
        char c = *s;
        if (k == 0) pan_add(c);
        if (!(c == ' ' && (ch[k].tn == 0 || ch[k].tail[ch[k].tn - 1] == ' '))) {
            if (ch[k].tn == PW) {      /* shift: keep the end */
                for (int i = 0; i < PW - 16; i++) ch[k].tail[i] = ch[k].tail[i + 16];
                ch[k].tn -= 16;
            }
            ch[k].tail[ch[k].tn++] = c;
            ch[k].tail[ch[k].tn] = 0;
        }

        if (c == ' ' && ch[k].ln == 0) continue;
        if (ch[k].ln == 0) {                   /* phrase starts: remember the dial */
            struct icap_radio rs;
            ch[k].dial_hz = (icap_radio_state(&rs) == 0) ? rs.freq_hz : 0;
        }
        ch[k].line[ch[k].ln++] = c;
        if (ch[k].ln >= maxl) {
            /* cut at the last space if there is one */
            int cut = ch[k].ln;
            for (int i = ch[k].ln - 1; i > maxl / 2; i--) if (ch[k].line[i] == ' ') { cut = i; break; }
            /* no space: words glued together. Wait (up to the buffer size), so the
             * word-spacing fix sees whole words instead of a blind cut */
            if (cut == ch[k].ln && cfg.words && ch[k].ln < (int)sizeof ch[k].line - 1) continue;
            char rest[80]; int rn = 0;
            for (int i = cut + 1; i < ch[k].ln; i++) rest[rn++] = ch[k].line[i];
            ch[k].ln = cut;
            send_line(k, tone);
            for (int i = 0; i < rn; i++) ch[k].line[i] = rest[i];
            ch[k].ln = rn;
        }
    }
    ch[k].last_ms = now;
}

/* ------------------------------------------------ panel */

/* channel tag on the panel: 1..9, then A, B, C */
static char chan_tag(int k) { return (char)(k < 9 ? '1' + k : 'A' + (k - 9)); }

static void bar(char *p, float snr_db)
{
    int n = (int)((snr_db - 6.0f) / 3.0f);         /* 6 dB → empty, 30 dB → full */
    if (n < 0) n = 0;
    if (n > 8) n = 8;
    *p++ = '[';
    for (int i = 0; i < 8; i++) *p++ = i < n ? '#' : '-';
    *p++ = ']';
    *p = 0;
}

static float db20(float r)
{
    /* approximate 20·log10(r) without libm: octaves + interpolation */
    if (r <= 1e-6f) return -120.0f;
    float d = 0.0f;
    while (r >= 2.0f) { r *= 0.5f; d += 6.0206f; }
    while (r < 1.0f) { r *= 2.0f; d -= 6.0206f; }
    return d + 6.0206f * (r - 1.0f) * (5.0f / 3.0f - r / 3.0f);   /* log2 ≈ −x²/3 + 2x − 5/3; error < 0.1 dB */
}

/* radio frequency as 7.030.50 (MHz.kHz.tens of Hz) */
static char *sfreq(char *p, uint32_t hz)
{
    uint32_t mhz = hz / 1000000u, khz = hz / 1000u % 1000u, dhz = hz / 10u % 100u;
    p = sint(p, (int)mhz, 0); *p++ = '.';
    *p++ = (char)('0' + khz / 100); *p++ = (char)('0' + khz / 10 % 10); *p++ = (char)('0' + khz % 10);
    *p++ = '.';
    return s2(p, dhz);
}

/* Text "spectrum" line: `cols` columns between tmin and tmax; the height is the
 * power above the floor (~3 dB steps). Columns of active channels are marked
 * with '|' (single) or with their number (multi). */
static void spectrum(cwd_t *d, char *out, int cols)
{
    static const char *blocks[9] = { " ", "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
                                     "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88" };
    static const char *ascii = " .:-=+*#%";
    char *p = out;
    float f0 = (float)cfg.tmin, f1 = (float)cfg.tmax;
    for (int c = 0; c < cols; c++) {
        float fa = f0 + (f1 - f0) * c / cols, fb = f0 + (f1 - f0) * (c + 1) / cols;
        /* bins whose centre lies in [fa, fb) */
        float xa = (fa - d->bin_f0) / d->bin_step, xb = (fb - d->bin_f0) / d->bin_step;
        int ba = (int)xa; if ((float)ba < xa) ba++;
        int bb = (int)xb; if ((float)bb < xb) bb++;
        if (bb <= ba) bb = ba + 1;
        float mx = 0.0f;
        for (int b = ba; b < bb; b++) if (b >= 0 && b < d->nbins && d->spec[b] > mx) mx = d->spec[b];
        float db = db20(mx / d->floor) * 0.5f;           /* power → dB */
        int lv = (int)(db / 3.0f);
        if (lv < 0) lv = 0;
        if (lv > 8) lv = 8;
        int mark = -1;
        for (int k = 0; k < d->nch; k++) {
            cwd_status_t s; cwd_status(d, k, &s);
            if (!s.active) continue;
            if (s.tone_hz >= fa && s.tone_hz < fb) mark = k;
        }
        if (mark >= 0) { *p++ = d->nch > 1 ? chan_tag(mark) : '|'; continue; }
        if (cfg.spec == 1) p = scpy(p, blocks[lv]);
        else if (cfg.spec == 2) *p++ = (char)('0' + (lv > 9 ? 9 : lv));
        else *p++ = ascii[lv];
    }
    *p = 0;
}

static void draw(cwd_t *d, uint32_t overruns, uint32_t rate_sps, int fonttest)
{
    char l[200], *p = l;
    struct icap_radio rs;
    if (icap_radio_state(&rs) == 0 && rs.freq_hz) { p = sfreq(p, rs.freq_hz); *p++ = ' '; }
    p = scpy(p, d->nch > 1 ? "SKIM multi " : "SKIM ");
    p = scpy(p, cfg.det == CWD_DET_VITERBI ? "VIT" : cfg.det == CWD_DET_AUTO ? "AUTO" : "THR");
    p = scpy(p, " ");
    p = sint(p, (int)rate_sps, 0);
    p = scpy(p, "sps");
    if (overruns) { p = scpy(p, " OVR "); p = sint(p, (int)overruns, 0); }
    icap_set_line(0, l);

    int cols = cfg.cols;
    int spec_line = cfg.spec ? 1 : 0;
    if (d->nch == 1) {
        cwd_status_t s; cwd_status(d, 0, &s);
        p = sint(l, (int)(s.wpm + 0.5f), 2); p = scpy(p, " WPM ");
        p = sint(p, (int)(s.tone_hz + 0.5f), 4); p = scpy(p, " Hz ");
        float db = db20(s.snr);
        p = sint(p, (int)db, 2); p = scpy(p, "dB ");
        bar(p, db);
        p += slen(p);
        p = scpy(p, s.open ? (s.mark ? " *" : "  ") : " -");
        if (chan_call(0)[0]) { *p++ = ' '; p = scpy(p, chan_call(0)); }
        icap_set_line(1, l);
        int first = 2;
        if (spec_line) { spectrum(d, l, cols); icap_set_line(2, l); first = 3; }
        if (fonttest) { icap_set_line(first, "+*#%@ |^_~ <>[]{}/\\ ()"); first++; }
        for (int i = PL - (8 - first); i < PL; i++) icap_set_line(first + i - (PL - (8 - first)), pan[i]);
    } else {
        if (spec_line) { spectrum(d, l, cols); icap_set_line(1, l); }
        else icap_set_line(1, " Hz  WPM dB text");
        /* 6 rows: the active channels that received text most recently */
        int show[6], ns = 0, taken[CWD_MAXCH] = {0};
        while (ns < 6) {
            int b = -1;
            for (int k = 0; k < d->nch; k++) {
                if (taken[k]) continue;
                cwd_status_t s; cwd_status(d, k, &s);
                if (!s.active) continue;
                if (b < 0 || ch[k].last_ms > ch[b].last_ms) b = k;
            }
            if (b < 0) break;
            taken[b] = 1; show[ns++] = b;
        }
        for (int row = 0; row < 6; row++) {
            if (row >= ns) { icap_set_line(2 + row, ""); continue; }
            int k = show[row];
            cwd_status_t s; cwd_status(d, k, &s);
            p = l;
            if (spec_line) { *p++ = chan_tag(k); *p++ = ' '; }
            p = sint(p, (int)(s.tone_hz + 0.5f), 4); *p++ = ' ';
            /* tone, callsign if known, and the text (no WPM or SNR) */
            if (chan_call(k)[0]) p = scpy(p, chan_call(k));
            *p++ = s.open ? (s.mark ? '*' : ' ') : '-';
            int room = cols - (int)(p - l);
            int a = ch[k].tn - room;
            if (a < 0) a = 0;
            for (int i = a; i < ch[k].tn; i++) *p++ = ch[k].tail[i];
            *p = 0;
            icap_set_line(2 + row, l);
        }
    }
}

/* ------------------------------------------------ app */

int app_main(const struct icap_svc *svc)
{
    icap_use(svc);
    icap_clear();
    icap_print("SKIM: multi CW decoder by EA5JLJ");

    if (!ICAP_HAS(audio_read)) {
        icap_print("This firmware has no RX audio.");
        while (!icap_should_stop()) icap_sleep_ms(200);
        return 0;
    }
    read_cfg();

    /* memory: the decoder state goes in the app heap */
#ifdef SKIM_HOST
    static cwd_t host_dec;                 /* simulator: 64-bit pointers */
    cwd_t *d = &host_dec;
    if (0) {
#else
    cwd_t *d = (cwd_t *)icap_heap();
    if (!d || icap_heap_size() < sizeof(cwd_t)) {
#endif
        struct icap_meminfo mi;
        d = 0;
        if (icap_mem_info(&mi) == 0) {
            uint32_t lo = (mi.free_lo + 7u) & ~7u;
            if (mi.free_hi > lo && mi.free_hi - lo >= sizeof(cwd_t)) d = (cwd_t *)(uintptr_t)lo;
        }
    }
    if (!d) {
        char l[48], *p = scpy(l, "Out of memory: need ");
        p = sint(p, (int)(sizeof(cwd_t) / 1024 + 1), 0); scpy(p, " KB");
        icap_print(l);
        while (!icap_should_stop()) icap_sleep_ms(200);
        return 0;
    }

    cwd_cfg_t c; cwd_default_cfg(&c);
    c.mode = cfg.mode;
    c.detector = cfg.det;
    c.max_channels = cfg.chans;
    c.tone_min_hz = (float)cfg.tmin;
    c.tone_max_hz = (float)cfg.tmax;
    c.tone_fixed_hz = (float)cfg.tone;
    c.wpm_init = (float)cfg.wpm;
    cwd_init(d, &c);

    /* one log per session (SKIM01.LOG, SKIM02.LOG…): the SDK cannot append to a
     * file, and always opening the same one emptied it on every start. The
     * numbers go round 01…99 and only the last cfg.logs sessions are kept, so
     * the SD card does not fill up with logs. */
    if (cfg.log) {
        char name[16];
        static char used[100];
        int any = 0;
        for (int n = 1; n <= 99; n++) {
            log_name(name, n);
            int fd = icap_open(name, ICAP_O_READ);
            used[n] = fd >= 0;
            if (fd >= 0) { icap_close(fd); any = 1; }
        }
        /* the new one goes right after the newest: the used one followed by a free one */
        int n = 1;
        if (any) {
            for (int i = 1; i <= 99; i++)
                if (used[i] && !used[i % 99 + 1]) { n = i % 99 + 1; break; }
        }
        /* keep the newest cfg.logs - 1 before it, delete the older ones */
        for (int k = cfg.logs, i = (n - cfg.logs + 98) % 99 + 1; k < 99; k++, i = (i + 97) % 99 + 1) {
            if (!used[i]) break;
            log_name(name, i);
            icap_remove(name, 0);
        }
        log_name(name, n);
        log_fd = icap_open(name, ICAP_O_WRITE);
        if (log_fd >= 0) {
            char h[96], *q = scpy(h, "# SKIM (EA5JLJ, MIT) ");
            struct icap_time t;
            if (icap_clock_now(&t) == 0) {
                q = sint(q, t.year, 0); *q++ = '-'; q = s2(q, t.mon); *q++ = '-'; q = s2(q, t.day); *q++ = ' ';
                q = s2(q, t.hour); *q++ = ':'; q = s2(q, t.min);
            }
            char m[8];
            if (icap_mode(m, sizeof m) > 0) { q = scpy(q, " (started with the mode button: "); q = scpy(q, m); *q++ = ')'; }
            else q = scpy(q, " (started from autostart or the Apps menu)");
            *q++ = '\r'; *q++ = '\n';
            icap_write(log_fd, h, (uint32_t)(q - h));
        }
    }

    static int16_t buf[160];
    char txt[CWD_OUT];
    uint32_t overruns = 0, got = 0, t_draw = icap_ticks(), t_rate = t_draw, rate = 0, t_start = t_draw;
    int last_mode = -1, warned = 0;
    uint32_t t_noncw = 0;
    /* CPU use: decoder time against audio time, logged once a minute */
    uint32_t busy_ms = 0, busy_max = 0, audio_n = 0, t_load = icap_ticks(), ovr_logged = 0;
    /* dial being turned: every tone moves, so decoding is useless and costly.
     * Pause while the dial moves fast and start afresh once it stays put. */
    uint32_t dial_prev = 0, t_dial = icap_ticks(), t_moved = 0, tunes = 0;
    int tuning = 0;

    while (!icap_should_stop()) {
        int n = icap_audio_read(buf, 160);
        uint32_t now = icap_ticks();
        if (n == ICAP_E_OVERRUN) { overruns++; continue; }
        if (n < 0) { icap_sleep_ms(50); continue; }
        if (n == 0) {
            icap_sleep_ms(5);
        } else if (tuning) {
            got += (uint32_t)n;         /* audio read and dropped while the dial moves */
        } else {
            got += (uint32_t)n;
            uint32_t t0 = icap_ticks();
            unsigned mask = cwd_process(d, buf, n);
            uint32_t dt = icap_ticks() - t0;
            busy_ms += dt;
            if (dt > busy_max) busy_max = dt;
            audio_n += (uint32_t)n;
            for (int k = 0; k < cwd_channels(d); k++) {
                if (!(mask & (1u << k))) continue;
                int m = cwd_take(d, k, txt, sizeof txt);
                if (m > 0) { cwd_status_t s; cwd_status(d, k, &s); add_text(k, txt, s.tone_hz, now); }
            }
            icap_yield();               /* after every 20 ms block: the radio's UI stays responsive */
        }
        if (now - t_dial >= DIAL_POLL_MS) {
            t_dial = now;
            struct icap_radio rs;
            if (icap_radio_state(&rs) == 0 && rs.freq_hz) {
                int32_t df = (int32_t)(rs.freq_hz - dial_prev);
                if (dial_prev && (df > DIAL_FAST_HZ || df < -DIAL_FAST_HZ)) {
                    if (!tuning) {          /* starts moving: close what was decoded so far */
                        for (int k = 0; k < cwd_channels(d); k++) if (ch[k].ln) send_line(k, ch[k].tone);
                        tuning = 1;
                    }
                    t_moved = now;
                }
                dial_prev = rs.freq_hz;
            }
            if (tuning && now - t_moved >= DIAL_SETTLE_MS) {
                cwd_init(d, &c);            /* stopped: decode the new frequency from scratch */
                for (int k = 0; k < cwd_channels(d); k++) { ch[k].ln = 0; ch[k].tn = 0; ch[k].tail[0] = 0; }
                tuning = 0;
                tunes++;
            }
        }
        /* phrase ended by silence (pause ms) */
        for (int k = 0; k < cwd_channels(d); k++)
            if (ch[k].ln && now - ch[k].last_ms > (uint32_t)cfg.pause) {
                /* not while the decoder holds this channel's first elements of a
                 * new transmission: their text joins this line ("E" + "A5RJ") */
                cwd_status_t s; cwd_status(d, k, &s);
                if (!s.held || now - ch[k].last_ms > 4u * (uint32_t)cfg.pause) send_line(k, ch[k].tone);
            }
        if (now - t_rate >= 1000u) { rate = got; got = 0; t_rate = now; }
        if (now - t_load >= 60000u && audio_n > 0) {
            if (log_fd >= 0) {          /* "# load: 9% (8 channels), audio lost: 0" */
                char m[128], *q = scpy(m, "# load: ");
                q = sint(q, (int)(busy_ms * 100u / (audio_n / 8u + 1u)), 0);
                q = scpy(q, "% ("); q = sint(q, cwd_channels(d), 0);
                q = scpy(q, " channels), longest block: "); q = sint(q, (int)busy_max, 0);
                q = scpy(q, " ms, audio lost: "); q = sint(q, (int)(overruns - ovr_logged), 0);
                q = scpy(q, ", dial pauses: "); q = sint(q, (int)tunes, 0);
                *q++ = '\r'; *q++ = '\n';
                icap_write(log_fd, m, (uint32_t)(q - m));
            }
            ovr_logged = overruns;
            busy_ms = 0; busy_max = 0; audio_n = 0; t_load = now; tunes = 0;
        }
        if (now - t_draw >= 250u) {
            draw(d, overruns, rate, cfg.fonttest && now - t_start < 10000u);
            t_draw = now;
            /* log the radio's mode code when it changes
             * (to learn which number is CW, CW-R, RTTY-R…) */
            struct icap_radio rs;
            if (icap_radio_state(&rs) == 0) {
                radio_mode = rs.mode;
                /* one-time warning if the radio stays out of CW for 8 s */
                if (rs.mode != RMODE_CW) { if (!t_noncw) t_noncw = now; }
                else t_noncw = 0;
                if (t_noncw && !warned && now - t_noncw > 8000u) {
                    icap_popup("SKIM is decoding, but the\nradio is not in CW mode.\n\nSwitch back to CW to see\neach station's frequency.");
                    warned = 1;
                }
            }
            if (log_fd >= 0 && icap_radio_state(&rs) == 0 && rs.mode != last_mode) {
                char m[48], *q = scpy(m, "# radio mode: ");
                q = sint(q, rs.mode, 0);
                q = scpy(q, " dial ");
                q = sfreq(q, rs.freq_hz);
                *q++ = '\r'; *q++ = '\n';
                icap_write(log_fd, m, (uint32_t)(q - m));
                last_mode = rs.mode;
            }
        }
    }

    cwd_flush(d);
    for (int k = 0; k < cwd_channels(d); k++) {
        int m = cwd_take(d, k, txt, sizeof txt);
        cwd_status_t s; cwd_status(d, k, &s);
        if (m > 0) add_text(k, txt, s.tone_hz, icap_ticks());
        send_line(k, ch[k].tone);
    }
    if (log_fd >= 0) icap_close(log_fd);
    return 0;
}
