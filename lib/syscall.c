#include "syscall.h"

// 大括号代码块
// 大括号中最后一个语句的值会作为大括号代码块的返回值

/* 无参数的系统调用 */
#define _syscall0(NUMBER) ({    \
    int ret;                    \
    asm volatile( "int $0x80"   \
    : "=a" (ret)                \
    : "a" (NUMBER)              \
    : "memory"                  \
    );                          \
    ret;                        \
})


/* 一个参数的系统调用 */
#define _syscall1(NUMBER, ARG1) ({  \
    int ret;                    \
    asm volatile( "int $0x80"   \
    : "=a" (ret)                \
    : "a" (NUMBER), "b" (ARG1)  \
    : "memory"                  \
    );                          \
    ret;                        \
})

/* 两个参数的系统调用 */
#define _syscall2(NUMBER, ARG1, ARG2) ({    \
    int ret;                    \
    asm volatile( "int $0x80"   \
    : "=a" (ret)                \
    : "a" (NUMBER), "b" (ARG1), "c" (ARG2)  \
    : "memory"                  \
    );                          \
    ret;                        \
})

/* 三个参数的系统调用 */
#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({   \
    int ret;                    \
    asm volatile( "int $0x80"   \
    : "=a" (ret)                \
    : "a" (NUMBER), "b" (ARG1), "c" (ARG2), "d" (ARG3) \
    : "memory"                  \
    );                          \
    ret;                        \
})


/*** 系统调用之栈传递参数 ***/
/* 用户程序需要在执行int 0x80之前，将参数和子功能号压入用户栈
 * 这里约定下: 先压入参数, 再压入子功能号
 */
 
/* 无参数的系统调用  之栈传递参数版本 */
/***
#define _syscall0_stack(NUMBER) ({ \
    int ret; \
    asm volatile( "pushl %[number]; int $0x80; addl $4, %%esp" \
    : "=a" (ret)            \
    : [number] "i" (NUMBER) \
    : "memory"              \
    );                      \
    ret;                    \
})
***/

/* 三个参数的系统调用 之栈传递参数版本 */
/***
#define _syscall3_stack(NUMBER, ARG0, ARG1, ARG2) ({ \
    int ret;        \
    asm volatile (  \
    "pushl %[arg2]; pushl %[arg1]; pushl %[arg0];"  \
    "pushl %[number]; int $0x80; addl $16, %%esp"   \
    : "=a" (ret)                                    \
    : [number] "i" (NUMBER), [arg0] "g" (ARG0),     \
      [arg1] "g" (ARG1), [arg2] "g" (ARG2)          \
    : "memory"      \
    );              \
    ret;            \
})
***/

/* 返回当前任务pid */
unsigned int getpid()
{
    // 测试栈传递参数的版本
    // return _syscall0_stack(SYS_GETPID);
    
    return _syscall0(SYS_GETPID);
}

// unsigned int write(char *str)
// {
    // return _syscall1(SYS_WRITE, str);
// }


/* 把buf中count个字符写入文件描述符fd */
unsigned int write(signed int fd, const void *buf, unsigned int count)
{
    return _syscall3(SYS_WRITE, fd, buf, count);
}


/* 申请size字节大小的内存, 并返回内存首地址 */
void *malloc(unsigned int size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

/* 释放vaddr指向的内存 */
void free(void *vaddr)
{
    _syscall1(SYS_FREE, vaddr);
}


/* 派生子进程, 返回子进程pid */
signed short int fork()
{
    return _syscall0(SYS_FORK);
}


/* 从文件描述符fd中读取count个字节到buf */
signed int read(signed int fd, void *buf, unsigned int count)
{
    return _syscall3(SYS_READ, fd, buf, count);
}


/* 输出一个字符 */
void putchar(char char_ascii)
{
    _syscall1(SYS_PUTCHAR, char_ascii);
}


/* 清空屏幕 */
void clear(void)
{
    _syscall0(SYS_CLEAR);
}


/* 获取当前工作目录 */
char *getcwd(char *buf, unsigned int size)
{
    return (char *)_syscall2(SYS_GETCWD, buf, size);
}

/* 以flag方式打开文件pathname */
signed int open(char *pathname, unsigned char flag)
{
    return _syscall2(SYS_OPEN, pathname, flag);
}

/* 关闭文件 */
signed int close(signed int fd)
{
    return _syscall1(SYS_CLOSE, fd);
}

/* 设置文件偏移量 */
signed int lseek(signed int fd, signed int offset, unsigned char whence)
{
    return _syscall3(SYS_LSEEK, fd, offset, whence);
}

/* 删除文件 */
signed int unlink(const char *pathname)
{
    return _syscall1(SYS_UNLINK, pathname);
}

/* 创建目录 */
signed int mkdir(const char *pathname)
{
    return _syscall1(SYS_MKDIR, pathname);
}

/* 打开目录 */
struct dir *opendir(const char *name)
{
    return (struct dir *)_syscall1(SYS_OPENDIR, name);
}

/* 关闭目录 */
signed int closedir(struct dir *dir)
{
    return _syscall1(SYS_CLOSEDIR, dir);
}

/* 删除目录pathname */
signed int rmdir(const char *pathname)
{
    return _syscall1(SYS_RMDIR, pathname);
}

/* 读取目录dir */
struct dir_entry *readdir(struct dir *dir)
{
    return (struct dir_entry *)_syscall1(SYS_READDIR, dir);
}

/* 回归目录指针 */
void rewinddir(struct dir *dir)
{
    _syscall1(SYS_REWINDDIR, dir);
}

/* 获取path属性到buf中 */
signed int stat(const char *path, struct stat *buf)
{
    return _syscall2(SYS_STAT, path, buf);
}

/* 改变工作目录为path */
signed int chdir(const char *path)
{
    return _syscall1(SYS_CHDIR, path);
}

/* 显示任务列表 */
void ps(void)
{
    _syscall0(SYS_PS);
}

signed int execv(const char *path, char **argv)
{
    return _syscall2(SYS_EXECV, path, argv);
}

/* 等待子进程, 子进程状态存储到status */
signed short int wait(signed int *status)
{
    return _syscall1(SYS_WAIT, status);
}

/* 以状态status退出 */
void exit(signed int status)
{
    _syscall1(SYS_EXIT, status);
}


/* 将文件描述符 old_local_fd重定向到 new_local_fd */
void fd_redirect(unsigned int old_local_fd, unsigned int new_local_fd)
{
    _syscall2(SYS_FD_REDIRECT, old_local_fd, new_local_fd);
}


/* 创建管道, pipefd[0] 读管道, pipefd[1] 写管道 */
signed int pipe(signed int pipefd[2])
{
    return _syscall1(SYS_PIPE, pipefd);
}

/* 显示系统支持的命令 */
void help(void)
{
    _syscall0(SYS_HELP);
}
