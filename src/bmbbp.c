#include <stdint.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/logging/log.h>

/* Source code for the Big Mouth Billy Bass Protocol (bmbbp) */

typedef enum {
	HEAD,
	MOUTH,
	TAIL,
	RELEASE,
} movement_t;

struct movement_instruction {
	sys_snode_t node;
	movement_t type;
	uint32_t timestamp;
};

struct bmbbp_audio {
	sys_snode_t node;
	const char *wav;
	sys_slist_t instructions;
};

/* Top list of audio files, which each have a list of instructions associated */
static sys_slist_t audios = SYS_SLIST_STATIC_INIT(&audios);

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
	return 0;
}

int bmbbp_add(const char *wavfilename, const char *datfilename)
{
	struct bmbbp_audio *new = k_malloc(sizeof(struct bmbbp_audio));
	new->wav = wavfilename;
	sys_slist_init(&new->instructions);
	if (add_instructions(datfilename, &new->instructions) == 0) {
		LOG_INF("Added %d instructions for song %s", sys_slist_len(&new->instructions), new->wav);
		sys_slist_append(&audios, &new->node);
		return 0;
	} else {
		/* Failed to parse instructions, don't add this to the list */
		k_free(new);
		return -1;
	}
}


