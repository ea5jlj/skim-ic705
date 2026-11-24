/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* cwfile — decodes a WAV (any rate/channels) with the cwdec core, feeding it
 * 160-sample blocks just like the app on the radio does.
 *
 * usage: cwfile input.wav [options]
 *   -m         multi mode: one line per station
 *   -a         in multi mode, also show lines that look like noise
 *   -c N       channels in multi mode (default 8, like SKIM; max 12)
 *   -R lo-hi   tone search range in Hz (default 200-2600; max ~2500 Hz wide)
 *   -V         Viterbi detector (default: threshold)
 *   -A         AUTO: threshold or Viterbi, word by word
 *   -f Hz      one channel fixed on that tone
 *   -v         status every second (single mode)
 *   -r ref.txt compare with a reference text (CER, single mode)
 *   -w         fix word spacing with the CW vocabulary (src/words.c), as SKIM does;
 *              in single mode the text is printed at the end
 */
#include "../src/cwdec.h"
#include "../src/callsign.h"
#include "../src/words.h"
#include "../src/dict.h"
#include "wav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void norm(const char *in, char *out)
{
    int n = 0, sp = 1;
    for (; *in; in++) {
        int c = toupper((unsigned char)*in);
        if (c == ' ' || c == '\n') { if (!sp) { out[n++] = ' '; sp = 1; } }
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

/* multi-mode log: text segments per station, in order of appearance */
typedef struct { float t0, tone; int ch; char txt[4096]; int n; char call[16]; } seg_t;
static seg_t segs[512];
static int nseg;
static int last_seg[CWD_MAXCH];
static cs_t css[CWD_MAXCH];
static float cs_tone[CWD_MAXCH];

static void log_text(int ch, float tone, float t, const char *s)
{
    if (fabsf(cs_tone[ch] - tone) > 25.0f) { cs_reset(&css[ch]); cs_tone[ch] = tone; }
    cs_feed(&css[ch], s);
    int k = last_seg[ch] - 1;
    if (k < 0 || fabsf(segs[k].tone - tone) > 25.0f || t - segs[k].t0 > 120.0f) {
        if (nseg >= 512) return;
        k = nseg++;
        segs[k].t0 = t; segs[k].tone = tone; segs[k].ch = ch; segs[k].n = 0; segs[k].call[0] = 0;
        last_seg[ch] = k + 1;
    }
    if (cs_best(&css[ch])[0]) snprintf(segs[k].call, sizeof segs[k].call, "%s", cs_best(&css[ch]));
    int m = strlen(s);
    if (segs[k].n + m < (int)sizeof segs[k].txt - 1) {
        memcpy(segs[k].txt + segs[k].n, s, m);
        segs[k].n += m;
        segs[k].txt[segs[k].n] = 0;
    }
}

/* Multi-mode display filter. Removes runs of 3 or more words that are typical
 * noise (1–2 letters of E/T/I/S/H/M/N/A/5, or containing '*') and decides
 * whether real words remain. Returns 0 if the whole line is noise. */
static int clean_line(char *t)
{
    char *w[1024]; int n = 0;
    for (char *q = strtok(t, " "); q && n < 1024; q = strtok(NULL, " ")) w[n++] = q;
    int junk[1024];
    for (int i = 0; i < n; i++) {
        int len = strlen(w[i]), weak = 1;
        for (char *r = w[i]; *r; r++) if (!strchr("ETISHMNA5*", *r)) weak = 0;
        junk[i] = strchr(w[i], '*') != NULL || (len <= 2 && weak);
    }
    char out[4096]; int on = 0, good = 0, weakc = 0, tot = 0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && junk[j]) j++;
        if (j - i >= 3) { i = j; continue; }          /* noise run */
        int end = j > i ? j : i + 1;                 /* short run or normal word */
        for (; i < end && i < n; i++) {
            int len = strlen(w[i]);
            if (on + len + 2 >= (int)sizeof out) break;
            if (on) out[on++] = ' ';
            memcpy(out + on, w[i], len); on += len;
            int weak = 1;
            for (char *r = w[i]; *r; r++) { if (!strchr("ETISHMNA5*", *r)) weak = 0; if (strchr("EISH5*", *r)) weakc++; }
            tot += len;
            if (len >= 3 && !weak && !strchr(w[i], '*')) good++;
        }
    }
    out[on] = 0;
    strcpy(t, out);
    return good >= 2 && weakc <= 0.5f * tot;
}

/* word-spacing fix over a long text, a few words at a time (SKIM works on
 * DECODE lines of about 40 characters) */
