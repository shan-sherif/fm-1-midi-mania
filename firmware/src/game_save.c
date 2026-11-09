/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* Midi-Mania: the game's settings and high scores, kept after power-off in the settings record (settings_persist.c,
 * favorites.factory: rows 0..13 are the engines' favourites, and the rows of the engines Midi-Mania leaves out are
 * free: engines.c MM_ENG). So they are written as MENU's settings are (settings_later, then settings_poll: never while
 * a song plays) and go in the installer's backup with them (editor_backup.c object 1).
 *   row 11: the settings. [0] GS_MARK, [1] GS_VER, [2] your part (pl.mode), [3] SCROLL UP, [4] OFFSET (int8, 2 ms a
 *     step), [5] PLAY YOU, [6] RANGE, [7..10] NOTE SPEED per part. Anything else in [0..1] (a Felucca's favourites of
 *     that engine, or nothing yet): the defaults, and this row and the scores' rows cleared
 *   rows 2 3 5 6 7 8: the high scores, HS_ENTRIES entries of 8 bytes: an id (hs_id: the song, your part, your RANGE; 0 empty)
 *     u32, the best score (judge.c units: 3 a PERFECT; shown x 100) u16, its accuracy %, its flags (bit 0: every note
 *     hit, no MISS). Full: a new one takes the place of the lowest score */
#define GS_ROW (favorites.factory[11])
#define GS_MARK 0x4Du                                /* 'M' */
#define GS_VER 1u
#define HS_ENTRIES 24u
static const uint8_t HS_ROWS[6] = {2, 3, 5, 6, 7, 8};
#define HS_FULL_COMBO 1u

static uint8_t *hs_at(uint32_t i) { return &favorites.factory[HS_ROWS[i / 4u]][(i % 4u) * 8u]; }
static uint32_t hs_rd32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* a song with your part and RANGE: FNV-1a over its title, its notes' count, its length and its first note's time */
static uint32_t hs_id(const mm_song_t *s, uint32_t mode, uint32_t range)
{
    uint32_t h = 2166136261u, i, v[4];
    const char *t = s->title;
    for (i = 0; t && t[i] && i < 24u; i++)
        h = (h ^ (uint8_t)t[i]) * 16777619u;
    v[0] = s->nnote;
    v[1] = s->length;
    v[2] = s->nnote ? s->note[0].time : 0u;
    v[3] = mode * 16u + range;
    for (i = 0; i < 16u; i++)
        h = (h ^ (uint8_t)(v[i / 4u] >> (8u * (i % 4u)))) * 16777619u;
    return h ? h : 1u;                               /* (0: an empty entry) */
}
static int32_t hs_find(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < HS_ENTRIES; i++)
        if (hs_rd32(hs_at(i)) == id)
            return (int32_t)i;
    return -1;
}
/* the best for song s with your part and RANGE now: 1 and its score (x 100), accuracy, flags; 0 none yet */
static int hs_best(const mm_song_t *s, uint32_t *score, uint32_t *acc, uint32_t *flags)
{
    int32_t i = hs_find(hs_id(s, pl.mode, pl.range));
    const uint8_t *b;
    if (i < 0)
        return 0;
    b = hs_at((uint32_t)i);
    *score = (uint32_t)(b[4] | b[5] << 8) * 100u;
    *acc = b[6];
    *flags = b[7];
    return 1;
}

static struct {
    uint8_t loaded;                                  /* game_load ran: the row is ours */
    uint32_t sig;                                    /* the settings as last put in the row */
    uint8_t done_seen;                               /* pl_sc.done seen (the result recorded once) */
    uint8_t new_best;                                /* the last result is a new best (graph_result) */
    uint32_t prev_score;                             /* the best before it, x 100 (0: none) */
    uint32_t gen;                                    /* +1 a score written (SONGS draws again) */
} gs;

static uint32_t gs_sig(void)
{
    uint32_t h = 2166136261u, i;
    const uint8_t v[6] = {pl.mode, pl.up, (uint8_t)pl.offset, pl.you, pl.range, 0};
    for (i = 0; i < 6u; i++)
        h = (h ^ v[i]) * 16777619u;
    for (i = 0; i < 4u; i++)
        h = (h ^ pl.speed[i]) * 16777619u;
    return h;
}
static void gs_put(void)                             /* the settings into the row */
{
    uint32_t i;
    GS_ROW[0] = GS_MARK;
    GS_ROW[1] = GS_VER;
    GS_ROW[2] = pl.mode;
    GS_ROW[3] = pl.up;
    GS_ROW[4] = (uint8_t)pl.offset;
    GS_ROW[5] = pl.you;
    GS_ROW[6] = pl.range;
    for (i = 0; i < 4u; i++)
        GS_ROW[7u + i] = pl.speed[i];
    gs.sig = gs_sig();
}

