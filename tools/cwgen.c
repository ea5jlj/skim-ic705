/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* cwgen — generates a synthetic CW WAV at 8 kHz.
 * usage: cwgen out.wav [options] [-t "TEXT"]
 *   -w wpm  -f tone  -s snr_db  -j jitter  -r dash_ratio  -q qsb_db  -p qsb_period
 *   -d drift_hz  -m qrm_tone  -M qrm_db  -S seed  -L letter_gap  -W word_gap (units)
 *   -c ms   first element of each transmission cut short (slow TX switching)
 */
#include "synth.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: cwgen out.wav [-w wpm] [-f Hz] [-s dB] [-j jit] [-r ratio] [-q dB] [-p s] [-d Hz] [-m Hz] [-M dB] [-S seed] [-L units] [-W units] [-c ms] [-t text]\n"); return 1; }
    synth_cfg_t c; synth_default(&c);
    const char *text = NULL;
    for (int i = 2; i + 1 < argc; i += 2) {
        const char *o = argv[i], *v = argv[i + 1];
        if (!strcmp(o, "-w")) c.wpm = atof(v);
        else if (!strcmp(o, "-f")) c.tone = atof(v);
        else if (!strcmp(o, "-s")) c.snr_db = atof(v);
        else if (!strcmp(o, "-j")) c.jitter = atof(v);
        else if (!strcmp(o, "-r")) c.dash_ratio = atof(v);
        else if (!strcmp(o, "-q")) c.qsb_db = atof(v);
        else if (!strcmp(o, "-p")) c.qsb_period_s = atof(v);
        else if (!strcmp(o, "-d")) c.drift_hz = atof(v);
        else if (!strcmp(o, "-m")) c.qrm_tone = atof(v);
        else if (!strcmp(o, "-M")) c.qrm_db = atof(v);
        else if (!strcmp(o, "-S")) c.seed = atoi(v);
        else if (!strcmp(o, "-L")) c.letter_gap = atof(v);
        else if (!strcmp(o, "-W")) c.word_gap = atof(v);
        else if (!strcmp(o, "-c")) c.clip_ms = atof(v);
        else if (!strcmp(o, "-t")) text = v;
    }
    char buf[512];
    if (!text) { synth_text(buf, 200, c.seed); text = buf; }
    char qrm[512]; synth_text(qrm, 200, c.seed + 999);
    int n;
    int16_t *x = synth_render(&c, text, qrm, &n);
    wav_write(argv[1], x, n, 8000);
    printf("%s\n", text);
    free(x);
    return 0;
}
