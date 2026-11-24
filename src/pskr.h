/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/*
 * pskr — builds PSKReporter reception reports: UDP datagrams in the IPFIX
 * format described at https://pskreporter.info/pskdev.html.
 *
 * The caller queues one record per station heard (call, frequency in Hz, S/N
 * in dB, time) and, about every five minutes, asks for a datagram and sends
 * it to report.pskreporter.info port 4739 from the same UDP socket. Records
 * are taken out of the queue when a datagram is built. The record format
 * descriptors go into the first three datagrams and then once an hour.
 *
 * Sender records: senderCallsign, frequency (5 bytes, so microwave
 * transverter frequencies fit), sNR (1 byte), mode, informationSource
 * (1 = automatically extracted), flowStartSeconds. Receiver record:
 * receiverCallsign, receiverLocator, decoderSoftware.
 * Freestanding C, no dynamic memory: the state lives in the caller's memory.
 */
#ifndef PSKR_H
#define PSKR_H
#include <stdint.h>

#define PSKR_CALL   16          /* callsign buffer, with the terminating 0 */
#define PSKR_QUEUE  40          /* records waiting to be sent */
#define PSKR_MAXDG  1400        /* largest datagram */

typedef struct {
    char     call[PSKR_CALL];
    uint64_t freq;              /* Hz */
    int8_t   snr;               /* dB */
    uint32_t t;                 /* seconds since 1970 */
} pskr_rec_t;

typedef struct {
    char       my_call[PSKR_CALL], my_loc[8], soft[40], mode[8];
    uint32_t   id;              /* observation domain: random, fixed for the session */
    uint32_t   seq;             /* records sent so far */
    int        sent;            /* datagrams built so far */
    uint32_t   last_tmpl;       /* time the descriptors were last sent */
    pskr_rec_t q[PSKR_QUEUE];
    int        nq;
} pskr_t;

/* mode: ADIF mode ("CW"); id: a random number for this session */
void pskr_init(pskr_t *p, const char *my_call, const char *my_loc, const char *software,
               const char *mode, uint32_t id);
/* queue one record; when the queue is full the oldest one is dropped.
 * Returns the number of records waiting. */
int  pskr_add(pskr_t *p, const char *call, uint64_t freq_hz, int snr_db, uint32_t t);
/* build the next datagram at time now (seconds since 1970) into buf; returns
 * its length, 0 if there is nothing to send. Records that do not fit stay
 * queued for the next datagram. */
int  pskr_build(pskr_t *p, uint8_t *buf, int max, uint32_t now);
/* seconds since 1970 from a UTC date and time */
uint32_t pskr_unix(int year, int mon, int day, int hour, int min, int sec);

#endif
