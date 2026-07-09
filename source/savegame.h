/*
 * Save/Load a game blob through the PS3 XMB "Saved Data Utility"
 * (sysSaveListSave2/Load2). The utility is modal and must be pumped by
 * sysUtilCheckCallback() every frame, so the blocking call runs on a background
 * thread while the raylib loop keeps drawing; the caller polls savegame_status().
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SAVEGAME_IDLE = 0,
    SAVEGAME_BUSY,       /* the modal dialog / thread is running */
    SAVEGAME_OK,         /* finished; for a load, result_data() is valid */
    SAVEGAME_FAIL,
    SAVEGAME_CANCELLED   /* user backed out of the dialog */
};

/* Begin a save. Copies `data` (size bytes) and the PARAM.SFO strings; icon is the
 * ICON0.PNG bytes for a new save (pass NULL/0 to skip). Shows the XMB slot picker. */
void savegame_start_save(const void *data, unsigned size,
                         const char *title, const char *subtitle, const char *detail,
                         const unsigned char *icon, unsigned icon_size);

/* Begin a load. Shows the XMB list of existing saves to pick from. */
void savegame_start_load(void);

int savegame_status(void);                              /* one of SAVEGAME_* */
const void *savegame_result_data(unsigned *out_size);   /* the loaded blob (after OK) */
void savegame_clear(void);                              /* free buffers, back to IDLE */

#ifdef __cplusplus
}
#endif
