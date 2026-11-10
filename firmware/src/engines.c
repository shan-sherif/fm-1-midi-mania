/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SAVE > PHRASES) and the parts' sounds at power-on. */
#include "../engines/src/dsp.c"   /* Felucca-Engines (MIT): the shared DSP blocks */

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with large per-part state
 * (engine_t.mem: PHYS's slots, GRAIN's grains and index, DRUM's lanes, FM6's notes, SLICE's reverse windows)
 * keep it in one region per part instead of an array each. The region holds the state of the engine that
 * last claimed it (eng_mem_owner); a switch to another such engine zeroes its engine_t.mem bytes, which is
 * the state each had at power-on, so a switch sounds as it did with the arrays (tests/union_test.c). State
 * that runs on across notes and switches stays outside (WHEEL's wheels, GRAIN's random numbers).
 * Sized by the largest, ADPCM when it is built (else PHYS, 3 x SYMP's slot); an engine that needs more than ENG_MEM_PART grows it for
 * every part (x NPART of the pool). Zero-initialised pool section: main.c fm1_cstart clears it. */
#define ENG_MEM_PART 12888u
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
#define ENG_MEM(p) ((void *)eng_mem[p].b)  /* part p's region (p < NPART) */
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")

#include "../engines/src/eng_analog.c"   /* Felucca-Engines (MIT): ANALOG LOFI TRIO WHEEL NOISE (Midi-Mania: all but
                                         * NOISE, the sounds MM_SOUNDS lists) */
#include "../engines/src/eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"        /* VOICE: formant (CHOIR AAH, VOX LEAD, WOW BASS) */
#include "../engines/src/eng_trio.c"
#include "../engines/src/eng_wheel.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */

/* a number no engine plays here (a plugin's, not loaded; a later Felucca's; Midi-Mania: an engine it does not build):
 * a sine at the note, as a missing sample (eng_sample.c smp_sine), and NO PLUGIN said (snd_missing) */
#define MISS_P {"-", F_INT, 0, 127, 0, 0, 0}
static const preset_t MISS_PRESETS[] = {{.name = "MISSING", .env = {0, 0, 127, 20}}};
static void miss_on(track_t *t, voice_t *v) { (void)t, (void)v; }
static void miss_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t i, p = v->ph[0];
    (void)t;
    for (i = 0; i < n; i++, p += m->inc)
        out[i] += voice_amp(sine_i(p) >> 1, m, i) << 1;
    v->ph[0] = p;
}
static const engine_t ENG_MISSING = {
    .name = "MISSING",
    .page_title = {"MISSING", "MISSING"},
    .edit = {MISS_P, MISS_P, MISS_P, MISS_P, MISS_P, MISS_P, MISS_P, MISS_P},
    .presets = MISS_PRESETS,
    .npresets = 1,
    .knob = {P_ATK, P_DEC, P_SUS, P_REL},
    .note_on = miss_on,
    .render = miss_render,
};

/* the editor protocol, user presets and projects store these indices: append, never reorder. With MM_PLUGINS
 * a plugin can take an engine's place (plugin.c plg_add), so the table is in RAM then, and the numbers past
 * Felucca's are ENG_MISSING until a plugin plays one */
static const engine_t *const ENGINES[NENGINES] = {
#define MM_ENG(e) &ENG_MISSING   /* Midi-Mania: not built (its number kept: a stored sound of it plays a sine) */
    &ENG_ANALOG,                 /* 0 */
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
    MM_ENG(ENG_PHASE),           /* 2 */
    &ENG_LOFI,                   /* 3 */
    &ENG_SAMPLE,                 /* 4 */
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
    MM_ENG(ENG_GRAIN),           /* 8 */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    MM_ENG(ENG_NOISE),           /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* the sample source a track's sound plays that has no data (with plugins also: a plugin's engine number that no plugin
 * plays here, code 128 and up) (SAMPLE's SET, GRAIN's SRC, SLICE's SRC: an empty or
 * invalid user slot, a set or PIANO this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
    if (e != &ENG_SAMPLE)
        return 0;
    *code = 1u + si % 32u;
    return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with MM_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    4, 12,                       /* Midi-Mania: SAMPLE FM6 PHYS ANALOG (DRUM below) */
    9, 0,
    10,                          /* DRUM */
};

