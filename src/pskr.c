/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* pskr — see pskr.h. Layout and byte values from https://pskreporter.info/pskdev.html */
#include "pskr.h"

#define RX_TMPL 0x9992          /* link ids of the two record formats */
#define TX_TMPL 0x9993
#define ENT     30351u          /* PSKReporter enterprise number (0x768F) */

static int  slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, int max) { int i = 0; for (; s && s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }

static uint8_t *u16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)v; return b + 2; }
static uint8_t *u32(uint8_t *b, uint32_t v) { b = u16(b, v >> 16); return u16(b, v & 0xFFFFu); }
static uint8_t *str(uint8_t *b, const char *s) { int n = slen(s); if (n > 254) n = 254; *b++ = (uint8_t)n; for (int i = 0; i < n; i++) *b++ = (uint8_t)s[i]; return b; }
/* an enterprise field specifier: id with the enterprise bit, length (0xFFFF = variable) */
static uint8_t *ef(uint8_t *b, uint32_t id, uint32_t len) { b = u16(b, 0x8000u | id); b = u16(b, len); return u32(b, ENT); }
static uint8_t *pad4(uint8_t *start, uint8_t *b) { while ((b - start) & 3) *b++ = 0; return b; }

void pskr_init(pskr_t *p, const char *my_call, const char *my_loc, const char *software,
               const char *mode, uint32_t id)
{
    uint8_t *z = (uint8_t *)p;
    for (unsigned i = 0; i < sizeof *p; i++) z[i] = 0;
    scpy(p->my_call, my_call, PSKR_CALL);
    scpy(p->my_loc, my_loc, sizeof p->my_loc);
    scpy(p->soft, software, sizeof p->soft);
    scpy(p->mode, mode, sizeof p->mode);
    p->id = id;
}

int pskr_add(pskr_t *p, const char *call, uint64_t freq_hz, int snr_db, uint32_t t)
{
    if (p->nq == PSKR_QUEUE) {                        /* full: drop the oldest */
        for (int i = 1; i < PSKR_QUEUE; i++) p->q[i - 1] = p->q[i];
        p->nq--;
    }
    pskr_rec_t *r = &p->q[p->nq++];
    scpy(r->call, call, PSKR_CALL);
    r->freq = freq_hz;
    r->snr = (int8_t)(snr_db > 127 ? 127 : snr_db < -128 ? -128 : snr_db);
    r->t = t;
    return p->nq;
}

static int rec_len(const pskr_t *p, const pskr_rec_t *r)
{
    return 1 + slen(r->call) + 5 + 1 + 1 + slen(p->mode) + 1 + 4;
}

int pskr_build(pskr_t *p, uint8_t *buf, int max, uint32_t now)
{
    if (p->nq == 0 || !p->my_call[0] || max < 200) return 0;
    int tmpl = p->sent < 3 || now - p->last_tmpl >= 3600u;
    uint8_t *b = buf + 16;                            /* header filled in at the end */

    if (tmpl) {
        /* receiver format: receiverCallsign, receiverLocator, decoderSoftware */
        uint8_t *s = b;
        b = u16(b, 3); b = u16(b, 0);                 /* options template set, length later */
        b = u16(b, RX_TMPL); b = u16(b, 3); b = u16(b, 1);
        b = ef(b, 2, 0xFFFF); b = ef(b, 4, 0xFFFF); b = ef(b, 8, 0xFFFF);
        b = u16(b, 0);
        u16(s + 2, (uint32_t)(b - s));
        /* sender format: senderCallsign, frequency (5), sNR (1), mode,
         * informationSource (1), flowStartSeconds */
        s = b;
        b = u16(b, 2); b = u16(b, 0);
        b = u16(b, TX_TMPL); b = u16(b, 6);
        b = ef(b, 1, 0xFFFF); b = ef(b, 5, 5); b = ef(b, 6, 1);
        b = ef(b, 10, 0xFFFF); b = ef(b, 11, 1);
        b = u16(b, 150); b = u16(b, 4);               /* flowStartSeconds (IANA) */
        u16(s + 2, (uint32_t)(b - s));
    }

    /* receiver record */
    uint8_t *s = b;
    b = u16(b, RX_TMPL); b = u16(b, 0);
    b = str(b, p->my_call); b = str(b, p->my_loc); b = str(b, p->soft);
    b = pad4(s, b);
    u16(s + 2, (uint32_t)(b - s));

    /* sender records, as many as fit */
    s = b;
    b = u16(b, TX_TMPL); b = u16(b, 0);
    int n = 0;
    while (n < p->nq && (b - buf) + rec_len(p, &p->q[n]) + 3 <= max) {
        const pskr_rec_t *r = &p->q[n++];
        b = str(b, r->call);
        *b++ = (uint8_t)(r->freq >> 32); b = u32(b, (uint32_t)r->freq);
        *b++ = (uint8_t)r->snr;
        b = str(b, p->mode);
        *b++ = 1;                                     /* automatically extracted */
        b = u32(b, r->t);
    }
    if (n == 0) return 0;
    b = pad4(s, b);
    u16(s + 2, (uint32_t)(b - s));

    /* header: version 10, length, export time, sequence (records sent), id */
    uint8_t *h = buf;
    h = u16(h, 10); h = u16(h, (uint32_t)(b - buf)); h = u32(h, now); h = u32(h, p->seq); u32(h, p->id);
    p->seq += (uint32_t)n;

    for (int i = n; i < p->nq; i++) p->q[i - n] = p->q[i];
    p->nq -= n;
    p->sent++;
    if (tmpl) p->last_tmpl = now;
    return (int)(b - buf);
}

uint32_t pskr_unix(int year, int mon, int day, int hour, int min, int sec)
{
    /* days from 1970-01-01 (civil calendar, valid from 1970 on) */
    int y = year - (mon <= 2);
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    return (uint32_t)(days * 86400L + hour * 3600L + min * 60L + sec);
}
