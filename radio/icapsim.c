/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* icapsim — minimal ICAP loader simulator for a desktop computer, to test the whole app
 * (skim.c) without the radio. It provides the icap.h service table with:
 *   - RX audio read from a WAV file (8 kHz), as fast as the app asks for it;
 *   - clock and ticks simulated from the audio consumed;
 *   - Apps-folder files in a local directory (-d, default .);
 *   - the 8-line panel, printed every -p seconds and at the end;
 *   - the DECODE screen: each line printed with the prefix "DECODE| ".
 *
 * usage: icapsim input.wav [-d apps_dir] [-p seconds] [-m mode] [-D sec:Hz] [-U 0|1]
 *                [-C CALL] [-L LOCATOR] [-P 0|1]   (@MYCALL, @MYLOC, @MODECFG pskr=)
 *                [-M code]                         (radio mode code: 32 CW FIL1, 33 CW FIL2…)
 *   -D 30:7029500  after 30 s the dial moves to 7029500 Hz (the audio tone does NOT
 *                  change: only useful to check that stations are remembered)
 */
#include "icap.h"
#include "../tools/wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int app_main(const struct icap_svc *svc);

static int16_t *aud; static int naud, pos;
static char panel[8][128];
static const char *dir = ".";
static double panel_every = 0, next_panel = 0;
static const char *mode_name = "";
static FILE *files[4];

static double now_s(void) { return pos / 8000.0; }

static void show_panel(const char *why)
{
    printf("+---- panel %s (t=%.1fs) ----\n", why, now_s());
    for (int i = 0; i < 8; i++) printf("| %s\n", panel[i]);
    printf("+-----------------------------\n");
}

static void s_print(const char *s)
{
    for (int i = 0; i < 7; i++) strcpy(panel[i], panel[i + 1]);
    snprintf(panel[7], sizeof panel[7], "%s", s);
    printf("PRINT | %s\n", s);
}
static void s_set_line(int l, const char *s) { if (l >= 0 && l < 8) snprintf(panel[l], sizeof panel[l], "%s", s); }
static void s_clear(void) { memset(panel, 0, sizeof panel); }

static int sim_mode = 32;                                  /* -M code: radio mode code (32 = CW, FIL1) */
static int sim_usb = 0;                                     /* -U 1: @FREQ reports usb=1 */
static const char *sim_call, *sim_loc;                      /* -C CALL, -L LOCATOR: @MYCALL, @MYLOC */
static int sim_pskr = -1;                                   /* -P 0|1: @MODECFG pskr= (absent if not given) */
static uint32_t sim_dial(void);
static int s_open(const char *name, int mode)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    /* firmware virtual files: @MYCALL, @MYLOC, @MODECFG (from -C, -L, -P) and @FREQ */
    static char fb[96];
    fb[0] = 0;
    if (!strcmp(name, "@MYCALL") && sim_call) snprintf(fb, sizeof fb, "%s\n", sim_call);
    if (!strcmp(name, "@MYLOC") && sim_loc) snprintf(fb, sizeof fb, "%s\n", sim_loc);
    if (!strcmp(name, "@MODECFG") && sim_pskr >= 0) snprintf(fb, sizeof fb, "mode=SKIM sub=- pskr=%d gen=1\n", sim_pskr);
    if (!strcmp(name, "@FREQ")) snprintf(fb, sizeof fb, "f=%u usb=%d rx=main trv=0\n", (unsigned)sim_dial(), sim_usb);
    if (fb[0] && !(mode & 1)) {
        for (int i = 0; i < 4; i++) if (!files[i]) { files[i] = fmemopen(fb, strlen(fb), "r"); return files[i] ? i : ICAP_E_IO; }
        return ICAP_E_NOFD;
    }
    if (name[0] == '@') return ICAP_E_NAME;
    for (int i = 0; i < 4; i++) if (!files[i]) {
        files[i] = fopen(p, (mode & 1) ? "wb" : "rb");
        return files[i] ? i : ICAP_E_IO;
    }
    return ICAP_E_NOFD;
}
static int s_read(int fd, void *b, uint32_t n) { return (fd >= 0 && fd < 4 && files[fd]) ? (int)fread(b, 1, n, files[fd]) : ICAP_E_ARG; }
static int s_write(int fd, const void *b, uint32_t n) { return (fd >= 0 && fd < 4 && files[fd]) ? (int)fwrite(b, 1, n, files[fd]) : ICAP_E_ARG; }
/* network: nothing leaves the computer; each UDP datagram is appended to
 * PSKR.DAT as a 2-byte big-endian length and the bytes */
static int s_net_status(void) { return 0; }
static int s_net_open(const char *host, uint32_t port, uint32_t flags, uint32_t to)
{ (void)to; fprintf(stderr, "net_open %s:%u flags %u\n", host, (unsigned)port, (unsigned)flags); return 7; }
static int s_net_send(int h, const void *b, uint32_t n, uint32_t to)
{
    (void)to; if (h != 7) return ICAP_E_ARG;
    char p[512]; snprintf(p, sizeof p, "%s/PSKR.DAT", dir);
    FILE *f = fopen(p, "ab"); if (!f) return ICAP_E_IO;
    fputc((int)(n >> 8), f); fputc((int)(n & 255), f); fwrite(b, 1, n, f); fclose(f);
    return (int)n;
}
static int s_net_recv(int h, void *b, uint32_t n, uint32_t to) { (void)h; (void)b; (void)n; (void)to; return 0; }
static int s_net_close(int h) { (void)h; return 0; }
static int s_net_error(void) { return 0; }

