/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the UI (firmware/src/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, TOOLS INIT, user preset) changes the sound only: the
 *              steps, LEN DIV SWING GATE, ARP, SCL, the SLICER and the mix stay the track's. Power-on:
 *              every sequencer empty.
 *   PATTERNS   SAVE > PHRASES: KNOB 1 PAT (factory patterns, then user presets that hold one), OCT+
 *              LOAD; over the user's steps the REPLACE? dialog (OCT- / OCT+); the PRESETS hint.
 *   UNDO       a load keeps a copy of the track; SAVE held swaps back what the loads changed (again:
 *              redo); browsing keeps the copy from before the first load; power-on and projects: none.
 *   REC        a tap on every screen arms the selected track and starts PLAY, without navigation.
 *              Long REC never clears. On STEP, armed and playing: keys do not write the
 *              cursor step. With the ARP on, the arp's notes are recorded, not the keys.
 *   MIDI IN    MENU > MIDI > MIDI IN (ROUT): CH1-4 (channels 5..16 ignored) / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    PATTERNS, USER, PROJECT, TOOLS: the knobs pick, OCT+ does it, OCT- cancels or goes
 *              HOME, on release, never both together; the OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   GRID       SEQ > STEP on a DRUM track: white keys toggle the selected lane's steps of the page, black
 *              keys 1..8 select the lane (and only they play), 9 held = ACC, 10 / 11 the page; KNOB 1..4 STEP
 *              LANE HIT ACC; the key LEDs (hits, the playhead inverted, the lane, ACC, the page keys); a step's
 *              GM notes shown on their lanes and made the lane's own by an edit; live recording into the grid
 *              (lane keys on the grid, GM keys elsewhere; other GM drums as notes; no TIE holds); 12 BEAT from
 *              PATTERNS fills the grid (undo); sound loads keep it, and other engines play it as GM notes.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 *   SOAK       #63: 150000 random frames (turns, pages, transport, REC / motion, wrapping clocks): no roll
 *              outlives ROLL_FRAMES, the cards always equal a forced redraw once the rolls are over.
 *   FM6 OPS    (1.2, #76) EDIT > OPERATOR, OP ENV, OPERATOR 2 on an FM6 track: KNOB 1 the operator, each of the 84 bytes
 *              the pages edit written alone, SLOT OWN, the sound following, one undo per gesture, the macros on top,
 *              projects and user presets keep the patch; FM6 only, not automatable.
 *   USB SERIAL #67: MENU > USB SERIAL ON (default, the descriptors as before) / OFF (audio + MIDI only, class 0):
 *              the menu, the settings, the descriptor at boot (built with the console, as the firmware: MM_CDC).
 *   SCREEN OFF (1.1.5): MENU > DISPLAY > SCREEN OFF against a simulated clock and the panel's commands (lcd_power):
 *              dark after the time, nothing drawn, any input wakes it (SLPOUT, 120 ms, redraw, DISPON) and is
 *              swallowed; dialogs / count-in / UPDATE MODE keep it on; saved; the sound bit for bit while dark.
 * Built and run by tests/run_tests.sh. */
#ifndef MM_CDC
#define MM_CDC 1                                    /* (as src/midimania.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* bsp/boards/fm1/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* bsp/boards/fm1/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int host_idle, host_idle_glow;             /* .. the idle animation (1.2: MENU > ANIM IDLE) running, its glow */
static int fm1_led_anim_on(void) { return host_anim || host_idle; }
static void fm1_led_idle_start(uint32_t glow) { host_idle = 1; host_idle_glow = (int)glow; }
static int fm1_led_idle_on(void) { return host_idle; }
static void fm1_led_anim_stop(void) { host_anim = host_idle = 0; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);                /* (a test's: time passing in a blocking loop, as panel_setup's) */
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
#ifndef HOST_IRQ_STUBS                                   /* (tests/drumfuzz_test.c: its own, an emulated ISR) */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#endif
static uint16_t host_screen[240 * 240];
/* #181 a blank frame: while host_fl_on, every write of a frame is watched (host_fl_begin .. host_fl_end); a pixel that
 * shows, between two writes, a value that is neither the one before the frame nor the one after (a fill of the
 * background before a canvas lands over it) flashed */
static uint8_t host_fl_on, host_fl_n[240 * 240];
static uint16_t host_fl_was[240 * 240], host_fl_mid[240 * 240];
static void host_put(uint32_t k, uint16_t v)
{
    if (host_fl_on) {
        if (!host_fl_n[k])
            host_fl_was[k] = host_screen[k];
        else if (host_screen[k] != host_fl_was[k] && host_fl_n[k] < 2u)
            host_fl_mid[k] = host_screen[k], host_fl_n[k] = 2;   /* (2: an in-between value kept) */
        if (!host_fl_n[k])
            host_fl_n[k] = 1;
    }
    host_screen[k] = v;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_put((y + j) * 240u + x + i, (uint16_t)((c >> 8) | (c << 8)));
}
static void lcd_sync(void) {}
/* MENU > SCREEN OFF (lcd.c lcd_power / lcd_wake_now): what the panel was told, in order (0 off: DISPOFF, SLPIN;
 * 1 SLPOUT; 2 DISPON; 3 lcd_wake_now), with the time; host_bl the panel showing (the backlight itself stays on: PA2 is
 * the key matrix's /OE too, lcd.c lcd_power) */
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
/* (the HOME oscilloscope's buffer, scope_buf: fx.c, with hostsim.c) */
static uint32_t host_blit_rows;                    /* rows blitted (what a frame draws) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blit_rows += h;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_put((y + j) * 240u + x + i, p[j * w + i]);
}
static struct { uint32_t stage; } mm_dbg;
#define MM_FLASH 0
#ifndef MM_VERSION
#define MM_VERSION "TEST"
#endif
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c mm_init */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_lit_ms, 0, sizeof midi_lit_ms); midi_seen = 0;   /* (1.5.5: no MIDI IN light) */
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(&ui, 0, sizeof ui);
    page_over = 0;                                /* (no layer's knobs; roll_settle's test page gone) */
    cin_left = cin_total = 0;                     /* (no count-in) */
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    memset(&mom, 0, sizeof mom);                  /* MOMENTARY: no value held (the pool at power-on) */
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    ml_off();                                     /* (MIDI LEARN off, nothing learned) */
    memset(&undo, 0, sizeof undo);
    fop_op = fop_stg = 0;                         /* (FM6's operator pages: OP 1, stage 1, as at power-on) */
    memset(pat_last, 0, sizeof pat_last);
    memset(pat_from, 0, sizeof pat_from);                /* (the header's slot letter: none) */
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);
        pat_last[i] = TRK_DEF[i][2];
    }
    undo_depth--;
    song.sel = 0;
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_GREY_INDEX;
    palette_set(UI_GREY_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_latched = 0;
    memcpy((void *)perf_map, PF_DEF, sizeof PF_DEF);    /* .. its default key map (the settings' 0), no key's effect */
    perf_remap = 0;
    memset(kb_fx, 0, sizeof kb_fx);
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
    memset(&motion, 0, sizeof motion);                  /* (main.c: no motion at power-on) */
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_locked, 0, sizeof motion_locked);
    motion_base_valid = motion_full = 0;
    memset(snd_said, 0, sizeof snd_said);               /* (.bss at power-on: nothing said yet) */
    memset(&scrn, 0, sizeof scrn);                        /* MENU > SCREEN OFF: on, idle from now (main.c) */
    scrn.idle = fm1_ms;
    kb_asleep = 0;
    panel_moved = 0;
}

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns) */
}






















