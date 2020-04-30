#ifndef __SHELL_PIPE_H
#define __SHELL_PIPE_H

#include "global.h"

#define PIPE_FLAG 0xFFFF

/* 判断文件描述符 local_fd 是否为管道 */
bool is_pipe(unsigned int local_fd);



/* 将文件描述符 old_local_fd 重定向为 new_local_fd */
// 文件描述符为PCB中数组 fd_table 的下标, 数组元素的值是全局文件表 file_table 的下标
// 文件描述符重定向的原理为: 将数组fd_table中下标为 old_local_fd 的元素值 用下标为 new_local_fd 的元素值替换
void sys_fd_redirect(unsigned int old_local_fd, unsigned int new_local_fd);


/* 创建管道, 成功返回0, 失败返回-1 */
signed int sys_pipe(signed int pipefd[2]);


/* 从管道中读数据 */
// 从文件描述符fd中读取count字节到buf
unsigned int pipe_read(signed int fd, void *buffer, unsigned int count);


/* 往管道中写数据 */
unsigned int pipe_write(signed int fd, const void *buffer, unsigned int count);

#endif