static int s_remove(const char *name, int flags)
{
    (void)flags;
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return remove(p) == 0 ? 0 : ICAP_E_IO;
}
static int s_close(int fd) { if (fd >= 0 && fd < 4 && files[fd]) { fclose(files[fd]); files[fd] = 0; return 0; } return ICAP_E_ARG; }

static void s_sleep(uint32_t ms) { (void)ms; }
static void s_yield(void) {}
static uint32_t s_tick(void) { return (uint32_t)(pos / 8); }
static int s_mem(struct icap_meminfo *mi) { memset(mi, 0, sizeof *mi); return 0; }
static double move_t = -1; static uint32_t move_hz;          /* -D sec:Hz: move the dial */
static uint32_t sim_dial(void) { return (move_t >= 0 && now_s() >= move_t) ? move_hz : 7030000; }
static int s_radio(struct icap_radio *r)
{
    memset(r, 0, sizeof *r);
    r->freq_hz = (move_t >= 0 && now_s() >= move_t) ? move_hz : 7030000;
    r->mode = (uint8_t)sim_mode;
    return 0;
}
static int s_stop(void)
{
    if (panel_every > 0 && now_s() >= next_panel) { show_panel("periodic"); next_panel += panel_every; }
    return pos >= naud;
}
static uint32_t s_task(void) { return 1; }
static int s_clock(struct icap_time *t)
{
    int s = (int)now_s();
    memset(t, 0, sizeof *t);
    t->year = 2026; t->mon = 11; t->day = 22;
    t->hour = (uint8_t)(12 + s / 3600); t->min = (uint8_t)(s / 60 % 60); t->sec = (uint8_t)(s % 60);
    return 0;
}
static int s_audio(int16_t *b, int n)
{
    int k = naud - pos < n ? naud - pos : n;
    if (k <= 0) return 0;
    memcpy(b, aud + pos, k * sizeof(int16_t));
    pos += k;
    return k;
}
static int s_rate(void) { return 8000; }
static void s_decode(const char *s) { printf("DECODE| %s\n", s); }
static int s_mode(char *b, int max) { snprintf(b, max, "%s", mode_name); return (int)strlen(b); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: icapsim f.wav [-d dir] [-p sec] [-H heap_kb] [-m mode] [-D sec:Hz] [-U 0|1] [-C call] [-L loc] [-P 0|1] [-M code]\n"); return 1; }
    int heap_kb = 1024;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-d")) dir = argv[i + 1];
        else if (!strcmp(argv[i], "-p")) { panel_every = atof(argv[i + 1]); next_panel = panel_every; }
        else if (!strcmp(argv[i], "-H")) heap_kb = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-m")) mode_name = argv[i + 1];
        else if (!strcmp(argv[i], "-U")) sim_usb = atoi(argv[i + 1]);   /* @FREQ usb= 0 or 1 */
        else if (!strcmp(argv[i], "-C")) sim_call = argv[i + 1];
        else if (!strcmp(argv[i], "-M")) sim_mode = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-L")) sim_loc = argv[i + 1];
        else if (!strcmp(argv[i], "-P")) sim_pskr = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-D")) { double a; unsigned b; if (sscanf(argv[i + 1], "%lf:%u", &a, &b) == 2) { move_t = a; move_hz = b; } }
    }
    aud = wav_read_8k(argv[1], &naud);
    if (!aud) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    static struct icap_svc svc;
    svc.abi_version = ICAP_ABI_VERSION; svc.radio_id = ICAP_RADIO_IC705; svc.size = sizeof svc;
    svc.print = s_print; svc.set_line = s_set_line; svc.clear = s_clear;
    svc.f_open = s_open; svc.f_read = s_read; svc.f_write = s_write; svc.f_close = s_close;
    svc.sleep_ms = s_sleep; svc.yield = s_yield; svc.tick_ms = s_tick;
    svc.mem_info = s_mem; svc.radio_state = s_radio; svc.should_stop = s_stop; svc.task_id = s_task;
    /* on 64-bit hosts pointers do not fit in heap_base (32 bits): skim.c uses static
     * memory when built with SKIM_HOST; heap_size is informational only */
    svc.heap_base = 0;
    svc.heap_size = (uint32_t)heap_kb * 1024;
    svc.clock_now = s_clock; svc.audio_read = s_audio; svc.audio_rate = s_rate; svc.decode_out = s_decode;
    svc.mode = s_mode; svc.remove = s_remove;
    svc.net_status = s_net_status; svc.net_open = s_net_open; svc.net_send = s_net_send;
    svc.net_recv = s_net_recv; svc.net_close = s_net_close; svc.net_error = s_net_error;
    int r = app_main(&svc);
    show_panel("final");
    printf("app_main returned %d\n", r);
    free(aud);
    return 0;
}
