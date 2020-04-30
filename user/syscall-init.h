#ifndef __USER_SYSCALLINIT_H
#define __USER_SYSCALLINIT_H

void syscall_init(void);

unsigned int sys_getpid(void);

/* 在文件 fs/fs.c 中重新实现了 sys_write */
// unsigned int sys_write(char *str);

#endif