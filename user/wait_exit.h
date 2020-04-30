#ifndef __USER_WAIT_EXIT_H
#define __USER_WAIT_EXIT_H



/* 等待子进程调用exit, 将子进程的退出状态保存到status指向的变量
 * 成功则返回子进程的pid, 失败则返回-1
 */
signed short int sys_wait(signed int *status);


/* 子进程用来结束自己时调用 */
void sys_exit(signed int status);

#endif