/*
 * MikMod audio for RayChess. raylib's own audio device isn't linkable on the
 * RSXGL stack, so SFX go through MikMod: the original click / clickCancel WAVs
 * are embedded via bin2o (the data/ .bin blobs) and played from memory.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);      /* once at startup (after the PS3 stack is up)  */
void audio_update(void);    /* every frame — drives MikMod's software mixer */
void audio_shutdown(void);  /* on exit                                      */

void audio_play_click(void);   /* piece selected / move played */
void audio_play_cancel(void);  /* invalid action               */

#ifdef __cplusplus
}
#endif