/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The old GLOBAL page's four (1.2: a
 * test page over HOME > CLOCK, whose KNOB 4 is empty since TUNE went to the MENU): BPM (also in the header), SWG, CLK (a
 * name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static const page_t ROLL_PG = {"CLOCK", FAM_TRK, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}};
#define STRIP(c) CARD_X(c), Y_LABEL + ROLL_Y, CARD_X(c) + COL_W, Y_LABEL + ROLL_Y + ROLL_H





/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */



















#ifndef UI_TEST_NO_MAIN









/* "On the step sequencer all the LEDs go out": SEQ > STEP, the piano roll and the DRUM grid, stopped and playing,
 * in every LEDS mode, with the LEDs where the hardware has them (bsp/boards/fm1/fm1_input.h FM1_KEYMAP). The buttons keep
 * their glow and their active states; on the piano roll the keys glow under the held and playing ones; on the
 * grid its map is lit and its keys that do something glow (DIM LO / HI). Before: keys_own() took the grid's keys
 * out of the glow, so in DIM 26 of 27 keys of an empty 16-step grid went fully dark */





/* #39: KNOB 1..4 with a layer's button held are the layer's only, also as it is let go: a knob turned in the frame
 * the button is let go, while its map still shows (an effect key held after FX), or just after: the page under the
 * layer (ARP: ARP MODE on) or HOME kept its values. GLO / SCL / EDIT change only what their own knobs edit */