/* power-on (main.c mm_init, before SONGS opens): the saved settings, or the defaults and the rows made ours */
static void game_load(void)
{
    uint32_t i;
    gs.loaded = 1;
    if (GS_ROW[0] != GS_MARK || GS_ROW[1] != GS_VER) {
        memset(GS_ROW, 0, sizeof GS_ROW);
        for (i = 0; i < sizeof HS_ROWS; i++)
            memset(favorites.factory[HS_ROWS[i]], 0, sizeof favorites.factory[0]);
        gs_put();                                    /* (written with the next settings save, nothing forced now) */
        return;
    }
    pl.mode = (uint8_t)(GS_ROW[2] < MM_NMODES && ((MM_PLAYABLE >> GS_ROW[2]) & 1u) ? GS_ROW[2] : 0u);
    pl.up = (uint8_t)(GS_ROW[3] != 0u);
    pl.offset = (int8_t)((int8_t)GS_ROW[4] < -50 ? -50 : (int8_t)GS_ROW[4] > 50 ? 50 : (int8_t)GS_ROW[4]);
    pl.you = (uint8_t)(GS_ROW[5] != 0u);
    pl.range = (uint8_t)(GS_ROW[6] < MM_RANGES ? GS_ROW[6] : 0u);
    for (i = 0; i < 4u; i++) {
        uint32_t v = GS_ROW[7u + i];
        if (v >= PL_SPEED_MIN && v <= PL_SPEED_MAX)
            pl.speed[i] = (uint8_t)v;
    }
    gs.sig = gs_sig();
    track_select(pl.mode % NTRK);                    /* (the keys play your part, as ALGORITHM picks it) */
}

/* the result of a judged song, once (player.c pl_sc.done): a new best kept */
static void gs_record(void)
{
    const mm_song_t *s = pl_song();
    uint32_t id = hs_id(s, pl.mode, pl.range), score = pl_judge.score > 65535u ? 65535u : pl_judge.score, i, low = 0;
    uint32_t acc = pl_judge_accuracy(), flags = pl_judge.count[J_MISS] ? 0u : HS_FULL_COMBO;
    int32_t at = hs_find(id);
    uint8_t *b;
    gs.new_best = 0;
    gs.prev_score = 0;
    if (!score)
        return;                                      /* (nothing hit: no best to keep) */
    if (at >= 0) {
        b = hs_at((uint32_t)at);
        gs.prev_score = (uint32_t)(b[4] | b[5] << 8) * 100u;
        if (score <= (uint32_t)(b[4] | b[5] << 8))
            return;
    } else {                                         /* a free entry, else the lowest score's */
        for (i = 0; i < HS_ENTRIES; i++) {
            b = hs_at(i);
            if (!hs_rd32(b)) {
                low = i;
                break;
            }
            if ((b[4] | b[5] << 8) < (hs_at(low)[4] | hs_at(low)[5] << 8))
                low = i;
        }
        at = (int32_t)low;
    }
    b = hs_at((uint32_t)at);
    b[0] = (uint8_t)id; b[1] = (uint8_t)(id >> 8); b[2] = (uint8_t)(id >> 16); b[3] = (uint8_t)(id >> 24);
    b[4] = (uint8_t)score; b[5] = (uint8_t)(score >> 8);
    b[6] = (uint8_t)acc;
    b[7] = (uint8_t)flags;
    gs.new_best = 1;
    gs.gen++;
    settings_later();                                /* (written once the page is left or idle, never mid-song) */
}

/* every UI pass (ui_input.c): a setting changed (your part, SCROLL, OFFSET, PLAY, RANGE, NOTE SPEED): into the row,
 * saved later; a judged song just ended: its result recorded */
static void game_save_poll(void)
{
    if (!gs.loaded)
        return;
    if (gs_sig() != gs.sig) {
        gs_put();
        settings_later();
    }
    if (pl_sc.done && !gs.done_seen)
        gs_record();
    gs.done_seen = pl_sc.done;
}

/* graph_result (ui_graph.c): the last result a new best (1), and the best before it (x 100; 0: none) */
static int game_new_best(uint32_t *prev)
{
    *prev = gs.prev_score;
    return gs.new_best;
}
