#include "thread.h"
#include "string.h"
#include "memory.h"

#include "interrupt.h"

#include "print.h"
#include "stdio.h"      // sprintf
#include "debug.h"

#include "process.h"
#include "file.h"       // stdout_id

#include "sync.h"

#define PAGE_SIZE 4096

// 定义在 thread/switch.asm中
extern void switch_to(struct task_struct* current, struct task_struct* next);

// 定义在 kernel/main.c
extern void init(void);

struct task_struct *main_thread;    // 主线程PCB
struct task_struct *idle_thread;    // idle线程, 系统空闲时运行的线程

struct list thread_ready_list;      // 就绪队列

// 当线程因为某些原因阻塞了，不能放在就绪队列中
struct list thread_all_list;        // 所有任务队列

static struct list_elem *thread_tag;    // 用于保存队列中的线程结点



/* pid 的位图, 最大支持1024个pid */
unsigned char pid_bitmap_bits[128] = {0};   // 128 * 8 = 1024

// pid 池
struct pid_pool{
    struct bitmap pid_bitmap;   // pid位图
    unsigned int pid_start;     // 起始pid
    struct lock pid_lock;       // 用于分配pid的锁 
}pid_pool;
   


static void idle(void *arg __attribute__((unused)));



/* 初始化pid池 */
static void pid_pool_init(void)
{
    pid_pool.pid_start = 1;
    
    pid_pool.pid_bitmap.bits = pid_bitmap_bits;
    pid_pool.pid_bitmap.bitmap_bytes_len = 128;
    bitmap_init(&pid_pool.pid_bitmap);
    lock_init(&pid_pool.pid_lock);
}

/* 分配pid */
static signed short int allocate_pid(void)
{
    // 静态局部变量
    // 存储/生命周期：存储在全局数据区，直到程序运行结束。
    //                在声明处首次初始化，以后的函数调用不再进行初始化
    // 作用域：局部作用域
    // static signed short int next_pid = 0;
    // next_pid++;
    lock_acquire(&pid_pool.pid_lock);
    
    signed int bit_index = bitmap_scan(&pid_pool.pid_bitmap, 1);
    bitmap_set(&pid_pool.pid_bitmap, bit_index, 1);
    
    lock_release(&pid_pool.pid_lock);
    // return next_pid;
    return (bit_index + pid_pool.pid_start);
}

/* 释放pid */
void release_pid(signed short int pid)
{
    lock_acquire(&pid_pool.pid_lock);
    
    signed int bit_index = pid - pid_pool.pid_start;
    bitmap_set(&pid_pool.pid_bitmap, bit_index, 0);
    
    lock_release(&pid_pool.pid_lock);
}


/* fork进程时为其分配pid,因为allocate_pid已经是静态的,别的文件无法调用.
 * 不想改变函数定义了,故定义fork_pid函数来封装一下。*/
signed short int fork_pid(void)
{
    return allocate_pid();
}





/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func *function, void *func_arg)
{
    // 执行function前要开中断，避免后面的时钟中断被屏蔽，而无法调度其它线程
    intr_enable();
    
    function(func_arg);
}


/* 初始化线程栈thread_stack
 * 将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg)
{
    /* 先预留中断使用栈的空间，thread.h中定义的结构体 */
    // intr_stack栈有2个作用，1) 任务被中断时，用来保存任务的上下文; 2) 预留给进程，用来填充用户进程的上下文，也就是寄存器环境
    // user/process.c start_process() 对intr_stack初始化
    pthread->self_kstack -= sizeof(struct intr_stack);  // self_kstack在init_thread()中已初始化为PCB最顶端
    
    /* 再预留线程栈空间，thread.h中定义的结构体 */
    // 在线程创建过程中，把线程的上下文保存在了struct thread_stack栈中
    // thread_stack栈是由函数 kernel_thread 使用
    pthread->self_kstack -= sizeof(struct thread_stack);
    
    // 初始化线程栈thread_stack
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;
    
    // ip 指令指针寄存器
    kthread_stack->eip = kernel_thread;     // 函数kernel_thread
        // 结合线程栈struct thread_stack的定义，当处理器进入kernel_thread函数体时，
        // 栈顶为返回地址、栈顶+4为参数function、栈顶+8为参数func_arg
    
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    // 将这4个寄存器初始化为0，因为线程中的函数尚未执行，执行之后寄存器才会有值
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}




