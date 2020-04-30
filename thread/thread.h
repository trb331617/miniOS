#ifndef __THREAD_H
#define __THREAD_H

#include "list.h"
#include "memory.h"

/* 自定义通用函数类型,它将在很多线程函数中做为形参类型 */
typedef void thread_func(void*);

/* 每个任务可以打开的文件数 */
#define MAX_FILES_OPEN_PER_PROC     8

#define TASK_NAME_LEN               16

/* 进程/线程的6个状态 */
enum task_status{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};


/***********   中断栈intr_stack   ***********
 * 此结构用于中断发生时保护程序(线程或进程)的上下文环境:
 * 进程或线程被外部中断或软中断打断时,会按照此结构压入上下文
 *
 * 进入中断后，在core_interrupt.asm中的中断入口程序"intr_%1_entry"所执行的
 * 上下文保护的一系列压栈操作都是压入了此结构中
 *
 * 寄存器, core_interrupt.asm中intr_exit的出栈操作是此结构的逆操作
 * 此栈在线程自己的内核栈中位置固定,所在页的最顶端
********************************************/
struct intr_stack {
    unsigned int vec_id; // core_interrupt 宏VECTOR中push %1压入的中断号
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp_dummy;	 // 虽然pushad把esp也压入,但esp是不断变化的,所以会被popad忽略
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;

/* 以下由cpu从低特权级进入高特权级时压入 */
    unsigned int err_code;		 // err_code会被压入在eip之后
    void (*eip) (void);
    unsigned int cs;
    unsigned int eflags;
    void* esp;
    unsigned int ss;
};



/***********  线程栈thread_stack  ***********
 * 线程自己的栈,用于存储线程中待执行的函数
 * 此结构在线程自己的内核栈中位置不固定,
 * 用在switch_to时保存线程环境。
 * 实际位置取决于实际运行情况。
 ******************************************/
struct thread_stack {
   unsigned int ebp;
   unsigned int ebx;
   unsigned int edi;
   unsigned int esi;

/* 线程第一次执行时,eip指向待调用的函数kernel_thread 
 * 其它时候,eip是指向switch_to的返回地址*/
   void (*eip) (thread_func* func, void* func_arg);

/*****   以下仅供第一次被调度上cpu时使用   ****/

/* 参数unused_ret只为占位置，充当为返回地址 */
   void (*unused_retaddr);  // 在返回地址所在的栈帧占个位置
                            // 按照规定，栈顶为返回地址，栈顶之上为参数
   thread_func* function;   // 由Kernel_thread所调用的函数名
   void* func_arg;    // 由Kernel_thread所调用的函数所需的参数
};



/* 进程/线程的PCB, 程序控制块 */
struct task_struct {
// self_kstack是各线程的内核栈顶指针，在线程被创建时被初始化为自己PCB所在页的顶端
   unsigned int *self_kstack;	 // 各内核线程都用自己的内核栈
   
   signed short int pid;
   
   enum task_status status;
   char name[TASK_NAME_LEN];   // 线程/进程的名字，最长不超过16个字符
   
   unsigned char priority;		 // 线程优先级。优先级越高，时间片ticks越长
   /* 简单优先级调度的基础 */
   unsigned char ticks;     // 每次在处理器上执行的时间嘀嗒数
   unsigned int elapsed_ticks;  // 执行了多久
   
   // general_tag是线程的标签，当线程被加入到就绪队列或其他等待队列中时
   // 就把该线程PCB中general_tag的地址加入队列
   struct list_elem general_tag; // 用于线程在一般的队列中的结点
   
   // 为管理所有线程，还存在一个全部线程队列thread_all_list
   struct list_elem all_list_tag; // 用于线程队列thread_all_list中的结点
   
   unsigned int *pgdir;     // 进程页目录表的虚拟地址，如果该任务为线程则为NULL
                            // 寄存器CR3中加载的是页目录表的物理地址，所以后面还需要将pgdir转换为物理地址
                            
   struct virtual_addr user_vaddr;  // 用户进程的虚拟地址位图
   
   // 用户进程内存块描述符, 本项目中定义了7种规格的内存块
   struct mem_block_desc u_block_desc[MEM_DESC_COUNT];  
   
   /* 文件描述符数组 */
   signed int fd_table[MAX_FILES_OPEN_PER_PROC];   
   
   unsigned int current_work_dir_inode_id;  // 进程当前所在工作目录的inode编号
   
   signed short int parent_pid;     // 父进程pid
   
   signed char exit_status;     // 进程结束时自己调用exit传入的参数
   
   // PCB的上端是0特权级栈，将来线程在内核态下的任何栈操作都是用此PCB中的栈
   // 如果出现了某些异常导致入栈操作过多，则会破坏PCB低处的线程信息
   unsigned int stack_magic;	 // 用这串数字做栈的边界标记,用于检测栈的溢出
                    // 每次在线程/进程调度时要判断是否触及到了进程信息的边界
};


extern struct list thread_ready_list;
extern struct list thread_all_list;


void thread_create(struct task_struct *pthread, thread_func function, void *func_arg);
void init_thread(struct task_struct *pthread, char *name, int priority);
struct task_struct *thread_start(char *name, int priority, thread_func function, void *func_arg);
void thread_init(void);
struct task_struct *running_thread(void);
void schedule(void);

void thread_block(enum task_status stat);
void thread_unblock(struct task_struct *pthread);

void thread_yield(void);


/* fork进程时为其分配pid,因为allocate_pid已经是静态的,别的文件无法调用.
 * 不想改变函数定义了,故定义fork_pid函数来封装一下。*/
signed short int fork_pid(void);



/* 打印任务列表 */
void sys_ps(void);


/* 回收 thread_over 的pcb 和页表, 并将其从调度队列中去除 */
void thread_exit(struct task_struct *thread_over, bool need_schedule);

/* 根据pid找pcb, 若找到则返回该pcb, 否则返回NULL */
struct task_struct *pid2thread(signed int pid);

/* 释放pid */
void release_pid(signed short int pid);
#endif
