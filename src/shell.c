#include <zephyr/drivers/retained_mem.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

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

void register_shell_cmds(void)
{
	SHELL_CMD_REGISTER(dfu, NULL, "Go to DFU mode for UF2", dfu_handler);
}
