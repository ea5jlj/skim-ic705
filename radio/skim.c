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
#include "../src/pskr.h"

#define SKIM_VERSION "2026.11.24"     /* shown to PSKReporter as the decoding software */

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
    int tmin, tmax, tone, spec, fonttest, pause, calls, pitch, cwr, clean, words, logs, spot;
} cfg = { CWD_MODE_MULTI, CWD_DET_THRESHOLD, 8, 30, 40, 1, 1, 20, 250, 2500, 0, 2, 0, 1500, 1, 600, 0, 1, 1, 10, 0 };

/* not inlined: its 3 KB buffer must leave the stack once the settings are read,
 * or it stays under app_main for the whole session */
__attribute__((noinline)) static void read_cfg(void)
{
    int fd = icap_open("SKIM.CFG", ICAP_O_READ);
    if (fd < 0) return;
    char buf[3072];                         /* SKIM.CFG with its comments is ~2.5 KB; on the
                                               stack, which is free at start, not in BSS */
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
            else if (keyis(p, "spot")) cfg.spot = sval(v);
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
    float    snr;       /* signal/noise of the channel (linear) when text last came */
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
/* the low 4 bits of the mode code are the filter (CW: FIL1 32, FIL2 33, FIL3 34):
 * only the high 4 bits say the mode */
#define IS_CW(m) (((m) & 0xF0) == RMODE_CW)
static int radio_mode = RMODE_CW;

/* Stations are remembered by their REAL FREQUENCY (not by the audio tone, which
 * changes when the dial moves): each keeps its callsign, CQ and QSO state. A
 * decoder channel points to the station it is hearing right now. */
#define NSTN 16
#ifndef STN_TOL
#define STN_TOL 20                      /* Hz: same station if closer than this. The two sides
                                           of a QSO are often only 20-30 Hz apart (G3PQD 601 Hz,
                                           IN3TWX 574 Hz); one station's tone varies by ±3 Hz */
#endif
#define PAIR_HZ 100                     /* a QSO partner is within this... */
#define PAIR_MS 120000u                 /* ...and was heard in the last 2 minutes */
#ifndef CROWD_HZ
#define CROWD_HZ 1000                   /* this many named stations within this, heard in */
#endif
#ifndef CROWD_N
#define CROWD_N  3                      /* the last 2 minutes: a pile-up, no partner guessing */
#endif
typedef struct { int32_t key; uint32_t last; int used; cs_t cs; } stn_t;
static stn_t *stn;                      /* NSTN stations, in the heap (the app window is small) */
static int ch_st[CWD_MAXCH];

#define DIAL_POLL_MS   100u     /* how often the dial is read */
#define DIAL_FAST_HZ   50       /* more than this in DIAL_POLL_MS: the dial is being turned */
#define DIAL_SETTLE_MS 400u     /* still for this long: decoding starts again */

static int32_t station_key(uint32_t dial, float tone)
{
    if (cfg.pitch <= 0 || !dial || !IS_CW(radio_mode)) return (int32_t)(tone + 0.5f);   /* no dial or not in CW: key by tone */
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

/* Label of channel k's station: its callsign and the station it works. Two
 * stations in a QSO are usually within a few tens of Hz, and often only one
 * of them sends both calls ("IN3TWX DE G3PQD"). A station with no callsign of
 * its own, close to one in a QSO, is that QSO's other side: IN3TWX>G3PQD. */
static void chan_label(int k, const char **call, const char **peer)
{
    *call = chan_call(k);
    *peer = cfg.calls ? cs_peer(CS(k)) : "";
    if (!cfg.calls || (*call)[0]) return;
    int me = ch_st[k], crowd = 0, pick = -1;
    uint32_t now = icap_ticks();
    for (int i = 0; i < NSTN; i++) {
        if (i == me || !stn[i].used || now - stn[i].last > PAIR_MS) continue;
        int32_t dd = stn[i].key - stn[me].key;
        if (dd < 0) dd = -dd;
        const char *b = cs_best(&stn[i].cs), *q = cs_peer(&stn[i].cs);
        if (b[0] && dd <= CROWD_HZ) crowd++;
        if (dd <= PAIR_HZ && b[0] && q[0] && !cs_cq(&stn[i].cs) && pick < 0) pick = i;
    }
    /* many named stations around: a pile-up, where the callers sit on or near
     * the DX's frequency and who works whom cannot be told apart */
    if (pick >= 0 && crowd < CROWD_N) { *call = cs_peer(&stn[pick].cs); *peer = cs_best(&stn[pick].cs); }
}

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

/* A callsign that ends in an unreadable character (F8F* of F8FAZ) keeps one
 * * at the end, so it is not taken for a complete call; a lone * right after
 * a callsign is joined to it ("F8F *" -> "F8F*"). Elsewhere the * is dropped
 * (*N9CLU, a lost first letter, still counts for ON9CLU). last = 0 for
 * the pass before the word-spacing fix, which keeps lone *s for the last one. */
static int clean_text(const char *in, char *out, int max, int last)
{
    int on = 0, prev = -1;              /* prev: start of the last word written */
    while (*in) {
        while (*in == ' ') in++;
        const char *w = in;
        while (*in && *in != ' ') in++;
        int n = (int)(in - w);
        if (!n) continue;
        int stars = 0;
        for (int i = 0; i < n; i++) stars += w[i] == '*';
        if (stars == n) {                                   /* a lone * (or **) */
            if (!last) { if (on && on < max - 1) out[on++] = ' '; if (on < max - 1) { out[on++] = '*'; prev = on - 1; } continue; }
            if (prev >= 0 && out[on - 1] != '*' && on < max - 1) {
                char save = out[on]; out[on] = 0;
                int call = cs_is_call(out + prev);
                out[on] = save;
                if (call) out[on++] = '*';
            }
            continue;
        }
        if (junk_word(w, n)) continue;
        char tmp[64]; int tn = 0;
        for (int i = 0; i < n && tn < 63; i++) if (w[i] != '*') tmp[tn++] = w[i];
        if (!tn) continue;
        tmp[tn] = 0;
        int keep = w[n - 1] == '*' && cs_is_call(tmp);      /* F8F*: a call cut short at the end */
        if (on && on < max - 1) out[on++] = ' ';
        prev = on;
        for (int i = 0; i < tn && on < max - 1; i++) out[on++] = tmp[i];
        if (keep && on < max - 1) out[on++] = '*';
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

/* ------------------------------------------------ PSKReporter spots
 * With spot=1 in SKIM.CFG, a station that names itself ("CQ X", "DE X",
 * "TEST X", "X TEST" or "X X") in two different lines, with exactly the same
 * letters, is reported to PSKReporter, once an hour per band. SKIM sends the
 * reports itself (src/pskr.c: UDP to report.pskreporter.info, about every
 * five minutes), with the station's own frequency: the firmware's @SPOT
 * service only adds a positive audio tone to the dial, which cannot place a
 * CW station below the dial. The owner's call and locator come from @MYCALL
 * and @MYLOC; nothing is sent while PSKREPORT is off for SKIM (@MODECFG).
 * Every station queued and every datagram sent is written to the log. */
#define SPOT_N    24
#define SPOT_MS   3600000u              /* the same call on the same band: once an hour */
#define FLUSH_MS  300000u               /* a datagram at most every 5 minutes... */
#define FLUSH_JIT 30000u                /* ...plus up to 30 s, so stations do not send together */
static struct { char call[CS_LEN + 1]; uint32_t t; uint32_t band; int seen; int done; } spotted[SPOT_N];
static pskr_t  *ps;                     /* in the heap, after the word list; 0 = spots off */
static uint8_t *ps_buf;
static int      ps_net = -1, ps_off_logged;
static uint32_t ps_next, ps_rnd = 12345u;

static void logline(const char *a, const char *b)
{
    if (log_fd < 0) return;
    char m[128], *q = scpy(m, a); if (b) q = scpy(q, b);
    *q++ = '\r'; *q++ = '\n';
    icap_write(log_fd, m, (uint32_t)(q - m));
}

static int read_vfile(const char *name, char *b, int max)   /* one line of a firmware virtual file */
{
    b[0] = 0;                                                    /* empty if there is none */
    int fd = icap_open(name, ICAP_O_READ);
    if (fd < 0) return 0;
    int n = icap_read(fd, b, (uint32_t)(max - 1));
    icap_close(fd);
    if (n < 0) n = 0;
    b[n] = 0;
    while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == '\r' || b[n - 1] == ' ')) b[--n] = 0;
    return n;
}

static int tok_is(const char *a, int n, const char *k) { int i = 0; for (; i < n; i++) if (k[i] != a[i]) return 0; return k[n] == 0; }

/* the station names itself in this line */
static int self_named(const char *body, const char *call)
{
    const char *tk[24]; int tl[24], nt = 0;
    for (const char *p = body; *p && nt < 24;) {
        while (*p == ' ') p++;
        if (!*p) break;
        tk[nt] = p; while (*p && *p != ' ') p++; tl[nt] = (int)(p - tk[nt]); nt++;
    }
    int seen = 0;
    for (int i = 0; i < nt; i++) {
        if (!tok_is(tk[i], tl[i], call)) continue;
        if (++seen >= 2) return 1;                                     /* "X X" */
        if (i > 0 && (tok_is(tk[i - 1], tl[i - 1], "DE") || tok_is(tk[i - 1], tl[i - 1], "CQ") ||
                      tok_is(tk[i - 1], tl[i - 1], "TEST"))) return 1;
        if (i + 1 < nt && tok_is(tk[i + 1], tl[i + 1], "TEST")) return 1;
    }
    return 0;
}

static int snr_db(float snr)                    /* envelope S/N (linear amplitude) in dB */
{
    int db = 0;
    while (snr >= 1.12202f && db < 60) { snr *= 0.89125f; db++; }   /* 1 dB steps, no libm */
    return db;
}

/* the true dial in Hz (transverter-aware) from @FREQ; 0 if the firmware has none */
static uint64_t true_dial(void)
{
    char b[96];
    if (!read_vfile("@FREQ", b, sizeof b)) return 0;
    for (char *p = b; *p; p++)
        if (p[0] == 'f' && p[1] == '=' && (p == b || p[-1] == ' ')) {
            uint64_t v = 0;
            for (p += 2; *p >= '0' && *p <= '9'; p++) v = v * 10u + (uint64_t)(*p - '0');
            return v;
        }
    return 0;
}

static uint32_t utc_now(void)                   /* seconds since 1970; 0 if the clock is not set */
{
    struct icap_time t;
    if (icap_clock_now(&t) != 0 || t.year < 2024) return 0;
    return pskr_unix(t.year, t.mon, t.day, t.hour, t.min, t.sec);
}

/* spots on: room for the queue and a datagram, MY CALL and a locator */
static void spot_init(uint8_t *mem, uint32_t n, uint32_t now)
{
    if (!cfg.spot) return;
    uint32_t need = ((uint32_t)sizeof(pskr_t) + 7u) & ~7u;
    if (!mem || n < need + PSKR_MAXDG) { logline("# spots: off (no memory left)", 0); return; }
    char call[32], loc[16];
    if (!read_vfile("@MYCALL", call, sizeof call)) { logline("# spots: off (set MY CALL on the radio)", 0); return; }
    read_vfile("@MYLOC", loc, sizeof loc);       /* may come later, from the GPS */
    ps = (pskr_t *)(void *)mem;
    ps_buf = mem + need;
    ps_rnd ^= now * 2654435761u ^ utc_now();
    pskr_init(ps, call, loc, "SKIM " SKIM_VERSION, "CW", ps_rnd);
    ps_next = now + FLUSH_MS + (ps_rnd >> 8) % FLUSH_JIT;
    logline("# spots: on, as ", call);
}

static void spot_flush(uint32_t now, int last)
{
    if (!ps || !ps->nq) return;
    if (!last && now - ps_next > 0x80000000u && ps->nq < 30) return;      /* not yet, and room left */
    ps_rnd = ps_rnd * 1103515245u + 12345u;
    ps_next = now + FLUSH_MS + (ps_rnd >> 8) % FLUSH_JIT;
    char b[64];
    if (read_vfile("@MODECFG", b, sizeof b)) {    /* the owner's PSKREPORT switch for SKIM */
        for (char *p = b; *p; p++)
            if (p[0] == 'p' && p[1] == 's' && p[2] == 'k' && p[3] == 'r' && p[4] == '=' && p[5] == '0') {
                if (!ps_off_logged) logline("# spots: not sent, PSKREPORT is off for SKIM (DECODE > SET)", 0);
                ps_off_logged = 1; ps->nq = 0; return;
            }
    }
    if (!ps->my_loc[0] && read_vfile("@MYLOC", b, sizeof b)) {
        for (int i = 0; i < 7 && b[i]; i++) { ps->my_loc[i] = b[i]; ps->my_loc[i + 1] = 0; }
    }
    if (!ps->my_loc[0]) {                       /* PSKReporter needs the receiver's locator */
        if (!ps_off_logged) logline("# spots: waiting for a position (GPS or manual) on the radio", 0);
        ps_off_logged = 1;
        return;
    }
    if (icap_net_status() != 0) { logline("# spots: waiting, no network", 0); return; }
    if (ps_net < 0) {
        ps_net = icap_net_open("report.pskreporter.info", 4739, ICAP_NET_UDP, 3000);
        if (ps_net < 0) { logline("# spots: cannot reach report.pskreporter.info", 0); return; }
    }
    uint32_t t = utc_now();
    int sent = 0;
    while (ps->nq) {
        int left = ps->nq;
        int len = pskr_build(ps, ps_buf, PSKR_MAXDG, t);
        if (len <= 0) break;
        if (icap_net_send(ps_net, ps_buf, (uint32_t)len, 2000) < 0) {
            icap_net_close(ps_net); ps_net = -1;
            logline("# spots: send failed, will retry", 0);
            break;
        }
        sent += left - ps->nq;
    }
    if (sent) { char m[48], *q = scpy(m, "# spots sent: "); q = sint(q, sent, 0); scpy(q, " stations"); logline(m, 0); }
}

static void spot(int k, float tone, const char *body, uint32_t now)
{
    const char *call = chan_call(k);
    if (!ps || !call[0] || cfg.pitch <= 0 || !IS_CW(radio_mode) || !self_named(body, call)) return;
    uint64_t dial = true_dial();
    if (!dial) dial = ch[k].dial_hz;
    float off = (float)cfg.pitch - tone;                            /* station = dial + off (CW) */
    if (cfg.cwr) off = -off;
    uint64_t f = (uint64_t)((int64_t)dial + (int64_t)(off >= 0 ? off + 0.5f : off - 0.5f));
    uint32_t band = (uint32_t)(f / 1000000u);
    /* confirmed first: the same call, named by the station in two different
     * lines. A misread (YP4MCSM for YP40CSM, DL1M for DL1MRL) rarely repeats. */
    int slot = -1, lru = 0;
    for (int i = 0; i < SPOT_N; i++) {
        if (spotted[i].call[0] && tok_is(call, slen(call), spotted[i].call)) { slot = i; break; }
        if (!spotted[i].call[0] || spotted[i].t < spotted[lru].t) lru = i;
    }
    if (slot >= 0 && (now - spotted[slot].t >= SPOT_MS || spotted[slot].band != band)) {
        spotted[slot].seen = 0; spotted[slot].done = 0;
    }
    if (slot < 0) {
        slot = lru;
        for (int i = 0; i < CS_LEN && call[i]; i++) spotted[slot].call[i] = call[i];
        spotted[slot].call[slen(call) < CS_LEN ? slen(call) : CS_LEN] = 0;
        spotted[slot].seen = 0; spotted[slot].done = 0;
    }
    spotted[slot].band = band;
    if (spotted[slot].done) return;
    spotted[slot].t = now;
    if (++spotted[slot].seen < 2) return;
    uint32_t t = utc_now();
    if (!t) return;                                                 /* no clock: not reported */
    spotted[slot].done = 1;
    pskr_add(ps, call, f, snr_db(ch[k].snr), t);
    if (log_fd >= 0) {                                              /* "# spot: EA4XYZ 7025.0 (18 dB)" */
        char m[64], *r = scpy(m, "# spot: "); r = scpy(r, call); *r++ = ' ';
        uint32_t dk = (uint32_t)((f + 50u) / 100u);                 /* tenths of kHz */
        r = sint(r, (int)(dk / 10u), 0); *r++ = '.'; *r++ = (char)('0' + dk % 10u);
        r = scpy(r, " ("); r = sint(r, snr_db(ch[k].snr), 0); r = scpy(r, " dB)");
        *r = 0; logline(m, 0);
    }
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
    if (cfg.clean) { if (clean_text(ch[k].line, body, 80, !cfg.words) < 2) { ch[k].ln = 0; return; } }
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
        if (cfg.clean) { if (clean_text(fixed, body, 120, 1) < 2) { ch[k].ln = 0; return; } }   /* noise the split freed: "III CQ DX" */
        else scpy(body, fixed);
    }
    /* the callsign logic reads the finished line, with its spacing fixed */
    cs_feed(CS(k), body);
    cs_feed(CS(k), " ");
    spot(k, tone, body, icap_ticks());

    /* DECODE: "7029.9 CQ EA4XYZ: text" / "7029.9 EA4XYZ>DL1ABC: text" / "7029.9 text" */
    char out[180], *p = out;
    if (cfg.pitch > 0 && ch[k].dial_hz && IS_CW(radio_mode)) p = sfreq_station(p, ch[k].dial_hz, tone);
    else p = sint(p, (int)(tone + 0.5f), 4);      /* not in CW: the tone, not a wrong frequency */
    *p++ = ' ';
    const char *call, *peer;
    chan_label(k, &call, &peer);
    if (call[0]) {
        if (cfg.calls && cs_cq(CS(k)) && call == chan_call(k)) p = scpy(p, "CQ ");    /* only with a callsign: a bare "CQ" reads as text */
        p = scpy(p, call);
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
    const char *lc, *lp;
    chan_label(k, &lc, &lp);
    int pre = 7 + (lc[0] ? slen(lc) + 2 + (cs_cq(CS(k)) ? 3 : 0) : 0) + (lp[0] ? slen(lp) + 1 : 0);
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

/* English and Spanish word list for the word-spacing fix: SKIMDICT.BIN (tools/mkdict.py
 * --bin) in the Apps folder, read into the memory left after the decoder.
 * Returns its size, 0 when there is none, -1 when it is damaged (SKIM then
 * works without it). */
static int load_dict(uint8_t *room, uint32_t room_n)
{
    if (!room || room_n < 4 + 677 * 2) return 0;
    int fd = icap_open("SKIMDICT.BIN", ICAP_O_READ);
    if (fd < 0) return 0;
    uint32_t n = 0;
    for (;;) {
        int r = icap_read(fd, room + n, room_n - n > 4096 ? 4096 : room_n - n);
        if (r <= 0) break;
        n += (uint32_t)r;
        if (n == room_n) break;
    }
    icap_close(fd);
    const uint16_t *idx = (const uint16_t *)(room + 4);
    if (n < 4 + 677 * 2 || room[0] != 'S' || room[1] != 'K' || room[2] != 'D' || room[3] != '1' ||
        4u + 677u * 2u + idx[676] != n) return -1;              /* not ours, or cut short */
    for (int i = 0; i < 676; i++) if (idx[i] > idx[i + 1]) return -1;  /* damaged */
    wd_dict(idx, room + 4 + 677 * 2);
    return (int)n;
}

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
    uint8_t *room = 0; uint32_t room_n = 0;          /* memory left after the decoder (word list) */
#ifdef SKIM_HOST
    static cwd_t host_dec;                 /* simulator: 64-bit pointers */
    static uint8_t host_room[65536];
    cwd_t *d = &host_dec;
    room = host_room; room_n = sizeof host_room;
    if (0) {
#else
    cwd_t *d = (cwd_t *)icap_heap();
    if (d && icap_heap_size() >= sizeof(cwd_t)) {
        room = (uint8_t *)d + ((sizeof(cwd_t) + 7u) & ~7u);
        room_n = icap_heap_size() - (uint32_t)((sizeof(cwd_t) + 7u) & ~7u);
    } else {
#endif
        struct icap_meminfo mi;
        d = 0;
        if (icap_mem_info(&mi) == 0) {
            uint32_t lo = (mi.free_lo + 7u) & ~7u;
            if (mi.free_hi > lo && mi.free_hi - lo >= sizeof(cwd_t)) {
                d = (cwd_t *)(uintptr_t)lo;
                uint32_t r = (lo + (uint32_t)sizeof(cwd_t) + 7u) & ~7u;
                if (mi.free_hi > r) { room = (uint8_t *)(uintptr_t)r; room_n = mi.free_hi - r; }
            }
        }
    }
    if (!d) {
        char l[48], *p = scpy(l, "Out of memory: need ");
        p = sint(p, (int)(sizeof(cwd_t) / 1024 + 1), 0); scpy(p, " KB");
        icap_print(l);
        while (!icap_should_stop()) icap_sleep_ms(200);
        return 0;
    }
    {   /* the station table, first in the memory left after the decoder */
        uint32_t need = ((uint32_t)(sizeof(stn_t) * NSTN) + 7u) & ~7u;
        if (!room || room_n < need) {
            icap_print("Out of memory for the stations");
            while (!icap_should_stop()) icap_sleep_ms(200);
            return 0;
        }
        stn = (stn_t *)(void *)room;
        for (uint32_t i = 0; i < need; i++) room[i] = 0;
        room += need; room_n -= need;
    }

    int dict_bytes = cfg.words ? load_dict(room, room_n) : 0;

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
            q = scpy(h, dict_bytes > 0 ? "# word list: SKIMDICT.BIN, " :
                        dict_bytes < 0 ? "# word list: none (SKIMDICT.BIN damaged or too big)" :
                                         "# word list: none (SKIMDICT.BIN not found)");
            if (dict_bytes > 0) { q = sint(q, dict_bytes, 0); q = scpy(q, " bytes"); }
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

    {   /* PSKReporter spots: the memory after the word list */
        uint32_t used = dict_bytes > 0 ? (((uint32_t)dict_bytes + 7u) & ~7u) : 0;
        spot_init(room && room_n > used ? room + used : 0, room && room_n > used ? room_n - used : 0, icap_ticks());
    }

    while (!icap_should_stop()) {
        int n = icap_audio_read(buf, 160);
        uint32_t now = icap_ticks();
        if (n == ICAP_E_OVERRUN) { overruns++; continue; }
        if (n < 0) { icap_sleep_ms(50); continue; }
        if (n == 0) {
            icap_sleep_ms(5);
        } else if (tuning || !IS_CW(radio_mode)) {   /* dial moving, or not in CW: paused */
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
                if (m > 0) { cwd_status_t s; cwd_status(d, k, &s); ch[k].snr = s.snr; add_text(k, txt, s.tone_hz, now); }
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
        if (now - t_rate >= 1000u) { rate = got; got = 0; t_rate = now; spot_flush(now, 0); }
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
                /* SKIM decodes only in CW: outside it (the decoder mode of the
                 * RTTY button, SSB…) it pauses, and starts afresh back in CW */
                int was_cw = IS_CW(radio_mode);
                radio_mode = rs.mode;
                /* tell the owner once per pause, after 8 s out of CW (not at
                 * start, when the radio is briefly in the decoder mode) */
                if (!IS_CW(radio_mode)) {
                    if (!t_noncw) t_noncw = now;
                    if (!warned && now - t_noncw > 8000u) {
                        icap_popup("SKIM paused.\nSwitch to CW mode.");   /* short: CLOSE covers the lower lines */
                        warned = 1;
                    }
                } else { t_noncw = 0; warned = 0; }
                if (was_cw && !IS_CW(radio_mode)) {
                    for (int k = 0; k < cwd_channels(d); k++) if (ch[k].ln) send_line(k, ch[k].tone);
                    logline("# paused: radio not in CW", 0);
                } else if (!was_cw && IS_CW(radio_mode)) {
                    cwd_init(d, &c);
                    for (int k = 0; k < cwd_channels(d); k++) { ch[k].ln = 0; ch[k].tn = 0; ch[k].tail[0] = 0; }
                    logline("# resumed: CW", 0);
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
    spot_flush(icap_ticks(), 1);                /* what is left in the queue */
    if (ps_net >= 0) icap_net_close(ps_net);
    if (log_fd >= 0) icap_close(log_fd);
    return 0;
}