/* 初始化线程基本信息 */
void init_thread(struct task_struct *pthread, char *name, int priority)
{
    memset(pthread, 0, sizeof(*pthread));
    
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);
    
    if(pthread == main_thread)
        // 由于把main函数也封装成一个线程，并且它一直是运行的，故设为TASK_RUNNING
        pthread->status = TASK_RUNNING;
    else
        pthread->status = TASK_READY;
    
    // self_kstack是线程自己在内核态下使用的栈顶地址
    pthread->self_kstack = (unsigned int *)((unsigned int)pthread + PAGE_SIZE); // 参数phtread为最低地址
    pthread->priority = priority;
    pthread->ticks = priority;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    
    /* 初始化文件描述符数组 */
    pthread->fd_table[0] = 0;   // 预留标准输入0 标注输出1 标准错误2
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    unsigned char fd_index = 3; // 其余的初始为-1
    while(fd_index < MAX_FILES_OPEN_PER_PROC)
    {
        pthread->fd_table[fd_index] = -1;
        fd_index++;
    }
    
    pthread->current_work_dir_inode_id = 0;     // 以根目录作为默认路径
    
    pthread->parent_pid = -1;   // -1 表示没有父进程
    
    pthread->stack_magic = 0x19870916;          // 自定义的魔数
}




/* 创建优先级为priority的线程，线程名为name，线程所执行的函数是function(func_arg) */
struct task_struct *thread_start(char *name, int priority, thread_func function, void *func_arg)
{
    // PCB都位于内核空间，包括用户进程的PCB也是在内核空间
    struct task_struct *thread = get_kernel_pages(1);    // 在内核空间中申请一页内存
                                                        // thread指向的是PCB的最低地址
    
    init_thread(thread, name, priority);        // 初始化线程基本信息
    thread_create(thread, function, func_arg);  // 初始化线程栈
    
    
    // 确保之前不在队列中
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    // 加入就绪线程队列
    list_append(&thread_ready_list, &thread->general_tag);
    
    // 确保之前不在队列中
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    // 加入全部线程队列
    list_append(&thread_all_list, &thread->all_list_tag);
    
    
    /* 执行完这句汇编后，线程就会开始执行 */
    // 输入部分，通用约束"g"表示内存或寄存器都可以，栈顶self_kstack值赋给esp
    // thread_create()中初始化的0弹入到4个相应的寄存器中
    // ret 把栈顶的数据作为返回地址送上处理器的EIP寄存器，即开始执行 kernel_thread()函数
    // asm volatile("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" : : "g"(thread->self_kstack) : "memory");
    
    return thread;
}





/* 获取当前线程PCB指针 */
struct task_struct *running_thread()
{
    unsigned int esp;
    asm ("mov %%esp, %0" : "=g"(esp));
    
    // 取esp整数部分，即PCB起始地址
    return (struct task_struct *)(esp & 0xfffff000);
}


/* 将kernel中main函数完善为主线程 */
static void make_main_thread(void)
{
    // 因为main线程早已运行
    // 在loader.asm中进入内核时mov esp, 0xc009_f000就是为其预留的PCB
    // 因此pcb地址为0xc009_e000，不需要通过get_kernel_page另分配一页
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);
    
    // main函数是当前线程，当前线程不在thread_ready_list中，所以只加在thread_all_list中
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}




/* 实现任务调度
 * 将当前线程换下处理器，并在就绪队列中找出下个可运行的程序，将其换上处理器 */
// 由device/timer.c中的时钟中断处理函数调用
void schedule()
{
    // 处理器进入中断后，会自动把标志寄存器eflags中的IF位置0，即中断处理程序在关中断的情况下运行
    // 能进入中断处理程序就表示已经处在关中断情况下
    ASSERT(intr_get_status() == INTR_OFF);
    
    struct task_struct *current_thread = running_thread();
    if(current_thread->status == TASK_RUNNING)
    {
        // 若此线程只是cpu时间片到了，将其加入到就绪队列尾
        ASSERT(!elem_find(&thread_ready_list, &current_thread->general_tag));
        list_append(&thread_ready_list, &current_thread->general_tag);
        
        current_thread->ticks = current_thread->priority; // 将此线程的ticks重置为其priority
        current_thread->status = TASK_READY;
    }
    else
    {
        // 若此线程需要某事件发生后才能继续上CPU运行，不需要将其加入队列，
        // 因为当前线程不在就绪队列中
    }
    
    // 如果就绪队列中没有可运行的任务, 就唤醒idle
    if(list_empty(&thread_ready_list))
        thread_unblock(idle_thread);
    
    // 尚未实现idle线程，因此有可能就绪队列为空
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;  // thread_tag清空
    
    // 将thread_ready_list队列中的第一个就绪线程弹出，准备将其调度上CPU
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    
    // 激活更新任务页表，如果是进程还需要需改TSS中的esp0
    process_activate(next);
    
    // 将线程current的上下文保护好，再将线程next的上下文转载到处理器，从而完成任务切换
    switch_to(current_thread, next);    // 准备切换寄存器映像
}



