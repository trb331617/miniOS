#ifndef __LIB_SYSCALL_H
#define __LIB_SYSCALL_H

#include "fs.h"     // struct stat

enum SYSCALL_NR{
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
    SYS_FORK,
    SYS_READ,
    SYS_PUTCHAR,
    SYS_CLEAR,
    
    SYS_GETCWD,
    SYS_OPEN,
    SYS_CLOSE,
    SYS_LSEEK,
    SYS_UNLINK,
    SYS_MKDIR,
    SYS_OPENDIR,
    SYS_CLOSEDIR,
    SYS_CHDIR,
    SYS_RMDIR,
    SYS_READDIR,
    SYS_REWINDDIR,
    SYS_STAT,
    SYS_PS,
    
    SYS_EXECV,
    
    SYS_WAIT,
    SYS_EXIT,
    
    SYS_FD_REDIRECT,
    SYS_PIPE,
    
    SYS_HELP
};

unsigned int getpid(void);
// unsigned int write(char *str);

/* 把buf中count个字符写入文件描述符fd */
unsigned int write(signed int fd, const void *buf, unsigned int count);

void *malloc(unsigned int size);
void free(void *vaddr);

signed short int fork(void);


/* 输出一个字符 */
void putchar(char char_ascii);

/* 清空屏幕 */
void clear(void);


/* 从文件描述符fd中读取count个字节到buf */
signed int read(signed int fd, void *buf, unsigned int count);


char* getcwd(char* buf, unsigned int size);
signed int open(char* pathname, unsigned char flag);
signed int close(signed int fd);
signed int lseek(signed int fd, signed int offset, unsigned char whence);
signed int unlink(const char* pathname);
signed int mkdir(const char* pathname);
struct dir* opendir(const char* name);
signed int closedir(struct dir* dir);
signed int rmdir(const char* pathname);
struct dir_entry* readdir(struct dir* dir);
void rewinddir(struct dir* dir);
signed int stat(const char* path, struct stat* buf);
signed int chdir(const char* path);
void ps(void);

signed int execv(const char *path, char **argv);

/* 等待子进程, 子进程状态存储到status */
signed short int wait(signed int *status);


/* 以状态status退出 */
void exit(signed int status);

/* 将文件描述符 old_local_fd重定向到 new_local_fd */
void fd_redirect(unsigned int old_local_fd, unsigned int new_local_fd);

/* 创建管道, pipefd[0] 读管道, pipefd[1] 写管道 */
signed int pipe(signed int pipefd[2]);

/* 显示系统支持的命令 */
void help(void);

#endif