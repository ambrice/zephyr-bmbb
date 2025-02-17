#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>

#include "bmbbp.h"
#include "motor.h"

static const struct gpio_dt_spec mouth0 = GPIO_DT_SPEC_GET(DT_NODELABEL(mouth0), gpios);
static const struct gpio_dt_spec mouth1 = GPIO_DT_SPEC_GET(DT_NODELABEL(mouth1), gpios);

static const struct gpio_dt_spec body0 = GPIO_DT_SPEC_GET(DT_NODELABEL(body0), gpios);
static const struct gpio_dt_spec body1 = GPIO_DT_SPEC_GET(DT_NODELABEL(body1), gpios);

/* Define the thread to control the motors */
#define MOTOR_STACK_SIZE 512
K_THREAD_STACK_DEFINE(motor_stack_area, MOTOR_STACK_SIZE);
struct k_thread motor_thread_data;

LOG_MODULE_DECLARE(bmbb);

static struct {
	k_tid_t tid;
	bool cancel;
	sys_slist_t instructions;
	struct movement_instruction *current_instruction;
	int64_t start_time;
} s_ctx;

static void move_head(void)
{
	gpio_pin_set_dt(&body1, 0);
	gpio_pin_set_dt(&body0, 1);
}

static void open_mouth(uint32_t delay_ms)
{
	gpio_pin_set_dt(&mouth0, 0);
	gpio_pin_set_dt(&mouth1, 1);
	k_msleep(delay_ms);
	gpio_pin_set_dt(&mouth1, 0);
}

static void move_tail(uint32_t delay_ms)
{
	gpio_pin_set_dt(&body0, 0);
	gpio_pin_set_dt(&body1, 1);
	k_msleep(delay_ms);
	gpio_pin_set_dt(&body1, 0);
}

static void release_body(void)
{
	gpio_pin_set_dt(&body0, 0);
	gpio_pin_set_dt(&body1, 0);
}

static void process_instruction(const struct movement_instruction *inst)
{
	switch (inst->type) {
	case HEAD:
		move_head();
		break;
	case MOUTH:
		open_mouth(100);
		break;
	case TAIL:
		move_tail(100);
		break;
	case RELEASE:
		release_body();
		break;
	}
}

void handle_motors(void *, void *, void *)
{
	int64_t start_time = s_ctx.start_time;
	struct movement_instruction *inst = SYS_SLIST_PEEK_HEAD_CONTAINER(&s_ctx.instructions, inst, node);

	while (inst != NULL && !s_ctx.cancel) {
		int64_t current_time = k_uptime_get();
		if (start_time + inst->timestamp > current_time) {
			k_msleep(start_time + inst->timestamp - current_time);
		}
		process_instruction(inst);
		inst = SYS_SLIST_PEEK_NEXT_CONTAINER(inst, node);
	}
}

int motor_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&mouth0) ||
		!gpio_is_ready_dt(&mouth1)	||
		!gpio_is_ready_dt(&body0)	||
		!gpio_is_ready_dt(&body1)) 
	{
		return -1;
	}

	ret = gpio_pin_configure_dt(&mouth0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&mouth1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&body0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&body1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	return 0;
}

int motor_start(sys_slist_t instructions, int64_t initial_timestamp)
{
	/* Make sure we're not currently playing */
	if (motor_busy()) {
		LOG_ERR("motor_start called while motor thread is running");
		return -EBUSY;
	}

	s_ctx.start_time = initial_timestamp;
	s_ctx.instructions = instructions;

	s_ctx.cancel = false;
	s_ctx.tid = k_thread_create(&motor_thread_data, motor_stack_area,
			K_THREAD_STACK_SIZEOF(motor_stack_area),
			handle_motors, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	return 0;
}

void motor_cancel(void)
{
	if (s_ctx.tid != NULL) {
		s_ctx.cancel = true;
		k_thread_join(s_ctx.tid, K_FOREVER);
	}
}

bool motor_busy(void)
{
	if (s_ctx.tid != NULL && k_thread_join(s_ctx.tid, K_NO_WAIT) != 0) {
		return true;
	}
	return false;
}