static int16_t lk_p[NTRK][P_COUNT], lk_g[G_COUNT];

/* #102: the TIMER5 scan counts the knobs' detents while ui_input runs; one that came after the layer's read of
 * KNOB 1..4 (host_enc_late) was read again by the page's: the layer's control and the page under it both moved
 * (HOME's KNOB 1 with FX's FILTER). Every layer (FX GLO SCL EDIT) x KNOB 1..4 x held past HOLD / a combo / locked
 * by a double tap, over every page and HOME, with late detents on the knob, PRESETS and ALGORITHM while it is open
 * and on the knob as it closes: the same state as with the detents read in time (the layer gets them all, one a
 * frame later), the layer's own control moved, and nothing under the layer changed (lk_same) */
typedef struct {
    int16_t p[NTRK][P_COUNT], g[G_COUNT];
    int8_t k[4];
    uint32_t sel, snd[NTRK], mot[NTRK];
    uint8_t home, page;
} rc_state_t;
static uint32_t rc_snd0;

















/* the header (ui_draw.c draw_head, 1.2's three slots): its elements measured in the render (every pixel off BG; an
 * element is a run of inked columns, split at 2 empty columns and at the battery's cell), each one's ink centred on row
 * 12 (top + bottom + 1 within 1 px of H_HEAD); the track cushion's ink from x 4; the battery's in 214..236, nothing
 * between 208 and it; with the centre shown the BPM's ink within 2 px of x 120; a message / a label centred on x 120
 * (its ink between 40 and 205, within 3 px); GLO with the tempo turned: the label right of the BPM and its lock, no
 * battery. Every state of the header, FLAT and LINE, every palette */
#define HEAD_EL 32
static int32_t hel[HEAD_EL][4], nhel;                  /* x0 x1 (last inked column) top bot */
enum { HS_STOPPED, HS_PLAY_REC, HS_SONG, HS_MSG, HS_OCT, HS_USB, HS_KEYCAP, HS_REC_OTHER, HS_BPM_TURN, HS_BPM_LOCK,
       HS_NOFILE, HS_REC_LAYER, HS_LAYER_LOCK, HS_GLO_TURN, HS_MSG2, HS_BEAT2, HS_COUNTIN, HS_BATT1, HS_BATT0, HS_BPM90,
       HS_N };
static const char *const HS_NAME[HS_N] = {"stopped", "play+REC", "song", "message", "OCT", "USB", "keycap", "REC other",
    "BPM turn", "BPM LOCK", "NO SAMPLE", "REC layer", "EDIT lock", "GLO turn", "msg2", "beat 2", "count-in", "battery 1",
    "battery 0", "BPM 90"};



/* SONG lanes (1.2, song_chain.c): a section names a slot per track ("-": silent). The rows of before (one slot each)
 * play bit for bit as before lanes (tests/song_probe.h, against the firmware before); each lane plays its own slot's
 * steps, LEN / DIV, QUANTIZE and automation; "-" plays nothing and releases; the section's length is its clock
 * track's loop (the first lane not silent; every lane silent: a 4/4 bar); all four restart together; STOP restores */
#include "song_probe.h"




/* FM6's operator pages (EDIT > OPERATOR, OP ENV, OPERATOR 2: ui_fm6op.c, Discussion #76): in an FM6 track's EDIT cycle
 * only; KNOB 1 the operator (shared by the three pages), KNOB 2..4 one byte each of the track's patch: every operator's
 * 14 bytes, each detent exactly that byte, SLOT OWN; the sound follows (a modulator's LEVEL brightens, a carrier's
 * lowers it, COARSE doubles the pitch, a slower attack, FIXED); a gesture is one undo (SAVE held: the patch and SLOT
 * back, held again: redone); projects and user presets keep the edited patch; the macros stay on top of it */
typedef struct { double e, early, hf; uint32_t zc; } fm6_snd_t;