/* with plugins: ENGINE_ORDER, then the plugins of their own as they came (plugin.c plg_add), eng_nshown of them */
#define eng_nshown() NENG_SHOWN
#define ENG_ORDER ENGINE_ORDER
/* Midi-Mania: each part is one instrument, its engine fixed (part 1 DRUMS, 2 KEYS, 3 GUITAR, 4 BASS: player.c MM_*):
 * the engines one can pick are the selected part's only, so PRESETS (and its ENG knob) offer that instrument's sounds,
 * and ui.c set_engine_of refuses any other (MM_PART_ENGINE) */
static const uint8_t MM_PART_ENGINE[NPART] = {ENGI_SAMPLE, ENGI_DRUM, ENGI_PHYS, ENGI_FM6};
#undef ENG_ORDER
#undef eng_nshown
#define ENG_ORDER (&MM_PART_ENGINE[song.sel % NPART])
#define eng_nshown() 1u

/* each part's sounds: the ones that suit it, from the engines Midi-Mania builds (a part may play several engines). PRESETS
 * (ui.c preset_all_*), SOUNDS (ENV) and SONGS' SOUND (ui_songs.c) offer these only, in this order; ui.c set_engine_of
 * refuses an engine the part has no sound of. MM_ALL: every preset of the engine (the DRUM kits). No two of a part's
 * share a name. The presets' numbers:
 *   SAMPLE  build/gen/mm_samples.h: 0 PIANO 2 FLUTE
 *   FM6     eng_fm6.c: 0 TINE EP 1 BELL 2 FM BASS 3 BRASS 4 PAD 6 ORGAN 7 PLUCK
 *   PHYS    eng_phys.c: 0 BELL TREE 1 MARIMBA 2 PLUCK 3 BOWED METAL 4 KALIMBA 7 DRONE STRING 8 HARP
 *   ANALOG  eng_analog.c: 0 SAW LEAD 2 SQR BASS 3 PWM STR 4 ACID 5 SINE KEY 6 RAVE 7 SUB BASS 11 STRINGS
 *   LOFI    eng_lofi.c: 0 PULSE LD 1 WAVE BASS 3 WAVE LEAD
 *   VOICE   eng_formant.c: 0 CHOIR AAH 1 VOX LEAD 2 WOW BASS
 *   TRIO    eng_trio.c: 0 FAT BASS 1 ARP LEAD 2 SYNC LEAD
 *   WHEEL   eng_wheel.c: 0 FULL ORGAN 1 JAZZ PERC 2 GOSPEL */
typedef struct { uint8_t e, k; } mm_sound_t;
#define MM_ALL 0xFFu
static const mm_sound_t MM_SND_DRUMS[] = {{ENGI_DRUM, MM_ALL}};
static const mm_sound_t MM_SND_KEYS[] = {
    {ENGI_SAMPLE, 0}, {ENGI_SAMPLE, 2}, {ENGI_FM6, 0}, {ENGI_FM6, 6},              /* PIANO FLUTE TINE EP ORGAN */
    {ENGI_PHYS, 8}, {ENGI_PHYS, 4}, {ENGI_PHYS, 1},                                /* HARP KALIMBA MARIMBA */
    {ENGI_WHEEL, 0}, {ENGI_WHEEL, 1}, {ENGI_WHEEL, 2},                             /* FULL ORGAN JAZZ PERC GOSPEL */
    {ENGI_FM6, 1}, {ENGI_PHYS, 0}, {ENGI_FM6, 4}, {ENGI_ANALOG, 11},               /* BELL BELL TREE PAD STRINGS */
    {ENGI_FM6, 3}, {ENGI_FORMANT, 0}, {ENGI_ANALOG, 5}};                           /* BRASS CHOIR AAH SINE KEY */
static const mm_sound_t MM_SND_GUITAR[] = {
    {ENGI_PHYS, 2}, {ENGI_PHYS, 8}, {ENGI_PHYS, 4}, {ENGI_PHYS, 7},                /* PLUCK HARP KALIMBA DRONE STRING */
    {ENGI_PHYS, 3}, {ENGI_ANALOG, 3},                                              /* BOWED METAL PWM STR */
    {ENGI_ANALOG, 0}, {ENGI_TRIO, 2}, {ENGI_TRIO, 1}, {ENGI_FORMANT, 1},           /* SAW LEAD SYNC LEAD ARP LEAD VOX LEAD */
    {ENGI_LOFI, 0}, {ENGI_LOFI, 3}};                                               /* PULSE LD WAVE LEAD */
