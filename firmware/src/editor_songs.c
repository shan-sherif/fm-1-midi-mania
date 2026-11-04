/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 fm-1-midi-mania contributors */
/* Midi-Mania: the stored songs over USB (web/EDITOR_PROTOCOL.md "Songs"; sent from Studio, platforms/web/index.html
 * "On your FM-1"). A song
 * is a chart (chart.h, docs/CHART_FORMAT.md) in the song region (player.c MM_SONG_BASE), in the first gap of free
 * 4 KiB sectors it fits (mm_song_room); deleting one erases its sectors. Nothing ever moves, so a power cut or a pulled
 * cable loses at most the song being written (its header is written last, after its CRC holds) or deleted (its header
 * sector erased first). A song playing stops first: the list's order changes, and an erase stalls the audio.
 *   SONG_LIST    -> built in, stored, max, sectors, free sectors, largest gap (sectors);
 *                   per stored song: sector, sectors, parts, bpm x10 (2 x 7 bit), seconds (2 x 7 bit), title (24, 0-ended)
 *   SONG_BEGIN   size (3 x 7 bit) -> rc, sector         rc 0 ok, 1 bad size, 2 no room, 3 16 songs, 4 flash, 5 busy
 *                its first sector erased; the chart goes there
 *   SONG_WRITE   off (3 x 7 bit), pack7 data -> off (3 x 7 bit), rc
 *                the chart's bytes from off 256 on, 256 a frame, in order (the last may be shorter); a sector erased
 *                when the writes reach its start. rc 0 ok, 1 bad offset or length, 2 erase, 3 write, 4 no BEGIN
 *   SONG_END     pack7 of the chart's first 256 bytes (all of it if shorter) -> rc, sector
 *                the header checked, the CRC over the whole chart, then the first 256 bytes written and the songs
 *                listed again. rc 0 ok, 1 no BEGIN or a bad frame, 2 a bad header (or not the BEGIN's size), 3 CRC,
 *                4 flash, 5 busy (a song would not stop)
 *   SONG_DELETE  sector -> rc, sector                  rc 0 ok, 1 no song starts there, 4 flash, 5 busy
 *   SONG_READ    sector, off (3 x 7 bit) -> sector, off (3 x 7 bit), rc, pack7 data
 *                up to 256 of the chart's bytes from off (the backup: web/fm1backup.js); rc 0 ok, 1 no song starts
 *                there, 2 off not a multiple of 256 or past its end. Reads only: nothing stops */
enum { ED_SONG_LIST = 88, ED_SONG_BEGIN, ED_SONG_WRITE, ED_SONG_END, ED_SONG_DELETE, ED_SONG_READ };
#define ED_SONG_HEAD 256u                           /* the chart's first bytes: written by SONG_END, last */

static struct {
    uint8_t on;                                     /* a BEGIN taken: at, size */
    uint32_t at, size;
} ed_sg;
static uint8_t ed_sg_buf[ED_SONG_HEAD] __attribute__((aligned(4), section(".pool")));

static uint32_t ed_u21(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14; }
static void ed_b21(uint32_t v) { ed_b(v); ed_b(v >> 7); ed_b(v >> 14); }
static void ed_sg_inval(uint32_t off, uint32_t n)
{
    fm1_irq_off();
    fl_inval(MM_SONG_BASE + off, n);
    fm1_irq_on();
}
/* a song playing stopped (FREE PLAY keeps going: it reads no song), and the sequencer's transport (ed_flash_stop) */
static int ed_sg_stop(void)
{
    uint32_t t0 = fm1_ms;
    if (player_busy() && !player_is_free()) {
        player_stop();
        while ((pl.playing || pl.req == PL_REQ_STOP) && fm1_ms - t0 < 100u)   /* the audio ISR stops it at its next block */
            fm1_wdt_feed();
        if (pl.playing)
            return 1;
    }
    return ed_flash_stop();
}
static int ed_sg_erase(uint32_t off)                /* one sector of the region */
{
    uint32_t took;
    int rc;
    audio_silence();
    rc = fl_erase4k(MM_SONG_BASE + off, &took);
    fm1_wdt_feed();
    ed_sg_inval(off, MM_SONG_SECTOR);
    return rc;
}
static uint32_t ed_sg_begin(uint32_t size, uint32_t *at)
{
    int32_t r;
    ed_sg.on = 0;
    if (size < sizeof(chart_head_t) || size > MM_SONG_SIZE)
        return 1;
    if (mm_nstored >= MM_SONG_MAX)
        return 3;
    if ((r = mm_song_room(size)) < 0)
        return 2;
    if (ed_sg_stop())
        return 5;
    *at = (uint32_t)r;
    if (ed_sg_erase(*at))                           /* (its header's sector: a broken upload's never lists) */
        return 4;
    ed_sg.at = *at;
    ed_sg.size = size;
    ed_sg.on = 1;
    return 0;
}
static uint32_t ed_sg_write(uint32_t off, const uint8_t *a, uint32_t na)
{
    uint32_t len;
    if (!ed_sg.on)
        return 4;
    len = ed_unpack7(a, na, ed_sg_buf, ED_SONG_HEAD);
    if (off < ED_SONG_HEAD || (off & (ED_SONG_HEAD - 1u)) || !len || off + len > ed_sg.size ||
        (len < ED_SONG_HEAD && off + len != ed_sg.size))
        return 1;
    if (!(off & (MM_SONG_SECTOR - 1u)) && ed_sg_erase(ed_sg.at + off))   /* the first write into a sector */
        return 2;
    if (fl_write(MM_SONG_BASE + ed_sg.at + off, ed_sg_buf, len))
        return 3;
    ed_sg_inval(ed_sg.at + off, len);
    return 0;
}
static uint32_t ed_sg_end(const uint8_t *a, uint32_t na)
{
    const chart_head_t *h = (const chart_head_t *)ed_sg_buf;
    uint32_t n = ed_sg.size < ED_SONG_HEAD ? ed_sg.size : ED_SONG_HEAD, c;
    if (!ed_sg.on)
        return 1;
    ed_sg.on = 0;                                    /* (one END an upload: a bad one ends it too) */
    if (ed_unpack7(a, na, ed_sg_buf, n) != n)
        return 1;
    if (h->magic != CHART_MAGIC || h->head_size < sizeof(chart_head_t) || h->head_size > n || (h->head_size & 3u) ||
        h->live != 0xFFu || h->nbeat > MM_SONG_SIZE / 4u || h->nnote > MM_SONG_SIZE / sizeof(chart_note_t) ||
        h->head_size + h->nbeat * 4u + h->nnote * (uint32_t)sizeof(chart_note_t) != ed_sg.size)
        return 2;
    c = mm_crc32_add(0xFFFFFFFFu, ed_sg_buf + h->head_size, n - h->head_size);
    if (ed_sg.size > n)
        c = mm_crc32_add(c, mm_song_base + ed_sg.at + n, ed_sg.size - n);
    if (~c != h->crc32)
        return 3;
    if (ed_sg_stop())                                /* (a song started since BEGIN: the list's order changes) */
        return 5;
    if (fl_write(MM_SONG_BASE + ed_sg.at, ed_sg_buf, n))
        return 4;
    ed_sg_inval(ed_sg.at, n);
    mm_song_scan();
    return 0;
}
static uint32_t ed_sg_delete(uint32_t sec)
{
    uint32_t i, s, end;
    for (i = 0; i < mm_nstored && mm_stored_off[i] != sec * MM_SONG_SECTOR; i++)
        ;
    if (i == mm_nstored)
        return 1;
    if (ed_sg_stop())
        return 5;
    ed_sg.on = 0;                                    /* (an upload under way: its room may have changed) */
    end = (mm_stored_off[i] + mm_stored_size[i] + MM_SONG_SECTOR - 1u) / MM_SONG_SECTOR;
    for (s = sec; s < end; s++)                      /* its header's sector first: it is gone from then on */
        if (ed_sg_erase(s * MM_SONG_SECTOR)) {
            mm_song_scan();
            return 4;
        }
    mm_song_scan();
    return 0;
}

