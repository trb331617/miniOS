// FILE: main.c
// TITLE: 
// DATE: 20200425

#include "print.h"
#include "console.h"
#include "stdio.h"      // printf
#include "stdio_kernel.h"   // printk

#include "init.h"

#include "thread.h"
#include "memory.h"     // sys_malloc()
#include "fs.h"         // sys_open()
#include "dir.h"        // struct dir
#include "syscall.h"    // fork()
#include "shell.h"      // my_shell

#include "assert.h"      // assert


void init(void);
void writefile2filesys(void);


//int _start(void)
int main(void)
{
    put_str("i am kernel chapter_10\b2 =^_^=\n");

    init_all();

    // writefile2filesys();
    
    cls_screen();    // 清屏启动时的初始化信息
    console_put_str("[OS@localhost /]$ ");
    
    thread_exit(running_thread(), true);
    return 0;
}


/* init进程 */
// init是用户级进程, 是第一个启动的程序, pid为1, 后续的所有进程都是它的孩子
// init是所有进程的父进程, 它还要负责所有子进程的资源回收

// init是用户级进程, 因此要调用 process_execute 来创建进程
// init的pid是1, 目前系统中有主线程 idle线程, 因此应该在创建主线程的函数 make_main_thread之前创建init
// 也就是在 thread_init()中完成
void init(void)
{
    unsigned int ret_pid = fork();
    if(ret_pid)     // 父进程
    {
        int status, child_pid;
        while(1) // init在此处不停地回收过继给它的子进程
        {
            child_pid = wait(&status);
            printf("i am init, my pid is %d, i recieve a child, it's pid is %d, status is %d\n", child_pid, status);
        }
    }
    else        // 子进程
    {
        my_shell();
    }
    panic("ERROR: during init, should not be here");
}


// 从指定位置读取裸盘数据写入文件系统
void writefile2filesys(void)
{
/*************    写入应用程序    *************/
    unsigned int file_size = 7328;
    unsigned int sec_cnt = DIV_ROUND_UP(file_size, 512);
    void* prog_buf = sys_malloc(file_size);

    // 指定操作的设备, 即第0个ide通道上的第0块硬盘
    struct disk* sda = &channels[0].devices[0];
    // 以sba第300个扇区为起始, 读取sec_cnt个扇区到缓冲区prog_buf
    ide_read(sda, 300, prog_buf, sec_cnt);

    signed int fd = sys_open("/cat", O_CREAT|O_RDWR);
    if (fd != -1)
    {
        // 将程序写入 prog_no_arg
        if(sys_write(fd, prog_buf, file_size) == -1)
        {
            printk("file write error!\n");
            while(1);
        }
    }    


    file_size = 37;
    sec_cnt = DIV_ROUND_UP(file_size, 512);
    prog_buf = sys_malloc(file_size);

    // 指定操作的设备, 即第0个ide通道上的第0块硬盘
    sda = &channels[0].devices[0];
    // 以sba第300个扇区为起始, 读取sec_cnt个扇区到缓冲区prog_buf
    ide_read(sda, 350, prog_buf, sec_cnt);

    fd = sys_open("/hello.txt", O_CREAT|O_RDWR);
    if (fd != -1)
    {
        // 将程序写入 prog_no_arg
        if(sys_write(fd, prog_buf, file_size) == -1)
        {
            printk("file write error!\n");
            while(1);
        }
    }     
}