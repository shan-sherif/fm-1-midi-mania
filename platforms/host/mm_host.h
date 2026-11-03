/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The host API: Felucca on a platform that is not the FM-1. platforms/host/mm_host.c builds the firmware's
 * sources against a simulated FM-1 (its clock, panel, LEDs, screen and flash), and these functions are all a platform
 * needs to run it: a frontend (platforms/sdl, platforms/libretro, platforms/web's worklet, a port of your own) boots it,
 * asks it for sound, hands it the controls and MIDI, and shows its screen and LEDs. docs/PORTING.md is the guide.
 *
 * One thread: every call from the same thread (the device's state is never shared). The device's clock runs only
 * inside fh_render: it advances with the audio asked for (FS frames a second), so a platform without sound calls
 * fh_render by its own clock and drops the samples.
 *
 * Built as one translation unit: a frontend #includes mm_host.c (and the platforms/common files it uses) and
 * then its own code; FH_API is what a build exports (the web build: the wasm exports). */
#ifndef MM_HOST_H
#define MM_HOST_H
#include <stdint.h>

#ifndef FH_API
#define FH_API
#endif
#define FH_RENDER_MAX 1024u              /* frames one fh_render gives at most */
#define FH_SCREEN_W 240u
#define FH_SCREEN_H 240u
#define FH_NKEYS 27u                     /* the keys F3 .. G5 */

/* ------------------------------------------------------------- the device --- */
/* the flash first (fh_nor_erase, then fh_nor's bytes from the platform's file), the plugins (fh_plugin_add), then
 * fh_boot: power-on as the FM-1's (the splash, the last session's music) */
FH_API void fh_boot(void);
FH_API uint32_t fh_booted(void);
FH_API uint32_t fh_sample_rate(void);    /* FS: 44100 */
FH_API uint32_t fh_now_ms(void);         /* the device's milliseconds */
FH_API uint32_t fh_playing(void);        /* the sequencer runs */

/* ---------------------------------------------------------------- sound --- */
/* n frames (at most FH_RENDER_MAX) into fh_out_l / fh_out_r, -1.0 .. 1.0: the device runs until they exist */
FH_API void fh_render(uint32_t n);
FH_API float *fh_out_l(void);
FH_API float *fh_out_r(void);

/* ------------------------------------------------------------- controls --- */
/* buttons by their printed labels (bit b = B_FX .. B_OCTUP, firmware/src/panel.c), held while the bit is set; keys bit
 * k = key k (F3 .. G5); an encoder by role (EN_SELECT .. EN_K4) turned by n detents; MASTER 0 .. 1023 */
FH_API void fh_buttons(uint32_t labels);
FH_API void fh_keys(uint32_t mask);
FH_API void fh_enc(uint32_t role, int32_t n);
FH_API void fh_master(uint32_t v);
FH_API uint32_t fh_held_buttons(void);   /* what fh_buttons / fh_keys / fh_master last gave */
FH_API uint32_t fh_held_keys(void);
FH_API uint32_t fh_master_level(void);
/* a MIDI channel message (status 0x80 .. 0xEF), as a USB-MIDI packet arrives: 0 = the device's queue was full */
FH_API uint32_t fh_midi(uint32_t status, uint32_t d1, uint32_t d2);

/* --------------------------------------------------------- screen, LEDs --- */
/* the screen: FH_SCREEN_W x FH_SCREEN_H RGB565 as the FM-1's panel gets it (high byte first); fh_screen_draws counts
 * the drawing calls (unchanged: nothing to show again) */
FH_API uint16_t *fh_screen(void);
FH_API uint32_t fh_screen_draws(void);
/* the LEDs, lit / dim glow / breathing / the DRUM grid's mid level: bit b = the button labelled b (fh_lit_buttons
 * bit 14: PLAY's green), keys bit k. fh_anim_levels: during the power-on sweep (or the idle animation) each LED's
 * level (0 .. 64 / 64 of lit, | 0x80 the glow under it: 0 .. 13 the buttons by label, 14 .. 40 the keys), else 0 */
FH_API uint32_t fh_lit_buttons(void);
FH_API uint32_t fh_lit_keys(void);
FH_API uint32_t fh_dim_buttons(void);
FH_API uint32_t fh_dim_keys(void);
FH_API uint32_t fh_dim_level(void);
FH_API uint32_t fh_breath_buttons(void);
FH_API uint32_t fh_breath_keys(void);
FH_API uint32_t fh_mid_keys(void);
FH_API uint8_t *fh_anim_levels(void);

/* ---------------------------------------------------------------- flash --- */
/* the FM-1's 1 MiB NOR (settings, projects, user presets, user samples): the platform keeps the bytes and gives them
 * back before fh_boot; fh_flash_writes counts the erases and writes (a change: time to save it) */
FH_API uint8_t *fh_nor(void);
FH_API uint32_t fh_nor_size(void);
FH_API void fh_nor_erase(void);
FH_API uint32_t fh_flash_writes(void);


/* ---------------------------------------------------------------- tests --- */
/* a heavy song straight into the tracks (T1 FM6, T2 PHYS, T3 a sine: GRAIN is not built; held chords; T4 DRUM), started
 * on the sequencer (Midi-Mania: PLAY plays the songs); fh_test_stop stops it */
FH_API void fh_test_heavy(void);
/* engine e's presets p, p + 1, p + 2 (wrapping) on T1..T3 in the heavy song, T4 its drums: the engine's presets */
FH_API uint32_t fh_test_engine(uint32_t e, uint32_t p);
FH_API uint32_t fh_test_user_preset(uint32_t i);   /* user preset slot i holds one */
FH_API uint32_t fh_test_click(void);
FH_API void fh_test_stop(void);
/* stored songs: the song region's offset in fh_nor() and its size, the first free sector in it (a new chart goes
 * there: docs/CHART_FORMAT.md), and the rescan after writing one (returns the songs listed, built in + stored) */
FH_API uint32_t fh_song_base(void);
FH_API uint32_t fh_song_size(void);
FH_API uint32_t fh_song_free(void);
FH_API uint32_t fh_songs_rescan(void);                     /* the heavy song (and a song) stopped */                /* METRONOME's CLICK: 0 OFF, 2 ON */
#endif /* MM_HOST_H */
