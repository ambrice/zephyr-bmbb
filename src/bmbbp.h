#ifndef __BMBBP_H__
#define __BMBBP_H__

#include <zephyr/sys/slist.h>

typedef enum {
	SONGS,
	JOKES,
} bmbbp_mode_t;

typedef enum {
	HEAD,
	MOUTH,
	TAIL,
	RELEASE,
} bmbbp_movement_t;

struct movement_instruction {
	sys_snode_t node;
	bmbbp_movement_t type;
	int32_t timestamp;
};

struct bmbbp_audio {
	sys_snode_t node;
	const char *wav;
	sys_slist_t instructions;
};

int bmbbp_init();

int bmbbp_add(bmbbp_mode_t mode, const char *wavfilename, const char *datfilename);

void bmbbp_toggle_mode(void);

const char *bmbbp_next_song(void);

struct movement_instruction *bmbbp_next_instruction(void);

const char *bmbbp_current_song(void);

void bmbbp_cancel_current_song(void);

const char *bmbbp_start_playing(void);

#endif // __BMBBP_H__