static int ed_songs_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, at = 0, rc;
    if (cmd < ED_SONG_LIST || cmd > ED_SONG_READ)
        return 0;
    if (cmd == ED_SONG_READ) {                       /* (reads the songs listed: none without flash) */
        uint32_t sec = na == 4u ? a[0] : 0u, off = na == 4u ? ed_u21(a + 1) : 0u, n = 0;
        for (i = 0; i < mm_nstored && mm_stored_off[i] != sec * MM_SONG_SECTOR; i++)
            ;
        rc = na != 4u || i == mm_nstored ? 1u : (off & (ED_SONG_HEAD - 1u)) || off >= mm_stored_size[i] ? 2u : 0u;
        ed_b(sec);
        ed_b21(off);
        ed_b(rc);
        if (!rc) {
            n = mm_stored_size[i] - off < ED_SONG_HEAD ? mm_stored_size[i] - off : ED_SONG_HEAD;
            ed_bk_pack(mm_song_base + mm_stored_off[i] + off, n);
        }
        return 1;
    }
    if (cmd != ED_SONG_LIST && (!flash_ok || !mm_song_base)) {   /* (no flash: refused as busy) */
        ed_b(5);
        ed_b(0);
        return 1;
    }
    switch (cmd) {
    case ED_SONG_LIST:
        ed_b(MM_NSONGS);
        ed_b(mm_nstored);
        ed_b(MM_SONG_MAX);
        ed_b(MM_SONG_NSEC);
        ed_b(mm_song_free_sectors());
        ed_b(mm_song_largest_gap());
        for (i = 0; i < mm_nstored; i++) {
            const mm_song_t *s = &mm_stored[i];
            uint32_t secs = (s->length + CHART_FS / 2u) / CHART_FS;
            ed_b(mm_stored_off[i] / MM_SONG_SECTOR);
            ed_b((mm_stored_size[i] + MM_SONG_SECTOR - 1u) / MM_SONG_SECTOR);
            ed_b(s->parts);
            ed_b(s->bpm_x10);
            ed_b(s->bpm_x10 >> 7);
            ed_b(secs > 16383u ? 16383u : secs);
            ed_b((secs > 16383u ? 16383u : secs) >> 7);
            ed_str(s->title, 24);
        }
        break;
    case ED_SONG_BEGIN:
        rc = na == 3u ? ed_sg_begin(ed_u21(a), &at) : 1u;
        ed_b(rc);
        ed_b(at / MM_SONG_SECTOR);
        break;
    case ED_SONG_WRITE:
        rc = na >= 5u ? ed_sg_write(ed_u21(a), a + 3, na - 3u) : 1u;
        ed_b21(na >= 3u ? ed_u21(a) : 0u);
        ed_b(rc);
        break;
    case ED_SONG_END:
        at = ed_sg.at;
        rc = ed_sg_end(a, na);
        ed_b(rc);
        ed_b(at / MM_SONG_SECTOR);
        break;
    case ED_SONG_DELETE:
        rc = na == 1u ? ed_sg_delete(a[0]) : 1u;
        ed_b(rc);
        ed_b(na == 1u ? a[0] : 0u);
        break;
    }
    return 1;
}
