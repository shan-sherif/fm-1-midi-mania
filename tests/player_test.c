/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* firmware/src/player.c on the host: a built-in song (build/gen/midimania_songs.h, from midi/) through the real audio
 * path (mix_block -> events_block -> player_block):
 *   - every note starts in the block its time falls in, in order;
 *   - at most 8 voices sound at once, and the song ends with every note let go;
 *   - STOP lets every note go at once; START again plays from the top.
 *   cc -O1 -std=gnu99 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
 *      tests/player_test.c -o build/player_test -lm && build/player_test */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../platforms/host/firmware.c"
#include "../platforms/host/tracks.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("player: %-68s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out[2 * CTL];
static uint32_t voices_now(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].gate;
    return n;
}

/* song s as a chart (chart.h, docs/CHART_FORMAT.md) at off in a song region: its size; live 0xFF, or 0x00 deleted */
static uint32_t put_chart(uint8_t *region, uint32_t off, const mm_song_t *s, uint8_t live)
{
    chart_head_t h;
    uint32_t nb = s->nbeat * 4u, nn = s->nnote * (uint32_t)sizeof *s->note, i;
    memset(&h, 0, sizeof h);
    h.magic = CHART_MAGIC;
    h.version = CHART_VERSION;
    h.head_size = sizeof h;
    h.nbeat = s->nbeat;
    h.nnote = s->nnote;
    h.length = s->length;
    h.bpm_x10 = s->bpm_x10;
    h.beats_per_bar = s->bpb;
    for (i = 0; i < 24u && s->title[i]; i++)
        h.title[i] = s->title[i];
    memcpy(h.part, s->part, sizeof h.part);
    h.beat0 = s->beat0;
    h.parts = s->parts;
    h.live = live;
    memcpy(region + off + sizeof h, s->beat, nb);
    memcpy(region + off + sizeof h + nb, s->note, nn);
    h.crc32 = mm_crc32(region + off + sizeof h, nb + nn);
    memcpy(region + off, &h, sizeof h);
    return (uint32_t)sizeof h + nb + nn;
}

