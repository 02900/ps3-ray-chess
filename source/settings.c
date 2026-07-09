/*
 * Settings persistence via the lv2 filesystem SYSCALLS. This PSL1GHT build ships
 * the sysFs* headers but no linkable wrappers, so — exactly like the RAM query in
 * sysinfo.c — we invoke the syscalls directly (lv2syscall + return_to_user_prog).
 *
 * /dev_hdd0 is always mounted on real hardware and RPCS3, and this fixed path works
 * the same whether the app is PKG-installed or ps3load'd as a .self.
 *
 * lv2 fs syscall numbers: 801 open, 802 read, 803 write, 804 close, 811 mkdir.
 */
#include "settings.h"

#include <ppu-types.h>
#include <ppu-lv2.h>

/* lv2 sys_fs_open flags (match the SYS_O_* values in <lv2/sysfs.h>). */
#define FS_O_RDONLY  0x0
#define FS_O_WRONLY  0x1
#define FS_O_CREAT   0x40
#define FS_O_TRUNC   0x200

LV2_SYSCALL fs_open(const char *path, u32 flags, s32 *fd, u64 mode, const void *arg, u64 argsize)
{
    lv2syscall6(801, (u64)path, (u64)flags, (u64)fd, mode, (u64)arg, argsize);
    return_to_user_prog(s32);
}
LV2_SYSCALL fs_read(s32 fd, void *buf, u64 nbytes, u64 *nread)
{
    lv2syscall4(802, (u64)fd, (u64)buf, nbytes, (u64)nread);
    return_to_user_prog(s32);
}
LV2_SYSCALL fs_write(s32 fd, const void *buf, u64 nbytes, u64 *nwrite)
{
    lv2syscall4(803, (u64)fd, (u64)buf, nbytes, (u64)nwrite);
    return_to_user_prog(s32);
}
LV2_SYSCALL fs_close(s32 fd)
{
    lv2syscall1(804, (u64)fd);
    return_to_user_prog(s32);
}
LV2_SYSCALL fs_mkdir(const char *path, u64 mode)
{
    lv2syscall2(811, (u64)path, mode);
    return_to_user_prog(s32);
}

#define DIR_PATH  "/dev_hdd0/RAYCHESS"
#define FILE_PATH "/dev_hdd0/RAYCHESS/settings.bin"
#define SETTINGS_MAGIC 0x52434831u   /* "RCH1" — magic + version */

typedef struct {
    u32 magic;
    s32 timeControlIndex;
    s32 player1IsWhite;
    s32 autoFlip;
} disk_settings_t;

int settings_load(raychess_settings_t *out)
{
    s32 fd = -1;
    if (fs_open(FILE_PATH, FS_O_RDONLY, &fd, 0, 0, 0) != 0) return 0;

    disk_settings_t d;
    u64 nread = 0;
    s32 rc = fs_read(fd, &d, sizeof(d), &nread);
    fs_close(fd);

    if (rc != 0 || nread != sizeof(d) || d.magic != SETTINGS_MAGIC) return 0;

    out->timeControlIndex = d.timeControlIndex;
    out->player1IsWhite   = d.player1IsWhite;
    out->autoFlip         = d.autoFlip;
    return 1;
}

void settings_save(const raychess_settings_t *in)
{
    fs_mkdir(DIR_PATH, 0755);   /* ignore "already exists" */

    s32 fd = -1;
    if (fs_open(FILE_PATH, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, &fd, 0666, 0, 0) != 0) return;

    disk_settings_t d;
    d.magic            = SETTINGS_MAGIC;
    d.timeControlIndex = in->timeControlIndex;
    d.player1IsWhite   = in->player1IsWhite;
    d.autoFlip         = in->autoFlip;

    u64 nwrote = 0;
    fs_write(fd, &d, sizeof(d), &nwrote);
    fs_close(fd);
}
