#ifndef __BMBBP_H__
#define __BMBBP_H__

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
	struct movement_instruction *current_instruction;
};

int bmbbp_init();

int bmbbp_add(const char *wavfilename, const char *datfilename);

const char *bmbbp_next_song(void);

struct movement_instruction *bmbbp_next_instruction(void);

const char *bmbbp_current_song(void);

void bmbbp_cancel_current_song(void);

const char *bmbbp_start_playing(void);

#endif // __BMBBP_H__
