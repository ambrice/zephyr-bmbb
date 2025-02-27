/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fs/fs.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/poweroff.h>

#include "bmbbp.h"

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/"DISK_DRIVE_NAME":"
#define FS_RET_OK FR_OK

#include <ff.h>

LOG_MODULE_REGISTER(bmbb);

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.flags = FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_NO_FORMAT,
};

#define FS_RET_OK FR_OK

static const char *disk_mount_pt = DISK_MOUNT_PT;

void register_shell_cmds(void);

#define SHUTDOWN_TIME K_SECONDS(60)

static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_NODELABEL(button1), gpios);

static void shutdown_handler(struct k_timer *timer_id)
{
	int ret;

	/* configure button1 as input, interrupt as level active to allow wake-up */
	ret = gpio_pin_configure_dt(&button1, GPIO_INPUT);
	if (ret < 0) {
		LOG_INF("Could not configure button1 GPIO (%d)\n", ret);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_LEVEL_ACTIVE);
	if (ret < 0) {
		LOG_INF("Could not configure button1 GPIO interrupt (%d)\n", ret);
		return;
	}
	LOG_INF("Shutting down");
	k_sleep(K_SECONDS(5));
	sys_poweroff();
}

K_TIMER_DEFINE(shutdown_timer, shutdown_handler, NULL);

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *		   success.
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

	if (evt->code == INPUT_KEY_A && evt->value == 1) {
		/* Short press, next song/joke */
		bmbbp_cancel_current_song();
		const char *wav = bmbbp_next_song();
		LOG_INF("Playing song %s", wav);
		bmbbp_start_playing();
		k_timer_start(&shutdown_timer, SHUTDOWN_TIME, K_NO_WAIT);
	} else if (evt->code == INPUT_KEY_B && evt->value == 1) {
		k_timer_start(&shutdown_timer, SHUTDOWN_TIME, K_NO_WAIT);
		LOG_INF("Long press, switching mode");
		/* TODO: switch between songs and jokes (files in SONGS/ or JOKES/ directories) */
	}
}

static const struct device *const longpress_dev = DEVICE_DT_GET(DT_NODELABEL(longpress));
INPUT_CALLBACK_DEFINE(longpress_dev, input_cb, NULL);

int main(void)
{
	uint32_t reset_cause;
	hwinfo_get_reset_cause(&reset_cause);
	hwinfo_clear_reset_cause();
	LOG_INF("Reset cause: 0x%04x", reset_cause);

	mp.mnt_point = disk_mount_pt;

	int res = fs_mount(&mp);

	if (res == FS_RET_OK) {
		LOG_INF("Disk mounted.");
		find_songs(disk_mount_pt);
	} else {
		LOG_ERR("Error mounting disk.");
	}

	bmbbp_init();

	if (reset_cause & RESET_LOW_POWER_WAKE) {
		// Woken up from pressing the button, play the song
		bmbbp_cancel_current_song();
		const char *wav = bmbbp_next_song();
		LOG_INF("Playing song %s", wav);
		bmbbp_start_playing();
	}

	k_timer_start(&shutdown_timer, SHUTDOWN_TIME, K_NO_WAIT);

	return 0;
}
