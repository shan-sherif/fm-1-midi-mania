/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* firmware/src/judge.c on a computer:
 *   cc -std=gnu99 -Wall -Wextra -Ifirmware/src tests/judge_test.c -o build/judge_test && build/judge_test */
#include <stdio.h>
#include "../firmware/src/judge.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    bad += !ok;
}

#define MS(x) ((uint32_t)((uint64_t)(x) * CHART_FS / 1000u))   /* chart_ms(), as a constant for the table */
#define PLAY(t, k) {MS(t), 0, (k), 60, 100, CN_PLAY}

int main(void)
{
    static const chart_note_t notes[] = {
        PLAY(1000, 5),
        {MS(1000), 0, 0, 36, 100, 3u | CN_DRUM},     /* backing: never judged */
        PLAY(1100, 5),                               /* the same key soon after */
        PLAY(1100, 9),                               /* a chord with it */
        PLAY(2000, 0),
        PLAY(3000, 26),
    };
    uint8_t grade[sizeof notes / sizeof notes[0]];
    judge_t j;
    int32_t dt = 0;

    judge_init(&j, notes, sizeof notes / sizeof notes[0], grade);
    check("five playable notes", judge_total(&j) == 5u);
    check("a press far from any note is nothing", judge_press(&j, 5, MS(500), 0) == J_NONE);
    check("on time: PERFECT", judge_press(&j, 5, MS(1010), &dt) == J_PERFECT && dt == (int32_t)MS(10));
    check("the next press of that key takes the next note, GREAT", judge_press(&j, 5, MS(1050), &dt) == J_GREAT);
    check("dt is negative when early", dt == -(int32_t)MS(50));
    check("the chord's other key, late: GOOD", judge_press(&j, 9, MS(1200), 0) == J_GOOD);
    check("the wrong key is nothing", judge_press(&j, 1, MS(2000), 0) == J_NONE);
    judge_tick(&j, MS(2050));
    check("still pressable inside the window", grade[4] == J_NONE);
    judge_tick(&j, MS(2200));
    check("past the window: MISS", grade[4] == J_MISS && j.combo == 0u);
    check("a missed note cannot be hit afterwards", judge_press(&j, 0, MS(2200), 0) == J_NONE);
    j.offset = -(int32_t)MS(40);                     /* this player presses 40 ms late */
    check("calibration moves the press", judge_press(&j, 26, MS(3040), 0) == J_PERFECT);
    check("counts", j.count[J_PERFECT] == 2u && j.count[J_GREAT] == 1u && j.count[J_GOOD] == 1u &&
                        j.count[J_MISS] == 1u);
    check("max combo 3, combo 1 after the miss", j.max_combo == 3u && j.combo == 1u);
    check("score", j.score == 3u + 2u + 1u + 3u);
    return bad != 0;
}
