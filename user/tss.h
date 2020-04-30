#ifndef __USER_TSS_H
#define __USER_TSS_H

#include "thread.h"

void update_tss_esp(struct task_struct *pthread);

void tss_init(void);

#endif