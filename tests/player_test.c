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
                       pl.on[i2].pitch < MM_SPLIT;
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
        ok &= mm_nstored == 0u && mm_store_end == 0u && mm_nsongs() == MM_NSONGS;
        o1 = put_chart(region, 0, s, 0xFFu);
        mm_song_scan();
        t = mm_song(MM_NSONGS);
        ok &= mm_nstored == 1u && mm_nsongs() == MM_NSONGS + 1u && !strncmp(t->title, s->title, 24) && t->nnote == s->nnote &&
              t->nbeat == s->nbeat && t->length == s->length && t->bpm_x10 == s->bpm_x10 && t->bpb == s->bpb &&
              t->beat0 == s->beat0 && t->parts == s->parts && !memcmp(t->part, s->part, sizeof t->part) &&
              !memcmp(t->note, s->note, s->nnote * sizeof *s->note) && !memcmp(t->beat, s->beat, s->nbeat * 4u) &&
              (const uint8_t *)t->note > region && (const uint8_t *)t->note < region + sizeof region &&
              mm_store_end == (o1 + 0xFFFu) / 0x1000u * 0x1000u;
        check("stored songs: none in an erased region; one chart listed after the built-in songs, as the song it came "
              "from (title, beats, notes, parts), read in place", ok);
        n0 = mm_store_end;
        o2 = put_chart(region, n0, s, 0x00u);         /* deleted */
        at3 = (n0 + o2 + 0xFFFu) / 0x1000u * 0x1000u;
        o3 = put_chart(region, at3, s, 0xFFu);
        region[at3 + 100u] ^= 1u;                     /* (its beats: the CRC fails) */
        put_chart(region, at3 + (o3 + 0xFFFu) / 0x1000u * 0x1000u, s, 0xFFu);
        mm_song_scan();
        check("stored songs: a deleted one and a damaged one (its CRC) skipped, the scan going on past them: 2 listed",
              mm_nstored == 2u && mm_store_end > at3);
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
