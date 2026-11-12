/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* The song player: a chart (chart.h) played on the four parts, in the audio ISR.
 *
 * Included by seq.c (before events_block, which calls player_block once a block), so every build and test that has
 * the sequencer has the player. The songs built in are build/gen/midimania_songs.h (tools/gen_songs.py, from midi/).
 *
 * The main loop asks (player_start, player_stop: one request at a time, taken at the next block); the ISR plays. A
 * note starts in the block its time falls in (CTL samples, 0.7 ms) through trk_note_on, as a key's does, and ends
 * after its length through trk_note_off. Each part plays with the sound it has: player_start loads the song's sounds
 * onto its parts first (main loop: set_engine_of / apply_preset_to, ui_songs.c). The metronome (click.c, MENU > CLICK
 * ON) clicks on the song's beats (tools/gen_songs.py: every quarter note of its tempo map): it follows the tempo. */
#include "chart.h"

typedef struct {
    const char *title;
    const chart_note_t *note;            /* by time */
    uint32_t nnote;
    uint32_t length;                     /* the last note's end, samples */
    uint16_t bpm_x10;                    /* the first tempo, for the list */
    uint8_t parts;                       /* bit p: part p has notes */
    chart_part_t part[CHART_NPART];      /* its sound: engine, preset */
    const uint32_t *beat;                /* its beats (every quarter note), by time: the metronome clicks on them */
    uint32_t nbeat;
    uint16_t beat0;                      /* the first one's number in the file: beat0 + k a bar's first when % bpb is 0 */
    uint8_t bpb;                         /* beats a bar (the time signature's), 4 without one */
} mm_song_t;

#include "midimania_songs.h"             /* build/gen: MM_NSONGS, MM_SONGS[] */

/* Stored songs: charts (chart.h, docs/CHART_FORMAT.md) in the flash's song region (once Felucca's user sample slots),
 * loaded from MIDI files on a computer (Midi-Mania Studio's page; the FM-1's over USB: editor_songs.c). Each starts on
 * a 4 KiB sector boundary and is read in place (mm_song_base: the region as the CPU sees it: the FM-1's XIP window,
 * the host's simulated flash; 0: none). mm_song_scan looks at every sector: a chart whose CRC holds and that is not
 * deleted is listed (after the built-in songs: mm_nsongs, mm_song(i)) and its sectors are in use; anything else is
 * free. A song is deleted by erasing its sectors and a new one goes into the first gap it fits (mm_song_room): songs
 * never move, so a power cut loses at most the one being written or deleted */
#ifndef MM_SONG_BASE                             /* (Studio, the browser build: a region of its own, bigger than the
                                                 * FM-1's, so a song too big for an FM-1 still loads there:
                                                 * platforms/web/build.sh) */
