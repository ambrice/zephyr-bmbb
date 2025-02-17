#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <zephyr/sys/slist.h>

int motor_init(void);

int motor_start(sys_slist_t instructions, int64_t initial_timestamp);

void motor_cancel(void);

bool motor_busy(void);

#endif // __MOTOR_H__
