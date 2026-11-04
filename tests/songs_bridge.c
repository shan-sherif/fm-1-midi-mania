/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* The firmware's editor protocol (editor.c, editor_songs.c) on stdin / stdout, for web/test_songs_usb.mjs: the song
 * upload page's code (web/fm1songs.js) against the real C protocol and a simulated song region.
 *   in:  "cmd hh hh .."  a request's cmd (decimal) and its bytes (hex)    out: its reply's bytes after the cmd (hex)
 *   in:  "S"             a power-on: the songs scanned again                out: the songs listed */
#define EDITOR_TEST_NO_MAIN
#include "editor_test.c"

int main(void)
{
    static char line[8192];
    static uint8_t a[1024];
    reset();
    memset(host_samples, 0xFF, sizeof host_samples);
    mm_song_base = &host_samples[0][0];
    mm_song_scan();
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        const char *p = line;
        unsigned cmd, v;
        int k;
        uint32_t n = 0, m, i;
        if (line[0] == 'S') {
            mm_song_scan();
            printf("%u\n", (unsigned)mm_nstored);
            continue;
        }
        if (sscanf(p, "%u%n", &cmd, &k) != 1)
            break;
        for (p += k; n < sizeof a && sscanf(p, "%x%n", &v, &k) == 1; p += k)
            a[n++] = (uint8_t)v;
        m = request(cmd, a, n);
        for (i = 5; i + 1u < m; i++)
            printf("%02x ", host_wire[i]);
        printf("\n");
    }
    return 0;
}
