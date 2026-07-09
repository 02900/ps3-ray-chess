/*
 * XMB Saved Data Utility wrapper (sysSaveListSave2 / sysSaveListLoad2).
 *
 * Mirrors PSL1GHT's samples/sys/save flow: a background thread runs the modal
 * utility (so the raylib render loop can keep pumping sysUtilCheckCallback), with
 * three callbacks — list (offer a "new save" slot), status (PARAM.SFO + recreate
 * mode), file (write/read our ICON0 + the game-state blob). The main thread polls
 * savegame_status().
 */
#include "savegame.h"

#include <sys/thread.h>
#include <sys/memory.h>
#include <sysutil/save.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define SAVE_PREFIX       "RAYCHESS-"
#define DATA_FILENAME     "GAME"
#define LIST_MAX_DIRS     100
#define LIST_MAX_FILES    3
#define BUF_SETTINGS_SIZE (LIST_MAX_DIRS * sizeof(sysSaveDirectoryList))
#define MEM_CONTAINER     (5 * 1024 * 1024)
#define THREAD_STACK      (16 * 1024)
#define THREAD_PRIO       1000

enum save_mode { MODE_ICON, MODE_DATA, MODE_DONE };

static struct {
    sys_ppu_thread_t tid;
    int saving, loading;
    volatile int status;     /* SAVEGAME_* */
    enum save_mode mode;

    char prefix[SYS_SAVE_MAX_DIRECTORY_NAME];
    sysSaveNewSaveGame new_save;
    sysSaveNewSaveGameIcon new_save_icon;

    unsigned char *data;     /* save: the blob; load: filled by the utility */
    unsigned data_size;
    unsigned char *icon;
    unsigned icon_size;

    char title[SYS_SAVE_MAX_TITLE];
    char subtitle[SYS_SAVE_MAX_SUBTITLE];
    char detail[SYS_SAVE_MAX_DETAIL];

    s32 result;
} S;

/* ---- callbacks ----------------------------------------------------------- */

static void list_cb(sysSaveCallbackResult *r, sysSaveListIn *in, sysSaveListOut *out)
{
    memset(out, 0, sizeof(*out));
    out->focus = SYS_SAVE_FOCUS_POSITION_LIST_HEAD;
    out->numDirectories = in->numDirectories;
    out->directoryList = in->directoryList;

    /* When saving, offer a fresh slot with a unique directory name. */
    if (S.saving && out->numDirectories < LIST_MAX_DIRS) {
        char *dir = (char *) malloc(SYS_SAVE_MAX_DIRECTORY_NAME + 1);
        int idx = -1, i, j;

        for (i = 0; idx == -1 && i <= 99; i++) {
            snprintf(dir, SYS_SAVE_MAX_DIRECTORY_NAME, "%s%d", S.prefix, i);
            idx = i;
            for (j = 0; j < (int) in->numDirectories; j++) {
                if (strcmp(dir, in->directoryList[j].directoryName) == 0) { idx = -1; break; }
            }
        }

        if (idx != -1) {
            memset(&S.new_save, 0, sizeof(S.new_save));
            memset(&S.new_save_icon, 0, sizeof(S.new_save_icon));
            S.new_save.position = SYS_SAVE_NEW_SAVE_POSITION_TOP;
            S.new_save.directoryName = dir;
            if (S.icon && S.icon_size > 0) {
                S.new_save.icon = &S.new_save_icon;
                S.new_save_icon.iconBufferSize = S.icon_size;
                S.new_save_icon.iconBuffer = S.icon;
            }
            out->newSaveGame = &S.new_save;
        } else {
            free(dir);
        }
    }

    r->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
}

static void status_cb(sysSaveCallbackResult *r, sysSaveStatusIn *in, sysSaveStatusOut *out)
{
    unsigned i;

    r->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    out->setParam = &in->getParam;

    if (S.loading) {
        out->recreateMode = SYS_SAVE_RECREATE_MODE_OVERWRITE_NOT_CORRUPTED;
        S.mode = MODE_DATA;
        S.data_size = 0;
        for (i = 0; i < in->numFiles; i++) {
            if (in->fileList[i].fileType == SYS_SAVE_FILETYPE_STANDARD_FILE) {
                S.data_size = (unsigned) in->fileList[i].fileSize;
            }
        }
        if (S.data_size == 0) { r->result = SYS_SAVE_CALLBACK_RESULT_CORRUPTED; return; }
        S.data = (unsigned char *) malloc(S.data_size);
    } else {
        out->recreateMode = SYS_SAVE_RECREATE_MODE_DELETE;
        S.mode = (S.icon && S.icon_size > 0) ? MODE_ICON : MODE_DATA;

        /* Rough free-space check (our data + the system file). */
        int needKB = (int) (S.data_size / 1024 + 1) + in->systemSizeKB;
        if ((in->freeSpaceKB + in->sizeKB) < needKB) {
            r->result = SYS_SAVE_CALLBACK_RESULT_NO_SPACE_LEFT;
            r->missingSpaceKB = needKB - (in->freeSpaceKB + in->sizeKB);
            return;
        }

        strncpy(in->getParam.title, S.title, SYS_SAVE_MAX_TITLE);
        strncpy(in->getParam.subtitle, S.subtitle, SYS_SAVE_MAX_SUBTITLE);
        strncpy(in->getParam.detail, S.detail, SYS_SAVE_MAX_DETAIL);
    }
}