/* Midi-Mania: SONGS (ARP): PLAY starts the song with its sounds and goes HOME, whose panel shows its notes falling.
 * Last in main: it renders 3 s of audio, whose reverb tail would reach the sound checks after it */
/* (the line of the first check of test_songs that fails) */
#define ok_at(e) do { int v_ = (e); if (!v_ && ok) printf("ui: SONGS: failed at line %d\n", __LINE__); ok &= v_; } while (0)
#define SONGS_GO() press(B_SEQ)                       /* (Midi-Mania: SEQ is the songs; HOME the game) */
#define SONGS_LEAVE() press(B_HOME)
#define EN_PART EN_ALGO
/* MM_SHOT_DIR set: the screen as it stands, to MM_SHOT_DIR/name.ppm (a look at the game's screens; no check) */
static void mm_shot(const char *name)
{
    const char *dir = getenv("MM_SHOT_DIR");
    char path[512];
    FILE *f;
    uint32_t i;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        fputc((c >> 11) * 255 / 31, f);
        fputc(((c >> 5) & 63u) * 255 / 63, f);
        fputc((c & 31u) * 255 / 31, f);
    }
    fclose(f);
}
static int test_songs(void)
{
    static int32_t o[2 * CTL];
    uint32_t b;
    int ok;
    if (!MM_NSONGS) {
        printf("ui: SONGS: no songs in midi/: skipped\n");
        return 0;
    }
    ui_power_on();
    SONGS_GO();
    turn(EN_SELECT, -100);                            /* the list's first row: FREE PLAY */
    ok = songs_on() && sg.free;
    {   /* FREE PLAY: PLAY, HOME; a key pressed, then let go, drawn from the line; the header says it; PLAY stops */
        uint32_t k = pl.mode == MM_DRUMS ? MM_DRUM_KEY[5] : 4u;   /* (DRUMS: KICK's pad) */
        press(B_PLAY);
        ok_at(player_is_free() && !songs_on() && ui.home);
        for (b = 0; b < 20u; b++)
            mix_block(o, CTL);
        fm1_in.notes = 1u << k;
        frame();
        for (b = 0; b < 40u; b++)
            mix_block(o, CTL);
        fm1_in.notes = 0;
        frame();
        for (b = 0; b < 40u; b++)
            mix_block(o, CTL);
        ui.force = 1;
        frame(); frame();
        mm_shot("free");
        ok_at(fp[0].used && fp[0].key == k && !fp[0].held && fp[0].t1 > fp[0].t0 && head_free() && pl.pos > fp[0].t1);
        {   /* MIDI IN on your part's channel: drawn as the keys are (player.c mm_midi_note) */
            uint32_t n0 = fp_next;
            midi_note_event(pl.mode, 38u, 100u);      /* (a snare on DRUMS; D2, folded up to D4's key, on KEYS) */
            ok_at(mm_midi_keys != 0u);
            frame();
            for (b = 0; b < 20u; b++)
                mix_block(o, CTL);
            midi_note_event(pl.mode, 38u, 0u);
            frame();
            ok_at(fp_next == n0 + 1u && fp[n0 % FP_N].used && !fp[n0 % FP_N].held && mm_midi_keys == 0u);
        }
        press(B_PLAY);
        mix_block(o, CTL);
        ok_at(!player_busy() && !player_is_free());
        frame();
        ok_at(!fp[0].used);                           /* (stopped: its presses gone) */
    }
    SONGS_GO();
    turn(EN_SELECT, 1);                               /* the first song */
    ok_at(songs_on() && !sg.free && sg.sel == 0u);
    {   /* your part: the one of song 1's first note you can play (another may start much later), KNOB 1 (ALGORITHM) */
        uint32_t want = MM_KEYS, i;
        for (i = 0; i < MM_SONGS[0].nnote; i++)
            if ((MM_PLAYABLE >> (MM_SONGS[0].note[i].flags & CN_PART_MASK)) & 1u) {
                want = MM_SONGS[0].note[i].flags & CN_PART_MASK;
                break;
            }
        for (i = 0; i < MM_NMODES && pl.mode != want; i++)
            turn(EN_PART, 1);
        ok_at(pl.mode == want);
    }
    press(B_PLAY);
    ok_at(player_busy() && pl.req_song == 0u && transport_req == 0u &&
          trk[pl.mode].eng_req == MM_SONGS[0].part[pl.mode].engine && !songs_on() && ui.home);
    for (b = 0; b < CHART_FS * 20u / CTL && !player_keys(pl.mode); b++)   /* to its first notes (at most 20 s), */
        mix_block(o, CTL);                            /* HOME drawn over them (ui_graph.c graph_notes) */
    ok_at(pl.playing && pl.on_n > 0u && player_keys(pl.mode) != 0u &&   /* (at once: the frames let time pass) */
          (key_leds(0) & player_keys(pl.mode)) == player_keys(pl.mode));
    ui.force = 1;
    frame(); frame();
    {   /* HOME's cards while it plays: KNOB 1 SPEED, 2 OFFSET, 3 TEMPO (live), 4 SCROLL; the sound's untouched */
        int16_t before[P_COUNT];
        uint32_t own = pl_song()->bpm_x10, sp;
        ok_at(pl.speed[MM_DRUMS] == 8u && pl.speed[MM_KEYS] == 4u);   /* (DRUMS' NOTE SPEED starts at 8) */
        gc_on = 0;                                    /* no knob turned: the cards hidden, the notes' panel tall */
        ok_at(!game_cards_shown() && nt_tall());
        frame();
        mm_shot("part_tall");
        {   /* the notes full screen send only their changed rows (graph_tall), every frame: after frames of the song
             * playing, the screen is what a full redraw at the same moment gives, for fewer rows */
            static uint16_t inc[240u * 240u];
            uint32_t k, sent = 0, steps = 0;
            for (k = 0; k < 12u; k++) {
                for (b = 0; b < 25u; b++)                 /* (~17 ms of song a frame) */
                    mix_block(o, CTL);
                frame();
                sent += nt_sent_rows;
                steps += nt_sent_rows > 0u;
            }
            memcpy(inc, host_screen, sizeof inc);
            ui.force = 1;
            frame();
            ok_at(nt_tall() && steps >= 6u && nt_sent_rows == NT_TALL_H && sent < 12u * NT_TALL_H / 2u &&
                  !memcmp(inc + NT_TALL_Y * 240u, host_screen + NT_TALL_Y * 240u, NT_TALL_PX * 2u));
            printf("ui: the notes full screen: %u of %u rows sent over 12 frames\n", (unsigned)sent,
                   (unsigned)(12u * NT_TALL_H));
            frame();                                       /* (the same moment: nothing changed, nothing sent) */
            ok_at(nt_sent_rows == 0u && !memcmp(inc + NT_TALL_Y * 240u, host_screen + NT_TALL_Y * 240u, NT_TALL_PX * 2u));
        }
        ok_at(nt_used() != 0u && (pl.mode == MM_DRUMS || (nt_used() & player_keys(pl.mode)) == player_keys(pl.mode)));
        {   /* RANGE narrower: fewer columns used (the rest greyed out), none new */
            uint32_t all = nt_used(), r = pl.range;
            pl.range = MM_RANGES - 1u;
            ok_at((nt_used() & ~all) == 0u);
            pl.range = (uint8_t)r;
        }
        PL_SPEED = 4u;                                /* (DRUMS start at 8: from 4 here) */
        sp = PL_SPEED;
        memcpy(before, TSEL->p, sizeof before);
        ok_at(game_cards() && ui.home);
        turn(EN_K1, 1);
        ok_at(PL_SPEED == sp + 1u && game_cards_shown() && !nt_tall());   /* (a turn: the cards shown) */
        turn(EN_K1, -1);
        turn(EN_K2, 3);
        ok_at(pl.offset == 3);
        turn(EN_K2, -3);
        turn(EN_K3, 5);
        ok_at(gc_bpm() == own + 50u);
        turn(EN_K3, -5);
        ok_at(gc_bpm() == own && pl.rate == PL_RATE_ONE);
        turn(EN_K4, 1);
        ok_at(pl.up == 1u);
        turn(EN_K4, -1);
        ok_at(pl.up == 0u && PL_SPEED == sp && pl.offset == 0 && !memcmp(before, TSEL->p, sizeof before));
        ui.force = 1;
        frame();
        mm_shot("part_cards");
    }
    press(B_PLAY);
    ok_at((pl.req == PL_REQ_STOP || !pl.playing) && transport_req == 0u);
    mix_block(o, CTL);
    ok_at(!pl.playing && player_keys(pl.mode) == 0u);
    SONGS_GO();
    ok_at(songs_on());
    SONGS_LEAVE();
    ok_at(!songs_on());
    {   /* KNOB 2: the tempo, OCT-: the song's own again */
        uint32_t own = MM_SONGS[0].bpm_x10;
        SONGS_GO();
        turn(EN_K2, 6);
        ok_at(sg.bpm_x10 == own + 60u);
        turn(EN_K2, -2000);
        ok_at(sg.bpm_x10 == own / 2u);
        press(B_OCTDN);
        ok_at(sg.bpm_x10 == own && song.octave == 0);
        turn(EN_K3, 1);                               /* KNOB 3: PLAY YOU, back to AUTO */
        ok_at(pl.you == 1u);
        turn(EN_K3, -1);
        ok_at(pl.you == 0u);
        turn(EN_K4, 2);                               /* KNOB 4: RANGE, held at its ends */
        ok_at(pl.range == 2u);
        turn(EN_K4, 9);
        ok_at(pl.range == MM_RANGES - 1u);
        ui.force = 1;
        frame();
        turn(EN_K4, -9);
        ok_at(pl.range == 0u);
        SONGS_LEAVE();
    }
    {   /* GAME (FX): KNOB 1 SCROLL UP (the drums below drawn rising), KNOB 2 NOTE SPEED, KNOB 3 OFFSET, OCT- 0 */
        press(B_FX);
        ok_at(game_on() && !songs_on());
        turn(EN_K1, 1);
        ok_at(pl.up == 1u);
        PL_SPEED = 4u;
        turn(EN_K2, 2);
        ok_at(PL_SPEED == 6u && pl_ahead() == CHART_FS * 14u / 10u);
        turn(EN_K2, 20);
        ok_at(PL_SPEED == PL_SPEED_MAX && PL_SPEED_MAX == 16u && pl_ahead() == CHART_FS * 4u / 10u);
        turn(EN_K2, -3);
        turn(EN_K3, -5);
        ok_at(pl.offset == -5);
        press(B_OCTDN);
        ok_at(pl.offset == 0 && song.octave == 0);
        ui.force = 1;
        frame();
        press(B_FX);
        ok_at(!game_on());
        PL_SPEED = 4u;
    }
    {   /* a song with drums: KNOB 1 to DRUMS, PLAY: HOME's drum lanes, a hit lights its lane's key */
        uint32_t i, d = MM_NSONGS;
        for (i = 0; i < MM_NSONGS && d == MM_NSONGS; i++)
            if ((MM_SONGS[i].parts >> MM_DRUMS) & 1u)
                d = i;
        if (d < MM_NSONGS) {
            SONGS_GO();
            turn(EN_SELECT, (int32_t)d + 1 - (int32_t)sg_row());
            for (i = 0; i < MM_NMODES && pl.mode != MM_DRUMS; i++)
                turn(EN_PART, 1);
            ok_at(sg.sel == d && pl.mode == MM_DRUMS);
            press(B_PLAY);
            ok_at(player_busy() && trk[MM_DRUMS].eng_req == ENGI_DRUM && ui.home);
            for (i = 0; i < CHART_FS * 4u / CTL && !player_keys(MM_DRUMS); i++)
                mix_block(o, CTL);
            ui.force = 1;
            frame();
            mm_shot("drums");
            ok_at(player_keys(MM_DRUMS) != 0u && (player_keys(MM_DRUMS) & ~0x2A52Au) == 0u);   /* (F#3 .. A#4) */
            if ((MM_SONGS[d].parts >> MM_KEYS) & 1u) {   /* ALGORITHM while it plays: KEYS, its lanes; RIGHT HAND */
                turn(EN_PART, 1);
                ok_at(pl.mode == MM_KEYS && song.sel == MM_KEYS);
                pl.range = 1u;
                for (i = 0; i < CHART_FS * 2u / CTL; i++)
                    mix_block(o, CTL);
                ui.force = 1;
                frame();
                mm_shot("keys");
                pl.range = 0u;
                turn(EN_PART, -1);
                ok_at(pl.mode == MM_DRUMS);
            }
            press(B_PLAY);
            mix_block(o, CTL);
        } else {
            printf("ui: SONGS: no song with drums in midi/: its check skipped\n");
        }
    }
    press(B_FX);                                      /* GAME's KNOB 1 back: SCROLL DOWN */
    turn(EN_K1, -1);
    ok_at(pl.up == 0u);
    press(B_HOME);
    ok_at(!game_on());
    return check("SONGS (SEQ): FREE PLAY first (your presses drawn); PLAY starts a song on its sounds and goes HOME (your part's notes, its keys lit); "
                 "PLAY stops; K2 the tempo, OCT- its own; K1 DRUMS: the drum lanes; GAME (FX): SCROLL SPEED OFFSET", ok);
}