int main(int argc, char **argv)
{
    const mm_song_t *s;
    uint32_t blocks, b, i, late = 0, order = 1, peak = 0, played, prev = 0;
    if (!MM_NSONGS) {
        printf("player: no songs in midi/: skipped\n");
        return 0;
    }
    s = &MM_SONGS[0];
    printf("player: song 0 \"%s\": %u notes, %.1f s\n", s->title, (unsigned)s->nnote, s->length / (double)CHART_FS);
    host_tracks_init();
    for (i = 0; i < CHART_NPART; i++)
        if ((s->parts >> i) & 1u)
            host_preset(&trk[i], s->part[i].engine, s->part[i].preset);
    for (i = 1; i < s->nnote; i++)
        order &= s->note[i].time >= s->note[i - 1].time;
    check("the chart's notes are in time order", order);

    player_start(0);
    check("START: busy at once (before the ISR took it)", player_busy());
    blocks = s->length / CTL + 2u;
    for (b = 0; b < blocks + 400u && (pl.playing || pl.req); b++) {
        uint32_t end;
        mix_block(out, CTL);
        end = (b + 1u) * CTL;                          /* every note with time < end has started */
        while (prev < pl.next) {
            if (s->note[prev].time >= end || s->note[prev].time < end - CTL)   /* not in [end - CTL, end) */
                late++;
            prev++;
        }
        if (voices_now() > peak)
            peak = voices_now();
        if (pl.playing && pl.pos != end)
            late++;
    }
    played = pl.next;
    check("every note of the song started", played == s->nnote);
    check("each in the block its time falls in", late == 0u);
    check("at most 8 voices held at once", peak <= NVOICE);
    check("the song ended by itself", !pl.playing && !player_busy());
    check("and let every note go", pl.on_n == 0u);

    player_start(0);
    for (b = 0; b < (s->note[0].time + CHART_FS * 10u) / CTL && !pl.on_n; b++)   /* to its first notes */
        mix_block(out, CTL);
    check("START again: plays from the top", pl.playing && pl.pos == b * CTL && pl.next > 0u &&
                                              s->note[0].time < pl.pos && s->note[0].time >= pl.pos - CTL);
    check("notes sounding at its first note", pl.on_n > 0u);
    player_stop();
    mix_block(out, CTL);
    check("STOP: nothing plays, every note let go", !pl.playing && pl.on_n == 0u);
    for (b = 0; b < 200u; b++)
        mix_block(out, CTL);
    check("..and no voice is held after it", voices_now() == 0u);

    /* PLAY YOU's score (player.c pl_sc, judge.c): every note of yours pressed in the block it starts in (PERFECT), but
     * every 7th left (MISS); a press near no note counts for nothing; at the end, the result */
    {
        uint32_t mode = pl.mode, k, n = 0, skipped = 0, pressed = 0, run = 0, best = 0, seq0, extra;
        for (k = 0; k < s->nnote; k++)
            n += pl_judge_mine(&s->note[k]);
        pl.you = 1;
        player_start(0);
        mix_block(out, CTL);
        check("PLAY YOU: the song is judged from its start, nothing graded yet",
              pl_sc.on && !pl_sc.done && judge_total(&pl_judge) == n && n > 0u && !pl_judge.score);
        seq0 = pl_sc.seq;
        player_judge_key(pl.mode, 0, 0u);                  /* (before any note of key 0 is near) */
        extra = pl_sc.seq != seq0 || pl_judge.count[J_PERFECT] + pl_judge.count[J_GREAT] + pl_judge.count[J_GOOD];
        for (b = 0, k = 0; b < blocks + 400u && (pl.playing || pl.req); b++) {
            mix_block(out, CTL);
            for (; k < s->nnote && s->note[k].time < pl.pos; k++) {
                const chart_note_t *c = &s->note[k];
                if (!pl_judge_mine(c))
                    continue;
                if ((pressed + skipped) % 7u == 6u) {   /* left: a MISS once its window passes */
                    skipped++;
                    run = 0;
                    continue;
                }
                player_judge_key(pl.mode, pl_judge_key(c), 0u);
                pressed++;
                if (++run > best)
                    best = run;
            }
        }
        printf("player: PLAY YOU: %u notes, %u pressed: PERFECT %u GREAT %u GOOD %u MISS %u, max combo %u, %u %%\n",
               (unsigned)n, (unsigned)pressed, (unsigned)pl_judge.count[J_PERFECT], (unsigned)pl_judge.count[J_GREAT],
               (unsigned)pl_judge.count[J_GOOD], (unsigned)pl_judge.count[J_MISS], (unsigned)pl_judge.max_combo,
               (unsigned)pl_judge_accuracy());
        check("a press with no note of yours near: not counted", !extra);
        check("every note pressed in its block is PERFECT, every one left a MISS",
              pl_judge.count[J_PERFECT] == pressed && pl_judge.count[J_MISS] == skipped && pressed + skipped == n &&
              !pl_judge.count[J_GREAT] && !pl_judge.count[J_GOOD]);
        /* (a note left becomes a MISS once its window has passed, 110 ms on: the presses before that still add to the
         * combo, so the longest run is at least the one between skips, and not much more) */
        check("the combo: the longest run between misses (and the presses in a miss's window); the score 3 a PERFECT",
              pl_judge.max_combo >= best && pl_judge.max_combo < 2u * best && pl_judge.score == 3u * pressed);
        check("the song ended: its result shown (done), no longer judging; accuracy under 100 %",
              !pl.playing && pl_sc.done && !pl_sc.on && pl_judge_accuracy() == (3u * pressed * 100u + n * 3u / 2u) / (n * 3u) &&
              pl_judge_accuracy() < 100u);
        player_start(0);
        mix_block(out, CTL);
        check("the next song: the result gone, judged afresh", !pl_sc.done && pl_sc.on && !pl_judge.score);
        player_stop();
        mix_block(out, CTL);
        pl.you = 0;
        player_start(0);
        mix_block(out, CTL);
        check("PLAY AUTO: not judged", !pl_sc.on);
        player_stop();
        mix_block(out, CTL);
        pl.mode = (uint8_t)mode;
    }

    /* a press at the key scan's tick (player.c pl_press_pos; the FM-1 build's MM_KEY_TICKS): placed on the song clock
     * between its steps, 24 ticks a microsecond; without a tick, or a stale one, the block's time */
    {
        uint32_t pos0 = pl.pos, tick0 = pl_tick, rate0 = pl.rate, free0 = pl.free;
        int ok;
        pl.pos = 100000u; pl_tick = 5000000u; pl.rate = PL_RATE_ONE; pl.free = 0;
        ok = pl_press_pos(pl_tick + 24000u) == 100044u &&          /* 1 ms after: 44.1 samples later */
             pl_press_pos(pl_tick - 48000u) == 99912u &&           /* 2 ms before: 88.2 samples earlier */
             pl_press_pos(pl_tick + 24u * 9000u) == 100000u &&     /* 9 ms away: stale, the block's time */
             pl_press_pos(0u) == 100000u;                          /* no tick (MIDI IN, host builds) */
        pl.rate = PL_RATE_ONE * 2u;
        ok = ok && pl_press_pos(pl_tick + 24000u) == 100088u;      /* TEMPO x2: twice the song time */
        pl.free = 1;
        ok = ok && pl_press_pos(pl_tick + 24000u) == 100044u;      /* FREE PLAY: real time, whatever TEMPO says */
        check("a press at its key's tick: between the clock's steps (1 ms = 44 samples), TEMPO counted, a stale tick not", ok);
        pl.pos = pos0; pl_tick = tick0; pl.rate = rate0; pl.free = (uint8_t)free0;
    }

    /* GAME's CALIBRATE (player.c pl_cal): clicks at 100 BPM on FREE PLAY's clock; taps 20 ms after each click (in
     * the first block at or past it) give an average near 20 ms, done after 12; a stop ends it */
    {
        uint32_t clicks = 0, want = 0, presses = 0;
        int32_t ms;
        player_cal_start();
        for (b = 0; b < (CHART_FS * 20u) / CTL && !pl_cal.done; b++) {
            mix_block(out, CTL);
            if (pl_cal.clicks != clicks) {             /* a click sounded at pl_cal.last: tap 20 ms after it */
                clicks = pl_cal.clicks;
                want = pl_cal.last + CHART_FS / 50u;
            }
            if (want && pl.pos >= want) {
                player_judge_key(pl.mode, 0, 0u);
                presses++;
                want = 0;
            }
        }
        ms = player_cal_ms();
        printf("player: CALIBRATE: %u clicks, %u taps of %u, average %d ms\n", (unsigned)pl_cal.clicks,
               (unsigned)pl_cal.taps, (unsigned)presses, (int)ms);
        check("CALIBRATE: the first 4 clicks to listen, then 12 taps counted: done, off",
              pl_cal.done && !pl_cal.on && pl_cal.taps == CAL_TAPS && pl_cal.clicks == CAL_SKIP + CAL_TAPS);
        check("CALIBRATE: taps 20 ms after the clicks average 20 ms (within a block)", ms >= 20 && ms <= 22);
        player_cal_start();
        for (b = 0; b < 10u; b++)
            mix_block(out, CTL);
        player_stop();
        mix_block(out, CTL);
        check("CALIBRATE stopped (a stop): off, not done, FREE PLAY's clock stopped", !pl_cal.on && !pl_cal.done && !pl.playing);
    }

    /* the tempo: song time runs at bpm / the song's own */
    {
        uint32_t want;
        player_tempo(s, s->bpm_x10 * 2u);
        player_start(0);
        for (b = 0; b < 1000u; b++)
            mix_block(out, CTL);
        check("tempo x2: the song runs twice as fast", pl.pos == 2u * 1000u * CTL);
        player_tempo(s, s->bpm_x10 * 3u / 4u);
        want = pl.pos + 1000u * CTL * 3u / 4u;
        for (b = 0; b < 1000u; b++)
            mix_block(out, CTL);
        check("tempo x0.75, changed while it plays: 3/4 the speed", pl.pos >= want - 1u && pl.pos <= want + 1u);
        player_stop();
        mix_block(out, CTL);
        player_tempo(s, s->bpm_x10);
        check("its own tempo again: PL_RATE_ONE", pl.rate == PL_RATE_ONE);
    }
    /* the drum lanes' keys: the black keys F#3 .. A#4 in HOME's order, BELL OH CH SNR RIM KICK CLAP TOM */
    {
        /* by lane, KICK SNARE CLAP CH OH TOM RIM BELL: their positions 5 3 6 2 1 7 4 0, keys F#3=1 G#3=3 A#3=5 C#4=8
         * D#4=10 F#4=13 G#4=15 A#4=17 */
        static const uint8_t WANT[NLANE] = {13, 8, 15, 5, 3, 17, 10, 1};
        uint32_t l, p, seen = 0, ok = 1;
        for (l = 0; l < NLANE; l++) {
            uint32_t k = pl_drum_key(DRUM_LANE_NOTE[l]);
            ok &= key_black(k) && key_place(k) == mm_drum_pos(l) && k == WANT[l];
        }
        for (p = 0; p < NLANE; p++)
            seen |= 1u << MM_DRUM_ORDER[p];
        check("a drum hit lights the key under its lane: BELL F#3, OH, CH, SNR, RIM, KICK, CLAP, TOM A#4",
              ok && seen == (1u << NLANE) - 1u);
    }
    /* the metronome: CLICK ON clicks the song's beats, each in its block, the bar's first accented; OFF none */
    {
        uint32_t clicks = 0, wrong = 0, k = 0, nb = s->nbeat < 12u ? s->nbeat : 12u;
        int ok = s->nbeat > 0u && s->bpb >= 1u;
        for (b = 1; b < s->nbeat; b++)
            ok &= s->beat[b] > s->beat[b - 1];
        click_mode = CLICK_ON;
        player_start(0);
        for (b = 0; nb && b < s->beat[nb - 1] / CTL + 2u; b++) {
            uint32_t end = (b + 1u) * CTL;
            click_req = 0;
            mix_block(out, CTL);
            if (click_req) {
                uint32_t want = (s->beat0 + k) % s->bpb ? 1u : 2u;
                clicks++;
                wrong += k >= s->nbeat || s->beat[k] >= end || s->beat[k] < end - CTL || click_req != want;
                k++;
            }
        }
        ok &= clicks == nb && !wrong;
        click_mode = CLICK_OFF;
        click_req = 0;
        for (b = 0; b < 200u; b++)
            mix_block(out, CTL);
        ok &= !click_req;
        player_stop();
        mix_block(out, CTL);
        check("metronome (CLICK ON): every beat of the song clicks in its block, the bar's first accented; OFF: none", ok);
    }
    /* PLAY YOU: your part's notes are not played (no voice of your part starts), their keys light all the same; RANGE
     * RIGHT HAND (KEYS GUITAR BASS): no lit key of your part comes from a note below middle C */
    {
        uint32_t first = 0, mode, played = 0, lit = 0, low = 0, i2;
        while (first + 1u < s->nnote && !((MM_PLAYABLE >> (s->note[first].flags & CN_PART_MASK)) & 1u))
            first++;                                  /* (your part: the first note's you can play) */
        mode = s->note[first].flags & CN_PART_MASK;
        pl.mode = (uint8_t)mode;
        pl.you = 1;
        pl.range = mode == MM_DRUMS ? 0u : 1u;
        player_start(0);
        for (b = 0; b < (s->note[first].time + 2u * CHART_FS) / CTL; b++) {   /* (to 2 s past that note) */
            mix_block(out, CTL);
            for (i2 = 0; i2 < NVOICE; i2++)
                played += trk[mode].v[i2].active && trk[mode].v[i2].gate;
            lit |= player_keys(mode);
            for (i2 = 0; i2 < pl.on_n; i2++)
                low += pl.on[i2].part == mode && pl.on[i2].key < CHART_NKEYS && mode != MM_DRUMS &&
                       pl.on[i2].pitch < MM_RANGE_SPLIT[MM_KEYS];
        }
        check("PLAY YOU: your part's notes silent (the keys play them), their keys still lit", !played && lit);
        check("RANGE RIGHT HAND: no key lit for your part's notes below middle C", !low);
        pl.you = 0;
        pl.range = 0;
        player_stop();
        mix_block(out, CTL);
    }
    /* PLAY YOU: a key pressed on time for one of your notes plays the song's note (seq.c kb_map); another part, or
     * AUTO: nothing (the key's own sound) */
    {
        uint32_t mode = 0, i2, k, ok;
        const chart_note_t *c = 0;
        for (i2 = 0; i2 < s->nnote && !((MM_PLAYABLE >> (s->note[i2].flags & CN_PART_MASK)) & 1u); i2++)
            ;                                         /* (your part: the first note's you can play) */
        mode = i2 < s->nnote ? s->note[i2].flags & CN_PART_MASK : 0u;
        pl.mode = (uint8_t)mode;
        pl.you = 1;
        pl.range = 0;
        for (i2 = 0; i2 < s->nnote && !c; i2++)
            if (mm_mine(&s->note[i2]))
                c = &s->note[i2];
        player_start(0);
        for (b = 0; b < CHART_FS * 60u / CTL && (pl.pre || !pl.playing || pl.pos + CTL < c->time); b++)
            mix_block(out, CTL);
        k = (c->flags & CN_DRUM) ? pl_drum_key(c->pitch) : c->key;
        ok = player_key_note(mode, k) == (int32_t)c->pitch && player_key_note((mode + 1u) & 3u, k) < 0;
        pl.you = 0;
        ok &= player_key_note(mode, k) < 0;
        check("PLAY YOU: your key on time plays the song's note; AUTO or another part: not", ok);
        player_stop();
        mix_block(out, CTL);
    }
    /* the DRUMS part's black keys F#3 .. A#4 play the lanes over them on HOME (seq.c kb_map) */
    {
        uint32_t p, ok = 1;
        for (p = 0; p < NLANE; p++) {
            int32_t nt = mm_drum_key_note(MM_DRUM_KEY[p]);
            ok &= nt >= 0 && drum_lane((uint32_t)nt) == MM_DRUM_ORDER[p];
        }
        ok &= mm_drum_key_note(0) < 0 && mm_drum_key_note(2) < 0;   /* (F3, G3: white keys, not lanes) */
        check("DRUMS: the black key under each lane plays it (BELL F#3 .. TOM A#4)", ok);
    }
    /* COUNT-IN 1 BAR (CLICK OFF: it clicks all the same): bpb clicks, the first accented, the last a beat before the
     * song's first beat; while they need more time than the song's start leaves, the song waits at 0 */
    if (s->nbeat > 1u) {
        uint32_t clicks = 0, first = 0, held = 1, last = 0, bpb = s->bpb;
        cin_bars = 1;
        click_mode = CLICK_OFF;
        player_start(0);
        mix_block(out, CTL);                          /* (the request taken) */
        if (click_req) { clicks++; first = click_req; last = 0; }
        click_req = 0;
        for (b = 1; pl.pre || pl.pos < s->beat[0]; b++) {
            if (pl.pre && (pl.pos || pl.next))
                held = 0;
            mix_block(out, CTL);
            if (click_req) { clicks++; last = b; }
            click_req = 0;
        }
        check("COUNT-IN 1 BAR: a bar of clicks, the first accented, before the song's first beat; the song held while they need it",
              clicks == bpb && first == 2u && held && pl.cin_k == bpb &&
              pl.cin_c0 + (int32_t)((bpb - 1u) * pl.cin_len) == (int32_t)s->beat[0] - (int32_t)pl.cin_len);
        (void)last;
        cin_bars = 0;
        player_stop();
        mix_block(out, CTL);
    }
    /* STORED SONGS (player.c mm_song_scan): charts (chart.h) in the song region, each on a 4 KiB sector; listed after
     * the built-in songs if their CRC holds and they are not deleted; read in place; one plays as a built-in one does */
    {
        static uint8_t region[MM_SONG_SIZE];
        const mm_song_t *t;
        uint32_t o1, o2, o3, n0, at3, k3, ok = 1;
        char msg[256];
        memset(region, 0xFF, sizeof region);          /* (erased flash) */
        mm_song_base = region;
        mm_song_scan();
        ok &= mm_nstored == 0u && mm_song_room(1u) == 0 && mm_song_free_sectors() == MM_SONG_NSEC && mm_nsongs() == MM_NSONGS;
        o1 = put_chart(region, 0, s, 0xFFu);
        mm_song_scan();
        t = mm_song(MM_NSONGS);
        ok &= mm_nstored == 1u && mm_nsongs() == MM_NSONGS + 1u && !strncmp(t->title, s->title, 24) && t->nnote == s->nnote &&
              t->nbeat == s->nbeat && t->length == s->length && t->bpm_x10 == s->bpm_x10 && t->bpb == s->bpb &&
              t->beat0 == s->beat0 && t->parts == s->parts && !memcmp(t->part, s->part, sizeof t->part) &&
              !memcmp(t->note, s->note, s->nnote * sizeof *s->note) && !memcmp(t->beat, s->beat, s->nbeat * 4u) &&
              (const uint8_t *)t->note > region && (const uint8_t *)t->note < region + sizeof region &&
              mm_song_room(1u) == (int32_t)((o1 + 0xFFFu) / 0x1000u * 0x1000u);
        check("stored songs: none in an erased region; one chart listed after the built-in songs, as the song it came "
              "from (title, beats, notes, parts), read in place", ok);
        n0 = (uint32_t)mm_song_room(1u);
        o2 = put_chart(region, n0, s, 0x00u);         /* deleted */
        at3 = (n0 + o2 + 0xFFFu) / 0x1000u * 0x1000u;
        o3 = put_chart(region, at3, s, 0xFFu);
        region[at3 + 100u] ^= 1u;                     /* (its beats: the CRC fails) */
        put_chart(region, at3 + (o3 + 0xFFFu) / 0x1000u * 0x1000u, s, 0xFFu);
        mm_song_scan();
        check("stored songs: a deleted one and a damaged one (its CRC) skipped, the scan going on past them: 2 listed",
              mm_nstored == 2u && !mm_sec_used[n0 / 0x1000u] && !mm_sec_used[at3 / 0x1000u]);
        {   /* a song deleted by erasing its sectors (editor_songs.c): a gap the scan goes past, and the first gap a
             * new chart fits in is where it goes; no room: -1 */
            uint32_t sz = put_chart(region, 0, s, 0xFFu), sec = (sz + 0xFFFu) / 0x1000u, g;
            memset(region, 0xFF, sizeof region);
            for (g = 0; g < 3u; g++)
                put_chart(region, g * sec * 0x1000u, s, 0xFFu);
            memset(region + sec * 0x1000u, 0xFF, sec * 0x1000u);   /* the second one deleted */
            mm_song_scan();
            ok = mm_nstored == 2u && mm_stored_off[1] == 2u * sec * 0x1000u && mm_stored_size[1] == sz &&
                 mm_song_room(sz) == (int32_t)(sec * 0x1000u) && mm_song_room(sec * 0x1000u + 1u) == (int32_t)(3u * sec * 0x1000u) &&
                 mm_song_free_sectors() == MM_SONG_NSEC - 2u * sec && mm_song_room(MM_SONG_SIZE + 1u) == -1 &&
                 mm_song_largest_gap() == MM_SONG_NSEC - 3u * sec;
            check("stored songs: an erased song's sectors a gap the scan goes past, the first gap that fits a new one", ok);
            memset(region, 0xFF, sizeof region);
            for (g = 0; g < MM_SONG_MAX + 1u && (g + 1u) * sec <= MM_SONG_NSEC; g++)
                put_chart(region, g * sec * 0x1000u, s, 0xFFu);
            mm_song_scan();
            check("stored songs: at most 16 listed, the sectors of any beyond them still in use",
                  g <= MM_SONG_MAX || (mm_nstored == MM_SONG_MAX && mm_song_free_sectors() == MM_SONG_NSEC - g * sec));
        }
        {   /* the stored song plays: every note started, the song ends by itself */
            uint32_t k;
            player_start(MM_NSONGS);
            mix_block(out, CTL);
            for (k = 0; k < (s->length + 2u * CHART_FS) / CTL && pl.playing; k++)
                mix_block(out, CTL);
            check("stored songs: one plays as the built-in one does (every note started, the end)",
                  !pl.playing && pl.song == MM_NSONGS && pl.next == s->nnote);
            player_stop();
            mix_block(out, CTL);
        }
        /* charts from tools/gen_songs.py --charts (argv): byte for byte the chart of the built-in song of their title,
         * and listed when written into the region */
        for (k3 = 1; k3 < (uint32_t)argc; k3++) {
            static uint8_t file[MM_SONG_SIZE];
            FILE *f = fopen(argv[k3], "rb");
            uint32_t len = f ? (uint32_t)fread(file, 1, sizeof file, f) : 0u, j, same = 0, n;
            const mm_song_t *w = 0;
            if (f)
                fclose(f);
            for (j = 0; j < MM_NSONGS; j++)
                if (len > 24u + 24u && !strncmp((const char *)file + 24, MM_SONGS[j].title, 24))
                    w = &MM_SONGS[j];
            memset(region, 0xFF, sizeof region);
            if (w) {
                n = put_chart(region, 0, w, 0xFFu);
                same = n == len && !memcmp(region, file, len);
                memset(region, 0xFF, sizeof region);
                memcpy(region, file, len);
                mm_song_scan();
                same &= mm_nstored == 1u && !strncmp(mm_song(MM_NSONGS)->title, w->title, 24);   /* (24: the chart's) */
            }
            snprintf(msg, sizeof msg, "stored songs: %s from tools/gen_songs.py --charts: the firmware's chart of "
                     "its song, byte for byte, and listed", argv[k3]);
            check(msg, w && same);
        }
        mm_song_base = 0;
        mm_song_scan();
    }
    printf("player: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
