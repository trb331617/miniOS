#include "init.h"
#include "print.h"
#include "interrupt.h"

#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"

/* 初始化所有模块 */
void init_all()
{
    // put_str("init_all...\n\n");
    
    idt_init();     // 初始化中断

    mem_init();     // 初始化内存管理系统

    thread_init();  // 初始化线程相关结构
    
    timer_init();   // 初始化PIT, 可编程定时计时器Programmable Interval Timer
    
    console_init(); // 初始化控制台
    
    keyboard_init(); // 初始化键盘
    
    tss_init();     // 初始化TSS(任务状态段)
    
    syscall_init(); // 初始化系统调用   

    intr_enable();  // ide_init 需要打开中断
    
    // 向硬盘控制器请求数据后, sema_down此信号量来阻塞线程
    // 直到硬盘完成后通过发中断, 由中断处理程序将此信号量sema_up, 唤醒线程
    ide_init();     // 初始化硬盘
    
    filesys_init(); // 初始化文件系统    
}