/* Midi-Mania's build (core.h MM_ENGINES_FULL 0): each part offers its own sounds (engines.c MM_SOUNDS), PRESETS and
 * SONGS' SOUND those only, no other engine loads. The power-on sounds: PIANO, DRUM kit, PLUCK, FM BASS */
static int test_mm_engines(void)
{
    static const char *const WANT[NPART][8] = {
        {"PIANO", "FLUTE", "TINE EP", "ORGAN", "HARP", "KALIMBA", "MARIMBA"}, {0},
        {"PLUCK", "HARP", "KALIMBA", "DRONE STRING"}, {"FM BASS", "PLUCK", "SUB BASS", "SQR BASS"}};
    static const uint8_t ON[NPART] = {ENGI_SAMPLE, ENGI_DRUM, ENGI_PHYS, ENGI_FM6};
    uint32_t i, all, k, e, pi, bad_name = 0;
    int ok = ENGINES[ENGI_ANALOG] == &ENG_ANALOG && ENGINES[ENGI_PHYS] == &ENG_PHYS && ENGINES[2] == &ENG_MISSING &&
             SMP_SET_ORIG[2] == 2u;                   /* (ANALOG and FLUTE back; PHASE's number stays, missing) */
    ui_power_on();
    for (i = 0; i < NPART; i++) {
        song.sel = (uint8_t)i;
        ok &= trk[i].eng_req == ON[i] && trk[i].preset == TRK_DEF[i][1];
        preset_all_pos(&all);
        ok &= all == (i == MM_DRUMS ? ENGINES[ENGI_DRUM]->npresets : mm_sound_n(i));
        for (k = 0; k < all; k++) {
            e = preset_all_at(k, &pi);
            if (i == MM_DRUMS)
                bad_name += e != ENGI_DRUM;
            else
                bad_name += !WANT[i][k] || !str_eq(ENGINES[e]->presets[pi].name, WANT[i][k]);
        }
    }
    ok &= !bad_name;
    set_engine_of(&trk[MM_DRUMS], ENGI_FM6);           /* refused: no FM6 sound on DRUMS */
    set_engine_of(&trk[MM_GUITAR], ENGI_ANALOG);       /* refused: GUITAR has no ANALOG sound */
    ok &= trk[MM_DRUMS].eng_req == ENGI_DRUM && trk[MM_GUITAR].eng_req == ENGI_PHYS;
    pl.mode = MM_KEYS;                                 /* SONGS' SOUND (PRESETS): KEYS from PIANO to FLUTE, to TINE EP */
    mm_sound_step(MM_KEYS, 1);
    ok &= trk[MM_KEYS].eng_req == ENGI_SAMPLE && str_eq(mm_sound_name(MM_KEYS), "FLUTE");
    mm_sound_step(MM_KEYS, 1);
    ok &= trk[MM_KEYS].eng_req == ENGI_FM6 && str_eq(mm_sound_name(MM_KEYS), "TINE EP");
    mm_sound_step(MM_KEYS, -2);
    ok &= trk[MM_KEYS].eng_req == ENGI_SAMPLE && str_eq(mm_sound_name(MM_KEYS), "PIANO");
    pl.mode = MM_BASS;
    mm_sound_step(MM_BASS, -2);                        /* (wraps: FM BASS back to SUB BASS) */
    ok &= trk[MM_BASS].eng_req == ENGI_ANALOG && str_eq(mm_sound_name(MM_BASS), "SUB BASS");
    song.sel = 0;
    pl.mode = 0;
    return check("Midi-Mania: each part its own sounds (KEYS PIANO FLUTE .. MARIMBA, BASS .. SUB BASS SQR BASS); "
                 "SONGS' SOUND", ok);
}
/* Midi-Mania without the sequencer (core.h MM_SEQ_FULL 0): the panel. HOME the songs, ARP the game, FX GAME, EDIT the
 * MIXER, SCL the CLOCK, SAVE PRESETS; ENV LFO GLO SEQ REC nothing; PLAY the selected song (no sequencer); their pages */
