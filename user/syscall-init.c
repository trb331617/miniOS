#include "syscall-init.h"
#include "syscall.h"

#include "thread.h"     // running_thread

#include "print.h"      // put_str cls_screen
#include "console.h"    // console_put_str

#include "string.h"     // strlen

#include "memory.h"     // sys_malloc sys_free

#include "fs.h"         // sys_write sys_putchar

#include "fork.h"       // sys_fork

#include "exec.h"       // sys_execv

#include "wait_exit.h"  // sys_wait

#include "pipe.h"       // sys_fd_redirect

#include "fs.h"         // sys_help

// 最大支持的系统调用子功能个数
#define syscall_number  32

void *syscall_table[syscall_number];


/* 初始化系统调用 */
void syscall_init(void)
{
    put_str("syscall_init begin...");
    
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_WRITE]  = sys_write;
    syscall_table[SYS_MALLOC] = sys_malloc;
    syscall_table[SYS_FREE] = sys_free;
    syscall_table[SYS_FORK] = sys_fork;
    syscall_table[SYS_READ] = sys_read;
    syscall_table[SYS_PUTCHAR] = sys_putchar;
    syscall_table[SYS_CLEAR] = cls_screen;
    

    syscall_table[SYS_GETCWD]   = sys_getcwd;
    syscall_table[SYS_OPEN]     = sys_open;
    syscall_table[SYS_CLOSE]    = sys_close;
    syscall_table[SYS_LSEEK]	= sys_lseek;
    syscall_table[SYS_UNLINK]   = sys_unlink;
    syscall_table[SYS_MKDIR]	= sys_mkdir;
    syscall_table[SYS_OPENDIR]	= sys_opendir;
    syscall_table[SYS_CLOSEDIR] = sys_closedir;
    syscall_table[SYS_CHDIR]	= sys_chdir;
    syscall_table[SYS_RMDIR]    = sys_rmdir;
    syscall_table[SYS_READDIR]  = sys_readdir;
    syscall_table[SYS_REWINDDIR] = sys_rewinddir;
    syscall_table[SYS_STAT]     = sys_stat;
    syscall_table[SYS_PS]       = sys_ps;
    
    syscall_table[SYS_EXECV]    = sys_execv;
    
    syscall_table[SYS_WAIT]     = sys_wait;
    syscall_table[SYS_EXIT]     = sys_exit;
    
    syscall_table[SYS_FD_REDIRECT] = sys_fd_redirect;
    syscall_table[SYS_PIPE] = sys_pipe;

    syscall_table[SYS_HELP] = sys_help;
    
    // put_str("syscall_init done!\n");
    put_str(" done!\n");
}


/* 返回当前任务的pid */
unsigned int sys_getpid(void)
{
    return running_thread()->pid;
}

/* 在文件 fs/fs.c 中重新实现了 sys_write
 * signed int sys_write(signed int fd, const void *buf, unsigned int count) */

// /* 打印字符串str(未实现文件系统前的版本) */
// unsigned int sys_write(char *str)
// {
    // console_put_str(str);
    // return strlen(str);
// }


