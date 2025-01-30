/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include "bmbbp.h"

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/"DISK_DRIVE_NAME":"
#define FS_RET_OK FR_OK

#include <ff.h>

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.flags = FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_NO_FORMAT,
};

#define FS_RET_OK FR_OK

LOG_MODULE_REGISTER(bmbb);

static const char *disk_mount_pt = DISK_MOUNT_PT;

void register_shell_cmds(void);

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
static int find_songs(const char *path)
{
	int res;
	struct fs_dir_t dirp;
    struct fs_file_t filep;
	static struct fs_dirent entry;

	fs_dir_t_init(&dirp);
    fs_file_t_init(&filep);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		LOG_ERR("Error opening dir %s [%d]", path, res);
		return res;
	}

	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			break;
		}

		if (entry.type != FS_DIR_ENTRY_DIR) {
            size_t namelen = strlen(entry.name);
            if (strncmp(entry.name + namelen - 4, ".WAV", 4) == 0)
            {
                /* Find the .dat file with the instructions */
                size_t buffersz = strlen(path) + namelen + 2;
                char *wavfile = k_malloc(buffersz);
                char *datfile = k_malloc(buffersz);
                memset(wavfile, 0, buffersz);
                memset(datfile, 0, buffersz);
                snprintf(wavfile, buffersz, "%s/%s", path, entry.name);
                strncpy(datfile, wavfile, buffersz);
                /* Replace the .WAV with .DAT */
                char *dot = strrchr(datfile, '.');
                if (dot != NULL) {
                    *dot = '\0';
                    strcat(datfile, ".DAT");
                } else {
                    LOG_ERR("Couldn't find the . in filename %s ?!", datfile);
                }
                bmbbp_add(wavfile, datfile);
            }
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return 0;
}

static void input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("input event %u val %d", evt->code, evt->value);
	bmbbp_cancel_current_song();
	const char *wav = bmbbp_next_song();
	LOG_INF("Playing song %s", wav);
	bmbbp_start_playing();
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

int main(void)
{
	mp.mnt_point = disk_mount_pt;

	int res = fs_mount(&mp);

	if (res == FS_RET_OK) {
		LOG_INF("Disk mounted.");
		find_songs(disk_mount_pt);
	} else {
		LOG_ERR("Error mounting disk.");
	}

	return 0;
}
