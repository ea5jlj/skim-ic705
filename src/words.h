/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * words — fixes word spacing in a decoded line using a small CW vocabulary
 * (CQ, DE, TU, 73, 5NN, RST, QTH…) and the callsign formation rules:
 *   - splits glued tokens: "CQCQDEIK4PLU" → "CQ CQ DE IK4PLU",
 *     "GM5NN73TUEE" → "GM 5NN 73 TU EE";
 *   - joins a callsign cut in two: "EA5 RJ" → "EA5RJ".
 * It only moves spaces: it never changes, adds or removes a letter. '=' and
 * '+' always stand apart ("TNXFERCALL=" → "TNX FER CALL ="). A token is split
 * only when every piece is a known word, a report, a number or a callsign
 * (or, right after DR, NAME, QTH or GE DR OM, one unknown word: "GEDRALFON"
 * → "GE DR ALFON"). Without a callsign or report, weak words (FER, CALL, DR…)
 * need strong ones (TNX, CU, GE…) in a token of 6+ letters, so plain-language
 * words (TOUR, HIDE, GLAND) are left alone.
 * Freestanding C, no dynamic memory.
 */
#ifndef WORDS_H
#define WORDS_H

/* in → out (max bytes including the terminating 0). known: callsigns already
 * identified on the band (may be 0 / nknown 0); they are preferred when
 * splitting and allow joining. Returns the length of out. */
int wd_fix(const char *in, char *out, int max, const char *const *known, int nknown);

/* Optional English word list (format of tools/mkdict.py: idx[677] bucket
 * offsets into data). With it, split words are joined ("TH ERE" → "THERE")
 * and glued phrases split around a CW word ("HPECUINPERSON" → "HPE CU IN
 * PERSON"); dictionary words are never split. Without it (default, or
 * wd_dict(0, 0)), only the CW vocabulary is used. The data must stay valid. */
void wd_dict(const unsigned short *idx, const unsigned char *data);

#endif
