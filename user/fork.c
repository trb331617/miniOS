#include "fork.h"

#include "thread.h"     // struct task_struct
    // MAX_FILS_OPEN_PER_PROC
    
#include "interrupt.h"  // intr_get_status
#include "file.h"       // file_table    
#include "process.h"    // USER_VADDR_START

#include "string.h"     // memcpy
#include "debug.h"      // ASSERT
#include "global.h"     // NULL

#include "pipe.h"       // is_pipe

extern void intr_exit(void);


/* 将父进程的PCB 虚拟地址位图拷贝给子进程 */
static signed int copy_pcb_vaddr_bitmap_stack0(struct task_struct *child_thread, struct task_struct *parent_thread)
{
// a) 复制PCB所在的整个页, 里面包含进程PCB信息及特权0级的栈, 里面包含了返回地址
//    然后再单独修改个别部分
    memcpy(child_thread, parent_thread, PAGE_SIZE);
    
    child_thread->pid = fork_pid();     // thread/thread.c 中, 仅是allocate_pid的封装
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority;   // 为子进程把时间片充满
    child_thread->parent_pid = parent_thread->pid;
    
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
    
    // 初始化进程自己的内存块描述符, 本项目中定义了7种规格的内存块
    // 如果没初始化的话, 则将继承父进程的块描述符, 子进程分配内存时会导致缺页异常
    block_desc_init(child_thread->u_block_desc);     
    
// b) 复制父进程的虚拟地址池的位图
    unsigned int bitmap_page_count = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PAGE_SIZE / 8, PAGE_SIZE);
    void *vaddr_bitmap = get_kernel_pages(bitmap_page_count); // 在内核中 ?
    if(vaddr_bitmap == NULL)
        return -1;
/* 此时child_thread->user_vaddr.vaddr_bitmap.bits还是指向父进程虚拟地址的位图地址
 * 下面将child_thread->user_vaddr.vaddr_bitmap.bits指向自己的位图vaddr_btmp */    
    memcpy(vaddr_bitmap, child_thread->user_vaddr.vaddr_bitmap.bits, bitmap_page_count * PAGE_SIZE);
    child_thread->user_vaddr.vaddr_bitmap.bits = vaddr_bitmap;
    
    // 调试用, 调试后删除。(父子进程应该是同名的)
    // ASSERT(strlen(child_thread->name) < 11);    // pcb.name的长度是16, 为避免下面strcat越界
    // strcat(child_thread->name, "_fork");
    
    return 0;
}


/* 复制子进程的进程体(代码和数据)及用户栈 */
// 拷贝进程的代码和数据资源, 也就是复制一份进程体
// 各用户进程的低3GB空间是独立的, 各用户进程不能互相访问彼此的空间, 但高1GB是内核空间,
// 因此, 借助内核空间作为数据中转。高1GB是内核空间, 内核空间是所有用户进程共享的
// 由于用户栈位于低3GB虚拟空间中的最高处, 所以循环到最后时会完成用户栈的复制
static void copy_body_stack3(struct task_struct *child_thread, struct task_struct *parent_thread, void *buf_page)
{
    unsigned char *vaddr_bitmap = parent_thread->user_vaddr.vaddr_bitmap.bits;
    unsigned int bitmap_bytes_len = parent_thread->user_vaddr.vaddr_bitmap.bitmap_bytes_len;
    unsigned int vaddr_begin = parent_thread->user_vaddr.vaddr_begin;
    
    /* 在父进程的用户空间中查找已有数据的页 */
    // 只拷贝用户空间中有效的部分, 即有数据的部分
    // 为节省缓冲区空间, 在父进程虚拟地址空间中每找到一页, 就在子进程虚拟地址空间中分配一页,
    // 一页一页的拷贝
    unsigned int index_byte = 0;
    while(index_byte < bitmap_bytes_len)    // 逐字节查看位图
    {
        if(vaddr_bitmap[index_byte])    // 该字节不为0
        {
            unsigned int index_bit = 0;
            while(index_bit < 8)
            {
                if((1 << index_bit) & vaddr_bitmap[index_byte])   // 逐位查看
                {
                    // 将该位转换为对应的虚拟地址
                    unsigned int user_vaddr = (index_byte * 8 + index_bit) * PAGE_SIZE + vaddr_begin;
                    
                    // 下面的操作是将父进程用户空间中的数据通过内核空间做中转, 最终复制到子进程的用户空间
                    
                    // a) 将父进程在用户空间中的数据复制到内核缓冲区buf_page, 
                    //    目的是下面切换到子进程的页表后, 还能访问到父进程的数据
                    memcpy(buf_page, (void *)user_vaddr, PAGE_SIZE);
                    
                    // b) 将页表切换到子进程, 目的是避免下面申请内存的函数将pte及pde安装在父进程的页表中
                    // 一定要将页表替换为子进程的页表
                    page_dir_activate(child_thread);    // 激活子进程的页表
                    
                    // c) 申请虚拟地址
                    get_a_page_without_operate_vaddrbitmap(PF_USER, user_vaddr);
                    
                    // d) 从内核缓冲区中将父进程数据复制到子进程的用户空间
                    memcpy((void *)user_vaddr, buf_page, PAGE_SIZE);
                    
                    // e) 恢复父进程页表, 继续寻找父进程占用的虚拟空间
                    page_dir_activate(parent_thread);
                }
                index_bit++;
            }
        }
        index_byte++;
    }
}


/* 为子进程构建thread_stack和修改返回值 */
// 父进程执行fork系统调用时会进入内核态, 中断入口程序会保存父进程的上下文,
// 这其中包括进程在用户态下的 cs:eip, 父进程从fork系统调用返回后, 可以继续执行fork之后的代码

