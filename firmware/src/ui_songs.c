/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* SONGS: the songs built into the firmware (player.c, midimania_songs.h): pick one, your part, its tempo; PLAY.
 *
 * Opened by the SEQ button (Midi-Mania without the sequencer; Felucca's build: ARP), over the whole screen as MENU is:
 *   SELECT or PRESETS  the song;
 *   ALGORITHM          your part (mm_part_turn): those the song has that you can play (player.c MM_PLAYABLE: KEYS DRUMS
 *                      GUITAR BASS; HOME shows its notes as its STYLE lays them out, ui_graph.c graph_notes);
 *   KNOB 1             CPU (the device plays your part) or PLAY (your notes silent, the keys play them: pl.you);
 *   KNOB 2             the tempo, 1 BPM a detent, from half the song's to twice it (its own shown beside);
 *   OCT-               the tempo back to the song's;
 *   KNOB 3             STYLE, your part's (player.c mm_style): KEYS 49 KEYS, 27 KEYS, 1 OCTAVE; DRUMS FULL, QUARTER,
 *                      HALF; GUITAR BASS STRINGS, CHROMATIC;
 *   KNOB 4             AUTO, which of your part's notes are yours (player.c mm_mine; the rest the device plays): KEYS
 *                      FULL, CURRENT RANGE, 1 OCTAVE; DRUMS ALL 8, 5 PADS, 4 PADS, 2 PADS; GUITAR BASS on STRINGS the
 *                      lanes (6 4 2, BASS 4 2), on CHROMATIC 1 OCTAVE;
 *   PRESETS            (Midi-Mania) SOUND: your part's, of its own (engines.c MM_SOUNDS); SELECT alone the song;
 *   PLAY               starts it and goes HOME (where PLAY stops it, ui_input.c); on the song playing: stops it;
 *   SEQ                again: GAME, the game's settings; HOME or ARP: back to the game.
 * The tempo turns while a song plays too. The keys still play the selected track meanwhile.
 *
 * Included by ui_input.c (before page_tap); ui_draw.c ui_draw_page calls songs_draw while songs_on(). */
#define SG_ROWS 3u                       /* songs listed at once */
#define SG_ROW_Y 28
#define SG_ROW_H 24
#define SG_OPT_Y (SG_ROW_Y + (int32_t)SG_ROWS * SG_ROW_H + 4)   /* PART, MODE, TEMPO, STYLE, AUTO, SOUND */
#define SG_OPT_H 18
#define SG_NOPT 6                        /* PART MODE TEMPO STYLE AUTO SOUND */
#define SG_FOOT_Y (SG_OPT_Y + SG_NOPT * SG_OPT_H + 4)

static struct {
    uint8_t on;
    uint8_t free;                        /* FREE PLAY selected (the list's first row; at power-on) */
    uint8_t sel;                         /* .. else the song selected (sg_song); kept while FREE PLAY is */
    uint16_t bpm_x10;                    /* the tempo for the selected song (its own when it is picked) */
    uint32_t list_sig, opt_sig, prog_sig;   /* drawn-state caches (0: draw again) */
} sg = {.free = 1};
/* the list's rows: 0 FREE PLAY, then the songs; the cursor's row */
#define SG_NROWS (mm_nsongs() + 1u)

/* a colour a part, as Studio's (platforms/web/index.html --p0..--p3): KEYS amber, DRUMS pink, GUITAR green, BASS
 * purple; darker on a light palette. In colour on GREY too, as the drum lanes (gfx.c cv_rgb): mm_part_text.
 * mm_part_ink: text on a fill of it */
static uint16_t mm_part_col(uint32_t m)
{
    static const uint16_t DARK_BG[4] = {0xFDC9u, 0xFA71u, 0x5F11u, 0xAC5Fu};    /* #FFB84F #FF4F8B #5FE08A #A88BFF */
    static const uint16_t LIGHT_BG[4] = {0xB340u, 0xC8CBu, 0x1C47u, 0x69FAu};   /* #B06A00 #C8185A #1A8A3E #6A3FD0 */
    return (ux.light ? LIGHT_BG : DARK_BG)[m & 3u];
}
static uint16_t mm_part_ink(void) { return ux.light ? 0xFFFFu : 0x0841u; }
static void mm_part_text(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *t, uint16_t fg, uint16_t bg,
                         uint32_t align)
{
    cv_rgb = 1;
    draw_text_line(x, y, w, f, t, fg, bg, align);
    cv_rgb = 0;
}
static uint32_t sg_row(void) { return sg.free ? 0u : sg.sel + 1u; }
/* FREE PLAY (player.c player_free): your part on its sound, no song; HOME draws the keys you press, scrolling away
 * from the hit line (ui_graph.c graph_free). Your parts: any you can play */
#include "game_save.c"                          /* your settings and high scores, kept (settings_persist.c) */

static int mm_free_now(void) { return player_busy() ? player_is_free() : sg.free; }

static int songs_on(void) { return sg.on; }

static const mm_song_t *sg_song(void) { return mm_song(sg.sel % (mm_nsongs() ? mm_nsongs() : 1u)); }

/* the song's parts in mode order; pl.mode kept when the song has it, else its first */
static void sg_mode_fix(void)                    /* (only the parts you can play: player.c MM_PLAYABLE) */
{
    uint32_t parts = sg.free ? MM_PLAYABLE : sg_song()->parts & MM_PLAYABLE, m;
    if ((parts >> pl.mode) & 1u)
        return;
    for (m = 0; m < MM_NMODES; m++)
        if (((parts ? parts : MM_PLAYABLE) >> m) & 1u) {   /* (none in the song: the first you can play) */
            pl.mode = (uint8_t)m;
            return;
        }
}

/* your part, m parts on (either way, wrapping): those the song has that you can play (MM_PLAYABLE). Midi-Mania: on
 * ALGORITHM, from anywhere, as it picks the track; the keys play it (song.sel) */
static void mm_part_turn(int32_t m)
{
    const mm_song_t *s = player_busy() ? pl_song() : sg_song();
    while (m) {
        uint32_t parts = mm_free_now() ? MM_PLAYABLE : s->parts & MM_PLAYABLE, k = pl.mode, step;
        for (step = 0; step < MM_NMODES; step++) {
            k = (k + (m > 0 ? 1u : MM_NMODES - 1u)) % MM_NMODES;
            if ((parts >> k) & 1u)
                break;
        }
        if (k != pl.mode)
            pl.range = 0;                             /* (another part: its RANGE from ALL) */
        pl.mode = (uint8_t)k;
        m += m > 0 ? -1 : 1;
    }
    track_select(pl.mode % NTRK);
}

static void sg_pick(uint32_t i)                   /* song i selected: its own tempo, a part it has */
{
    sg.sel = (uint8_t)i;
    sg.bpm_x10 = sg_song()->bpm_x10;
    sg_mode_fix();
}

/* the stored songs changed (player.c mm_song_scan: a song uploaded or deleted over USB, editor_songs.c; the browser
 * build's page): the selection kept if it is still a song, else the first; drawn again */
static uint32_t sg_gen;
static void sg_check(void)
{
    if (sg_gen == mm_songs_gen)
        return;
    sg_gen = mm_songs_gen;
    if (sg.sel >= mm_nsongs())
        sg.sel = 0;
    if (!mm_nsongs())
        sg.free = 1;
    if (mm_nsongs())
        sg_pick(sg.sel);
    sg.list_sig = sg.opt_sig = sg.prog_sig = 0;
}

static void songs_open(void)
{
    sg_check();
    sg.on = 1;
    sg.list_sig = sg.opt_sig = sg.prog_sig = 0;
    if (player_busy()) {                          /* (the song playing selected, its tempo as set) */
        sg.free = (uint8_t)player_is_free();
        if (!sg.free)
            sg.sel = pl.song;
    }
    if (!sg.bpm_x10)
        sg_pick(sg.sel);
    sg_mode_fix();
    ui.force = 1;
}

static void songs_close(void)
{
    sg.on = 0;
    ui.force = 1;                                 /* the page under it drawn again */
}

/* "m:ss" of a time in samples */
static void sg_time(char *b, uint32_t samples)
{
    uint32_t s = samples / CHART_FS, m = s / 60u;
    s %= 60u;
    if (m > 99u)
        m = 99u;
    if (m >= 10u)
        *b++ = (char)('0' + m / 10u);
    *b++ = (char)('0' + m % 10u);
    *b++ = ':';
    *b++ = (char)('0' + s / 10u);
    *b++ = (char)('0' + s % 10u);
    *b = 0;
}

static char *sg_uint(char *b, uint32_t v)         /* v in decimal at b, its end returned */
{
    char t[10];
    uint32_t n = 0;
    do t[n++] = (char)('0' + v % 10u); while ((v /= 10u) != 0u);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

static char *sg_bpm(char *b, uint32_t bpm_x10)    /* "114" or "114.5" */
{
    b = sg_uint(b, bpm_x10 / 10u);
    if (bpm_x10 % 10u) {
        *b++ = '.';
        b = sg_uint(b, bpm_x10 % 10u);
    }
    return b;
}

/* its parts on their instruments, its tempo, then ask the ISR to play it. Felucca's build: the song's sounds load onto its
 * parts (as PRESETS would). Midi-Mania's: a part's instrument is fixed (engines.c MM_PART_ENGINE) and keeps the preset
 * picked for it (SAVE > PRESETS); the keys play your part */
static void songs_play(uint32_t i)
{
    const mm_song_t *s;
    uint32_t p;
    sg_check();
    if (i >= mm_nsongs())                            /* (a stored song deleted meanwhile) */
        return;
    s = mm_song(i);
    for (p = 0; p < CHART_NPART; p++) {
        track_t *t = &trk[p];
        if (!((s->parts >> p) & 1u))
            continue;
        t->p[P_MUTE] = 0;
    }
    song.sel = (uint8_t)(pl.mode % NPART);           /* (the keys: your part) */
    player_tempo(s, sg.bpm_x10);
    player_start(i);
}

/* FREE PLAY: the keys play your part (song.sel), the player's clock runs (HOME scrolls your presses: ui_graph.c) */
static void mm_free_start(void)
{
    sg_mode_fix();
    song.sel = (uint8_t)(pl.mode % NPART);
    player_free();
}

static void game_close(void);
static void mm_nav(uint32_t b);

/* SOUND: part's sound, the dir-th next of its own (engines.c MM_SOUNDS; wraps), loaded as PRESETS loads one */
static void mm_sound_step(uint32_t part, int32_t dir)
{
    track_t *t = &trk[part % NPART];
    uint32_t n = mm_sound_n(part), pos = mm_sound_pos(part, t->eng_req % NENGINES, t->preset), k, e;
    if (!n || !dir)
        return;
    pos = pos >= n ? 0u : (uint32_t)(((int32_t)pos + dir % (int32_t)n + (int32_t)n) % (int32_t)n);
    e = mm_sound_at(part, pos, &k);
    if (t->eng_req != e)
        set_engine_of(t, e);
    apply_preset_to(t, k);
}
static const char *mm_sound_name(uint32_t part)    /* the sound part plays: its preset's name */
{
    const track_t *t = &trk[part % NPART];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    return e->npresets ? e->presets[t->preset % e->npresets].name : e->name;
}

static void songs_input(uint32_t pressed, uint32_t home, uint32_t oct)
{
    int32_t d = panel_enc(EN_SELECT), m = panel_enc(EN_ALGO), t = panel_enc(EN_K2), so = panel_enc(EN_PRESET);
    sg_check();
    {   /* Midi-Mania: SONGS is on SEQ; HOME, ARP or SEQ the game, EDIT the MIXER, SCL the METRONOME, FX GAME, SAVE
         * PRESETS (mm_nav) */
        static const uint8_t NAV[7] = {B_ARP, B_SEQ, B_EDIT, B_SCL, B_FX, B_SAVE, B_ENV};
        uint32_t k;
        if (home == BT_TAP) {
            mm_nav(B_HOME);
            enc_drop();
            return;
        }
        for (k = 0; k < 7u; k++)
            if ((pressed >> panel.btn[NAV[k]]) & 1u) {
                if (NAV[k] == B_SAVE)
                    ui.save_t0 |= 2u;                /* (no UNDO on its release) */
                mm_nav(NAV[k] == B_SEQ ? B_FX : NAV[k]);     /* (SEQ again: GAME, the game's settings) */
                enc_drop();
                return;
            }
    }
    if (d) {                                         /* SELECT: the rows, FREE PLAY first */
        int32_t n = (int32_t)sg_row() + d;
        n = n < 0 ? 0 : n >= (int32_t)SG_NROWS ? (int32_t)SG_NROWS - 1 : n;
        sg.free = (uint8_t)(n == 0);
        if (n)
            sg_pick((uint32_t)n - 1u);
        else
            sg_mode_fix();
    }
    if (m)                                           /* KNOB 1 (Midi-Mania: ALGORITHM): the next part the song has */
        mm_part_turn(m);
    {
        int32_t y = panel_enc(EN_K1), st = panel_enc(EN_K3), r = panel_enc(EN_K4);
        if (y)                                        /* KNOB 1: PLAY clockwise (YOU), CPU the other way (AUTO) */
            pl.you = (uint8_t)(y > 0);
        if (st)                                       /* KNOB 3: STYLE, your part's (its AUTO from the first) */
            mm_style_turn(st);
        if (r) {                                      /* KNOB 4: AUTO, your part's */
            int32_t v = (int32_t)mm_range() + r;
            pl.range = (uint8_t)(v < 0 ? 0 : v >= (int32_t)mm_ranges() ? (int32_t)mm_ranges() - 1 : v);
        }
    }
    if (so)                                           /* PRESETS: your part's SOUND */
        mm_sound_step(pl.mode, so);
    if (mm_nsongs() && !sg.free && (t || (oct & 1u))) {   /* KNOB 2: 1 BPM a detent; OCT-: the song's own */
        int32_t own = sg_song()->bpm_x10, b = (oct & 1u) ? own : (int32_t)sg.bpm_x10 + t * 10;
        int32_t lo = own / 2, hi = own * 2;
        sg.bpm_x10 = (uint16_t)(b < lo ? lo : b > hi ? hi : b);
        if (player_busy() && !player_is_free() && pl.song == sg.sel)
            player_tempo(sg_song(), sg.bpm_x10);    /* (live) */
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        if (player_busy() && (sg.free ? player_is_free() : !player_is_free() && pl.song == sg.sel))
            player_stop();
        else {
            if (sg.free)
                mm_free_start();
            else
                songs_play(sg.sel);
            songs_close();                           /* to the game (HOME's view): its panel shows your part's notes */
            go_home();                               /* (ui_graph.c) */
        }
    }
    enc_drop();                                      /* (KNOB 3 / 4 and ALGORITHM: nothing here) */
}

static void sg_list(void)
{
    char b[12], *e;
    uint32_t i, cur = sg_row(), top = cur >= SG_ROWS ? cur - SG_ROWS + 1u : 0u, playing = player_busy();
    lcd_fill(0, 0, 240, H_HEAD, T_SURF);
    draw_text_line(8, (H_HEAD - AF_M.h) / 2, 120, &AF_M, "SONGS", T_TEXT, T_SURF, 0);
    if (sg.free)
        str_cpy(b, "FREE", sizeof b);
    else {
        e = sg_uint(b, sg.sel + 1u);
        *e++ = '/';
        sg_uint(e, mm_nsongs());
    }
    draw_text_line(140, (H_HEAD - AF_S.h) / 2, 92, &AF_S, b, T_MID, T_SURF, 2);
    for (i = 0; i < SG_ROWS; i++) {
        uint32_t k = top + i;
        int32_t y = SG_ROW_Y + (int32_t)i * SG_ROW_H;
        if (k >= SG_NROWS) {
            lcd_fill(0, (uint32_t)y, 240, SG_ROW_H, T_BG);
            if (!mm_nsongs() && k == 1u)
                draw_text_line(8, (uint32_t)y + 3u, 224, &AF_M, "NO SONGS (ADD .MID TO MIDI/)", T_MID, T_BG, 0);
            continue;
        }
        {   /* row 0: FREE PLAY; row k: song k - 1 */
            int sel = k == cur, now = playing && (k ? !player_is_free() && k - 1u == pl.song : player_is_free());
            uint16_t bg = sel ? T_SEL : T_BG, ink = sel ? T_INK : now ? T_ACCENT : T_TEXT;
            lcd_fill(0, (uint32_t)y, 240, SG_ROW_H, bg);
            if (k)
                draw_text_line(8, (uint32_t)y + (SG_ROW_H - AF_M.h) / 2u, 172, &AF_M, mm_song(k - 1u)->title, ink, bg, 0);
            else                                      /* (FREE PLAY: bold and italic, not a song: gfx.c) */
                draw_text_line_bi(8, (uint32_t)y + (SG_ROW_H - AF_M.h) / 2u, 172, &AF_M, "FREE PLAY", 0, ink, bg);
            if (k)
                sg_time(b, mm_song(k - 1u)->length);
            else
                str_cpy(b, "--", sizeof b);
            draw_text_line(182, (uint32_t)y + (SG_ROW_H - AF_S.h) / 2u, 50, &AF_S, b, sel ? T_INK : T_MID, bg, 2);
        }
    }
}

/* YOUR PART  < DRUMS >   and   TEMPO  120  (SONG 114) */
static void sg_options(void)
{
    char b[32], *e;
    const mm_song_t *s = sg_song();
    uint32_t m, x = 76;                              /* 34 + 34 + 44 + 34 and 2 px gaps: 76 .. 226 */
    lcd_fill(0, SG_OPT_Y - 4, 240, SG_NOPT * SG_OPT_H + 4, T_BG);
    lcd_fill(0, SG_OPT_Y - 3, 240, 1, T_LINE);
    lcd_fill(0, SG_OPT_Y + SG_OPT_H, 3, (SG_NOPT - 1) * SG_OPT_H - 2, mm_part_col(pl.mode));   /* your part's options */
    draw_text_line(8, SG_OPT_Y + 2, 66, &AF_S, "ALG PART", T_MID, T_BG, 0);
    for (m = 0; m < MM_NMODES; m++) {               /* the four (those you can play), the song's lit, yours selected */
        int has = sg.free || !mm_nsongs() || ((s->parts >> m) & 1u), mine = m == pl.mode;
        uint16_t bg = mine ? mm_part_col(m) : T_BG, ink = mine ? mm_part_ink() : has ? mm_part_col(m) : T_DIM;
        uint32_t w = MM_PLAYABLE == 0xFu ? (m == MM_GUITAR ? 44u : 34u) : 74u;
        if (!((MM_PLAYABLE >> m) & 1u))
            continue;
        lcd_fill(x, SG_OPT_Y, w, SG_OPT_H - 2, bg);
        mm_part_text(x, SG_OPT_Y + (SG_OPT_H - 2 - AF_S.h) / 2u, w, &AF_S,
                       w > 44u ? MM_MODE_NAME[m] : m == MM_GUITAR ? "GTR" : m == MM_DRUMS ? "DRUM" : MM_MODE_NAME[m],
                       ink, bg, 1);
        x += w + 2u;
    }
    {   /* K1 MODE: CPU PLAY, the one in use selected (FREE PLAY: yours, no choice) */
        static const char *const MODES[2] = {"CPU", "PLAY"};
        uint32_t y = SG_OPT_Y + SG_OPT_H, k;
        int fp = sg.free || !mm_nsongs();
        draw_text_line(8, y + 2u, 66, &AF_S, "K1 MODE", fp ? T_DIM : T_MID, T_BG, 0);
        for (k = 0, x = 76; k < 2u; k++, x += 74u) {
            int mine = !fp && k == pl.you;
            uint16_t bg = mine ? mm_part_col(pl.mode) : T_BG;
            lcd_fill(x, y, 72, SG_OPT_H - 2, bg);
            mm_part_text(x, y + (SG_OPT_H - 2 - AF_S.h) / 2u, 72, &AF_S, MODES[k], mine ? mm_part_ink() : fp ? T_DIM : T_TEXT,
                           bg, 1);
        }
    }
    {   /* K2 TEMPO, the song's own beside (FREE PLAY: none) */
        uint32_t y = SG_OPT_Y + 2 * SG_OPT_H;
        draw_text_line(8, y + 2u, 66, &AF_S, "K2 TEMPO", sg.free || !mm_nsongs() ? T_DIM : T_MID, T_BG, 0);
        if (sg.free || !mm_nsongs())
            draw_text_line(76, y + 2u, 156, &AF_S, "FREE PLAY: YOUR OWN", T_DIM, T_BG, 0);
        else {
            e = sg_bpm(b, sg.bpm_x10);
            str_cpy(e, " BPM", sizeof b - (uint32_t)(e - b));
            draw_text_line(76, y, 86, &AF_M, b, sg.bpm_x10 == s->bpm_x10 ? T_TEXT : T_THEME, T_BG, 0);
            str_cpy(b, "SONG ", sizeof b);
            sg_bpm(b + 5, s->bpm_x10);
            draw_text_line(164, y + 2u, 68, &AF_S, b, T_MID, T_BG, 2);
        }
    }
    {   /* K3 STYLE and K4 AUTO: your part's (player.c mm_style, mm_range) */
        uint32_t y = SG_OPT_Y + 3 * SG_OPT_H;
        draw_text_line(8, y + 2u, 66, &AF_S, "K3 STYLE", T_MID, T_BG, 0);
        mm_part_text(76, y, 156, &AF_M, mm_style_name(pl.mode, mm_style()), mm_part_col(pl.mode), T_BG, 0);
        y += SG_OPT_H;
        draw_text_line(8, y + 2u, 66, &AF_S, "K4 AUTO", T_MID, T_BG, 0);
        mm_part_text(76, y, 156, &AF_M, mm_range_name(mm_range()), mm_part_col(pl.mode), T_BG, 0);
    }
    {   /* PRESETS SOUND: your part's */
        uint32_t y = SG_OPT_Y + 5 * SG_OPT_H;
        draw_text_line(8, y + 2u, 66, &AF_S, "PRE SOUND", T_MID, T_BG, 0);
        mm_part_text(76, y, 156, &AF_M, mm_sound_name(pl.mode), mm_part_col(pl.mode), T_BG, 0);
    }
}

static void sg_progress(void)
{
    char b[40], *e;
    uint32_t playing = player_busy(), len = mm_nsongs() ? pl_song()->length : 0u, pos = pl.pos, w = 0;
    lcd_fill(0, SG_FOOT_Y, 240, 240 - SG_FOOT_Y, T_BG);
    lcd_fill(0, SG_FOOT_Y, 240, 1, T_LINE);
    if (playing && player_is_free()) {               /* FREE PLAY: its time, no end */
        str_cpy(b, "FREE PLAY ", sizeof b);
        sg_time(b + 10, pos);
    } else if (playing) {
        str_cpy(b, "PLAYING ", sizeof b);
        sg_time(b + 8, pos);
        for (e = b; *e; e++)
            ;
        *e++ = ' '; *e++ = '/'; *e++ = ' ';
        sg_time(e, len);
        w = len ? (uint32_t)((uint64_t)(pos < len ? pos : len) * 224u / len) : 0u;
    } else {
        uint32_t best, acc, fl;
        str_cpy(b, sg.free ? "PLAY: START (THE KEYS: YOUR PART)" : "PLAY: START   OCT-: SONG TEMPO", sizeof b);
        if (!sg.free && mm_nsongs() && hs_best(sg_song(), &best, &acc, &fl)) {   /* your best (game_save.c) */
            str_cpy(b, "PLAY: START  BEST ", sizeof b);
            e = sg_uint(b + str_len(b), best);
            *e++ = ' ';
            e = sg_uint(e, acc);
            str_cpy(e, fl & HS_FULL_COMBO ? "% FC" : "%", 5);
        }
    }
    draw_text_line(8, SG_FOOT_Y + 2, 224, &AF_S, b, playing ? T_TEXT : T_MID, T_BG, 0);
    lcd_fill(8, SG_FOOT_Y + 19, 224, 3, T_LINE);
    if (w)
        lcd_fill(8, SG_FOOT_Y + 19, w, 3, T_THEME);
}

static void songs_draw(void)
{
    sg_check();
    uint32_t playing = player_busy(), pal = palette_cur() * 7919u;
    uint32_t list = 1u + sg_row() * 131u + (playing ? 7u + pl.song * 977u + (uint32_t)player_is_free() * 31u : 0u) + pal;
    uint32_t opt = 1u + sg_row() * 31u + pl.mode * 7u + sg.bpm_x10 * 1009u + pl.you * 104729u + mm_range() * 7907u + pal +
                   mm_style() * 1299709u +
                   (trk[pl.mode % NPART].eng_req * 257u + trk[pl.mode % NPART].preset) * 6553u;   /* (SOUND) */
    uint32_t prog = 1u + (playing ? 3u + pl.pos / (CHART_FS / 4u) * 17u + pl.song * 131u :   /* (stopped: the best */
                          sg.sel * 7u + pl.mode * 113u + mm_range() * 1031u + mm_style() * 4099u + gs.gen * 65537u +
                          sg.free * 3u) + pal;   /* shown) */
    if (ui.force || list != sg.list_sig) {
        sg.list_sig = list;
        sg_list();
    }
    if (ui.force || opt != sg.opt_sig) {
        sg.opt_sig = opt;
        sg_options();
    }
    if (ui.force || prog != sg.prog_sig) {
        sg.prog_sig = prog;
        sg_progress();
    }
}

/* ---------------------------------------------------------------------------------------------------------- GAME ---
 * The game's settings, on the FX button (ui_input.c page_tap; Midi-Mania has no effects pages), over
 * the whole screen as SONGS is. A knob a row:
 *   KNOB 1  SCROLL      DOWN: HOME's notes fall to a line at the bottom; UP: they rise to one at the top (pl.up)
 *   KNOB 2  NOTE SPEED  1 (slow: 4 s from the top of the panel to the line) .. 8 (1 s) .. 16 (fastest: 0.4 s); 4: 2 s
 *                       (PL_SPEED)
 *   KNOB 3  OFFSET      -100 .. +100 ms, 2 ms a detent: the player's timing calibration, for the judge (pl.offset);
 *                       OCT- puts it back to 0; OCT+ CALIBRATE sets it from your taps with a click (game_cal_poll)
 *   HOME or FX: back. Kept after power-off, with your part, PLAY and RANGE (game_save.c) */
#define GM_ROW_Y 40
#define GM_ROW_H 34

static struct {
    uint8_t on;
    uint32_t sig;
} gm;

static int game_on(void) { return gm.on; }

/* HOME while a song plays (its notes falling in the panel, ui_graph.c graph_notes): the four cards are the game's,
 * not the sound's: KNOB 1 NOTE SPEED, KNOB 2 OFFSET, KNOB 3 TEMPO (live, as SONGS' KNOB 2), KNOB 4 SCROLL. The same
 * values as GAME's (PL_SPEED, pl.offset, pl.rate, pl.up). Stopped: the sound's cards again */
static int game_cards(void) { return 1; }   /* (Midi-Mania without the
                                                  * sequencer: HOME's view is the game's, stopped too: its cards) */

static const mm_song_t *gc_song(void) { return player_busy() ? pl_song() : sg_song(); }   /* playing, else selected */
static uint32_t gc_bpm(void)                     /* its tempo now, BPM x 10 */
{
    if (!player_busy())
        return sg.bpm_x10 ? sg.bpm_x10 : sg_song()->bpm_x10;
    return (uint32_t)(((uint64_t)pl.rate * pl_song()->bpm_x10 + PL_RATE_ONE / 2u) / PL_RATE_ONE);
}

/* Midi-Mania without the sequencer: the cards hidden, the notes' panel over their place (ui_graph.c graph_tall), until
 * a knob turns: shown then, GC_SHOW_MS after the last turn */
#define GC_SHOW_MS 3000u
static uint32_t gc_t;                            /* fm1_ms of the last turn on the game */
static uint8_t gc_on;                            /* .. (0: none yet) */
static int game_cards_shown(void)                /* (stopped: shown, the scope under them) */
{
    return !player_busy() || (gc_on && fm1_ms - gc_t < GC_SHOW_MS);
}

static void game_card_knob(uint32_t k, int32_t s)  /* ui_input.c: KNOB k + 1 turned s detents on HOME */
{
    gc_t = fm1_ms;                                /* (the cards show) */
    gc_on = 1;
    if (k == 0u) {
        int32_t v = (int32_t)PL_SPEED + s;
        PL_SPEED = (uint8_t)(v < (int32_t)PL_SPEED_MIN ? PL_SPEED_MIN : v > (int32_t)PL_SPEED_MAX ? PL_SPEED_MAX : v);
    } else if (k == 1u) {
        int32_t v = (int32_t)pl.offset + s;
        pl.offset = (int8_t)(v < -50 ? -50 : v > 50 ? 50 : v);
    } else if (k == 2u) {
        int32_t own = gc_song()->bpm_x10, v = (int32_t)gc_bpm() + s * 10;
        v = v < own / 2 ? own / 2 : v > own * 2 ? own * 2 : v;
        if (player_busy())
            player_tempo(pl_song(), (uint32_t)v);
        if (!player_busy() || sg.sel == pl.song)
            sg.bpm_x10 = (uint16_t)v;                 /* (SONGS shows it too; stopped: the selected song's) */
    } else {
        pl.up = (uint8_t)(s > 0);
    }
}

/* (ui_draw.c's per-card state, set before each draw_column: no automation mark, the value's own face) */
#define GC_CARD() (card_mot_next = 0u, card_span.fn = 0)
static void game_cards_draw(void)                /* ui_draw.c draw_columns, HOME while a song plays */
{
    char v[12];
    int32_t own = gc_song()->bpm_x10, bpm = (int32_t)gc_bpm(), off = pl.offset * 2;
    GC_CARD();
    sg_uint(v, PL_SPEED);
    draw_column(0, "SPEED", v, "", VAL(0u), (int32_t)(PL_SPEED - PL_SPEED_MIN) * 1000 / (int32_t)(PL_SPEED_MAX - PL_SPEED_MIN),
                ICON_RATE);
    GC_CARD();
    v[0] = off < 0 ? '-' : '+';
    sg_uint(v + 1, (uint32_t)(off < 0 ? -off : off));
    draw_column(1, "OFFSET", v, "ms", VAL(1u), (off + 100) * 1000 / 200, ICON_TIME);
    GC_CARD();
    sg_bpm(v, (uint32_t)bpm);
    {   /* playing: the play icon, and the value flashes on each beat (white; the bar's first, the accent) */
        uint32_t bar, since = player_beat(&bar);
        uint16_t vc = since < CHART_FS / 8u ? (bar ? T_ACCENT : T_TEXT) : VAL(2u);
        draw_column(2, "TEMPO", v, "", vc, (bpm - own / 2) * 1000 / (own * 2 - own / 2),
                    player_busy() ? ICON_X_PLAY : ICON_TEMPO);
    }
    GC_CARD();
    draw_column(3, "SCROLL", pl.up ? "UP" : "DOWN", "", VAL(3u), -1, pl.up ? ICON_X_OCT_UP : ICON_X_OCT_DN);
}
/* OCT+ CALIBRATE (player.c pl_cal): a click at 100 BPM, you tap any key with it; after 12 taps OFFSET is set to cancel
 * your average lead or lag (judge.c adds it to every press). gm.cal: 0 nothing to show, 1 running, 2 done (gm.cal_ms
 * your average, +: late), 3 too few taps. OCT+ again, HOME or FX: stopped */
static struct { uint8_t cal; int16_t cal_ms; } gmc;
static void game_cal_stop(void)
{
    if (pl_cal.on || gmc.cal == 1u) {
        pl_cal.on = 0;
        player_stop();
    }
}
static void game_cal_poll(void)                  /* game_input, game_draw: the calibration ended */
{
    if (gmc.cal != 1u || pl_cal.on)
        return;
    player_stop();
    if (pl_cal.done) {
        int32_t ms = player_cal_ms(), o = -(ms >= 0 ? (ms + 1) / 2 : (ms - 1) / 2);   /* (2 ms a step, rounded) */
        pl.offset = (int8_t)(o < -50 ? -50 : o > 50 ? 50 : o);
        gmc.cal_ms = (int16_t)ms;
        gmc.cal = 2;
    } else
        gmc.cal = 3;
}
static void game_open(void) { gm.on = 1; gm.sig = 0; gmc.cal = 0; ui.force = 1; }
static void game_close(void) { game_cal_stop(); gmc.cal = 0; gm.on = 0; ui.force = 1; }

static void game_input(uint32_t pressed, uint32_t home, uint32_t oct)
{
    int32_t a = panel_enc(EN_K1), b = panel_enc(EN_K2), c = panel_enc(EN_K3);
    if (home == BT_TAP || ((pressed >> panel.btn[B_FX]) & 1u)) {
        game_close();
        if (home == BT_TAP)
            mm_nav(B_HOME);                          /* (Midi-Mania: HOME is the game) */
        enc_drop();
        return;
    }
    {   /* ARP the game, SEQ the songs, EDIT the MIXER, SCL the METRONOME, SAVE PRESETS (mm_nav) */
        static const uint8_t NAV[6] = {B_ARP, B_SEQ, B_EDIT, B_SCL, B_SAVE, B_ENV};
        uint32_t k;
        int32_t al = panel_enc(EN_ALGO);
        if (al)
            mm_part_turn(al);                        /* ALGORITHM: your part, here too */
        for (k = 0; k < 6u; k++)
            if ((pressed >> panel.btn[NAV[k]]) & 1u) {
                if (NAV[k] == B_SAVE)
                    ui.save_t0 |= 2u;
                mm_nav(NAV[k]);
                enc_drop();
                return;
            }
    }
    if (a)
        pl.up = (uint8_t)(a > 0);
    if (b) {
        int32_t s = (int32_t)PL_SPEED + b;
        PL_SPEED = (uint8_t)(s < (int32_t)PL_SPEED_MIN ? PL_SPEED_MIN : s > (int32_t)PL_SPEED_MAX ? PL_SPEED_MAX : s);
    }
    if (oct & 2u) {                                  /* OCT+: CALIBRATE, or stop it (not while a song plays) */
        if (gmc.cal == 1u) {
            game_cal_stop();
            gmc.cal = 0;
        } else if (!player_busy() || player_is_free()) {
            player_cal_start();
            gmc.cal = 1;
        }
    }
    game_cal_poll();
    if (c || (oct & 1u)) {
        int32_t o = (oct & 1u) ? 0 : (int32_t)pl.offset + c;
        pl.offset = (int8_t)(o < -50 ? -50 : o > 50 ? 50 : o);
    }
    enc_drop();
}

static void gm_row(uint32_t r, const char *label, const char *value, const char *note)
{
    uint32_t y = GM_ROW_Y + r * GM_ROW_H;
    lcd_fill(0, y, 240, GM_ROW_H, T_BG);
    draw_text_line(8, y + 8u, 72, &AF_S, label, T_MID, T_BG, 0);
    if (value)
        draw_text_line(84, y + 6u, 70, &AF_M, value, T_TEXT, T_BG, 0);
    if (note)
        draw_text_line(156, y + 8u, 76, &AF_S, note, T_MID, T_BG, 2);
    lcd_fill(8, y + GM_ROW_H - 1u, 224, 1, T_LINE);
}

static void game_draw(void)
{
    char v[16], n[16], *e;
    uint32_t sig, m, x;
    game_cal_poll();
    sig = 1u + pl.up * 3u + PL_SPEED * 31u + (uint32_t)(pl.offset + 64) * 1009u + palette_cur() * 7919u +
          gmc.cal * 104729u + pl_cal.taps * 7u + (pl_cal.clicks & 1u) * 13u + (uint32_t)gmc.cal_ms * 31337u +
          pl.mode * 65537u;
    if (!ui.force && sig == gm.sig)
        return;
    gm.sig = sig;
    lcd_fill(0, 0, 240, H_HEAD, T_SURF);
    draw_text_line(8, (H_HEAD - AF_M.h) / 2, 120, &AF_M, "GAME", T_TEXT, T_SURF, 0);
    lcd_fill(0, H_HEAD, 240, GM_ROW_Y - H_HEAD, T_BG);
    gm_row(0, "K1 SCROLL", 0, 0);                    /* DOWN  UP: the one in use selected */
    for (m = 0, x = 84; m < 2u; m++) {
        int mine = m == pl.up;
        uint16_t bg = mine ? T_SEL : T_BG;
        lcd_fill(x, GM_ROW_Y + 4, 64, GM_ROW_H - 9, bg);
        draw_text_line(x, GM_ROW_Y + 4 + (GM_ROW_H - 9 - AF_S.h) / 2u, 64, &AF_S, m ? "UP" : "DOWN",
                       mine ? T_INK : T_TEXT, bg, 1);
        x += 68u;
    }
    {   /* NOTE SPEED: its number, and the time from the top of the panel to the line */
        uint32_t ms = pl_ahead() * 1000u / CHART_FS;
        sg_uint(v, PL_SPEED);
        e = sg_uint(n, ms / 1000u);
        *e++ = '.';
        e = sg_uint(e, ms % 1000u / 100u);
        if (ms % 100u >= 10u)                        /* (0.55, 0.45: the hundredths too) */
            e = sg_uint(e, ms % 100u / 10u);
        str_cpy(e, " S", sizeof n - (uint32_t)(e - n));
        gm_row(1, "K2 SPEED", v, n);
    }
    {   /* OFFSET, ms */
        int32_t ms = pl.offset * 2;
        v[0] = ms < 0 ? '-' : '+';
        e = sg_uint(v + 1, (uint32_t)(ms < 0 ? -ms : ms));
        str_cpy(e, " MS", sizeof v - (uint32_t)(e - v));
        gm_row(2, "K3 OFFSET", v, "FOR THE JUDGE");
    }
    {   /* OCT+ CALIBRATE: what it says, the taps while it runs (a light on each click), the result: one line */
        uint32_t y = GM_ROW_Y + 3u * GM_ROW_H;
        char t[40];
        lcd_fill(0, y, 240, GM_ROW_H, T_BG);
        if (gmc.cal == 1u) {
            str_cpy(t, pl_cal.clicks <= CAL_SKIP ? "LISTEN..  TAPS " : "TAP WITH IT  TAPS ", sizeof t);
            e = sg_uint(t + str_len(t), pl_cal.taps);
            str_cpy(e, "/12  OCT+: STOP", sizeof t - (uint32_t)(e - t));
            lcd_fill(226, y + 12u, 8, 8, pl_cal.clicks & 1u ? T_THEME : T_LINE);   /* (the beat) */
        } else if (gmc.cal == 2u) {
            str_cpy(t, "SET: YOU TAP ", sizeof t);
            e = sg_uint(t + str_len(t), (uint32_t)(gmc.cal_ms < 0 ? -gmc.cal_ms : gmc.cal_ms));
            str_cpy(e, gmc.cal_ms < 0 ? " MS EARLY" : " MS LATE", sizeof t - (uint32_t)(e - t));
        } else if (gmc.cal == 3u)
            str_cpy(t, "TOO FEW TAPS: OCT+ TO TRY AGAIN", sizeof t);
        else
            str_cpy(t, "OCT+: CALIBRATE (TAP WITH A CLICK)", sizeof t);
        draw_text_line(8, y + 8u, 214, &AF_S, t, gmc.cal == 1u || gmc.cal == 2u ? T_TEXT : T_MID, T_BG, 0);
        lcd_fill(8, y + GM_ROW_H - 1u, 224, 1, T_LINE);
    }
    lcd_fill(0, GM_ROW_Y + 4 * GM_ROW_H, 240, 240 - (GM_ROW_Y + 4 * GM_ROW_H), T_BG);
    draw_text_line(8, 212, 224, &AF_S, "OCT-: OFFSET 0   HOME OR FX: BACK", T_MID, T_BG, 0);
}

/* ----------------------------------------------------------------------------------------------- METRONOME ---
 * On the SCL button (Midi-Mania without the sequencer: the CLOCK's BPM and sync had nothing left to drive). The same
 * settings as MENU > CLICK, CLICK LEVEL, COUNT-IN (ui.c rp_get / rp_put: kept with the settings), a knob a row:
 *   KNOB 1  CLICK     OFF / ON: the playing song's beats click (player.c), the bar's first accented
 *   KNOB 2  LEVEL     LOW / MID / HIGH
 *   KNOB 3  COUNT-IN  OFF / 1 BAR / 2 BARS: clicks before the song, at its first beat's tempo, whatever CLICK is
 *   HOME the songs, SCL back. (MENU's REC, "while recording", shows here as ON: there is no recording) */
static struct {
    uint8_t on;
    uint32_t sig;
} mtr;

static int metro_on(void) { return mtr.on; }
static void metro_open(void) { mtr.on = 1; mtr.sig = 0; ui.force = 1; }
static void metro_close(void)                     /* (a change made here saved now: ui.c settings_later waits for a page */
{                                                 /* to be left, and this screen is over the page) */
    if (mtr.on && set_later.on) {
        set_later.on = 0;
        settings_save();
    }
    mtr.on = 0;
    ui.force = 1;
}

static void metro_input(uint32_t pressed, uint32_t home)
{
    int32_t a = panel_enc(EN_K1), b = panel_enc(EN_K2), c = panel_enc(EN_K3);
    uint32_t changed = 0;
    if (home == BT_TAP || ((pressed >> panel.btn[B_SCL]) & 1u)) {
        metro_close();
        if (home == BT_TAP)
            mm_nav(B_HOME);
        enc_drop();
        return;
    }
    {   /* ARP the game, SEQ the songs, EDIT the MIXER, FX GAME, SAVE PRESETS (mm_nav) */
        static const uint8_t NAV[6] = {B_ARP, B_SEQ, B_EDIT, B_FX, B_SAVE, B_ENV};
        uint32_t k;
        int32_t al = panel_enc(EN_ALGO);
        if (al)
            mm_part_turn(al);                        /* ALGORITHM: your part, here too */
        for (k = 0; k < 6u; k++)
            if ((pressed >> panel.btn[NAV[k]]) & 1u) {
                if (NAV[k] == B_SAVE)
                    ui.save_t0 |= 2u;
                mm_nav(NAV[k]);
                enc_drop();
                return;
            }
    }
    if (a) {
        rp_put(RP_CLICK, a > 0 ? CLICK_ON : CLICK_OFF);
        changed = 1;
    }
    if (b) {
        int32_t v = (int32_t)rp_get(RP_LEVEL) + b;
        rp_put(RP_LEVEL, (uint32_t)(v < 0 ? 0 : v > 2 ? 2 : v));
        changed = 1;
    }
    if (c) {
        int32_t v = (int32_t)rp_get(RP_COUNTIN) + c;
        rp_put(RP_COUNTIN, (uint32_t)(v < 0 ? 0 : v > 2 ? 2 : v));
        changed = 1;
    }
    if (changed)
        settings_later();                            /* (kept, as MENU's: settings_persist.c) */
    enc_drop();
}

static void mt_opts(uint32_t r, const char *label, const char *const *names, uint32_t n, uint32_t sel)
{
    uint32_t y = GM_ROW_Y + r * GM_ROW_H, x = 84, w = (148u - 4u * (n - 1u)) / n, k;
    lcd_fill(0, y, 240, GM_ROW_H, T_BG);
    draw_text_line(8, y + 8u, 72, &AF_S, label, T_MID, T_BG, 0);
    for (k = 0; k < n; k++, x += w + 4u) {
        uint16_t bg = k == sel ? T_SEL : T_BG;
        lcd_fill(x, y + 4u, w, GM_ROW_H - 9u, bg);
        draw_text_line(x, y + 4u + (GM_ROW_H - 9u - AF_S.h) / 2u, w, &AF_S, names[k], k == sel ? T_INK : T_TEXT, bg, 1);
    }
    lcd_fill(8, y + GM_ROW_H - 1u, 224, 1, T_LINE);
}

static void metro_draw(void)
{
    static const char *const ONOFF[2] = {"OFF", "ON"}, *const LVL[3] = {"LOW", "MID", "HIGH"},
                      *const CIN[3] = {"OFF", "1 BAR", "2 BARS"};
    uint32_t click = rp_get(RP_CLICK) != CLICK_OFF, lvl = rp_get(RP_LEVEL), cin = rp_get(RP_COUNTIN);
    uint32_t sig = 1u + click * 3u + lvl * 31u + cin * 1009u + palette_cur() * 7919u;
    if (!ui.force && sig == mtr.sig)
        return;
    mtr.sig = sig;
    lcd_fill(0, 0, 240, H_HEAD, T_SURF);
    draw_text_line(8, (H_HEAD - AF_M.h) / 2, 160, &AF_M, "METRONOME", T_TEXT, T_SURF, 0);
    lcd_fill(0, H_HEAD, 240, GM_ROW_Y - H_HEAD, T_BG);
    mt_opts(0, "K1 CLICK", ONOFF, 2, click);
    mt_opts(1, "K2 LEVEL", LVL, 3, lvl % 3u);
    mt_opts(2, "K3 COUNT-IN", CIN, 3, cin % 3u);
    lcd_fill(0, GM_ROW_Y + 3 * GM_ROW_H, 240, 240 - (GM_ROW_Y + 3 * GM_ROW_H), T_BG);
    draw_text_line(8, 212, 224, &AF_S, "HOME OR SCL: BACK", T_MID, T_BG, 0);
}

/* ------------------------------------------------------------------------------------------------- SOUNDS ---
 * On the ENV button (Midi-Mania leaves Felucca's envelope page out): each part's sound, a knob a part, as the MIDI
 * channels play them (ROUT CH1-4): KNOB 1 KEYS (CH 1), KNOB 2 DRUMS (CH 2), KNOB 3 GUITAR (CH 3), KNOB 4 BASS (CH 4).
 * Their sounds as PRESETS gives each part (engines.c MM_SOUNDS, mm_sound_step: wrapping). A knob turned, the keys play
 * that part (to hear the sound) while the page is open: the knob's row marked; leaving it, they play your part again.
 *   HOME or ENV: back; ARP SEQ EDIT SCL FX SAVE as from GAME; ALGORITHM your part */
#define SND_FAST_MS 250u                         /* two turns this close: the list pops up */
#define SND_POP_MS 1500u                         /* .. and stays this long after the last turn */
#define SND_POP_ROWS 8u
static struct {
    uint8_t on;
    uint8_t pop;                                 /* the list shown: its part + 1, 0 = none */
    uint32_t sig;
    uint32_t turn_t[NPART];                      /* fm1_ms of each knob's last turn */
    uint32_t pop_t;                              /* .. of the list's part's last turn */
    uint8_t drawn;                               /* the list on the screen: its part + 1, 0 = none (snd_draw) */
} snd;

static int snd_on(void) { return snd.on; }
static void snd_open(void) { snd.on = 1; snd.pop = 0; snd.drawn = 0; snd.sig = 0; ui.force = 1; }
static void snd_close(void)                      /* (the keys: your part's again) */
{
    if (snd.on)
        track_select(pl.mode % NTRK);
    snd.on = 0;
    snd.pop = 0;
    ui.force = 1;
}

static void snd_input(uint32_t pressed, uint32_t home)
{
    static const uint8_t KN[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    uint32_t k;
    if (home == BT_TAP || ((pressed >> panel.btn[B_ENV]) & 1u)) {
        snd_close();
        if (home == BT_TAP)
            mm_nav(B_HOME);
        enc_drop();
        return;
    }
    {   /* ARP the game, SEQ the songs, EDIT the MIXER, SCL the METRONOME, FX GAME, SAVE PRESETS (mm_nav) */
        static const uint8_t NAV[6] = {B_ARP, B_SEQ, B_EDIT, B_SCL, B_FX, B_SAVE};
        int32_t al = panel_enc(EN_ALGO);
        if (al)
            mm_part_turn(al);                        /* ALGORITHM: your part, here too */
        for (k = 0; k < 6u; k++)
            if ((pressed >> panel.btn[NAV[k]]) & 1u) {
                if (NAV[k] == B_SAVE)
                    ui.save_t0 |= 2u;
                mm_nav(NAV[k]);
                enc_drop();
                return;
            }
    }
    for (k = 0; k < NPART; k++) {                    /* KNOB k + 1: part k's sound; the keys on it (to hear it) */
        int32_t n = panel_enc(KN[k]);
        if (!n)
            continue;
        track_select(k);                             /* (first: the sounds a part may load follow the keys' part) */
        mm_sound_step(k, n);
        if (n > 1 || n < -1 || fm1_ms - snd.turn_t[k] < SND_FAST_MS || snd.pop == k + 1u) {
            snd.pop = (uint8_t)(k + 1u);             /* turned quickly: its list pops up (kept up while it turns) */
            snd.pop_t = fm1_ms;
        }
        snd.turn_t[k] = fm1_ms;
    }
    if (snd.pop && fm1_ms - snd.pop_t >= SND_POP_MS)
        snd.pop = 0;                                 /* (gone: the rows drawn again, snd_draw's sig) */
    enc_drop();
}

/* the list a part's knob turned quickly pops up: its sounds, the one it plays filled in its colour, in a frame of it.
 * whole: its frame too (the page under it just drawn); else only its head and rows, each over itself (a turn while
 * it shows: nothing cleared first, so nothing flashes) */
static void snd_pop_draw(uint32_t k, int whole)
{
    uint32_t n = mm_sound_n(k), cur = mm_sound_pos(k, trk[k].eng_req % NENGINES, trk[k].preset), top, i, e, pk;
    uint32_t x = 16, y = 34, w = 208, h = 22u + SND_POP_ROWS * 20u + 4u;
    uint16_t c = mm_part_col(k);
    char b[24], *t;
    static const char *const NAME[NPART] = {"KEYS", "DRUMS", "GUITAR", "BASS"};
    top = cur < n && cur >= SND_POP_ROWS / 2u ? cur - SND_POP_ROWS / 2u : 0u;
    if (n > SND_POP_ROWS && top > n - SND_POP_ROWS)
        top = n - SND_POP_ROWS;
    if (whole) {
        lcd_fill(0, 208, 240, 32, T_BG);                   /* (the footer under it: hidden while it shows) */
        lcd_fill(x - 2u, y - 2u, w + 4u, h + 4u, c);       /* the frame */
        lcd_fill(x, y + 20u, w, h - 20u, T_SURF);
        lcd_fill(x, y, w, 20, c);                          /* the head: the part, CH n, the place (its text: its box) */
    }
    str_cpy(b, NAME[k], sizeof b);
    t = b + str_len(b);
    str_cpy(t, "  CH ", sizeof b - (uint32_t)(t - b));
    sg_uint(t + 5, k + 1u);
    mm_part_text(x + 6u, y + (20u - AF_S.h) / 2u, 120, &AF_S, b, mm_part_ink(), c, 0);
    t = sg_uint(b, cur < n ? cur + 1u : 0u);
    *t++ = '/';
    sg_uint(t, n);
    mm_part_text(x + 126u, y + (20u - AF_S.h) / 2u, 76, &AF_S, b, mm_part_ink(), c, 2);
    for (i = 0; i < SND_POP_ROWS; i++) {
        uint32_t ry = y + 22u + i * 20u, me = top + i == cur;
        uint16_t bg = me ? c : T_SURF;
        if (top + i >= n) {                                /* (a short list: the rest empty) */
            lcd_fill(x + 2u, ry, w - 4u, 20, T_SURF);
            continue;
        }
        e = mm_sound_at(k, top + i, &pk);
        mm_part_text(x + 8u, ry + (19u - AF_M.h) / 2u, w - 16u, &AF_M, ENGINES[e]->presets[pk].name,
                     me ? mm_part_ink() : T_TEXT, bg, 0);
        lcd_fill(x + 2u, ry, 6, 19, bg);                   /* (its edges: the text paints its own box) */
        lcd_fill(x + w - 8u, ry, 6, 19, bg);
        lcd_fill(x + 2u, ry + 19u, w - 4u, 1, T_SURF);
    }
    if (top)                                               /* more above, more below */
        lcd_fill(x + w - 8u, y + 24u, 4, 4, T_MID);
    if (top + SND_POP_ROWS < n)
        lcd_fill(x + w - 8u, y + h - 8u, 4, 4, T_MID);
}

static void snd_draw(void)
{
    static const char *const LABEL[NPART] = {"K1 KEYS", "K2 DRUMS", "K3 GUITAR", "K4 BASS"};
    char note[16], *e;
    uint32_t sig = 1u + palette_cur() * 7919u + song.sel * 131u, k;
    if (snd.pop && fm1_ms - snd.pop_t >= SND_POP_MS)
        snd.pop = 0;
    for (k = 0; k < NPART; k++)
        sig = sig * 31u + trk[k].eng_req * 257u + trk[k].preset;
    sig = sig * 7u + snd.pop;
    if (!ui.force && sig == snd.sig)
        return;
    snd.sig = sig;
    if (snd.pop && snd.drawn == snd.pop && !ui.force) {  /* the list up, turned again: the list only (no flash) */
        snd_pop_draw(snd.pop - 1u, 0);
        return;
    }
    snd.drawn = snd.pop;
    lcd_fill(0, 0, 240, H_HEAD, T_SURF);
    draw_text_line(8, (H_HEAD - AF_M.h) / 2, 160, &AF_M, "SOUNDS", T_TEXT, T_SURF, 0);
    lcd_fill(0, H_HEAD, 240, GM_ROW_Y - H_HEAD, T_BG);
    for (k = 0; k < NPART; k++) {                    /* a row a part: its sound, its channel, its place in its list */
        const track_t *t = &trk[k];
        uint32_t n = mm_sound_n(k), pos = mm_sound_pos(k, t->eng_req % NENGINES, t->preset);
        uint32_t y = GM_ROW_Y + k * GM_ROW_H;
        str_cpy(note, "CH ", sizeof note);
        e = sg_uint(note + 3, k + 1u);
        if (pos < n) {
            *e++ = ' '; *e++ = ' ';
            e = sg_uint(e, pos + 1u);
            *e++ = '/';
            sg_uint(e, n);
        }
        gm_row(k, LABEL[k], mm_sound_name(k), note);
        mm_part_text(8, y + 8u, 72, &AF_S, LABEL[k], mm_part_col(k), T_BG, 0);   /* its channel's colour */
        lcd_fill(0, y + 4u, song.sel == k ? 5u : 2u, GM_ROW_H - 8u, mm_part_col(k));   /* wide: the keys play it now */
    }
    lcd_fill(0, GM_ROW_Y + 4 * GM_ROW_H, 240, 240 - (GM_ROW_Y + 4 * GM_ROW_H), T_BG);
    draw_text_line(8, 212, 224, &AF_S, "HEAR IT ON THE KEYS   ENV: BACK", T_MID, T_BG, 0);
    if (snd.pop)
        snd_pop_draw(snd.pop - 1u, 1);
}

/* ----------------------------------------------------------------------------------------------- the panel ---
 * What each button does, from anywhere.
 *   HOME  the game: HOME's view (the notes, the cards; ARP too)        SEQ   the songs (SONGS; also at power-on, main.c)
 *   FX    GAME (the game's settings)                                   EDIT  the MIXER
 *   SCL   METRONOME                                                    SAVE  PRESETS (your part's instrument's sounds)
 *   PLAY  the selected song (ui_input.c), stopping the one playing     ENV   SOUNDS (each part's sound)
 *   LFO GLO REC: nothing
 *   ALGORITHM  your part (mm_part_turn), from anywhere                 PRESETS  its sound (shown bottom left on HOME) */
static void mm_open_page(int (*is)(const page_t *))   /* the page is() picks, out of SONGS and GAME */
{
    uint32_t i;
    for (i = 0; i < NPAGES && !is(&PAGES[i]); i++)
        ;
    if (i == NPAGES)
        return;
    songs_close();
    game_close();
    metro_close();
    snd_close();
    ui.page = (uint8_t)i;
    ui.fam_last[PAGES[i].fam] = (uint8_t)i;
    ui.home = 0;
    page_entered();
}
static int is_mixer(const page_t *p) { return p->graph == GR_TRK; }

static void mm_nav(uint32_t b)
{
    if (b == B_EDIT) {                                /* (a page: mm_open_page closes the screens) */
        mm_open_page(is_mixer);
        return;
    }
    songs_close();
    game_close();
    metro_close();
    snd_close();
    if (b == B_ARP || b == B_HOME)                    /* the game */
        go_home();
    else if (b == B_SEQ)
        songs_open();
    else if (b == B_SCL)
        metro_open();
    else if (b == B_FX)
        game_open();
    else if (b == B_ENV)
        snd_open();
    else if (b == B_SAVE)
        open_family(FAM_SAVE);                        /* (PRESETS: ui.c open_family) */
}

/* ui_input.c page_tap: a page button let go on a page or the game; 1: it acted */
static int mm_page_tap(uint32_t b)
{
    if (b == B_ARP || b == B_EDIT || b == B_SCL || b == B_SEQ || b == B_ENV) {
        mm_nav(b);
        return 1;
    }
    return b == B_LFO || b == B_GLO || b == B_REC;   /* (nothing) */
}

/* PLAY, no song playing (ui_input.c): the song selected, and the game */
static void mm_play_selected(void)
{
    sg_check();
    if (sg.free || !mm_nsongs()) {
        mm_free_start();
    } else {
        if (!sg.bpm_x10)
            sg_pick(sg.sel);
        songs_play(sg.sel);
    }
    songs_close();
    game_close();
    go_home();
}
