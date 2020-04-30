#ifndef __USER_FORK_H
#define __USER_FORK_H


/* fork子进程, 内核线程不可直接调用 */
// 克隆当前进程, 即父进程
signed short int sys_fork(void);


#endif