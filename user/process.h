#ifndef __USER_PROCESS_H
#define __USER_PROCESS_H

#include "thread.h"

// 4GB的虚拟地址空间中，0xc000_0000-1是用户空间的最高地址，0xc000_0000~0xffff_ffff是内核空间
// 由于申请内存时，内存管理模块返回的地址是内存空间的下边界
#define USER_STACK3_VADDR   (0xc0000000 - 0x1000)
#define USER_VADDR_START    0x8048000   // 即128M

#define default_prio        31

void process_activate(struct task_struct *pthread);
void process_execute(void *filename, char *name);


/* 创建页目录表，并复制内核1G空间对应的页目录项
 * 成功则返回页目录的虚拟地址，否则返回-1 */
unsigned int *create_page_dir(void);


/* 不同的进程在执行前，需要更新CR3寄存器为与之配套的页表，从而实现虚拟地址空间的隔离 */

/* 激活页表
 * 更新页目录表寄存器cr3, 使新页表生效 */
void page_dir_activate(struct task_struct *pthread);


#endif