// copy_pcb_vaddr_bitmap_stack0 中将父进程的内核栈复制到了子进程的内核栈中, 那里保存了返回地址, 也就是fork之后的地址,
// 为了让子进程也能继续fork之后的代码运行, 必须让他同父进程一样, 从中断退出, 
//也就是要经过 intr_exit
// 子进程是由调度器 schedule 调度执行的, 它要用到 switch_to 函数, 而switch_to要从栈
// thread_stack 中恢复上下文, 因此要构建 thread_stack
static signed int build_child_stack(struct task_struct *child_thread)
{
// a) 使子进程pid返回值为0
    // 获取子进程0级栈栈顶
    // 栈 intr_stack 是中断入口程序 intr_%1_entry 保存任务上下文的地方
    struct intr_stack *intr_0_stack = (struct intr_stack *)((unsigned int)child_thread + PAGE_SIZE - sizeof(struct intr_stack));
    
    // 修改子进程的返回值为0, fork会为子进程返回0
    // 根据abi约定, eax寄存器中是函数返回值
    intr_0_stack->eax = 0;
    
// b) 为 switch_to 构建 struct thread_stack, 将其构建在紧临intr_stack之下的空间
    // eip
    unsigned int *ret_addr_in_thread_stack = (unsigned int *)intr_0_stack - 1;
    
    /*** 这三行不是必要的,只是为了梳理thread_stack中的关系 ***/
    // 实际运行中不需要这3个具体值, 这里只是为了使thread_stack栈代码上更具可读性
    unsigned int* esi_ptr_in_thread_stack = (unsigned int*)intr_0_stack - 2; 
    unsigned int* edi_ptr_in_thread_stack = (unsigned int*)intr_0_stack - 3; 
    unsigned int* ebx_ptr_in_thread_stack = (unsigned int*)intr_0_stack - 4;
    /**********************************************************/

    // ebp在thread_stack中的地址便是当时的esp(0级栈的栈顶)
    // esp 为 (unsigned int)intr_0_stack - 5
    // thread_stack的栈顶, 必须存放在PCB中偏移为0的位置, 即task_struct中self_kstack
    // 将来 switch_to 要用它作为栈顶, 并执行一系列pop来恢复上下文
    unsigned int *ebp_ptr_in_thread_stack = (unsigned int *)intr_0_stack - 5;
    
    // switch_to 的返回地址更新为 intr_exit, 直接从中断返回
    // 子进程被调度时, 直接从中断返回, 也就是实现了从fork之后的代码处继续执行
    *ret_addr_in_thread_stack = (unsigned int)intr_exit;
    
    /**** 下main这2行赋值只是为了使构建的thread_stack更加清晰, 其实也不需要
     **** 因为在进入 intr_exit 后一系列的pop会把寄存器中的数据覆盖  ***/
    *ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack = \
    *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;
    /*********************************************************/    
    
    // 把构建的thread_stack的栈顶做为switch_to恢复数据时的栈顶
    // 这样switch_to便获得了刚刚构建的thread_stack栈顶, 从而使程序迈向了intr_exit
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    
    return 0; 
}



/* 更新inode打开数 */
// 遍历fd_table中除前3个标准文件描述符之外的所有文件描述符, 从中获得全局文件表
// file_table 的下标 global_id, 通过它在file_table中找到相应的文件结构, 使相应文件
// 结构中的fd_inode的 i_open_count 加1
static void update_inode_open_counts(struct task_struct *thread)
{
    signed int local_fd = 3, global_fd = 0;
    while(local_fd < MAX_FILES_OPEN_PER_PROC)
    {
        global_fd = thread->fd_table[local_fd];
        ASSERT(global_fd < MAX_FILE_OPEN);
        if(global_fd != -1)
        {
            // 管道是由父子进程共享的, 在fork时也要增加管道的打开数
            if(is_pipe(local_fd))
                file_table[global_fd].fd_pos++;
            else
                file_table[global_fd].fd_inode->inode_open_count++;
        }
        local_fd++;
    }
}



/* 拷贝父进程本身所占资源给子进程 */
// 是前面函数的封装
static signed int copy_process(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    // 内核缓冲区, 作为父进程用户空间的数据复制到子进程用户空间的中转
    void *buf_page = get_kernel_pages(1);
    if(buf_page == NULL)
        return -1;
    
    // a) 复制父进程的PCB、虚拟地址位图、内核栈到子进程
    if(copy_pcb_vaddr_bitmap_stack0(child_thread, parent_thread) == -1)
        return -1;
    
    // b) 为子进程创建页表, 此页表仅包括内核空间
    child_thread->pgdir = create_page_dir();    // user/process.c
    if(child_thread->pgdir == NULL)
        return -1;
    
    // c) 复制父进程进程体及用户栈给子进程
    copy_body_stack3(child_thread, parent_thread, buf_page);
    
    // d) 构建子进程thread_stack和修改返回值pid
    build_child_stack(child_thread);
    
    // e) 更新文件inode的打开数
    update_inode_open_counts(child_thread);
    
    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}



/* fork子进程, 内核线程不可直接调用 */
// 克隆当前进程, 即父进程
signed short int sys_fork(void)
{
    struct task_struct *parent_thread = running_thread();
    struct task_struct *child_thread = get_kernel_pages(1); // 为子进程创建PCB
    
    if(child_thread == NULL)
        return -1;
    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);
    
    if(copy_process(child_thread, parent_thread) == -1)
        return -1;
    
    // 添加到就绪线程队列和所有线程队列, 子进程由调度器安排运行
    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
    list_append(&thread_ready_list, &child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);
    
    return child_thread->pid;   // 父进程返回子进程的pid
}