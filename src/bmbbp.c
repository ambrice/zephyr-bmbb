#include <stdint.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bmbbp.h"
#include "audio.h"
#include "motor.h"

/* Source code for the Big Mouth Billy Bass Protocol (bmbbp) */

/* Top list of audio files, which each have a list of instructions associated */
static sys_slist_t s_song_audios = SYS_SLIST_STATIC_INIT(&s_song_audios);
static sys_slist_t s_joke_audios = SYS_SLIST_STATIC_INIT(&s_joke_audios);
static bmbbp_mode_t s_mode = SONGS;
static struct bmbbp_audio *s_current_audio = NULL;

LOG_MODULE_DECLARE(bmbb);

static int add_instructions(const char *datfilename, sys_slist_t *instructions)
{
	struct fs_file_t datfile;
	fs_file_t_init(&datfile);

	int err = fs_open(&datfile, datfilename, FS_O_READ);
	if (err != 0) {
		LOG_ERR("Failed to open %s for reading", datfilename);
		return err;
	}

	/* Read instructions from .dat file.  Instructions are like 'M030500'
	* to open the mouth 30.5 seconds into the song.  To keep it simple:
	* 1. All timestamps must be 6 characters, 0 padded
	* 2. Must end in \n, windows not supported
	*/
	char line[8];
	ssize_t len;
	while (true) {
		len = fs_read(&datfile, line, sizeof(line));
		if (len < sizeof(line) || line[7] != '\n') {
			/* Probably EOF */
			break;
		}
		struct movement_instruction *new = k_malloc(sizeof(struct movement_instruction));
		switch (line[0]) {
		case 'H':
			new->type = HEAD;
			break;
		case 'M':
			new->type = MOUTH;
			break;
		case 'T':
			new->type = TAIL;
			break;
		case 'R':
			new->type = RELEASE;
			break;
		}
		/* Subtracting a 200ms fudge factor because that seems to match up better. */
		new->timestamp = strtoul(&line[1], NULL, 10) - 200;
		sys_slist_append(instructions, &new->node);
	}
	fs_close(&datfile);
	return 0;
}

int bmbbp_init(void)
{
	audio_init();
	motor_init();
	s_mode = SONGS;
	return 0;
}

int bmbbp_add(bmbbp_mode_t mode, const char *wavfilename, const char *datfilename)
{
	sys_slist_t *audiolist = mode == SONGS ? &s_song_audios : &s_joke_audios;
	struct bmbbp_audio *new = k_malloc(sizeof(struct bmbbp_audio));
	new->wav = wavfilename;
	sys_slist_init(&new->instructions);
	if (add_instructions(datfilename, &new->instructions) == 0) {
		LOG_INF("Added %d instructions for song %s", sys_slist_len(&new->instructions), new->wav);
		sys_slist_append(audiolist, &new->node);
		return 0;
	} else {
		/* Failed to parse instructions, don't add this to the list */
		k_free(new);
		return -1;
	}
}

void bmbbp_toggle_mode(void) {
	s_mode = !s_mode;
	s_current_audio = NULL;
}

const char *bmbbp_next_song(void)
{
	sys_slist_t *audiolist = s_mode == SONGS ? &s_song_audios : &s_joke_audios;
	if (s_current_audio == NULL || s_current_audio == SYS_SLIST_PEEK_TAIL_CONTAINER(audiolist, s_current_audio, node)) {
		s_current_audio = SYS_SLIST_PEEK_HEAD_CONTAINER(audiolist, s_current_audio, node);
	} else {
		s_current_audio = SYS_SLIST_PEEK_NEXT_CONTAINER(s_current_audio, node);
	}

	if (s_current_audio != NULL) {
		return s_current_audio->wav;
	}
	return NULL;
}

const char *bmbbp_current_song(void)
{
	if (s_current_audio == NULL) {
		return NULL;
	}
	return s_current_audio->wav;
}

void bmbbp_cancel_current_song(void)
{
	if (s_current_audio != NULL) {
		audio_cancel();
		motor_cancel();
	}
}

const char *bmbbp_start_playing(void)
{
	if (s_current_audio == NULL) {
		LOG_ERR("bmbbp start_playing called before next_song");
		return NULL;
	}

	int64_t initial_timestamp = k_uptime_get();
	LOG_INF("Initial timestamp: %lld", initial_timestamp);

	if (audio_play(s_current_audio->wav) != 0) {
		return NULL;
	}

	if (motor_start(s_current_audio->instructions, initial_timestamp) != 0) {
		return NULL;
	}

	return s_current_audio->wav;
}
