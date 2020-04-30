#ifndef __DEVICE_TIMER_H
#define __DEVICE_TIMER_H

#include "stdint.h"

void timer_init(void);

void milli_time_sleep(unsigned int milli_seconds);

#endif