static void file_cb(sysSaveCallbackResult *r, sysSaveFileIn *in, sysSaveFileOut *out)
{
    memset(out, 0, sizeof(*out));

    switch (S.mode) {
        case MODE_ICON:
            out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
            out->fileType = SYS_SAVE_FILETYPE_CONTENT_ICON0;
            out->size = S.icon_size;
            out->bufferSize = S.icon_size;
            out->buffer = S.icon;
            r->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
            r->incrementProgress = 30;
            S.mode = MODE_DATA;
            break;

        case MODE_DATA:
            out->fileOperation = S.saving ? SYS_SAVE_FILE_OPERATION_WRITE
                                          : SYS_SAVE_FILE_OPERATION_READ;
            out->filename = (char *) DATA_FILENAME;
            out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
            out->size = S.data_size;
            out->bufferSize = S.data_size;
            out->buffer = S.data;
            r->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
            r->incrementProgress = 100;
            S.mode = MODE_DONE;
            break;

        case MODE_DONE:
        default:
            r->result = SYS_SAVE_CALLBACK_RESULT_DONE;
            if (S.loading && in->previousOperationResultSize != S.data_size) {
                r->result = SYS_SAVE_CALLBACK_RESULT_CORRUPTED;
            }
            break;
    }
}

/* ---- worker thread ------------------------------------------------------- */

static void save_thread(void *arg)
{
    (void) arg;
    sysSaveListSettings ls;
    sysSaveBufferSettings bs;
    sys_mem_container_t container;
    s32 ret;

    strncpy(S.prefix, SAVE_PREFIX, SYS_SAVE_MAX_DIRECTORY_NAME);

    memset(&ls, 0, sizeof(ls));
    ls.sortType = SYS_SAVE_SORT_TYPE_TIMESTAMP;
    ls.sortOrder = SYS_SAVE_SORT_ORDER_DESCENDING;
    ls.pathPrefix = S.prefix;

    memset(&bs, 0, sizeof(bs));
    bs.maxDirectories = LIST_MAX_DIRS;
    bs.maxFiles = LIST_MAX_FILES;
    bs.bufferSize = BUF_SETTINGS_SIZE;
    bs.buffer = malloc(bs.bufferSize);

    if (sysMemContainerCreate(&container, MEM_CONTAINER) != 0) {
        if (bs.buffer) free(bs.buffer);
        S.status = SAVEGAME_FAIL;
        S.tid = 0;
        sysThreadExit(0);
        return;
    }

    if (S.saving) {
        ret = sysSaveListSave2(SYS_SAVE_CURRENT_VERSION, &ls, &bs,
                               list_cb, status_cb, file_cb, container, NULL);
    } else {
        ret = sysSaveListLoad2(SYS_SAVE_CURRENT_VERSION, &ls, &bs,
                               list_cb, status_cb, file_cb, container, NULL);
    }

    sysMemContainerDestroy(container);
    if (bs.buffer) free(bs.buffer);
    if (S.new_save.directoryName) { free(S.new_save.directoryName); S.new_save.directoryName = NULL; }

    S.result = ret;
    if (ret == SYS_SAVE_RETURN_DONE)          S.status = SAVEGAME_OK;
    else if (ret == SYS_SAVE_RETURN_CANCELED) S.status = SAVEGAME_CANCELLED;
    else                                      S.status = SAVEGAME_FAIL;

    S.tid = 0;
    sysThreadExit(0);
}

/* ---- public API ---------------------------------------------------------- */

void savegame_start_save(const void *data, unsigned size,
                         const char *title, const char *subtitle, const char *detail,
                         const unsigned char *icon, unsigned icon_size)
{
    if (S.status == SAVEGAME_BUSY) return;
    savegame_clear();

    S.saving = 1; S.loading = 0;
    S.data_size = size;
    S.data = (unsigned char *) malloc(size);
    if (S.data) memcpy(S.data, data, size);

    if (icon && icon_size) {
        S.icon = (unsigned char *) malloc(icon_size);
        if (S.icon) { memcpy(S.icon, icon, icon_size); S.icon_size = icon_size; }
    }

    strncpy(S.title,    title    ? title    : "", SYS_SAVE_MAX_TITLE - 1);
    strncpy(S.subtitle, subtitle ? subtitle : "", SYS_SAVE_MAX_SUBTITLE - 1);
    strncpy(S.detail,   detail   ? detail   : "", SYS_SAVE_MAX_DETAIL - 1);

    S.status = SAVEGAME_BUSY;
    if (sysThreadCreate(&S.tid, save_thread, NULL, THREAD_PRIO, THREAD_STACK, 0, "raychess_save") < 0) {
        S.status = SAVEGAME_FAIL;
    }
}

void savegame_start_load(void)
{
    if (S.status == SAVEGAME_BUSY) return;
    savegame_clear();

    S.saving = 0; S.loading = 1;

    S.status = SAVEGAME_BUSY;
    if (sysThreadCreate(&S.tid, save_thread, NULL, THREAD_PRIO, THREAD_STACK, 0, "raychess_load") < 0) {
        S.status = SAVEGAME_FAIL;
    }
}

int savegame_status(void) { return S.status; }

const void *savegame_result_data(unsigned *out_size)
{
    if (out_size) *out_size = S.data_size;
    return S.data;
}

void savegame_clear(void)
{
    if (S.data) { free(S.data); S.data = NULL; }
    if (S.icon) { free(S.icon); S.icon = NULL; }
    S.data_size = 0;
    S.icon_size = 0;
    S.saving = 0;
    S.loading = 0;
    S.status = SAVEGAME_IDLE;
}
