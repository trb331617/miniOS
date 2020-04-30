#include "process.h"


#include "memory.h"
#include "tss.h"

#include "debug.h"

#include "console.h"
#include "string.h"
#include "interrupt.h"
#include "global.h"

// intr_exit()函数是用户进程进入3特权级的关键
extern void intr_exit(void);

/********************************************************
 * 本系统中，创建进程的第一步是在线程中运行函数start_process
 ********************************************************/

/* 构建用户进程初始上下文信息
 * 即填充用户进程的struct intr_stack，
 * 通过假装从中断返回的方式，间接使filename运行 */
// 参数filename为用户程序名，用户程序是从文件系统上加载到内存的，因此进程名为进程的文件名
static void start_process(void *filename)
{
    // 用户进程在执行前，是由操作系统的程序加载器将用户程序从文件系统读到内存，
    // 再根据程序文件的格式解析其内容，将程序中的段展开到相应的内存地址。
    // 程序格式中会记录程序的入口地址， CPU 把CS:[E]IP 指向它，该程序就被执行了。
    // C 语言中虽然不能直接控制这两个寄存器，但函数调用其实就是改变这两个寄存器的指向

    void *function = filename;  // 文件系统尚未实现，这里先用普通函数代替用户程序
    struct task_struct *current = running_thread();
    
    current->self_kstack += sizeof(struct thread_stack);
    
    // 用户进程上下文保存在struct intr_stack栈中
    // thread/thread.c thread_create()中在PCB顶部预留了intr_stack、thread_stack的空间
    // 虽然这里是把 intr_stack 定义在PCB最顶端，但它完全可以用局部变量代替(struct intr_stack proc_stack;)
    // 因为它只用这一次，之后不需要再次访问
    struct intr_stack *proc_stack = (struct intr_stack *)current->self_kstack;
    
    // edi esi ebp esp
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    // ebx edx ecx eax
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    // gs
    // 操作系统不允许用户进程访问显存，所以将其初始化为0。另外，
    // 在特权为3的用户环境下，即使gs赋值为其它值，但由于cpl为3，特权检查时CPU会自动将相应段寄存器的选择子置0
    proc_stack->gs = 0;     // 用户态用不上，直接初始为0
    // ds es fs
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    // eip cs eflags esp ss
    proc_stack->eip = function; // 待执行的用户程序地址
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    
    // 为用户进程分配3特权级下的栈，从用户内存池中分配的地址
    proc_stack->esp = (void *)((unsigned int)get_a_page(PF_USER, USER_STACK3_VADDR) + PAGE_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    
    /* 一般情况下，CPU不允许从高特权级转向低特权级，除非是从中断和调用门返回的情况下 */
    
    // 切换esp
    // jmp intr_exit跳转到中断出口地址，通过一系列pop指令和iretd指令，将 proc_stack 中的数据载入CPU寄存器
    // 从而使程序"假装"退出中断，进入特权级3
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(proc_stack) : "memory");
}


/* 不同的进程在执行前，需要更新CR3寄存器为与之配套的页表，从而实现虚拟地址空间的隔离 */

/* 激活页表
 * 更新页目录表寄存器cr3, 使新页表生效 */
void page_dir_activate(struct task_struct *pthread)
{
/********************************************************
 * 执行此函数时,当前任务可能是线程。
 * 之所以对线程也要重新安装页表, 原因是上一次被调度的可能是进程,
 * 否则不恢复页表的话,线程就会使用进程的页表了。
 ********************************************************/

/* 若为内核线程，需要重新填充页表为0x10_0000 */
// 默认为内核的页目录物理地址，也就是内核线程所用的页目录表
// 本项目中在loader.asm开启分页机制时，就已将内核的页目录和页表存放在1MB之上(基地址为0x10_0000)
    unsigned int pagedir_phy_addr = 0x100000;  
    if(pthread->pgdir != NULL)  // 用户态进程有自己的页目录表
        // pgdir为页表的虚拟地址，而cr3存储的是物理地址
        pagedir_phy_addr = addr_v2p((unsigned int)pthread->pgdir);
    // 更新页目录寄存器cr3, 使新页表生效
    asm volatile("movl %0, %%cr3" : : "r"(pagedir_phy_addr) : "memory"); // 切换页表
}


