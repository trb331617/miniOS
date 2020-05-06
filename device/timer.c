/* 
 * FILE: timer.c
 * TITLE: 使用8253来提高时钟中断的频率

 * 使用8253来给IRQ0引脚上的时钟中断信号“提速”，使其发出的中断信号频率快一些。它默认的频率是18.206Hz，即一秒内大约发出18 次中断信号。
 * 通过对8253 编程，使时钟一秒内发100 次中断信号，即中断信号频率为100Hz.

 * DATE: 20200313
 * USAGE:
 *
 */

#include "timer.h"
#include "io.h"
#include "print.h"

#include "thread.h"
#include "interrupt.h"
#include "debug.h"

#define IRQ0_FREQUENCY      100     // 时钟中断的频率，这里我们设置为100Hz    
#define INPUT_FREQUENCY     1193180 // 计数器0的工作脉冲信号频率
#define TIMER0_VALUE        INPUT_FREQUENCY / IRQ0_FREQUENCY    // 计数器的计数初值
#define TIMER0_PORT         0x40    // 端口号，用来指定初始值value的目的端口号
#define TIMER0_ID           0       // 控制字中选择计数器的号码    
#define TIMER_MODE          2       // 计数器的工作方式，2为比率发生器
#define READ_WRITE_LATCH    3       // 计数器的读/写/锁存方式

// PIT, 可编程定时计时器Programmable Interval Timer
#define PIT_CONTROL_PORT    0x43

unsigned int ticks;     // 记录内核自中断开启以来总共的嘀嗒数

// 每多少毫秒发生一次中断
// 本项目中设置的时钟中断频率为每秒100次, 因此每隔10毫秒一次中断, 即一个中断周期是10毫秒
#define milli_seconds_per_intr  (1000 / IRQ0_FREQUENCY)


/* 把操作的计数器id 读写属性rwl 计数器模式mode 写入模式控制寄存器，并赋初始值value */
static void frequency_set(uint8_t counter_port, \
                uint8_t counter_id, \
                uint8_t rwl, \
                uint8_t counter_mode, \
                uint16_t counter_value)
{
    // 往控制寄存器端口0x43写入控制字
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_id << 6 | rwl << 4 | counter_mode << 1));
    // 先写入 counter_value低8位
    outb(counter_port, (uint8_t)counter_value);
    // 再写入 counter_value高8位
    outb(counter_port, (uint8_t)counter_value >> 8);
}




/* 时钟的中断处理函数 */
static void intr_timer_handler(void)
{
    struct task_struct *current_thread = running_thread();
    
    ASSERT(current_thread->stack_magic == 0x19870916);  // 检查栈是否溢出
    
    // 记录系统自开中断以来所运行的嘀嗒数，类似于系统运行时长的概念，在写用户程序时可能用到
    current_thread->elapsed_ticks++;    // 记录此线程占用的CPU时间
    
    ticks++;    // 从内核第一次处理时间中断后开始至今的嘀嗒数，内核态和用户态总共的嘀嗒数
                // 实际上就是时钟中断发生的次数
    
    if(current_thread->ticks == 0)  // 若进程时间片用完，就开始调度新的进程上CPU
        schedule();
    else
        // 每个线程在处理器上运行期间都会有很多次时钟中断发生，每次中断处理程序都会将线程的时间片ticks减1
        current_thread->ticks--;    // 将当前进程的时间片-1
}





/* 初始化PIT 8253 */
void timer_init()
{
    put_str("timer_init begin...");
    // 设置8253定时周期，也就是发中断的周期
    frequency_set(TIMER0_PORT, TIMER0_ID, READ_WRITE_LATCH, TIMER_MODE, TIMER0_VALUE);
    
    register_handler(0x20, intr_timer_handler);     // 注册安装中断处理程序
    
    // put_str("timer_init done!\n");
    put_str(" done!\n");
}



/* 以tick为单位的sleep, 任何时间形式的sleep会转换此ticks */
static void ticks_to_sleep(unsigned int sleep_ticks)
{
    unsigned int start_tick = ticks;
    
    // 若间隔的ticks数不够便让出CPU
    while(ticks - start_tick < sleep_ticks)
        thread_yield();
}


/* 以毫秒为单位的sleep  1秒 = 1000 毫秒 */
void milli_time_sleep(unsigned int milli_seconds)
{
    unsigned int sleep_ticks = DIV_ROUND_UP(milli_seconds, milli_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks);
}