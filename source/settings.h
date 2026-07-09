/*
 * Persisted Opciones (game pace, Jugador 1 colour, auto-flip), stored on the PS3
 * HDD at /dev_hdd0/RAYCHESS/settings.bin so they survive relaunches. Best-effort:
 * a missing/invalid file just leaves the defaults in place, and a failed write is
 * silently ignored.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int timeControlIndex;
    int player1IsWhite;   /* 0 / 1 */
    int autoFlip;         /* 0 / 1 */
} raychess_settings_t;

/* Fills *out and returns 1 if a valid saved file exists; returns 0 otherwise
 * (leaving *out untouched, so the caller keeps its defaults). */
int settings_load(raychess_settings_t *out);

/* Writes the settings to disk (creating the folder if needed). Silent on failure. */
void settings_save(const raychess_settings_t *in);

#ifdef __cplusplus
}
#endif