/* 激活线程/进程的页表
 * 更新TSS中的esp0为进程的特权级0的栈 */
void process_activate(struct task_struct *pthread)
{
    ASSERT(pthread != NULL);
    
    // 激活线程/进程的页表
    page_dir_activate(pthread);
    
    // 内核线程特权级本身就是0，处理器进入中断时并不会从tss中获取0特权级栈地址
    // 即，内核线程不需要更新esp0
    if(pthread->pgdir)
        update_tss_esp(pthread);
}


/* 创建页目录表，并复制内核1G空间对应的页目录项
 * 成功则返回页目录的虚拟地址，否则返回-1 */
unsigned int *create_page_dir(void)
{
    // 用户进程的页表不能让用户直接访问到，所以在内核空间来申请
    // 用户进程的创建是在内核中完成的，因此目前是在内核的页表中，虚拟地址0xffff_f000是用来访问内核页目录表的物理基地址
    unsigned int *page_dir_vaddr = get_kernel_pages(1);
    if(page_dir_vaddr == NULL)
    {
        console_put_str("[ERROR]create_page_dir: get_kernel_page failed!\n");
        return NULL;
    }
    
/************************** 1  先复制页目录表中的内核部分  *************************************/
    // page_dir_vaddr + 0x300*4 是内核页目录的第768项所在地址
    // 用户空间3G，内核空间1G。3G/(1024*4KB)=0x300, 即768
    // 每个表项本身占4字节，所以0x300*4
    // 1024即2^10, /4字节=2^8个页目录项, 2^8 * 4MB = 1G
    memcpy((unsigned int *)((unsigned int)page_dir_vaddr + 0x300*4), \
           (unsigned int *)(0xfffff000+0x300*4), 1024);

/************************** 2  更新页目录地址 **********************************/
    // 页目录项中存储的是页表所在物理页的物理地址及页目录项属性
    unsigned int new_page_dir_phy_addr = addr_v2p((unsigned int)page_dir_vaddr);
    
    // 页目录地址是存入在页目录的最后一项,更新页目录地址为新页目录的物理地址
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
    
    return page_dir_vaddr;
}


/* 创建用户进程虚拟地址位图 */
static void create_user_vaddr_bitmap(struct task_struct *user_prog)
{
    // readelf -e可查看可执行程序的"Entry Point Address"
    user_prog->user_vaddr.vaddr_begin = USER_VADDR_START;   // 用户进程的起始地址0x0804_8000
    // 0xc000_0000为3G
    unsigned int bitmap_page_count = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PAGE_SIZE / 8, PAGE_SIZE);
    // 存放位图bitmap的起始地址
    user_prog->user_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_page_count);
    user_prog->user_vaddr.vaddr_bitmap.bitmap_bytes_len = (0xc0000000 - USER_VADDR_START) / PAGE_SIZE / 8;
    bitmap_init(&user_prog->user_vaddr.vaddr_bitmap);
}

/* 创建用户进程，并加入就绪队列 */
// 参数filename 用户进程地址, name 进程名
void process_execute(void *filename, char *name)
{ 
    // PCB内核的数据结构，由内核来维护进程信息，因此要在内核内存池中申请
    struct task_struct *thread = get_kernel_pages(1);
    
    init_thread(thread, name, default_prio);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();
    
    block_desc_init(thread->u_block_desc);      // 初始化内存规格信息, 为malloc做准备
    
    enum intr_status old_status = intr_disable();   // 关中断
    
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    
    intr_set_status(old_status);    // 恢复中断状态
}