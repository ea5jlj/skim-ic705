/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE. */
/* wordfix — runs the word-spacing fixer (src/words.c) over text and shows
 * the lines it changes. Reads SKIMnn.LOG files (it takes the raw text after
 * "| tone") or plain text lines.
 *
 * usage: wordfix [-a] [-k CALL]... file...   (no file: stdin)
 *   -a       print every line, not only the changed ones
 *   -k CALL  a callsign to treat as already known (repeatable)
 */
#include "../src/words.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *known[32];
static int nknown, all;

static void run(FILE *f)
{
    char line[1024], out[1024];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#') continue;
        char *t = line;
        char *bar = strstr(line, "| ");
        if (bar) {                               /* SKIM log: "| tone raw text" */
            t = bar + 2;
            while (isdigit((unsigned char)*t)) t++;
            while (*t == ' ') t++;
        }
        for (char *q = t; *q; q++) *q = (char)toupper((unsigned char)*q);
        wd_fix(t, out, sizeof out, known, nknown);
        if (all || strcmp(t, out) != 0) {
            /* compare ignoring spaces: only spacing may change */
            char a[1024], b[1024]; int na = 0, nb = 0;
            for (char *q = t; *q; q++) if (*q != ' ') a[na++] = *q;
            for (char *q = out; *q; q++) if (*q != ' ') b[nb++] = *q;
            a[na] = b[nb] = 0;
            printf("  %s\n%s %s\n", t, strcmp(a, b) ? "!!" : "->", out);
        }
    }
}

int main(int argc, char **argv)
{
    int files = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a")) all = 1;
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) { if (nknown < 32) known[nknown++] = argv[++i]; }
        else {
            FILE *f = fopen(argv[i], "r");
            if (!f) { perror(argv[i]); return 1; }
            run(f); fclose(f); files++;
        }
    }
    if (!files) run(stdin);
    return 0;
}