/* 初始化线程环境 */
void thread_init(void)
{
    put_str("thread_init begin...");
    
    list_init(&thread_ready_list);  // 初始化就绪队列
    list_init(&thread_all_list);    // 初始化全部队列
    // lock_init(&pid_lock);           // 初始化锁，用于分配pid
    
    pid_pool_init();
    
    // 先创建第一个用户进程 init
    // 这是第一个进程, init进程的pid为1
    process_execute(init, "init");
    
    // 将当前main函数创建为线程
    make_main_thread();
    
    // 创建idle线程
    idle_thread = thread_start("idle", 10, idle, NULL);
    
    // put_str("thread_init done\n");
    put_str(" done!\n");
}



/* 当前线程将自己阻塞
 * 修改线程状态为阻塞、触发调度, 切换线程执行 */
void thread_block(enum task_status stat)
{
    // stat为BLOCKED WAITING HANGING这3种状态时不会被调度
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || \
        (stat == TASK_HANGING)));
        
    enum intr_status old_status = intr_disable();   // 关中断保证原子操作
    
    struct task_struct *current_thread = running_thread();
    current_thread->status = stat;      // 状态设为stat
    schedule();
    
    // 待当前线程被解除阻塞后，才继续运行下面的intr_set_status
    intr_set_status(old_status);
}


/* 将线程pthread解除阻塞
 * 将阻塞线程加入就绪队列、修改状态为READY */
void thread_unblock(struct task_struct *pthread)
{
    enum intr_status old_status = intr_disable();   // 关中断保证原子操作
    
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || \
        (pthread->status == TASK_HANGING)));
        
    if(pthread->status != TASK_READY)   // 保险起见
    {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));  // 保险起见. ASSERT只是调试期间用
        if(elem_find(&thread_ready_list, &pthread->general_tag))    // 保险起见
            PANIC("[ERROR]thread_unblock: blocked thread in ready_list\n");
        
        // 加到就绪队列的队首，使其尽快得到调度，保证这个睡了很久的线程能被优先调度
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    
    intr_set_status(old_status);
}



/* idle线程, 系统空闲时运行的线程 */
// idle_thread 线程在第一次创建时会被加入到就绪队列, 因此会执行一次, 然后阻塞;
// 当就绪队列为空时, schedule会将 idle_thread 解除阻塞, 也就是唤醒 idle_thread,
// idle_thread 会执行"sti hlt"先开中断, 再挂起CPU。
static void idle(void *arg __attribute__((unused)))
{
    while(1)
    {
        // 当前线程阻塞自己, 在其被唤醒后, 执行 hlt 指令, 使系统挂起
        // hlt指令让处理器停止执行指令, 也就是将处理器挂起, CPU利用率为0
        // 并不是 jmp $ 那样空兜CPU, CPU利用率为100%
        thread_block(TASK_BLOCKED);
        
        // 执行hlt时必须要保证目前处于开中断的情况下
        // 处理器已经停止运行, 因此不会再产生内部异常, 唯一能唤醒处理器的就是外部中断
        asm volatile("sti; hlt" : : : "memory");
    }
}



/* thread_yield 线程
 * 主动让出CPU, 换其他线程运行
 * thread_yield执行后任务的状态是TASK_READY, 即让出CPU, 它会被加入到就绪队列中, 下次还能继续被调度器调度执行
 * 而thread_block执行后任务的状态是TASK_BLOCKED, 需要被唤醒后才能加入到就绪队列 
 */
// 比如在等待硬盘操作的过程中, 最好把CPU主动让出来, 让CPU去执行其他任务
void thread_yield(void)
{
    struct task_struct *current = running_thread();
    enum intr_status old_status = intr_disable();
    
    ASSERT(!elem_find(&thread_ready_list, &current->general_tag));
    list_append(&thread_ready_list, &current->general_tag);
    current->status = TASK_READY;
    schedule();
    
    intr_set_status(old_status);
}




 