static const mm_sound_t MM_SND_BASS[] = {
    {ENGI_FM6, 2}, {ENGI_FM6, 7}, {ENGI_ANALOG, 7}, {ENGI_ANALOG, 2},              /* FM BASS PLUCK SUB BASS SQR BASS */
    {ENGI_TRIO, 0}, {ENGI_ANALOG, 4}, {ENGI_ANALOG, 6}, {ENGI_FORMANT, 2},         /* FAT BASS ACID RAVE WOW BASS */
    {ENGI_LOFI, 1}};                                                               /* WAVE BASS */
static const struct { const mm_sound_t *l; uint8_t n; } MM_SOUNDS[NPART] = {
    {MM_SND_KEYS, NELEM(MM_SND_KEYS)}, {MM_SND_DRUMS, NELEM(MM_SND_DRUMS)},
    {MM_SND_GUITAR, NELEM(MM_SND_GUITAR)}, {MM_SND_BASS, NELEM(MM_SND_BASS)}};

static uint32_t mm_sound_n(uint32_t part)       /* how many sounds part has */
{
    uint32_t i, n = 0;
    for (i = 0; i < MM_SOUNDS[part % NPART].n; i++) {
        const mm_sound_t *s = &MM_SOUNDS[part % NPART].l[i];
        n += s->k == MM_ALL ? ENGINES[s->e]->npresets : 1u;
    }
    return n;
}
static uint32_t mm_sound_at(uint32_t part, uint32_t n, uint32_t *k)   /* its n-th: the engine, *k the preset */
{
    uint32_t i;
    for (i = 0; i < MM_SOUNDS[part % NPART].n; i++) {
        const mm_sound_t *s = &MM_SOUNDS[part % NPART].l[i];
        uint32_t c = s->k == MM_ALL ? ENGINES[s->e]->npresets : 1u;
        if (n < c) {
            *k = s->k == MM_ALL ? n : s->k;
            return s->e;
        }
        n -= c;
    }
    *k = 0;
    return MM_SOUNDS[part % NPART].l[0].e;
}
static uint32_t mm_sound_pos(uint32_t part, uint32_t e, uint32_t k)   /* the index of engine e preset k, n: none */
{
    uint32_t i, n = mm_sound_n(part), pk;
    for (i = 0; i < n; i++)
        if (mm_sound_at(part, i, &pk) == e && pk == k)
            return i;
    return n;
}
static int mm_engine_ok(uint32_t part, uint32_t e)   /* part has a sound of engine e */
{
    uint32_t i;
    for (i = 0; i < MM_SOUNDS[part % NPART].n; i++)
        if (MM_SOUNDS[part % NPART].l[i].e == e)
            return 1;
    return 0;
}

/* the engines one can pick (engine 1 only with MM_FM4; with plugins, a number past Felucca's when a plugin plays
 * it), in ENG_ORDER: eng_ok(e), the n-th of them eng_vis(n), e's place among them eng_rank(e), the next / previous one
 * eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES && e != ENGI_DIGITAL; }
static uint32_t eng_vis(uint32_t n) { uint32_t c = eng_nshown(); return ENG_ORDER[n % c]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n, c = eng_nshown();
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without MM_FM4: its sounds play as FM6) */
    for (n = 0; n < c && ENG_ORDER[n] != e; n++)
        ;
    return n < c ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    uint32_t c = eng_nshown();
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : c - 1u)) % c);
}



/* factory sequence patterns: SAVE > PHRASES loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (DRUM plays them as GM drums, SLICE as slices; SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* SLICE (eng_slice.c): note = C4 + slice */
    {"CHOP", {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64},     /* 9 16 slices re-ordered */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"STUTTER", {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67},    /* 10 8 slices, repeats */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"SLICES", {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},   /* 11 in order */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* DRUM (General MIDI: 36 kick, 38 snare, 42 closed / 46 open hi-hat) */
    {"BEAT", {36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},     /* 12 */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP presets (RAVE, ARP 8BIT, ARP LEAD) suggest
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 13 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty: all are): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][3] = {{ENGI_SAMPLE, 0, 0}, {ENGI_DRUM, 0, 0}, {ENGI_PHYS, 2, 0}, {ENGI_FM6, 2, 0}};
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