#define MM_SONG_BASE 0xA0000u                    /* .. 0xDBFFF (240 KiB) */
#define MM_SONG_SIZE 0x3C000u
#endif
#define MM_SONG_SECTOR 0x1000u
#define MM_SONG_NSEC (MM_SONG_SIZE / MM_SONG_SECTOR)   /* 60 */
#ifndef MM_SONG_MAX
#define MM_SONG_MAX 16u                          /* stored songs listed at once */
#endif
static const uint8_t *mm_song_base;
static mm_song_t mm_stored[MM_SONG_MAX];
static char mm_stored_title[MM_SONG_MAX][25];
static uint32_t mm_stored_off[MM_SONG_MAX];      /* each one's offset in the region */
static uint32_t mm_stored_size[MM_SONG_MAX];     /* .. and its size, bytes */
static uint32_t mm_nstored;
static uint8_t mm_sec_used[MM_SONG_NSEC];        /* a sector a song is in (mm_song_room: the rest are free) */
static uint32_t mm_songs_gen;                    /* +1 a scan: SONGS checks its selection (ui_songs.c sg_check) */
static uint32_t mm_crc32_add(uint32_t c, const uint8_t *p, uint32_t n)   /* (from 0xFFFFFFFF; the CRC is ~c) */
{
    static const uint32_t T[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                   0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
/* zlib's CRC-32 (Python zlib.crc32, JavaScript the same) */
static uint32_t mm_crc32(const uint8_t *p, uint32_t n) { return ~mm_crc32_add(0xFFFFFFFFu, p, n); }
/* the chart at off in the region: its size (header, beats, notes), 0 if it is no chart or does not fit */
static uint32_t mm_chart_size(uint32_t off)
{
    const chart_head_t *h = (const chart_head_t *)(mm_song_base + off);
    uint32_t room = MM_SONG_SIZE - off, n;
    if (room < sizeof(chart_head_t) || h->magic != CHART_MAGIC || h->head_size < sizeof(chart_head_t) ||
        h->head_size > 256u || (h->head_size & 3u) || h->nbeat > room / 4u || h->nnote > room / sizeof(chart_note_t))
        return 0;
    n = h->head_size + h->nbeat * 4u + h->nnote * (uint32_t)sizeof(chart_note_t);
    return n <= room ? n : 0u;
}
static void mm_song_scan(void)
{
    uint32_t off = 0, n, i;
    mm_nstored = 0;
    mm_songs_gen++;
    for (i = 0; i < MM_SONG_NSEC; i++)
        mm_sec_used[i] = 0;
    if (!mm_song_base)
        return;
    while (off < MM_SONG_SIZE) {
        const chart_head_t *h = (const chart_head_t *)(mm_song_base + off);
        const uint8_t *body;
        n = mm_chart_size(off);
        body = mm_song_base + off + (n ? h->head_size : 0u);
        if (!n || h->live != 0xFFu || mm_crc32(body, n - h->head_size) != h->crc32) {
            off += MM_SONG_SECTOR;                  /* free (erased, deleted, a broken upload's) */
            continue;
        }
        for (i = off / MM_SONG_SECTOR; i < (off + n + MM_SONG_SECTOR - 1u) / MM_SONG_SECTOR; i++)
            mm_sec_used[i] = 1;                     /* (in use even past MM_SONG_MAX: never written over) */
        if (mm_nstored < MM_SONG_MAX) {
            mm_song_t *s = &mm_stored[mm_nstored];
            char *t = mm_stored_title[mm_nstored];
            uint32_t i, p;
            for (i = 0; i < 24u && h->title[i]; i++)
                t[i] = (char)(h->title[i] >= 32 && h->title[i] < 127 ? h->title[i] : '?');
            t[i] = 0;
            s->title = t;
            s->beat = (const uint32_t *)body;
            s->nbeat = h->nbeat;
            s->note = (const chart_note_t *)(body + h->nbeat * 4u);
            s->nnote = h->nnote;
            s->length = h->length;
            s->bpm_x10 = h->bpm_x10;
            s->bpb = h->beats_per_bar ? h->beats_per_bar : 4u;
            s->beat0 = h->beat0;
            for (p = 0; p < CHART_NPART; p++)
                s->part[p] = h->part[p];
            s->parts = h->parts;
            mm_stored_size[mm_nstored] = n;
            mm_stored_off[mm_nstored++] = off;
        }
        off = (off + n + MM_SONG_SECTOR - 1u) & ~(MM_SONG_SECTOR - 1u);
    }
}
/* where a chart of n bytes goes: the first run of free sectors it fits in (its offset in the region), or -1 */
static int32_t mm_song_room(uint32_t n)
{
    uint32_t need = (n + MM_SONG_SECTOR - 1u) / MM_SONG_SECTOR, run = 0, i;
    for (i = 0; n && i < MM_SONG_NSEC; i++) {
        run = mm_sec_used[i] ? 0u : run + 1u;
        if (run == need)
            return (int32_t)((i + 1u - need) * MM_SONG_SECTOR);
    }
    return -1;
}
static uint32_t mm_song_free_sectors(void)
{
    uint32_t i, k = 0;
    for (i = 0; i < MM_SONG_NSEC; i++)
        k += !mm_sec_used[i];
    return k;
}
static uint32_t mm_song_largest_gap(void)        /* sectors */
{
    uint32_t i, run = 0, best = 0;
    for (i = 0; i < MM_SONG_NSEC; i++) {
        run = mm_sec_used[i] ? 0u : run + 1u;
        best = run > best ? run : best;
    }
    return best;
}
static uint32_t mm_nsongs(void) { return MM_NSONGS + mm_nstored; }   /* built in, then stored */
static const mm_song_t *mm_song(uint32_t i)
{
#if MM_NSONGS
    if (i < MM_NSONGS)
        return &MM_SONGS[i];
#endif
    return &mm_stored[(i - MM_NSONGS) % MM_SONG_MAX];
}

#define PL_ON_MAX 32u                    /* notes the player holds at once (the chart keeps 8: room for overlaps) */
enum { PL_REQ_NONE, PL_REQ_START, PL_REQ_STOP };
#define PL_FREE 0xFFu                    /* pl.req_song: FREE PLAY, no song */
/* the modes: the part a song's instrument plays on (tools/gen_songs.py ROLES), and the one the player picks
 * (SONGS, ui_songs.c), whose notes HOME shows (ui_graph.c graph_notes) */
enum { MM_KEYS, MM_DRUMS, MM_GUITAR, MM_BASS, MM_NMODES };   /* parts 1..4: 1 KEYS (piano), 2 DRUMS (engines.c MM_PART_ENGINE) */
static const char *const MM_MODE_NAME[MM_NMODES] = {"KEYS", "DRUMS", "GUITAR", "BASS"};
/* the parts you can play: all four (Midi-Mania 0.8.0; before it DRUMS and KEYS). GUITAR and BASS are melodic parts as KEYS
 * is, laid out on HOME as GAME's LANES says (mm_view, below): STRINGS or CHROMATIC */
#define MM_PLAYABLE ((1u << MM_KEYS) | (1u << MM_DRUMS) | (1u << MM_GUITAR) | (1u << MM_BASS))
#define PL_RATE_ONE 65536u               /* pl.rate: the song's own tempo (Q16) */

static struct {
    volatile uint8_t req;                /* main loop -> ISR: PL_REQ_* (the ISR clears it) */
    volatile uint8_t req_song;           /* .. the song PL_REQ_START plays */
    volatile uint8_t playing;            /* ISR: a song plays (0 at its end too) */
    volatile uint8_t song;               /* ISR: the song playing, or the last one played */
    volatile uint8_t free;               /* ISR: FREE PLAY (player_free): no song, its clock running until STOP */
    volatile uint32_t rate;              /* main loop: song samples a sample, Q16 (the tempo; PL_RATE_ONE as written) */
    uint8_t mode;                        /* main loop: the player's part, MM_* (SONGS; HOME shows it, its keys light) */
    uint8_t up;                          /* main loop: HOME's notes rise to a line at the top (GAME: SCROLL UP),
                                          * 0: they fall to one at the bottom (ui_graph.c graph_notes) */
    uint8_t speed[4];                    /* main loop: GAME's NOTE SPEED 1..16 per part (PL_SPEED: your part's): song
                                          * time on HOME's panel (pl_ahead); DRUMS 8 at power-on, the others 4 */
    int8_t offset;                       /* main loop: GAME's OFFSET, ms / 2 (-100..+100 ms): the player's calibration,
                                          * for the judge (judge.c judge_t.offset) */
    uint8_t you;                         /* main loop: SONGS' PLAY: 0 AUTO (the device plays your part), 1 YOU (your
                                          * notes silent: the keys play them; their keys still light, as guides) */
    uint8_t range;                       /* main loop: SONGS' AUTO (K4; mm_range, mm_mine): your notes of your part, the
                                          * others the device plays. KEYS 0 FULL 1 CURRENT RANGE 2 1 OCTAVE; DRUMS 0 ALL 8
                                          * 1 5 PADS 2 4 PADS 3 2 PADS; GUITAR, BASS on STRINGS the lanes (6 4 2, 4 2) */
    uint8_t style[4];                    /* main loop: SONGS' STYLE per part (K3; mm_style): KEYS KS_*, DRUMS DS_*, GUITAR
                                          * BASS 0 STRINGS 1 CHROMATIC; 0 the default each */
    volatile uint32_t pos;              /* ISR: song samples since its start (the chart's clock) */
    volatile uint32_t keys[CHART_NPART]; /* ISR: per part, bit k: a note of key k sounds (LEDs, HOME's strip) */
    uint32_t frac;                       /* pos's fraction (Q16) */
    uint32_t next;                       /* the next note to start */
    uint32_t nb;                         /* the next beat (the metronome) */
    /* COUNT-IN (click.c cin_bars, the METRONOME page): cin_n clicks, cin_len apart (the song's first beat's length),
     * the last one a beat before its first beat: at song times cin_c0 + k * cin_len. Those before the song's start
     * (cin_c0 < 0) click in a pre-roll of pre samples, the song held at 0 */
    uint32_t pre, cin_n, cin_k, cin_len;
    int32_t cin_c0;
    uint8_t on_n;
    struct { uint32_t off; uint8_t part, pitch, key, quiet; } on[PL_ON_MAX];   /* sounding: its note-off time; quiet: a
                                                  * YOU note, not played (its key lit while it lasts, no note-off) */
} pl = {.rate = PL_RATE_ONE, .speed = {4, 8, 4, 4}};   /* (speed: by part: KEYS 4, DRUMS 8) */
#define PL_SPEED (pl.speed[pl.mode & 3u])          /* your part's NOTE SPEED */

/* the song time from the top of HOME's panel to the line: NOTE SPEED 1 (slow: notes far apart, 4 s) .. 8 (1 s) .. 16
 * (fastest, 0.4 s); 4, the default, 2 s */
#define PL_SPEED_MIN 1u
#define PL_SPEED_MAX 16u
static uint32_t pl_ahead(void)
{
    static const uint16_t MS[PL_SPEED_MAX] = {4000, 3000, 2500, 2000, 1700, 1400, 1200, 1000,
                                              900, 800, 700, 600, 550, 500, 450, 400};
    uint32_t s = PL_SPEED < PL_SPEED_MIN ? PL_SPEED_MIN : PL_SPEED > PL_SPEED_MAX ? PL_SPEED_MAX : PL_SPEED;
    return MS[s - 1u] * (CHART_FS / 1000u) + MS[s - 1u] * (CHART_FS % 1000u) / 1000u;
}

/* DRUMS: the eight drum lanes on HOME, left to right (ui_graph.c graph_drums), and their keys: the black keys F#3 ..
 * A#4 in that order, so the key under a lane is the one that lights (and, in the game, the one to press). The lanes
 * are the DRUM engine's (eng_drum.c: 0 KICK 1 SNARE 2 CLAP 3 HATCL 4 HATOP 5 TOM 6 RIM 7 BELL); a hit's lane is
 * eng_drum.c drum_lane of its General MIDI note, as the engine strikes it */
static const uint8_t MM_DRUM_ORDER[NLANE] = {7, 4, 3, 1, 6, 0, 2, 5};   /* BELL OH CH SNR RIM KICK CLAP TOM */
static const uint8_t MM_DRUM_KEY[NLANE] = {1, 3, 5, 8, 10, 13, 15, 17};  /* position p's key: F#3 G#3 .. A#4 */
static uint32_t mm_drum_pos(uint32_t lane)        /* the position of a lane on HOME, 0 = left */
{
    uint32_t p;
    for (p = 0; p < NLANE; p++)
        if (MM_DRUM_ORDER[p] == lane)
            return p;
    return 0;
}
static uint32_t pl_drum_key(uint32_t note)        /* a drum hit's key */
{
    return MM_DRUM_KEY[mm_drum_pos(drum_lane(note) % NLANE)];
}

/* seq.c kb_map (declared there): with no sequencer the DRUMS part's black keys F#3 .. A#4 play the lanes under them on
 * HOME (MM_DRUM_ORDER: BELL OH CH SNR RIM KICK CLAP TOM), so a key pressed is the drum shown; -1: not one of them */
static int32_t mm_drum_key_note(uint32_t k)
{
    uint32_t p;
    for (p = 0; p < NLANE; p++)
        if (MM_DRUM_KEY[p] == k)
            return DRUM_LANE_NOTE[MM_DRUM_ORDER[p]];
    return -1;
}

/* SONGS' K3 STYLE and K4 AUTO, per part (mm_mine: the notes of your part that are yours; the device plays the rest):
 *   KEYS    STYLE 49 KEYS (C2 .. C6 on HOME: the panel's 27 keys a window on them that OCT- and OCT+ move, a MIDI
 *           keyboard all of them), 27 KEYS (the panel's F3 .. G5: the chart's own keys), 1 OCTAVE (12 lanes, C .. B:
 *           any key of that name). AUTO FULL (every note yours, folded into the style's keys by octaves), CURRENT
 *           RANGE (only those in the style's own range: C2 .. C6, F3 .. G5, C4 .. B4), 1 OCTAVE (only C4 .. B4)
 *   DRUMS   STYLE FULL, QUARTER (only the hits on a beat: no 8ths or 16ths), HALF (only on a bar's beats 1 and 3, its
 *           even beats). AUTO the pads: ALL 8, 5 PADS, 4 PADS, 2 PADS
 *   GUITAR, BASS  STYLE STRINGS (lanes on the white keys F3 ..), CHROMATIC (12 lanes, C .. B). AUTO, STRINGS: the lanes,
 *           6 4 2 (BASS 4 2), every note still yours, the song's pitches squeezed onto them; CHROMATIC: 1 OCTAVE alone
 * pl.style per part (game_save.c keeps them); pl.range the AUTO of your part (0 when the part or its STYLE changes) */
enum { KS_27, KS_49, KS_OCT, KS_N };                     /* KEYS' STYLE values (0: 27 KEYS, as before them) */
static const uint8_t KS_ORDER[KS_N] = {KS_49, KS_27, KS_OCT};   /* .. as K3 steps through them */
enum { DS_FULL, DS_QUARTER, DS_HALF, DS_N };            /* DRUMS' */
static const uint8_t MM_DRUM_SETS[4] = {0xFFu, 0x5Bu, 0x1Bu, 0x03u};   /* ALL; KICK SNR CH OH RIM; KICK SNR CH OH;
                                                  * KICK SNR (bit l: the DRUM engine's lane l, eng_drum.c) */
static uint32_t mm_style_n(uint32_t mode) { return mode == MM_KEYS ? KS_N : mode == MM_DRUMS ? DS_N : 2u; }
static uint32_t mm_style(void) { return pl.style[pl.mode & 3u] % mm_style_n(pl.mode); }
static const char *mm_style_name(uint32_t mode, uint32_t st)
{
    static const char *const K[KS_N] = {"27 KEYS", "49 KEYS", "1 OCTAVE"}, *const D[DS_N] = {"FULL", "QUARTER", "HALF"},
                      *const G[2] = {"STRINGS", "CHROMATIC"};
    return mode == MM_KEYS ? K[st % KS_N] : mode == MM_DRUMS ? D[st % DS_N] : G[st % 2u];
}
/* K3: STYLE n steps on, in K3's order (KEYS: 49, 27, 1 OCTAVE), at the ends staying; its AUTO from the first */
static void mm_style_turn(int32_t n)
{
    uint32_t m = pl.mode & 3u, cnt = mm_style_n(m), at = 0, i;
    int32_t v;
    for (i = 0; i < cnt; i++)
        if ((m == MM_KEYS ? KS_ORDER[i] : i) == pl.style[m] % cnt)
            at = i;
    v = (int32_t)at + n;
    v = v < 0 ? 0 : v >= (int32_t)cnt ? (int32_t)cnt - 1 : v;
    if ((uint32_t)v != at)
        pl.range = 0;
    pl.style[m] = (uint8_t)(m == MM_KEYS ? KS_ORDER[v] : (uint32_t)v);
}
static uint32_t mm_string_n(void);                       /* (STRINGS' lanes: below) */
static uint32_t mm_ranges(void)                          /* how many AUTO choices your part has now */
{
    if (pl.mode == MM_KEYS)
        return 3u;
    if (pl.mode == MM_DRUMS)
        return 4u;
    return mm_style() ? 1u : pl.mode == MM_BASS ? 2u : 3u;
}
static uint32_t mm_range(void) { return pl.range < mm_ranges() ? pl.range : 0u; }   /* your part's AUTO */
static const char *mm_range_name(uint32_t r)
{
    static const char *const K[3] = {"FULL", "CURRENT RANGE", "1 OCTAVE"}, *const D[4] = {"ALL 8", "5 PADS", "4 PADS", "2 PADS"},
                      *const G[3] = {"6 LANES", "4 LANES", "2 LANES"};
    if (pl.mode == MM_KEYS)
        return K[r % 3u];
    if (pl.mode == MM_DRUMS)
        return D[r % 4u];
    if (mm_style())
        return "1 OCTAVE";
    return G[(r + (pl.mode == MM_BASS ? 1u : 0u)) % 3u];
}
/* KEYS: the lowest and highest note a STYLE holds (CURRENT RANGE: yours only between them) */
static void mm_keys_range(uint32_t st, uint32_t *lo, uint32_t *hi)
{
    *lo = st == KS_49 ? 36u : st == KS_OCT ? 60u : 53u;
    *hi = st == KS_49 ? 84u : st == KS_OCT ? 71u : 79u;
}
static uint32_t mm_fold49(uint32_t p)                    /* a note into 49 KEYS' C2 .. C6 by octaves */
{
    while (p < 36u)
        p += 12u;
    while (p > 84u)
        p -= 12u;
    return p;
}
static const mm_song_t *pl_song(void);
/* DRUMS' QUARTER and HALF: a hit within an eighth of a beat of one (HALF: of a bar's even beat, 1 and 3) */
static int mm_drum_kept(const chart_note_t *c)
{
    const mm_song_t *s;
    uint32_t st = pl.style[MM_DRUMS] % DS_N, lo = 0, hi, b, len, d;
    if (st == DS_FULL)
        return 1;
    s = pl_song();
    if (s->nbeat < 2u)
        return 1;
    hi = s->nbeat;
    while (lo < hi) {                                    /* (the first beat at or after the hit) */
        uint32_t mid = (lo + hi) / 2u;
        if (s->beat[mid] < c->time)
            lo = mid + 1u;
        else
            hi = mid;
    }
    b = lo >= s->nbeat ? s->nbeat - 1u : lo;
    if (b && c->time - s->beat[b - 1u] < (s->beat[b] > c->time ? s->beat[b] - c->time : c->time - s->beat[b]))
        b--;                                             /* (the nearer of it and the one before) */
    len = b + 1u < s->nbeat ? s->beat[b + 1u] - s->beat[b] : s->beat[b] - s->beat[b - 1u];
    d = c->time > s->beat[b] ? c->time - s->beat[b] : s->beat[b] - c->time;
    if (d > len / 8u)
        return 0;
    return st == DS_QUARTER || ((s->beat0 + b) % (s->bpb ? s->bpb : 4u)) % 2u == 0u;
}
static int mm_in_range(const chart_note_t *c)
{
    uint32_t r = mm_range(), lo, hi;
    if (c->flags & CN_DRUM)
        return ((MM_DRUM_SETS[r % 4u] >> (drum_lane(c->pitch) % NLANE)) & 1u) && mm_drum_kept(c);
    if ((c->flags & CN_PART_MASK) != MM_KEYS || r == 0u)
        return 1;
    if (r == 1u)
        mm_keys_range(pl.style[MM_KEYS] % KS_N, &lo, &hi);
    else
        lo = 60u, hi = 71u;
    return c->pitch >= lo && c->pitch <= hi;
}
static int mm_mine(const chart_note_t *c)       /* a note of your part, in your STYLE and AUTO */
{
    return (c->flags & CN_PART_MASK) == pl.mode && mm_in_range(c);
}

static const mm_song_t *pl_song(void) { return mm_song(pl.song % (mm_nsongs() ? mm_nsongs() : 1u)); }

/* How HOME lays out your part's notes and which keys play them (mm_view), from its STYLE:
 *   STRINGS    GUITAR, BASS: mm_string_n() lanes (AUTO: 6 4 2, BASS 4 2) on the white keys F3 G3 A3 B3 C4 D4 (the bottom
 *              row; DRUMS has the black keys): the distinct pitches your notes use in the song, lowest to highest,
 *              spread over them, so a higher note is never left of a lower one (mm_lanes_fit). It sounds as written
 *   CHROMATIC  GUITAR, BASS, and KEYS' 1 OCTAVE: 12 lanes, C to B: a note's lane is its name, whatever its octave; any key
 *              of that name plays it, its light on C4 .. B4
 *   KEYS       27 KEYS: the keyboard's 27 lanes, the chart's own key
 *   KEYS49     49 KEYS: C2 .. C6, a note on its pitch folded in (mm_fold49); a panel key k plays 53 + k, OCT- and OCT+
 *              an octave down or up (song.octave), a MIDI note its own (folded); lights on the panel keys under them
 * Two of your notes on one key within MM_LANE_MERGE are one to play (a chord on a string, an octave folded onto the
 * same key, two drums on one pad): the later one is the device's (mm_lane_dup), not judged */
enum { MM_VIEW_STRINGS, MM_VIEW_CHROMATIC, MM_NVIEWS, MM_VIEW_KEYS = MM_NVIEWS, MM_VIEW_KEYS49 };
#define MM_LANES_MAX 12u
static const uint8_t MM_STRING_KEY[6] = {0, 2, 4, 6, 7, 9};   /* lane l's key: the white keys F3 G3 A3 B3 C4 D4 */
#define MM_OCT_KEY0 7u                                        /* CHROMATIC: C's key, C4 (lane l: key 7 + l) */
#define MM_LANE_MERGE (CHART_FS * 30u / 1000u)                /* (the judge's PERFECT window) */
static int mm_has_view(uint32_t mode) { return mode == MM_GUITAR || mode == MM_BASS; }
static uint32_t mm_view(void)
{
    uint32_t st = mm_style();
    if (mm_has_view(pl.mode))
        return st ? MM_VIEW_CHROMATIC : MM_VIEW_STRINGS;
    if (pl.mode == MM_KEYS)
        return st == KS_49 ? MM_VIEW_KEYS49 : st == KS_OCT ? MM_VIEW_CHROMATIC : MM_VIEW_KEYS;
    return MM_VIEW_KEYS;                                  /* (DRUMS: its pads, graph_drums) */
}
static uint32_t mm_string_n(void)                        /* STRINGS' lanes: AUTO's 6 4 2 (BASS 4 2) */
{
    static const uint8_t G[3] = {6, 4, 2}, B[2] = {4, 2};
    return pl.mode == MM_BASS ? B[mm_range() % 2u] : G[mm_range() % 3u];
}
static uint32_t mm_lanes(void)                   /* your part's lanes in a lane view; 0: a keyboard's (or DRUMS) */
{
    uint32_t v = mm_view();
    return v == MM_VIEW_STRINGS ? mm_string_n() : v == MM_VIEW_CHROMATIC ? 12u : 0u;
}
static uint32_t mm_lane_key(uint32_t l)          /* lane l's key */
{
    return mm_view() == MM_VIEW_CHROMATIC ? MM_OCT_KEY0 + l % 12u : MM_STRING_KEY[l % 6u];
}
static int32_t mm_key_lane(uint32_t k)           /* the lane key k plays; -1: none */
{
    uint32_t l, n = mm_lanes();
    if (mm_view() == MM_VIEW_CHROMATIC)
        return k < CHART_NKEYS ? (int32_t)((k + 5u) % 12u) : -1;   /* (key 0 is F3: F is 5 above C) */
    for (l = 0; l < n; l++)
        if (MM_STRING_KEY[l] == k)
            return (int32_t)l;
    return -1;
}
static uint32_t mm_key0(void) { return (uint32_t)(53 + 12 * (int32_t)song.octave); }   /* 49 KEYS: panel key 0's note */
/* STRINGS: each pitch's lane for the song shown, your part and STYLE and AUTO (MM_LANE_NONE: a pitch none of your notes
 * has). Made in the main loop (mm_lanes_fit: once one of them changes); the audio ISR reads it */
#define MM_LANE_NONE 0xFFu
static uint8_t mm_str_lane[128];
static uint32_t mm_lanes_sig;
static void mm_lanes_fit(void)
{
    const mm_song_t *s = pl_song();
    uint32_t sig = (uint32_t)(uintptr_t)s * 31u + pl.mode * 7u + mm_range() * 131u + mm_view() * 1031u + mm_lanes() * 8191u +
                   s->nnote * 17u + 1u;
    uint32_t i, p, n = 0, r = 0, lanes = mm_lanes(), seen[4] = {0, 0, 0, 0};
    if (sig == mm_lanes_sig)
        return;
    mm_lanes_sig = sig;
    for (i = 0; i < s->nnote; i++)
        if (mm_mine(&s->note[i]) && !(s->note[i].flags & CN_DRUM))
            seen[(s->note[i].pitch >> 5) & 3u] |= 1u << (s->note[i].pitch & 31u);
    for (p = 0; p < 128u; p++)
        n += (seen[p >> 5] >> (p & 31u)) & 1u;
    for (p = 0; p < 128u; p++) {
        if (!((seen[p >> 5] >> (p & 31u)) & 1u) || !lanes) {
            mm_str_lane[p] = MM_LANE_NONE;
            continue;
        }
        /* rank r of n onto the lanes: n <= lanes spread from the first to the last (rounded); more, equal shares */
        mm_str_lane[p] = (uint8_t)(n <= lanes ? (n > 1u ? (2u * r * (lanes - 1u) + (n - 1u)) / (2u * (n - 1u)) : (lanes - 1u) / 2u)
                                              : r * lanes / n);
        r++;
    }
}
static uint32_t mm_note_lane(const chart_note_t *c)   /* your note c's lane in a lane view */
{
    if (mm_view() == MM_VIEW_CHROMATIC)
        return c->pitch % 12u;
    return mm_str_lane[c->pitch & 127u] == MM_LANE_NONE ? 0u : mm_str_lane[c->pitch & 127u];
}
/* your note c on the key it sits on now (the judge's): a lane's key (a lane view), its folded note (49 KEYS: 36 .. 84),
 * its pad (DRUMS), else the chart's own */
static uint32_t mm_note_key(const chart_note_t *c)
{
    if (c->flags & CN_DRUM)
        return pl_drum_key(c->pitch);
    if ((c->flags & CN_PART_MASK) == pl.mode) {
        if (mm_lanes())
            return mm_lane_key(mm_note_lane(c));
        if (mm_view() == MM_VIEW_KEYS49)
            return mm_fold49(c->pitch);
    }
    return c->key;
}
/* the panel key whose light shows note c (49 KEYS: under the window OCT has put the keys on; none off it: 0xFF) */
static uint32_t mm_led_key(const chart_note_t *c)
{
    uint32_t k = mm_note_key(c);
    if (!(c->flags & CN_DRUM) && (c->flags & CN_PART_MASK) == pl.mode && mm_view() == MM_VIEW_KEYS49)
        return k >= mm_key0() && k - mm_key0() < CHART_NKEYS ? k - mm_key0() : 0xFFu;
    return k;
}
/* c the later of two of your notes on one key within MM_LANE_MERGE (one to play, the first): a chord on a string,
 * an octave folded onto one key, and DRUMS' two hits on one pad (two GM drums of one lane at once: kick 35 + 36, hats
 * 42 + 44, two toms; a flam). One press can only be one key-down, so the twin would be a MISS under its PERFECT; it
 * is the device's instead (played for you) */
static int mm_lane_dup(const chart_note_t *c)
{
    const mm_song_t *s = pl_song();
    const chart_note_t *b;
    uint32_t k;
    if (c < s->note || c >= s->note + s->nnote)
        return 0;
    k = mm_note_key(c);
    for (b = c; b-- > s->note && c->time - b->time <= MM_LANE_MERGE;)
        if (mm_mine(b) && !((b->flags ^ c->flags) & CN_DRUM) && mm_note_key(b) == k)
            return 1;
    return 0;
}
static int mm_yours(const chart_note_t *c) { return mm_mine(c) && !mm_lane_dup(c); }   /* yours to play */
/* a panel key pressed on your part as the key the notes are on (a lane view: its lane's key, any C is C's; 49 KEYS:
 * its note under OCT); -1: none */
static int32_t mm_press_key(uint32_t k)
{
    int32_t l;
    if (mm_view() == MM_VIEW_KEYS49 && pl.mode == MM_KEYS) {
        uint32_t p = mm_key0() + k;
        return p >= 36u && p <= 84u ? (int32_t)p : -1;
    }
    if (!mm_lanes())
        return (int32_t)k;
    l = mm_key_lane(k);
    return l < 0 ? -1 : (int32_t)mm_lane_key((uint32_t)l);
}

/* ---- the score (PLAY YOU): judge.c over your notes, the ones HOME draws (your part, your RANGE, on a key or a pad).
 * A song started with PLAY YOU is judged (pl_sc.on): a key pressed on your part takes the note it meets
 * (player_judge_key: PERFECT 30 ms, GREAT 60, GOOD 110 either side; GAME's OFFSET moves the presses), a note nobody
 * pressed is a MISS once its window has passed (player_block). HOME shows the last grade, the combo and the score
 * (ui_graph.c nt_score); when the song ends, the result (pl_sc.done: ui_graph.c graph_result) until the next one */
#include "judge.c"
#define PL_GRADE_MAX (MM_SONG_SIZE / (uint32_t)sizeof(chart_note_t))   /* the most notes a song can hold */
static uint8_t pl_grade[PL_GRADE_MAX] __attribute__((section(".pool")));
static judge_t pl_judge;
static struct {
    volatile uint8_t on;                 /* ISR: this song is judged (YOU at its start) */
    volatile uint8_t done;               /* ISR: it played to its end: the result to show (ui_graph.c graph_result) */
    volatile uint8_t last;               /* ISR: the last grade, J_PERFECT .. J_MISS */
    volatile int16_t dt_ms;              /* ISR: its press minus its note, ms (negative: early; a MISS: 0) */
    volatile uint32_t seq;               /* ISR: +1 a grade (the UI shows each new one) */
} pl_sc;
/* The press's time on the song clock. On the FM-1 (midimania.c MM_KEY_TICKS: seq.c passes the key scan's TIMER4 tick
 * of the key closing, fm1_in.note_t0, ~0.1 ms) it is placed between the clock's steps: pl.pos was set at TIMER4 tick pl_tick (player_block), and
 * the song runs at pl.rate, so the press at tick t0 is pl.pos + (t0 - pl_tick) of song time (t0 a moment before
 * pl_tick, or after it). Without a tick (MIDI IN, the host builds), or a tick out of reach (more than PL_T0_MAX from
 * pl_tick: a stale one), pl.pos: the audio block the press arrived in (2.9 ms) */
#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif
#if MM_KEY_TICKS
#define PL_NOW_TICK() fm1_ticks()
#else
#define PL_NOW_TICK() 0u
#endif
#define PL_T0_MAX (FM1_TICKS_PER_US * 8000u)     /* 8 ms of ticks */
static uint32_t pl_tick;                         /* ISR: the tick pl.pos was set at (0: none) */
static uint32_t pl_press_pos(uint32_t t0)
{
    int32_t d = (int32_t)(t0 - pl_tick);
    int64_t s;
    if (!t0 || !pl_tick || d > (int32_t)PL_T0_MAX || d < -(int32_t)PL_T0_MAX)
        return pl.pos;
    s = (int64_t)d * (int64_t)CHART_FS / (int64_t)(FM1_TICKS_PER_US * 1000000u);   /* real samples */
    if (!pl.free)                                                                  /* (FREE PLAY: real time) */
        s = s * (int64_t)pl.rate / (int64_t)PL_RATE_ONE;                           /* song samples (TEMPO) */
    return s < 0 && (uint32_t)-s > pl.pos ? 0u : (uint32_t)((int64_t)pl.pos + s);
}
static int pl_judge_mine(const chart_note_t *c)
{
    return mm_yours(c) && ((c->flags & CN_DRUM) || mm_lanes() || mm_view() == MM_VIEW_KEYS49 || c->key < CHART_NKEYS);
}
static uint32_t pl_judge_key(const chart_note_t *c) { return mm_note_key(c); }
static int32_t pl_judge_offset(void) { return (int32_t)pl.offset * (int32_t)CHART_FS / 500; }   /* (2 ms a step) */
static void pl_judge_start(const mm_song_t *s)   /* ISR: song s starts */
{
    judge_init(&pl_judge, s->note, s->nnote < PL_GRADE_MAX ? s->nnote : PL_GRADE_MAX, pl_grade);
    pl_judge.mine = pl_judge_mine;
    pl_judge.key = pl_judge_key;
    pl_judge.offset = pl_judge_offset();
    pl_sc.last = J_NONE;
    pl_sc.dt_ms = 0;
    pl_sc.on = pl.you;
}
static void pl_judge_tick(uint32_t now)          /* ISR: the notes whose window has passed: MISS */
{
    uint32_t miss = pl_judge.count[J_MISS];
    pl_judge.offset = pl_judge_offset();         /* (OFFSET turned while playing) */
    judge_tick(&pl_judge, now);
    if (pl_judge.count[J_MISS] != miss) {
        pl_sc.last = J_MISS;
        pl_sc.dt_ms = 0;
        pl_sc.seq++;
    }
}
/* GAME's CALIBRATE (ui_songs.c, OCT+): a click every CAL_PERIOD (100 BPM) on FREE PLAY's clock, and your taps (any
 * key) against them. After CAL_SKIP clicks (to find the beat), each tap within a third of a beat of a click counts:
 * its time minus the click's. CAL_TAPS of them, or CAL_CLICKS clicks, and it ends (done); the main loop sets OFFSET
 * from their average (player_cal_result). The click sounds at the start of its block (click.c), so its time is that */
#define CAL_PERIOD (CHART_FS * 6u / 10u)
#define CAL_SKIP 4u
#define CAL_TAPS 12u
#define CAL_CLICKS 40u
static struct {
    volatile uint8_t on;                 /* main loop: start (player_cal_start); ISR: off when done */
    volatile uint8_t done;               /* ISR: finished with CAL_TAPS taps (the main loop takes the result) */
    volatile uint8_t taps, clicks;       /* ISR: taps counted, clicks sounded */
    volatile int32_t sum;                /* ISR: the taps' time minus their click's, samples */
    uint32_t next, last;                 /* ISR: the next click's time, the last one's (FREE PLAY's pl.pos) */
} pl_cal;
static void pl_cal_block(uint32_t from, uint32_t to)   /* ISR, FREE PLAY: song samples [from, to) */
{
    if (pl_cal.next >= to)
        return;
    click_req = (uint8_t)(pl_cal.clicks % 4u ? 1u : 2u);
    pl_cal.last = from;
    pl_cal.next += CAL_PERIOD;
    if (++pl_cal.clicks >= CAL_CLICKS)
        pl_cal.on = 0;                           /* (too few taps: not done, OFFSET left as it was) */
}
static void pl_cal_press(uint32_t t0)            /* ISR: a key down */
{
    uint32_t t, c;
    int32_t d;
    if (!pl_cal.on || !pl.free || pl_cal.clicks <= CAL_SKIP)
        return;
    t = pl_press_pos(t0);
    c = t >= pl_cal.last + CAL_PERIOD / 2u ? pl_cal.last + CAL_PERIOD : pl_cal.last;   /* the click it is nearest */
    d = (int32_t)(t - c);
    if (d > (int32_t)(CAL_PERIOD / 3u) || d < -(int32_t)(CAL_PERIOD / 3u))
        return;
    pl_cal.sum += d;
    if (++pl_cal.taps >= CAL_TAPS) {
        pl_cal.on = 0;
        pl_cal.done = 1;
    }
}
/* main loop */
static void player_free(void);
static void player_stop(void);
static void player_cal_start(void)
{
    pl_cal.on = 0;
    pl_cal.done = 0;
    pl_cal.taps = 0;
    pl_cal.clicks = 0;
    pl_cal.sum = 0;
    pl_cal.next = CHART_FS / 2u;                 /* (the first click half a second in) */
    pl_cal.last = 0;
    player_free();
    pl_cal.on = 1;
}
/* the taps' average, ms (+: late): OFFSET is its opposite (judge.c adds OFFSET to every press) */
static int32_t player_cal_ms(void)
{
    int32_t a = pl_cal.taps ? pl_cal.sum / (int32_t)pl_cal.taps : 0;
    return (int32_t)((int64_t)a * 1000 / (int32_t)CHART_FS);
}

/* ISR: the key the notes are on (mm_note_key's) pressed on part, at TIMER4 tick t0 (0: unknown), judged */
static void player_judge_pk(uint32_t part, uint32_t pk, uint32_t t0)
{
    int32_t dt = 0;
    uint32_t g;
    if (!pl_sc.on || !pl.playing || pl.free || pl.pre || part != pl.mode % NPART)
        return;
    g = judge_press(&pl_judge, pk, pl_press_pos(t0), &dt);
    if (g == J_NONE)
        return;                                  /* (no note of yours near: not counted) */
    pl_sc.last = (uint8_t)g;
    pl_sc.dt_ms = (int16_t)((int64_t)dt * 1000 / (int32_t)CHART_FS);
    pl_sc.seq++;
}
/* ISR (seq.c keyboard_block; MIDI IN, mm_midi_note): panel key k pressed on part, at TIMER4 tick t0 (0: unknown) */
static void player_judge_key(uint32_t part, uint32_t k, uint32_t t0)
{
    int32_t pk;
    pl_cal_press(t0);
    if ((pk = mm_press_key(k)) >= 0)              /* (a lane view: a key with no lane, none) */
        player_judge_pk(part, (uint32_t)pk, t0);
}
/* the result: accuracy, 0..100 % (a PERFECT for every note: 100) */
static uint32_t pl_judge_accuracy(void)
{
    uint32_t n = pl_judge.count[J_PERFECT] + pl_judge.count[J_GREAT] + pl_judge.count[J_GOOD] + pl_judge.count[J_MISS];
    return n ? (pl_judge.score * 100u + n * 3u / 2u) / (n * 3u) : 0u;
}

static void pl_off(uint32_t i)                     /* sounding note i ends */
{
    if (!pl.on[i].quiet)
        trk_note_off(&trk[pl.on[i].part & 3u], pl.on[i].pitch);
    pl.on[i] = pl.on[--pl.on_n];
}

static void pl_all_off(void)
{
    while (pl.on_n)
        pl_off(pl.on_n - 1u);
}

static void pl_note(const chart_note_t *c)       /* chart note c starts now */
{
    uint32_t part = c->flags & CN_PART_MASK, i;
    for (i = 0; i < pl.on_n; i++)                  /* the same note again on its part: the old one ends first */
        if (pl.on[i].part == part && pl.on[i].pitch == c->pitch) {
            pl_off(i);
            break;
        }
    if (pl.on_n == PL_ON_MAX) {                    /* full (never with a converted chart): the oldest ends */
        uint32_t k, old = 0;
        for (k = 1; k < pl.on_n; k++)
            if (pl.on[k].off < pl.on[old].off)
                old = k;
        pl_off(old);
    }
    pl.on[pl.on_n].quiet = (uint8_t)(pl.you && mm_yours(c));  /* YOU: your note, the keys play it (a lane view's
                                                  * merged one: the device's, mm_lane_dup) */
    if (!pl.on[pl.on_n].quiet)
        trk_note_on(&trk[part], c->pitch, c->vel);
    pl.on[pl.on_n].off = c->time + (c->len ? c->len : 1u);
    pl.on[pl.on_n].part = (uint8_t)part;
    pl.on[pl.on_n].pitch = c->pitch;
    pl.on[pl.on_n].key = (uint8_t)mm_led_key(c);    /* (your part in a lane view: its lane's key lights) */
    if (part == pl.mode && !mm_in_range(c))
        pl.on[pl.on_n].key = 0xFFu;               /* (your part out of your RANGE: no key lights for it) */
    pl.on_n++;
}

static void pl_keys(void)                        /* pl.keys from the notes sounding */
{
    uint32_t i, m[CHART_NPART] = {0, 0, 0, 0};
    for (i = 0; i < pl.on_n; i++)
        if (pl.on[i].key < CHART_NKEYS)
            m[pl.on[i].part & 3u] |= 1u << pl.on[i].key;
    for (i = 0; i < CHART_NPART; i++)
        pl.keys[i] = m[i];
}

/* the count-in's clicks due before song time to (they come in order): the bar's first accented */
static void pl_cin(int32_t to)
{
    const mm_song_t *s = pl_song();
    uint32_t bpb = s->bpb ? s->bpb : 4u;
    while (pl.cin_k < pl.cin_n && pl.cin_c0 + (int32_t)(pl.cin_k * pl.cin_len) < to) {
        click_req = (uint8_t)(pl.cin_k % bpb ? 1u : 2u);
        pl.cin_k++;
    }
}

/* audio ISR, once a block of n samples (seq.c events_block) */
static void player_block(uint32_t n)
{
    const mm_song_t *s;
    uint32_t end, i, adv;
    if (pl.req) {
        uint32_t r = pl.req;
        pl_all_off();
        pl.playing = 0;
        pl.free = 0;
        pl_sc.on = 0;                              /* (a stop, a new song: the score's over; the result gone) */
        pl_sc.done = 0;
        if (r != PL_REQ_START || pl.req_song != PL_FREE)
            pl_cal.on = 0;                         /* (the calibration runs on FREE PLAY's clock: stopped with it) */
        pl_tick = 0;
        if (r == PL_REQ_START && pl.req_song == PL_FREE) {   /* FREE PLAY: the clock alone (HOME draws your presses) */
            pl.free = 1;
            pl.pos = 0;
            pl.frac = 0;
            pl.pre = 0;
            pl.cin_n = 0;
            pl.playing = 1;
        } else if (r == PL_REQ_START && pl.req_song < mm_nsongs()) {
            pl.song = pl.req_song;
            pl.pos = 0;
            pl.frac = 0;
            pl.next = 0;
            pl.nb = 0;
            pl.playing = 1;
            {   /* COUNT-IN: cin_bars bars of the song's first beat, before it */
                const mm_song_t *c = pl_song();
                uint32_t b0 = c->nbeat ? c->beat[0] : 0u;
                pl.cin_len = c->nbeat > 1u ? c->beat[1] - c->beat[0] : CHART_FS / 2u;
                pl.cin_n = cin_bars * (c->bpb ? c->bpb : 4u);
                pl.cin_k = 0;
                pl.cin_c0 = (int32_t)b0 - (int32_t)(pl.cin_n * pl.cin_len);
                pl.pre = pl.cin_c0 < 0 ? (uint32_t)-pl.cin_c0 : 0u;
            }
            pl_judge_start(pl_song());             /* PLAY YOU: scored */
        }
        pl.req = PL_REQ_NONE;
        pl_keys();
    }
    if (!pl.playing)
        return;
    if (pl.free) {                                 /* FREE PLAY: real time, whatever TEMPO says */
        uint32_t from = pl.pos;
        pl.pos += n;
        pl_tick = PL_NOW_TICK();
        if (pl_cal.on)
            pl_cal_block(from, pl.pos);            /* (GAME's CALIBRATE: its clicks) */
        return;
    }
    s = pl_song();
    pl.frac += n * pl.rate;                        /* the tempo: song samples this block (n * rate, Q16) */
    adv = pl.frac >> 16;
    pl.frac &= 0xFFFFu;
    if (pl.pre) {                                  /* COUNT-IN's pre-roll: its clicks, the song held at 0 */
        uint32_t d = adv < pl.pre ? adv : pl.pre;
        pl_cin(-(int32_t)pl.pre + (int32_t)d);
        pl.pre -= d;
        adv -= d;
        if (pl.pre)
            return;
    }
    end = pl.pos + adv;
    pl_cin((int32_t)end);                          /* (its clicks after the song's start, before its first beat) */
    for (i = pl.on_n; i-- > 0;)                    /* the note-offs of this block */
        if (pl.on[i].off < end)
            pl_off(i);
    while (pl.next < s->nnote && s->note[pl.next].time < end)
        pl_note(&s->note[pl.next++]);
    while (pl.nb < s->nbeat && s->beat[pl.nb] < end) {   /* the metronome (MENU > CLICK ON): the song's beats, the */
        if (click_mode == CLICK_ON)                   /* bar's first accented (click.c sounds it in this block) */
            click_req = (uint8_t)((s->beat0 + pl.nb) % (s->bpb ? s->bpb : 4u) ? 1u : 2u);
        pl.nb++;
    }
    pl.pos = end;
    pl_tick = PL_NOW_TICK();                       /* (a press between two blocks: player_judge_key, pl_press_pos) */
    pl_keys();
    if (pl_sc.on)
        pl_judge_tick(end);
    if (pl.next >= s->nnote && !pl.on_n) {         /* the last note has ended: done */
        pl.playing = 0;
        if (pl_sc.on) {                            /* (the notes still pending: MISS), the result shown */
            pl_judge_tick(end + CHART_FS);
            pl_sc.on = 0;
            pl_sc.done = 1;
        }
    }
}

/* MIDI IN on your part (midi_control.c, in the audio ISR): the keys its notes would sit on, for FREE PLAY (ui_graph.c
 * free_track draws them as it does the panel's keys, and lights them on the strip). A note's key: its pitch folded
 * into F3..G5 (as tools/gen_songs.py places a song's); on DRUMS, the pad of its lane */
static volatile uint32_t mm_midi_keys;           /* bit k: a MIDI note held on key k */
static volatile uint64_t mm_midi_p49;            /* 49 KEYS: bit p - 36: a MIDI note held on C2 + p (folded) */
static uint8_t mm_midi_n[CHART_NKEYS];           /* .. how many */
static uint8_t mm_midi_k[128];                   /* a MIDI note's key + 1 while held (0: not counted) */
static void mm_midi_note(uint32_t part, uint32_t note, int on)
{
    uint32_t k;
    note &= 127u;
    if (!on) {
        if (!mm_midi_k[note])
            return;
        mm_midi_p49 &= ~((uint64_t)1 << (mm_fold49(note) - 36u));
        k = mm_midi_k[note] - 1u;
        mm_midi_k[note] = 0;
        if (mm_midi_n[k] && !--mm_midi_n[k])
            mm_midi_keys &= ~(1u << k);
        return;
    }
    if (part != pl.mode % NPART || mm_midi_k[note])
        return;
    if (pl.mode == MM_DRUMS) {
        k = MM_DRUM_KEY[mm_drum_pos(drum_lane(note) % NLANE)];
    } else {
        int32_t n = (int32_t)note;
        while (n < 53)
            n += 12;
        while (n > 53 + (int32_t)CHART_NKEYS - 1)
            n -= 12;
        k = (uint32_t)(n - 53);
    }
    mm_midi_k[note] = (uint8_t)(k + 1u);
    mm_midi_n[k]++;
    mm_midi_keys |= 1u << k;
    if (pl.mode == MM_KEYS && mm_view() == MM_VIEW_KEYS49) {   /* 49 KEYS: a MIDI keyboard plays every key, its own */
        mm_midi_p49 |= (uint64_t)1 << (mm_fold49(note) - 36u);
        pl_cal_press(0u);
        player_judge_pk(part, mm_fold49(note), 0u);
        return;
    }
    player_judge_key(part, k, 0u);                /* (PLAY YOU: a MIDI keyboard's notes are judged as the keys') */
}

/* main loop */
static void player_start(uint32_t song) { pl.req_song = (uint8_t)song; pl.req = PL_REQ_START; }
static void player_free(void) { pl.req_song = PL_FREE; pl.req = PL_REQ_START; }   /* FREE PLAY */
static int player_is_free(void) { return pl.req == PL_REQ_START ? pl.req_song == PL_FREE : pl.playing && pl.free; }
static void player_stop(void) { pl.req = PL_REQ_STOP; }
static int player_busy(void) { return pl.playing || pl.req == PL_REQ_START; }
static uint32_t player_keys(uint32_t part) { return pl.playing ? pl.keys[part & 3u] : 0u; }   /* its keys sounding now */
/* the tempo: bpm_x10 against the song's own (the ISR takes it at the next block, while playing too) */
static void player_tempo(const mm_song_t *s, uint32_t bpm_x10)
{
    pl.rate = s->bpm_x10 ? (uint32_t)((uint64_t)bpm_x10 * PL_RATE_ONE / s->bpm_x10) : PL_RATE_ONE;
}

/* the first note of song s that may still be on screen at time t: its start at or after t - back (by time: a
 * binary search) */
static uint32_t player_find(const mm_song_t *s, uint32_t t, uint32_t back)
{
    uint32_t lo = 0, hi = s->nnote, from = t > back ? t - back : 0u;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (s->note[mid].time < from)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

/* PLAY YOU (seq.c kb_map, the audio ISR): key k pressed on part (the keys play your part): the song's note it meets,
 * yours (mm_mine) on that key, the nearest within PL_KEY_WIN of now (the judge's GOOD window, judge.c), so it sounds
 * as the song has it (its pitch: the key's lane folds it into the 27 keys; a drum's own sound in its lane); -1: none
 * near (the key plays its own note) */
#define PL_KEY_WIN (CHART_FS * 110u / 1000u)
static int32_t player_key_note(uint32_t part, uint32_t k)
{
    const mm_song_t *s;
    uint32_t i, pos, best = ~0u;
    int32_t note = -1, pk;
    if (!pl.playing || pl.free || !pl.you || pl.pre || part != pl.mode)
        return -1;
    if ((pk = mm_press_key(k)) < 0)
        return -1;                                /* (a lane view: a key with no lane plays its own note) */
    s = pl_song();
    pos = pl.pos;
    for (i = player_find(s, pos, PL_KEY_WIN); i < s->nnote && s->note[i].time <= pos + PL_KEY_WIN; i++) {
        const chart_note_t *c = &s->note[i];
        uint32_t d;
        if (mm_note_key(c) != (uint32_t)pk || !mm_yours(c))
            continue;
        d = c->time > pos ? c->time - pos : pos - c->time;
        if (d < best) {
            best = d;
            note = (int32_t)c->pitch;
        }
    }
    return note;
}

/* the beat the song is on (the play screen's pulse, ui_songs.c): song samples since it, *bar 1 when it begins a bar;
 * none yet (or stopped): ~0u */
static uint32_t player_beat(uint32_t *bar)
{
    const mm_song_t *s = pl_song();
    uint32_t b = pl.nb, pos = pl.pos;
    *bar = 0;
    if (!pl.playing || pl.free || !b || b > s->nbeat || pl.pre)
        return ~0u;
    b--;
    *bar = (s->beat0 + b) % (s->bpb ? s->bpb : 4u) == 0u;
    return pos >= s->beat[b] ? pos - s->beat[b] : 0u;
}
/* the beat sounding, in its bar (0: the bar's first); -1: none (stopped, counting in, before the first) */
static int32_t player_beat_in_bar(void)
{
    const mm_song_t *s = pl_song();
    uint32_t b = pl.nb;
    if (!pl.playing || pl.free || !b || b > s->nbeat || pl.pre)
        return -1;
    return (int32_t)((s->beat0 + b - 1u) % (s->bpb ? s->bpb : 4u));
}
