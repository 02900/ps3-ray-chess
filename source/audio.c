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

#define SR 22050   /* synthesis sample rate for the check/win tones */

static int     audio_ok = 0;
static SAMPLE *s_click = NULL, *s_cancel = NULL, *s_check = NULL, *s_win = NULL;

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

/* ---- tiny PCM synthesizer for the check / victory cues ------------------- */
/* Append a square-wave segment sweeping f0->f1 over ms at amp; f0<=0 => silence.
 * Short attack/release ramps avoid clicks; phase carries across calls. */
static int seg(SWORD *buf, int cap, int pos, double f0, double f1,
               int ms, int amp, double *phase)
{
	int n = (int)((long)ms * SR / 1000); if (n < 1) n = 1;
	int atk = SR * 3 / 1000, rel = SR * 10 / 1000;
	for (int i = 0; i < n && pos < cap; i++, pos++) {
		if (f0 <= 0 && f1 <= 0) { buf[pos] = 0; continue; }
		double frac = (double)i / n;
		double f = f0 + (f1 - f0) * frac;
		*phase += f / SR;
		if (*phase >= 1.0) *phase -= (double)(long)*phase;
		double e = 1.0;
		if (i < atk)          e = (double)i / atk;
		else if (i > n - rel) e = (double)(n - i) / rel;
		double v = (*phase < 0.5) ? amp : -amp;
		int s = (int)(v * e);
		buf[pos] = (SWORD)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	}
	return pos;
}

static void put_le32(unsigned char *p, u32 v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put_le16(unsigned char *p, u16 v) { p[0]=v; p[1]=v>>8; }

/* Wrap PCM (n mono 16-bit samples) in a little-endian WAV and load it. */
static SAMPLE *load_pcm(const SWORD *pcm, int n)
{
	long data = (long)n * 2, total = 44 + data;
	unsigned char *w = (unsigned char *)malloc(total);
	if (!w) return NULL;
	memcpy(w, "RIFF", 4);       put_le32(w + 4, (u32)(36 + data));
	memcpy(w + 8, "WAVE", 4);
	memcpy(w + 12, "fmt ", 4);  put_le32(w + 16, 16);
	put_le16(w + 20, 1);        put_le16(w + 22, 1);          /* PCM, mono   */
	put_le32(w + 24, SR);       put_le32(w + 28, SR * 2);     /* rate, byterate */
	put_le16(w + 32, 2);        put_le16(w + 34, 16);         /* block, bits */
	memcpy(w + 36, "data", 4);  put_le32(w + 40, (u32)data);
	for (int i = 0; i < n; i++) put_le16(w + 44 + i * 2, (u16)pcm[i]);
	SAMPLE *s = load_wav(w, total);
	free(w);
	return s;
}

static SWORD g_scratch[SR * 3];   /* up to 3 s */

static SAMPLE *synth_check(void)   /* two urgent descending beeps */
{
	double ph = 0; int n = 0;
	n = seg(g_scratch, SR * 3, n, 880, 880, 90, 9000, &ph);
	n = seg(g_scratch, SR * 3, n,   0,   0, 40,    0, &ph);
	n = seg(g_scratch, SR * 3, n, 740, 740, 110, 9000, &ph);
	return load_pcm(g_scratch, n);
}

static SAMPLE *synth_win(void)     /* short rising victory fanfare (C-E-G-C) */
{
	static const int mf[4] = { 523, 659, 784, 1047 };
	static const int ml[4] = { 130, 130, 130, 320 };
	double ph = 0; int n = 0;
	for (int k = 0; k < 4; k++) {
		n = seg(g_scratch, SR * 3, n, mf[k], mf[k], ml[k] - 12, 8000, &ph);
		n = seg(g_scratch, SR * 3, n, 0, 0, 12, 0, &ph);
	}
	return load_pcm(g_scratch, n);
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
	s_check  = synth_check();
	s_win    = synth_win();

	audio_ok = 1;
}

void audio_update(void)   { if (audio_ok) MikMod_Update(); }

void audio_shutdown(void)
{
	if (!audio_ok) return;
	audio_ok = 0;
	if (s_click)  Sample_Free(s_click);
	if (s_cancel) Sample_Free(s_cancel);
	if (s_check)  Sample_Free(s_check);
	if (s_win)    Sample_Free(s_win);
	MikMod_DisableOutput();
	MikMod_Exit();
}

void audio_play_click(void)  { if (audio_ok && s_click)  Sample_Play(s_click, 0, 0); }
void audio_play_cancel(void) { if (audio_ok && s_cancel) Sample_Play(s_cancel, 0, 0); }
void audio_play_check(void)  { if (audio_ok && s_check)  Sample_Play(s_check, 0, 0); }
void audio_play_win(void)    { if (audio_ok && s_win)    Sample_Play(s_win, 0, 0); }
