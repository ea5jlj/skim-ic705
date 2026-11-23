/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * words — fixes word spacing in a decoded line using a small CW vocabulary
 * (CQ, DE, TU, 73, 5NN, RST, QTH…) and the callsign formation rules:
 *   - splits glued tokens: "CQCQDEIK4PLU" → "CQ CQ DE IK4PLU",
 *     "GM5NN73TUEE" → "GM 5NN 73 TU EE";
 *   - joins a callsign cut in two: "EA5 RJ" → "EA5RJ".
 * It only moves spaces: it never changes, adds or removes a letter. A token
 * is split only when every piece is a known word, a report, a number or a
 * callsign, and at least one piece is a strong one (CQ, DE, TU, 73, a report
 * or a callsign), so plain-language words are left alone.
 * Freestanding C, no dynamic memory.
 */
#ifndef WORDS_H
#define WORDS_H

/* in → out (max bytes including the terminating 0). known: callsigns already
 * identified on the band (may be 0 / nknown 0); they are preferred when
 * splitting and allow joining. Returns the length of out. */
int wd_fix(const char *in, char *out, int max, const char *const *known, int nknown);

#endif
