/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* A chart: one song, converted from a MIDI file on a computer (tools/mid2chart.py, planned), stored in flash and
 * played by the device. docs/CHART_FORMAT.md describes the bytes; this header is the same thing for the firmware.
 *
 * Times are audio samples at CHART_FS from the song's start: the audio ISR counts the same samples, so playback and
 * judging share one clock with no conversion. Every multi-byte field is little-endian (the FM-1 and every host are).
 *
 * Layout: chart_head_t, then nbeat uint32_t beat times, then nnote chart_note_t by time (ties: any order). */
#ifndef CHART_H
#define CHART_H
#include <stdint.h>

#define CHART_FS 44100u              /* the FM-1's sample rate (Felucca core.h FS) */
#define CHART_MAGIC 0x31434D4Du      /* "MMC1" */
#define CHART_VERSION 2u             /* 2: beat0, parts and the deleted mark after crc32 (head_size 72) */
#define CHART_NKEYS 27u              /* the FM-1's keys, F3 .. G5: lane 0 = F3 */
#define CHART_NPART 4u               /* Felucca's synth parts */

/* chart_note_t flags */
#define CN_PART_MASK 3u              /* bits 0..1: the part (0..3) that sounds it */
#define CN_PLAY 4u                   /* the player plays it (judged); without it the device plays it (backing) */
#define CN_DRUM 8u                   /* a drum hit: pitch is a General MIDI drum note */

/* the part each of the four synth parts starts with: a Felucca engine and preset (engines.c ENGINES[]) */
typedef struct {
    uint8_t engine, preset, level, rsv;
} chart_part_t;

typedef struct {
    uint32_t magic;                  /* CHART_MAGIC */
    uint16_t version;                /* CHART_VERSION */
    uint16_t head_size;              /* sizeof(chart_head_t): a newer, longer header still loads */
    uint32_t nbeat;                  /* beat times after the header (bar lines, the metronome) */
    uint32_t nnote;                  /* notes after the beats */
    uint32_t length;                 /* the song's end, samples */
    uint16_t bpm_x10;                /* the first tempo x 10, for the song list (the beats are the real timing) */
    uint8_t beats_per_bar;           /* the first time signature's numerator: every this many beats is a bar line */
    uint8_t level;                   /* difficulty 1..10, the converter's estimate */
    char title[24];                  /* zero-padded, ASCII (the UI font) */
    chart_part_t part[CHART_NPART];
    uint32_t crc32;                  /* of everything after the header (the beats and the notes) */
    /* version 2 (head_size 72) */
    uint16_t beat0;                  /* the first beat's place in its bar: beat k is a bar's first when (beat0 + k) % bpb
                                      * is 0 */
    uint8_t parts;                   /* bit p: part p has notes */
    uint8_t live;                    /* 0xFF: in use (flash as erased); 0x00: deleted (programmed without an erase) */
} chart_head_t;

typedef struct {
    uint32_t time;                   /* note-on, samples from the start */
    uint32_t len;                    /* samples to its note-off (0: a hit with no length, as a drum) */
    uint8_t key;                     /* the lane, 0..CHART_NKEYS - 1 (CN_PLAY notes); what sounds is pitch */
    uint8_t pitch;                   /* the MIDI note that sounds */
    uint8_t vel;                     /* 1..127 */
    uint8_t flags;                   /* CN_* */
} chart_note_t;

_Static_assert(sizeof(chart_head_t) == 72u, "chart_head_t: docs/CHART_FORMAT.md");
_Static_assert(sizeof(chart_note_t) == 12u, "chart_note_t: docs/CHART_FORMAT.md");

static inline uint32_t chart_ms(uint32_t ms) { return (uint32_t)((uint64_t)ms * CHART_FS / 1000u); }

#endif /* CHART_H */
