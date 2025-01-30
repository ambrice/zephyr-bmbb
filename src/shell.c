#include <zephyr/drivers/retained_mem.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

#include "bmbbp.h"

/* For the UF2 bootloader, we can trigger DFU mode by 
 * writing magic value 0x57 to GPREGRET register and then rebooting.
 */

static int dfu_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* read the GPREGRET register */
	const struct device *const mem_dev = DEVICE_DT_GET(DT_NODELABEL(gpregret1));

	if (device_is_ready(mem_dev)) {
		shell_print(sh, "Rebooting into DFU mode");
		uint8_t val = 0x57;
		if (retained_mem_write(mem_dev, 0, &val, 1) == 0) {
			sys_reboot(SYS_REBOOT_WARM);
		} else {
			shell_error(sh, "Failed to write to GPREGRET");
		}
	} else {
		shell_error(sh, "Could not find gpregret1 device");
	}
	return 0;
}

SHELL_CMD_REGISTER(dfu, NULL, "Go to DFU mode for UF2", dfu_handler);

static int bmbb_cancel_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *wav = bmbbp_current_song();
	if (wav == NULL) {
		shell_print(sh, "No current song playing");
	} else {
		shell_print(sh, "Canceling %s", wav);
		bmbbp_cancel_current_song();
	}
	return 0;
}

static int bmbb_next_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *wav = bmbbp_next_song();
	shell_print(sh, "Next song is now %s", wav);
	return 0;
}

static int bmbb_play_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *wav = bmbbp_start_playing();
	shell_print(sh, "Now playing %s", wav);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bmbb,
		SHELL_CMD(cancel, NULL, "Cancel current audio", bmbb_cancel_handler),
		SHELL_CMD(next, NULL, "Set to next audio", bmbb_next_handler),
		SHELL_CMD(play, NULL, "Play current audio", bmbb_play_handler),
		SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bmbb, &sub_bmbb, "Big Mouth Billy Bass commands", NULL);
