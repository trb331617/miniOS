#ifndef __USER_EXEC_H
#define __USER_EXEC_H


/* 用path指向的程序替换当前进程 */
signed int sys_execv(const char *paht, const char *argv[]);

#endif