static void fix_words(char *t, const char *call)
{
    static char res[65536]; int rn = 0;
    const char *known[1] = { call };
    char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char chunk[512]; int cn = 0, words = 0;
        while (*p && words < 8 && cn < 400) {
            while (*p && *p != ' ' && cn < 400) chunk[cn++] = *p++;
            while (*p == ' ') p++;
            words++;
            if (*p) chunk[cn++] = ' ';
        }
        chunk[cn] = 0;
        char fixed[1024];
        int m = wd_fix(chunk, fixed, sizeof fixed, known, call && call[0] ? 1 : 0);
        if (rn && rn < (int)sizeof res - 1) res[rn++] = ' ';
        for (int i = 0; i < m && rn < (int)sizeof res - 1; i++) res[rn++] = fixed[i];
    }
    res[rn] = 0;
    memcpy(t, res, rn + 1);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: cwfile input.wav [-m] [-c N] [-a] [-V|-A] [-R lo-hi] [-f Hz] [-v] [-r ref.txt] [-w]\n");
        return 1;
    }
    wd_dict(dict_idx, dict_data);              /* -w uses the word list, as SKIM with SKIMDICT.BIN */
    cwd_cfg_t cfg; cwd_default_cfg(&cfg);
    cfg.max_channels = 8;
    int verbose = 0, all_lines = 0, wfix = 0;
    const char *ref = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) cfg.tone_fixed_hz = atof(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-m")) cfg.mode = CWD_MODE_MULTI;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) cfg.max_channels = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-a")) all_lines = 1;
        else if (!strcmp(argv[i], "-w")) wfix = 1;
        else if (!strcmp(argv[i], "-R") && i + 1 < argc) {
            float lo, hi;
            if (sscanf(argv[++i], "%f-%f", &lo, &hi) == 2) { cfg.tone_min_hz = lo; cfg.tone_max_hz = hi; }
        }
        else if (!strcmp(argv[i], "-V")) cfg.detector = CWD_DET_VITERBI;
        else if (!strcmp(argv[i], "-A")) cfg.detector = CWD_DET_AUTO;
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) ref = argv[++i];
    }
    int n;
    int16_t *x = wav_read_8k(argv[1], &n);
    if (!x) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    static cwd_t d;
    cwd_init(&d, &cfg);
    int multi = cwd_channels(&d) > 1;
    char out[CWD_OUT];
    static char all[65536]; int an = 0;

    for (int i = 0; i <= n; i += 160) {
        int k = n - i < 160 ? n - i : 160;
        unsigned mask = k > 0 ? cwd_process(&d, x + i, k) : cwd_flush(&d);
        for (int ch = 0; ch < cwd_channels(&d); ch++) {
            if (!(mask & (1u << ch))) continue;
            int m = cwd_take(&d, ch, out, sizeof out);
            if (!m) continue;
            if (multi) {
                cwd_status_t st; cwd_status(&d, ch, &st);
                log_text(ch, st.tone_hz, i / 8000.0f, out);
            } else {
                if (!wfix) { fputs(out, stdout); fflush(stdout); }
                if (an + m < (int)sizeof all) { memcpy(all + an, out, m); an += m; }
            }
        }
        if (verbose && !multi && (i / 160) % 50 == 0) {
            cwd_status_t s; cwd_status(&d, 0, &s);
            fprintf(stderr, "[%5.1fs %4.1f WPM %4.0f Hz SNR %4.1f dB %s]\n", i / 8000.0,
                    s.wpm, s.tone_hz, 20 * log10f(s.snr > 0 ? s.snr : 1e-6f), s.open ? "open" : "closed");
        }
    }
    all[an] = 0;

    if (multi) {
        for (int k = 0; k < nseg; k++) {
            char t[4096]; norm(segs[k].txt, t);
            if (!*t) continue;
            if (!all_lines && !clean_line(t)) continue;
            if (wfix) fix_words(t, segs[k].call);
            printf("%6.1fs %5.0f Hz %-9s %s\n", segs[k].t0, segs[k].tone, segs[k].call[0] ? segs[k].call : "-", t);
        }
    } else {
        if (wfix) { static char t[65536]; norm(all, t); fix_words(t, 0); fputs(t, stdout); memcpy(all, t, strlen(t) + 1); }
        putchar('\n');
        static cs_t one; cs_reset(&one); cs_feed(&one, all); cs_feed(&one, " ");
        if (cs_best(&one)[0]) printf("[callsign: %s]\n", cs_best(&one));
    }
    if (ref && !multi) {
        FILE *f = fopen(ref, "r");
        static char rt[65536], a[65536], b[65536];
        int rn = f ? (int)fread(rt, 1, sizeof rt - 1, f) : 0;
        if (f) fclose(f);
        rt[rn] = 0;
        norm(all, a); norm(rt, b);
        int e = lev(a, b);
        printf("CER %.1f%% (%d edits / %d reference characters)\n", 100.0 * e / strlen(b), e, (int)strlen(b));
    }
    free(x);
    return 0;
}
