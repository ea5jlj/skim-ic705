/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * callsign — works out the callsign of the station on a channel from the
 * decoded text:
 *   - a callsign is whatever follows the callsign formation rules (prefix,
 *     1–4 digits, 1–4 letter suffix (up to 6 with 2+ digits: special-event
 *     calls like HA40EUDXF); one-letter prefixes only B F G I K M N R W);
 *   - evidence for: after DE, after CQ / CQ TEST / CQ DX, before TEST or after
 *     TU (contests), or repeated ("DL1ABC DL1ABC");
 *   - evidence against: before DE or before a report (5NN, 599…), because
 *     then it is the station being called, not the one transmitting.
 * It also tracks whether the station is calling CQ (or TEST, in contests) and who it is in a QSO
 * with (the other station: the one before DE or before a report).
 * Freestanding C, no dynamic memory. Fed with the text as it is decoded.
 */
#ifndef CALLSIGN_H
#define CALLSIGN_H

#define CS_LEN   12
#define CS_CANDS 8

typedef struct {
    char  tok[3][CS_LEN + 4];   /* last complete tokens (0 = most recent) */
    char  cur[CS_LEN + 4];      /* token being built */
    int   cn;
    struct { char call[CS_LEN + 1]; float score; } c[CS_CANDS];
    char  best[CS_LEN + 1];     /* callsign shown ("" if none) */
    char  peer[CS_LEN + 1];     /* station it is in a QSO with ("" if unknown) */
    int   cq;                   /* 1 while calling CQ */
} cs_t;

void cs_reset(cs_t *s);
void cs_feed(cs_t *s, const char *text);
const char *cs_best(const cs_t *s);
/* who it is in a QSO with ("X DE me", "X 5NN", "X GE 5NN"); "" if unknown */
const char *cs_peer(const cs_t *s);
/* 1 if the station is calling CQ (cleared when a QSO starts) */
int  cs_cq(const cs_t *s);
/* 1 if the word follows the callsign formation rules */
int  cs_is_call(const char *w);

#endif
