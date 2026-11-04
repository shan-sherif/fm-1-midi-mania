/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* Timing judgement: how close a key press is to the chart note it was meant for.
 *
 * Plain C with no hardware and no Felucca state, so it runs the same on the FM-1, on a host build and in
 * tests/judge_test.c. Included into the one translation unit like Felucca's files (everything static).
 *
 * Times are samples (chart.h). The caller gives the press time as exactly as it knows it: on the FM-1 the key
 * scan's own timestamp of the key closing (fm1_in.note_t0, ~0.1 ms), turned into song samples, not the audio block
 * the press reached (up to 2.9 ms later).
 *
 * A press takes the earliest pending note on its key inside the GOOD window, so a fast run of the same key is
 * matched in order and one press never judges two notes. A note nobody pressed becomes a MISS once its GOOD window
 * has passed (judge_tick). */
#include <stdint.h>
#include "chart.h"

enum { J_NONE, J_PERFECT, J_GREAT, J_GOOD, J_MISS, J_N };   /* J_NONE: no note there (a press between notes) */

typedef struct {
    uint32_t perfect, great, good;   /* half-widths of the windows, samples (judge_init: the defaults) */
} judge_win_t;

typedef struct judge judge_t;
struct judge {
    const chart_note_t *note;        /* the chart's notes, by time */
    uint32_t n;
    uint8_t *grade;                  /* per note: 0 pending, else its J_*; the caller's, n bytes, zeroed */
    uint32_t head;                   /* every note before it is judged or a backing note: where the search starts */
    judge_win_t win;
    int32_t offset;                  /* the player's calibration, samples, added to every press (+: they press early) */
    uint32_t count[J_N];
    uint32_t combo, max_combo, score;
    /* which notes are the player's, and the key each one is on: by default the chart's own marks (CN_PLAY, its
     * key); Midi-Mania's player gives its own (player.c: your part and RANGE, a drum's pad) */
    int (*mine)(const chart_note_t *c);
    uint32_t (*key)(const chart_note_t *c);
};

static const uint8_t JUDGE_POINTS[J_N] = {0, 3, 2, 1, 0};
static int judge_mine(const judge_t *j, const chart_note_t *c) { return j->mine ? j->mine(c) : (c->flags & CN_PLAY) != 0u; }
static uint32_t judge_key(const judge_t *j, const chart_note_t *c) { return j->key ? j->key(c) : c->key; }

static void judge_init(judge_t *j, const chart_note_t *note, uint32_t n, uint8_t *grade)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        grade[i] = J_NONE;
    *j = (judge_t){0};
    j->note = note;
    j->n = n;
    j->grade = grade;
    j->win.perfect = chart_ms(30);
    j->win.great = chart_ms(60);
    j->win.good = chart_ms(110);
}

static void judge_count(judge_t *j, uint32_t i, uint32_t g)
{
    j->grade[i] = (uint8_t)g;
    j->count[g]++;
    j->score += JUDGE_POINTS[g];
    if (g == J_MISS) {
        j->combo = 0;
        return;
    }
    if (++j->combo > j->max_combo)
        j->max_combo = j->combo;
}

/* key pressed at song time t (samples): its grade (J_NONE: no note of that key near enough). *dt, when asked:
 * the press minus the note, samples (negative: early), for the UI's EARLY / LATE and for calibration */
static uint32_t judge_press(judge_t *j, uint32_t key, uint32_t t, int32_t *dt)
{
    uint32_t i, g, d;
    int64_t at = (int64_t)t + j->offset;
    for (i = j->head; i < j->n; i++) {
        const chart_note_t *c = &j->note[i];
        if ((int64_t)c->time > at + j->win.good)
            break;                                   /* by time: nothing later can be in the window */
        if (j->grade[i] != J_NONE || judge_key(j, c) != key || !judge_mine(j, c))
            continue;
        if ((int64_t)c->time + j->win.good < at)
            continue;                                /* too long ago (judge_tick makes it a MISS) */
        d = (uint32_t)(at > c->time ? at - c->time : c->time - at);
        g = d <= j->win.perfect ? J_PERFECT : d <= j->win.great ? J_GREAT : J_GOOD;
        if (dt)
            *dt = (int32_t)(at - (int64_t)c->time);
        judge_count(j, i, g);
        return g;
    }
    return J_NONE;
}

/* song time now (samples): every pending note whose window has passed is a MISS. Once per audio block */
static void judge_tick(judge_t *j, uint32_t now)
{
    int64_t at = (int64_t)now + j->offset;
    while (j->head < j->n) {
        const chart_note_t *c = &j->note[j->head];
        if (j->grade[j->head] == J_NONE && judge_mine(j, c)) {
            if ((int64_t)c->time + j->win.good >= at)
                break;                               /* still pressable */
            judge_count(j, j->head, J_MISS);
        }
        j->head++;
    }
}

/* the playable notes, and how many of them the player has had (judged): the progress bar, the result screen */
static uint32_t judge_total(const judge_t *j)
{
    uint32_t i, n = 0;
    for (i = 0; i < j->n; i++)
        n += judge_mine(j, &j->note[i]) != 0;
    return n;
}
