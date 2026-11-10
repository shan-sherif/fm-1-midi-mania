/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* The site's and README's pictures of the game (web/site_media/, tools/make_screens.sh): the host build, as the UI
 * tests run it (tests/ui_test.c, its harness only: UI_TEST_NO_MAIN), the built-in songs played with their notes hit
 * on time (each note's key held while it lasts, so the keys light and every press is PERFECT), the screen saved as
 * PPM (240 x 240) into OUT:
 *   view_keys27 view_keys49 view_drums view_guitar_strings view_guitar_chromatic view_bass_strings view_songs
 *   view_sounds view_sounds_list   each a moment of its view (DRUMS played by the FM-1: CPU, the pads lit as the song plays them)
 *   gif_keys_000 .. 119   FM Keys on 27 KEYS, 8 s at 15 frames a second (the GIF's frames)
 *   tools/make_screens [OUT]   (default build/screens) */
#define UI_TEST_NO_MAIN 1
#include "../tests/ui_test.c"

static int32_t ms_out[2 * CTL];
static const char *ms_dir = "build/screens";

static void ms_shot(const char *name)              /* the screen as it stands, OUT/name.ppm */
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s.ppm", ms_dir, name);
    if (!(f = fopen(path, "wb"))) {
        fprintf(stderr, "make_screens: cannot write %s\n", path);
        exit(1);
    }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        fputc((c >> 11) * 255 / 31, f);
        fputc(((c >> 5) & 63u) * 255 / 63, f);
        fputc((c & 31u) * 255 / 31, f);
    }
    fclose(f);
}
static uint32_t ms_song(const char *t)            /* the built-in song of that title (none: the first) */
{
    uint32_t i;
    for (i = 0; i < MM_NSONGS; i++)
        if (!strncmp(MM_SONGS[i].title, t, strlen(t)))
            return i;
    fprintf(stderr, "make_screens: no song \"%s\" built in: the first instead\n", t);
    return 0;
}
/* the keys your notes are played on now: each held from its note's start to just before its end */
static uint32_t ms_held(void)
{
    const mm_song_t *s = pl_song();
    uint32_t i, m = 0, pos = pl.pos;
    for (i = player_find(s, pos, CHART_FS * 4u); i < s->nnote && s->note[i].time <= pos; i++) {
        const chart_note_t *c = &s->note[i];
        uint32_t len = c->len ? c->len : CHART_FS / 10u, k;
        if (!pl_judge_mine(c) || c->time + len - CHART_FS / 50u <= pos)
            continue;
        k = mm_led_key(c);
        if (k < CHART_NKEYS)
            m |= 1u << k;
    }
    return m;
}
static void ms_run(uint32_t samples)               /* the song on, the keys as its notes are */
{
    uint32_t b;
    for (b = 0; b < samples / CTL; b++) {
        fm1_in.notes = ms_held();
        mix_block(ms_out, CTL);
    }
}
static void ms_start(uint32_t song, uint32_t mode, uint32_t style, uint32_t range, uint32_t you)
{
    songs_open();
    sg.free = 0;
    sg_pick(song);
    pl.mode = (uint8_t)mode;
    memset(pl.style, 0, sizeof pl.style);
    pl.style[mode] = (uint8_t)style;
    pl.range = (uint8_t)range;
    pl.you = (uint8_t)you;
    songs_play(song);
    songs_close();
    go_home();
    gc_on = 0;                                     /* (the cards hidden: the notes' panel tall) */
}
static void ms_stop(void)
{
    fm1_in.notes = 0;
    player_stop();
    mix_block(ms_out, CTL);
    frame();
}
static void ms_view(const char *name, const char *title, uint32_t mode, uint32_t style, uint32_t at_s)
{
    ms_start(ms_song(title), mode, style, 0, mode != MM_DRUMS);
    ms_run(at_s * CHART_FS);
    ui.force = 1;
    frame();
    frame();
    ms_shot(name);
    ms_stop();
}

int main(int argc, char **argv)
{
    uint32_t f;
    char n[32];
    if (argc > 1)
        ms_dir = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    ui_power_on();
    palette_set(0);
    ms_view("view_keys27", "FM Keys", MM_KEYS, KS_27, 14);
    ms_view("view_keys49", "FM Keys", MM_KEYS, KS_49, 14);
    ms_view("view_drums", "FM Beats", MM_DRUMS, DS_FULL, 12);
    ms_view("view_guitar_strings", "FM Guitar", MM_GUITAR, MM_VIEW_STRINGS, 14);
    ms_view("view_guitar_chromatic", "FM Guitar", MM_GUITAR, MM_VIEW_CHROMATIC, 24);
    ms_view("view_bass_strings", "FM Bass", MM_BASS, MM_VIEW_STRINGS, 14);
    {   /* SONGS with FM Keys selected; SOUNDS (ENV) */
        songs_open();
        sg.free = 0;
        sg_pick(ms_song("FM Keys"));
        pl.mode = MM_KEYS;
        memset(pl.style, 0, sizeof pl.style);
        pl.range = 0;
        pl.you = 1;
        ui.force = 1;
        frame();
        ms_shot("view_songs");
        songs_close();
        snd_open();
        ui.force = 1;
        frame();
        ms_shot("view_sounds");
        mm_sound_step(MM_KEYS, 8);                   /* KEYS' knob turned quickly: its list pops up (JAZZ PERC) */
        snd.pop = 1u + MM_KEYS;
        snd.pop_t = fm1_ms;
        frame();
        ms_shot("view_sounds_list");
        mm_sound_step(MM_KEYS, -8);
        snd_close();
        go_home();
    }
    ms_start(ms_song("FM Keys"), MM_KEYS, KS_27, 0, 1);   /* the GIF: 8 s from its 4th second, 15 frames a second */
    ms_run(4u * CHART_FS);
    for (f = 0; f < 120u; f++) {
        ms_run(CHART_FS / 15u);
        ui.force = f == 0u;
        frame();
        snprintf(n, sizeof n, "gif_keys_%03u", (unsigned)f);
        ms_shot(n);
    }
    ms_stop();
    printf("make_screens: %s: 8 views, 120 frames\n", ms_dir);
    return 0;
}
