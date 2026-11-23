/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * cwdec — streaming CW (Morse) decoder.
 *
 * Portable core, meant to run unchanged inside an IC-705 ICAP app:
 *   - freestanding C99: no malloc, no stdio, no libm (only sqrtf, which on
 *     hard-float ARM is a single VFP instruction).
 *   - Input: mono int16 audio at 8000 Hz (what icap_audio_read delivers).
 *   - Output: ASCII text per channel, in chunks, as audio arrives.
 *
 * Modes:
 *   CWD_MODE_SINGLE  one channel following the strongest station.
 *   CWD_MODE_MULTI   multi-signal: up to CWD_MAXCH channels, one per active tone
 *                    (CWD_MAXVIT with the Viterbi or AUTO detector).
 * Detectors (how the envelope becomes dots and dashes):
 *   CWD_DET_THRESHOLD  adaptive threshold with hysteresis (fast, low latency).
 *   CWD_DET_VITERBI    most likely sequence under a duration model
 *                      (more robust on weak signals; ~1 s delay).
 *   CWD_DET_AUTO       both at once; word by word it keeps the one that works
 *                      best for the current SNR and speed.
 *
 * Per-channel chain: I/Q mix to the tone → adaptive low-pass → 1 kHz envelope
 * → levels and threshold → timing (k-means) → detector → Morse table.
 * The tone search (Goertzel bank) is shared by all channels.
 */
#ifndef CWDEC_H
#define CWDEC_H

#include <stdint.h>

#define CWD_RATE      8000
#define CWD_NBINS_MAX 160
#define CWD_HIST      16
#define CWD_SHIST     40
#define CWD_EV        64
#ifndef CWD_MAXCH
#define CWD_MAXCH     12       /* channels with the threshold detector */
#endif
#ifndef CWD_MAXVIT
#define CWD_MAXVIT    4        /* channels with Viterbi state (det vit / auto) */
#endif
#define CWD_OUT       256
#define CWD_NPROF     3        /* operator timing profiles per channel */

/* Viterbi: states, maximum duration and traceback memory (in 4 ms ticks) */
#define CWD_VS        5        /* dot, dash, element gap, letter gap, word gap */
#define CWD_VDMAX     256
#define CWD_VRING     1024

enum { CWD_MODE_SINGLE = 0, CWD_MODE_MULTI = 1 };
enum { CWD_DET_THRESHOLD = 0, CWD_DET_VITERBI = 1, CWD_DET_AUTO = 2 };

#define CWD_WLEN      24       /* AUTO mode: maximum word length */
#define CWD_WQ        12       /* AUTO mode: queued words per detector */

typedef struct {
    float tone_min_hz;    /* tone search range (default 200) */
    float tone_max_hz;    /* (default 2600) */
    float tone_fixed_hz;  /* >0: one channel fixed on this tone, no search */
    float squelch;        /* minimum envelope signal/noise ratio */
    float wpm_init;       /* initial speed (default 20) */
    int   mode;           /* CWD_MODE_SINGLE / CWD_MODE_MULTI */
    int   detector;       /* CWD_DET_THRESHOLD / _VITERBI / _AUTO */
    int   max_channels;   /* in multi mode (1..CWD_MAXCH) */
} cwd_cfg_t;

typedef struct {
    float wpm;      /* estimated speed */
    float tone_hz;  /* channel tone */
    float snr;      /* envelope signal/noise (linear, amplitude) */
    int   active;   /* the channel has a station assigned */
    int   open;     /* squelch open (decoding) */
    int   mark;     /* current state: 1 = tone present */
    int   held;     /* elements held back (start of a transmission): more text is coming */
} cwd_status_t;

typedef struct {
    float delta[CWD_VDMAX + 1][CWD_VS];
    float aggG[CWD_VDMAX + 1], aggM[CWD_VDMAX + 1];
    uint8_t argG[CWD_VDMAX + 1], argM[CWD_VDMAX + 1];
    float psum[CWD_VDMAX + 1];
    uint16_t bpd[CWD_VRING][CWD_VS];
    uint8_t  bps[CWD_VRING][CWD_VS];
    float dur[CWD_VS][CWD_VDMAX + 1];
    int   dmax[CWD_VS];
    float mu[CWD_VS];
    uint32_t t, committed;
    float acc; int accn;
    float ms, ms2, ss, ss2;  /* mean and mean square: mark / space */
    int   lv_init;
} cwd_vit_t;

