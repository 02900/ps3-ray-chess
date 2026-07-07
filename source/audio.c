/*
 * MikMod audio for RayChess — plays ray-chess's original click / clickCancel
 * WAVs (16-bit mono PCM), embedded via bin2o and parsed from RAM through an
 * in-memory MREADER. No audio assets on disk and no raylib audio device (which
 * isn't linkable on the RSXGL stack). Init is defensive: any failure leaves
 * audio_ok = 0 and every call becomes a no-op.
 *
 * (MREADER/load_wav adapted from ps3-openfight's audio.c.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-types.h>
#include <mikmod.h>

#include "audio.h"

/* bin2o symbols for data/click.bin and data/clickcancel.bin. */
extern const unsigned char click_bin[];
extern const unsigned int  click_bin_size;
extern const unsigned char clickcancel_bin[];
extern const unsigned int  clickcancel_bin_size;

static int     audio_ok = 0;
static SAMPLE *s_click = NULL, *s_cancel = NULL;

/* ---- in-memory MREADER (lets MikMod parse a WAV from a RAM buffer) -------- */
typedef struct { MREADER core; const unsigned char *data; long size, pos; } MemReader;

static BOOL mr_eof(MREADER *r)  { MemReader *m = (MemReader *)r; return m->pos >= m->size; }
static long mr_tell(MREADER *r) { return ((MemReader *)r)->pos; }
static int  mr_get(MREADER *r)
{
	MemReader *m = (MemReader *)r;
	return (m->pos >= m->size) ? EOF : m->data[m->pos++];
}
static BOOL mr_read(MREADER *r, void *dst, size_t n)
{
	MemReader *m = (MemReader *)r;
	long rem = m->size - m->pos;
	if ((long)n > rem) { if (rem > 0) { memcpy(dst, m->data + m->pos, rem); m->pos += rem; } return 0; }
	memcpy(dst, m->data + m->pos, n); m->pos += n; return 1;
}
static BOOL mr_seek(MREADER *r, long off, int whence)
{
	MemReader *m = (MemReader *)r;
	long base = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? m->pos : m->size;
	long np = base + off;
	if (np < 0 || np > m->size) return -1;
	m->pos = np; return 0;
}
static SAMPLE *load_wav(const void *data, long size)
{
	MemReader mr;
	mr.core.Seek = mr_seek; mr.core.Tell = mr_tell; mr.core.Read = mr_read;
	mr.core.Get = mr_get;   mr.core.Eof = mr_eof;
	mr.data = (const unsigned char *)data; mr.size = size; mr.pos = 0;
	return Sample_LoadGeneric(&mr.core);
}

/* ---- public API ---------------------------------------------------------- */
void audio_init(void)
{
	if (audio_ok) return;

	MikMod_RegisterAllDrivers();
	MikMod_RegisterAllLoaders();

	md_mode = DMODE_STEREO | DMODE_16BITS | DMODE_SOFT_MUSIC | DMODE_SOFT_SNDFX;
	md_mixfreq = 48000;

	if (MikMod_Init("")) return;                        /* silent on failure */
	MikMod_SetNumVoices(0, 8);
	if (MikMod_EnableOutput()) { MikMod_Exit(); return; }

	s_click  = load_wav(click_bin,       (long)click_bin_size);
	s_cancel = load_wav(clickcancel_bin, (long)clickcancel_bin_size);

	audio_ok = 1;
}

void audio_update(void)   { if (audio_ok) MikMod_Update(); }

void audio_shutdown(void)
{
	if (!audio_ok) return;
	audio_ok = 0;
	if (s_click)  Sample_Free(s_click);
	if (s_cancel) Sample_Free(s_cancel);
	MikMod_DisableOutput();
	MikMod_Exit();
}

void audio_play_click(void)  { if (audio_ok && s_click)  Sample_Play(s_click, 0, 0); }
void audio_play_cancel(void) { if (audio_ok && s_cancel) Sample_Play(s_cancel, 0, 0); }