/* 以填充空格的方式输出buf */
// 用于对齐输出
static void pad_print(char *buf, signed int buf_len, void *ptr, char format)
{
    memset(buf, 0, buf_len);
    unsigned char out_pad_index = 0;
    
    switch(format){
    case 's':
        out_pad_index = sprintf(buf, "%s", ptr);
        break;
    case 'd':   // d 处理16位整数, 针对 pid 的补丁, 其实 task_struct 中pid可直接定义为32位
        out_pad_index = sprintf(buf, "%d", *((signed short int *)ptr));
    case 'x':   // x 处理32位整数
        out_pad_index = sprintf(buf, "%x", *((unsigned int *)ptr));
    }
    
    while(out_pad_index < buf_len)
    {
        // 以空格填充
        buf[out_pad_index++] = ' '; 
    }
    sys_write(stdout_id, buf, buf_len - 1);
}



/* 用于在 list_traversal 函数中的回调函数, 用于针对线程队列的处理 */
// 打印出进程的: pid ppid 状态 运行时间片 进程名
static bool elem2thread_info(struct list_elem *elem, int arg __attribute__((unused)))
{
    struct task_struct *thread = elem2entry(struct task_struct, all_list_tag, elem);
    
    char out_pad[16] = {0};
    
    pad_print(out_pad, 16, &thread->pid, 'd');
    
    if(thread->parent_pid == -1)
        pad_print(out_pad, 16, "NULL", 's');
    else
        pad_print(out_pad, 16, &thread->parent_pid, 'd');
    
    switch(thread->status){
    case 0:
        pad_print(out_pad, 16, "RUNNING", 's');
        break;
    case 1:
        pad_print(out_pad, 16, "READY", 's');
        break;
    case 2:
        pad_print(out_pad, 16, "BLOCKED", 's');
        break;
    case 3:
        pad_print(out_pad, 16, "WAITING", 's');
        break;
    case 4:
        pad_print(out_pad, 16, "HANGING", 's');
        break;
    case 5:
        pad_print(out_pad, 16, "DIED", 's');
        break;
    }
    
    pad_print(out_pad, 16, &thread->elapsed_ticks, 'x');
    
    memset(out_pad, 0, 16);
    ASSERT(strlen(thread->name) < 17);
    memcpy(out_pad, thread->name, strlen(thread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_id, out_pad, strlen(out_pad));
    
    // 此处返回false是为了迎合主调函数 list_traversal
    // 只有回调函数返回false时才会继续调用此函数
    return false;
}



/* 打印任务列表 */
void sys_ps(void)
{
    char *ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
    sys_write(stdout_id, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}




/* 回收 thread_over 的pcb 和页目录表, 并将其从调度队列中去除 */
void thread_exit(struct task_struct *thread_over, bool need_schedule)
{
    // 要保证 schedule 在关中断情况下调用
    intr_disable();
    
    thread_over->status = TASK_DIED;
    
    // 如果 thread_over 不是当前线程, 就有可能还在就绪队列中, 将其从中删除
    if(elem_find(&thread_ready_list, &thread_over->general_tag))
        list_remove(&thread_over->general_tag);
    
    if(thread_over->pgdir)  // 如果是进程, 回收进程的页目录表 一页框
        mfree_page(PF_KERNEL, thread_over->pgdir, 1);
    
    // 从 all_thread_list 中去掉此任务
    list_remove(&thread_over->all_list_tag);
    
    // 回收pcb所在的页, 主线程的pcb不在堆中, 跨过
    // 在laoder阶段指定在物理内存低端1MB中
    if(thread_over != main_thread)
        mfree_page(PF_KERNEL, thread_over, 1);
    
    release_pid(thread_over->pid);
    
    // 如果需要下一轮调度, 则主动调用 schedule
    // 调用 thread_exit 时, 有时候需要开始新调度, 不用回到主调函数; 
    // 有时候不需要新调度, 调用 thread_exit 后还要回到主调函数
    if(need_schedule)
    {
        schedule();
        PANIC("ERROR: during thread_exit, should not be here\n");
    }
}


/* 比对任务的pid */
// 回调函数, 对比任务的pid, 找到特定pid的任务就返回
// 调用 list_traversal 遍历全部队列中的所有任务, 通过回调函数 pid_check 过滤出特定pid 的任务
static bool pid_check(struct list_elem *pelem, signed int pid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if(pthread->pid == pid)
        return true;
    return false;
}


/* 根据pid找pcb, 若找到则返回该pcb, 否则返回NULL */
struct task_struct *pid2thread(signed int pid)
{
    struct list_elem *pelem = list_traversal(&thread_all_list, pid_check, pid);
    if(pelem == NULL)
        return NULL;
    struct task_struct *thread = elem2entry(struct task_struct, all_list_tag, pelem);
    return thread;
}