/* text builder of one detector */
typedef struct {
    int   code, nel, char_since_space, word_done;
    char  w[CWD_WLEN]; int wn;       /* word being built (AUTO mode) */
    uint32_t w_t0, w_t1;             /* end of first / last character (ms) */
    float w_snr, w_wpm;
} cwd_tb_t;

typedef struct {
    char  txt[CWD_WLEN];
    uint32_t t0, t1;
    float snr, wpm;
} cwd_word_t;

/* timing of one operator heard on a channel (several alternate on one frequency) */
typedef struct {
    float marks[CWD_HIST];  int nmarks, mpos;
    float spaces[CWD_SHIST]; int nspaces, spos;
    float dot, dash, gap, unit, wmul;
    float level;                  /* mean envelope of its marks */
    uint32_t last;                /* when it was last heard (channel ms) */
    int   used;
} cwd_prof_t;

typedef struct {
    int   active, locked;
    uint32_t last_seen;      /* Goertzel frame when its tone was last seen */
    float tone, tone_snr, best_snr;

    /* NCO and I/Q filter */
    float pr, pi, wr, wi;
    int   renorm;
    float fi[4], fq[4];
    float alpha, fdiv, tau_ms;
    int   decim;

    /* envelope and threshold (1 kHz) */
    uint32_t t;
    float env, noise, sig;
    int   init, raw, conf, pend, open;
    uint32_t edge_t;

    /* timing */
    float marks[CWD_HIST];  int nmarks, mpos;
    float spaces[CWD_SHIST]; int nspaces, spos;
    float dot, dash, gap, unit;   /* measured ms */
    float wmul;                   /* word-gap threshold, in units above the element gap */
    float ev[CWD_EV];  int nev, warm, warm_marks;
    float evl[CWD_EV];            /* envelope level of each buffered mark */

    /* operator profiles: which one is keying, and its level */
    cwd_prof_t prof[CWD_NPROF];
    int   cur;                    /* current profile, -1 = none yet */
    int   prev;                   /* the one heard before it */
    float lvl;                    /* level of the current operator's marks */
    float mk_sum; int mk_n;       /* envelope peak of the mark being keyed */
    float last_space;             /* length of the previous space (ms) */
    int   jump_n;                 /* marks in a row with a clear level jump */

    uint32_t warm_t0;

    /* text: [0] threshold, [1] Viterbi; in AUTO, word queues and merging */
    cwd_tb_t tb[2];
    cwd_word_t q[2][CWD_WQ];
    int   qn[2];
    uint32_t suppress[2];
    int   merged_any;

    char  out[CWD_OUT];
    int   outn;

    cwd_vit_t *v;                 /* Viterbi state (0 if this channel has none) */
} cwd_chan_t;

typedef struct {
    cwd_cfg_t cfg;

    /* tone search (Goertzel), shared */
    int   nbins;
    float bin_f0, bin_step;
    float coef[CWD_NBINS_MAX];
    float g1[CWD_NBINS_MAX], g2[CWD_NBINS_MAX];
    float spec[CWD_NBINS_MAX];
    float win[256];
    float floor;
    int   gcount;
    uint32_t frames;

    const char *table[256];

    int   nch;
    cwd_chan_t ch[CWD_MAXCH];
    cwd_vit_t vit[CWD_MAXVIT];
} cwd_t;

void cwd_default_cfg(cwd_cfg_t *c);
void cwd_init(cwd_t *d, const cwd_cfg_t *c);
/* Processes n samples. Returns a bit mask of channels with new text. */
unsigned cwd_process(cwd_t *d, const int16_t *x, int n);
/* End of input: flushes everything pending. Returns the mask. */
unsigned cwd_flush(cwd_t *d);
/* Copies (and consumes) channel ch's new text. Returns its length. */
int  cwd_take(cwd_t *d, int ch, char *out, int outmax);
int  cwd_channels(const cwd_t *d);
void cwd_status(const cwd_t *d, int ch, cwd_status_t *s);

#endif