static int test_mm_panel(void)
{
    static const uint8_t NOTHING[4] = {B_ENV, B_LFO, B_GLO, B_REC};
    uint32_t i, shown = 0;
    int ok;
    ui_power_on();
    songs_close();
    game_close();
    press(B_SEQ);
    ok = songs_on() && !game_on();
    press(B_SEQ);
    ok &= !songs_on() && ui.home;                     /* (SEQ on SONGS: the game) */
    press(B_SEQ);
    press(B_HOME);
    ok &= !songs_on() && ui.home && game_cards();     /* HOME: the game */
    press(B_EDIT);
    ok &= !songs_on() && !ui.home && cur_page()->graph == GR_TRK;   /* MIXER */
    press(B_SCL);
    ok &= metro_on() && !songs_on() && !game_on();   /* METRONOME: K1 CLICK, K2 LEVEL, K3 COUNT-IN (MENU's) */
    turn(EN_K1, 1);
    turn(EN_K2, 5);
    turn(EN_K3, 1);
    ui.force = 1;
    frame();
    ok &= rp_get(RP_CLICK) == CLICK_ON && click_mode == CLICK_ON && rp_get(RP_LEVEL) == 2u && rp_get(RP_COUNTIN) == 1u &&
          cin_bars == 1u;
    turn(EN_K1, -1);
    turn(EN_K2, -1);
    turn(EN_K3, -1);
    ok &= rp_get(RP_CLICK) == CLICK_OFF && rp_get(RP_LEVEL) == 1u && rp_get(RP_COUNTIN) == 0u;
    press(B_SCL);
    ok &= !metro_on();
    press(B_SCL);
    ok &= metro_on();
    press(B_ARP);
    ok &= ui.home && !songs_on() && !game_on() && game_cards();     /* the game: SPEED OFFSET TEMPO SCROLL */
    press(B_FX);
    ok &= game_on();
    press(B_FX);
    ok &= !game_on() && ui.home;
    press(B_SAVE);
    ok &= !ui.home && cur_page()->graph == GR_BROWSE;               /* PRESETS */
    for (i = 0; i < 4u; i++) {
        uint32_t pg = ui.page, home = ui.home;
        press(NOTHING[i]);
        ok &= ui.page == pg && ui.home == home && !ui.layer && !songs_on() && !game_on() && !song.rec;
    }
    for (i = 0; i < NPAGES; i++)
        shown |= (uint32_t)page_visible(i) << (PAGES[i].fam % 32u);
    ok &= shown == (1u << FAM_SAVE | 1u << FAM_TRK);                /* PRESETS, MIXER, CLOCK: no other page */
    if (MM_NSONGS) {
        press(B_ARP);
        press(B_PLAY);                                /* PLAY: the selected song, no sequencer */
        ok &= player_busy() && !song.playing && transport_req == 0u && ui.home;
        press(B_PLAY);
        ok &= (pl.req == PL_REQ_STOP || !pl.playing) && !song.playing && transport_req == 0u;
        player_stop();
    }
    if (MM_NSONGS) {                                  /* ALGORITHM: your part, DRUMS and KEYS only (GUITAR BASS later) */
        uint32_t t, ok2 = 1;
        press(B_HOME);
        for (t = 0; t < 6u; t++) {
            turn(EN_ALGO, 1);
            ok2 &= (pl.mode == MM_DRUMS || pl.mode == MM_KEYS) && song.sel == pl.mode;
        }
        ok &= ok2;
    }
    return check("Midi-Mania panel: HOME game, SEQ songs, FX GAME, EDIT MIXER, SCL METRONOME, SAVE PRESETS, ALGORITHM part, "
                 "PLAY the song",
                 ok);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0;
    /* Midi-Mania: its own checks (Felucca's UI checks were of what it leaves out: the full list is in Felucca's tests/ui_test.c) */
    bad += test_mm_engines();
    bad += test_mm_panel();
    bad += test_songs();
    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif
