#include <stdint.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/logging/log.h>

#include "bmbbp.h"
#include "audio.h"

/* Source code for the Big Mouth Billy Bass Protocol (bmbbp) */

/* Top list of audio files, which each have a list of instructions associated */
static sys_slist_t s_audios = SYS_SLIST_STATIC_INIT(&s_audios);
static struct bmbbp_audio *s_current_audio = NULL;
static bool s_currently_playing = false;

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
		new->timestamp = strtoul(&line[1], NULL, 10);
		sys_slist_append(instructions, &new->node);
	}
	fs_close(&datfile);
	return 0;
}

int bmbbp_init(void)
{
	return audio_init();
}

int bmbbp_add(const char *wavfilename, const char *datfilename)
{
	struct bmbbp_audio *new = k_malloc(sizeof(struct bmbbp_audio));
	new->wav = wavfilename;
	sys_slist_init(&new->instructions);
	if (add_instructions(datfilename, &new->instructions) == 0) {
		new->current_instruction = NULL;
		LOG_INF("Added %d instructions for song %s", sys_slist_len(&new->instructions), new->wav);
		sys_slist_append(&s_audios, &new->node);
		return 0;
	} else {
		/* Failed to parse instructions, don't add this to the list */
		k_free(new);
		return -1;
	}
}

const char *bmbbp_next_song(void)
{
	if (s_current_audio == NULL || s_current_audio == SYS_SLIST_PEEK_TAIL_CONTAINER(&s_audios, s_current_audio, node)) {
		s_current_audio = SYS_SLIST_PEEK_HEAD_CONTAINER(&s_audios, s_current_audio, node);
	} else {
		s_current_audio = SYS_SLIST_PEEK_NEXT_CONTAINER(s_current_audio, node);
	}

	if (s_current_audio != NULL) {
		return s_current_audio->wav;
	}
	return NULL;
}

struct movement_instruction *bmbbp_next_instruction(void)
{
	if (s_current_audio == NULL) {
		LOG_ERR("bmbbp next_instruction called before next_song");
		return NULL;
	}

	if (s_current_audio->current_instruction == NULL) {
		s_current_audio->current_instruction = SYS_SLIST_PEEK_HEAD_CONTAINER(&s_current_audio->instructions, s_current_audio->current_instruction, node);
	} else {
		s_current_audio->current_instruction = SYS_SLIST_PEEK_NEXT_CONTAINER(s_current_audio->current_instruction, node);
	}
	return s_current_audio->current_instruction;
}

const char *bmbbp_current_song(void)
{
	if (s_current_audio == NULL || !s_currently_playing) {
		return NULL;
	}
	return s_current_audio->wav;
}

void bmbbp_cancel_current_song(void)
{
	if (s_current_audio != NULL && s_currently_playing) {
		/* TODO: stop the presses! */
		s_currently_playing = false;
	}
}

const char *bmbbp_start_playing(void)
{
	if (s_current_audio == NULL) {
		LOG_ERR("bmbbp start_playing called before next_song");
		return NULL;
	} else if (s_currently_playing) {
		LOG_ERR("bmbbp start_playing called while previous song still playing");
		return NULL;
	}

	audio_play(s_current_audio->wav);

	/* TODO: start the presses! */
	s_currently_playing = true;
	return s_current_audio->wav;
}
