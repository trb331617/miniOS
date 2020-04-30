#include "wait_exit.h"

#include "thread.h"     // struct task_struct, pid2thread, release_pid
#include "fs.h"         // sys_close    

#include "debug.h"      // PANIC

#include "pipe.h"       // is_pipe
#include "file.h"       // file_table

/* 释放用户进程资源: 
 * 1 页表中对应的物理页
 * 2 虚拟内存池占物理页框
 * 3 关闭打开的文件 */
static void release_prog_resourece(struct task_struct *release_thread)
{
    unsigned int *pgdir_vaddr = release_thread->pgdir;
    
    // 页目录表PDT
    // 用户空间3G, 768*4M/1024 = 3G
    unsigned short int user_pde_count = 768, pde_index = 0;
    unsigned int pde = 0;
    unsigned int *var_pde_ptr = NULL;   // 和函数 pde_ptr 区分
    
    // 页表
    unsigned short int user_pte_count = 1024, pte_index = 0;
    unsigned int pte = 0;
    unsigned int *var_pte_ptr = NULL;   // 和函数 pte_ptr 区分
    
    unsigned int *first_pte_vaddr_in_pde = NULL; // pde中第0个pte的地址
    unsigned int page_phy_addr = 0;
    
    // 回收页表中用户空间的页框
    while(pde_index < user_pde_count)   // 外层循环遍历页目录表中的pde
    {
        var_pde_ptr = pgdir_vaddr + pde_index;    // 指针按类型加减
        pde = *var_pde_ptr;     // 页目录表项的值
        
        // 如果页目录项P位为1, 表示该页目录项下可能有页表项
        // 在回收内存空间时, 页表中的pte有可能被回收干净了, 但该页表所在的pde还在
        // 当初是为了减少页表的频繁变动而有意为之
        if(pde & 0x00000001)
        {
            // 一个页目录表项表示的内存容量为4M, 即 0x400000
            // pde_index*4M 为虚拟线性地址, 调用pte_ptr得到虚拟地址对应的页表项指针
            // 这里为 页表的基地址
            first_pte_vaddr_in_pde = pte_ptr(pde_index * 0x400000);
            pte_index = 0;
            while(pte_index < user_pte_count)   // 内层循环遍历页表中的pte
            {
                var_pte_ptr = first_pte_vaddr_in_pde + pte_index; // 指针按类型加减
                pte = *var_pte_ptr;     // 页表项的值, 即物理页的起始地址
                if(pte & 0x00000001)    // 表示分配了物理页
                {
                    // 将pte中记录的物理页框直接在相应内存池的位图中清0
                    page_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(page_phy_addr);
                }
                pte_index++;
            }
            // 将pde中记录的物理页框直接在相应内存池的位图中清0
            page_phy_addr = pde & 0xfffff000;
            free_a_phy_page(page_phy_addr);
        }
        pde_index++;
    }
    
    // 回收用户虚拟地址池所占的物理内存
    unsigned int bitmap_page_count = (release_thread->user_vaddr.vaddr_bitmap.bitmap_bytes_len) / PAGE_SIZE;
    unsigned char *user_vaddr_pool_bitmap = release_thread->user_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_page_count);
    
    // 关闭进程打开的文件
    unsigned char fd_index = 3;
    while(fd_index < MAX_FILES_OPEN_PER_PROC)
    {
        if(release_thread->fd_table[fd_index] != -1)
        {
            // 程序退出时, 管道也要做处理
            if(is_pipe(fd_index))
            {
                unsigned int global_fd = fd_local2global(fd_index);
                if(--file_table[global_fd].fd_pos == 0) // 如果减1后的值为0, 说明没有进程再打开此管道    
                {
                    // 回收管道环形缓冲区占用的一页内核页框
                    mfree_page(PF_KERNEL, file_table[global_fd].fd_inode, 1);
                    file_table[global_fd].fd_inode = NULL;
                }
            }
            else    // 普通文件
                sys_close(fd_index);
        }
        fd_index++;
    }
}




/* list_traversal 的回调函数
 * 查找 pelem 的 parent_id 是否为ppid, 成功返回true, 失败则返回false
 */
static bool find_child(struct list_elem *pelem, signed int ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    
    // 若该任务的parent_id为ppid, 则返回
    if(pthread->parent_pid == ppid)
        return true;
    // list_traversal只有在回调函数返回true时才会停止继续遍历,所以在此返回true
    // 返回false, 则list_traversal继续传递下一个元素
    return false;
}


/* list_traversal 的回调函数
 * 查找状态为 TASK_HANGING 的任务
 */
static bool find_hanging_child(struct list_elem *pelem, signed int ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    
    if(pthread->parent_pid == ppid && pthread->status == TASK_HANGING)
        return true;
    return false;
}


/* list_traversal 的回调函数
 * 将一个子进程过继给init
 */
static bool init_adopt_a_child(struct list_elem *pelem, signed int pid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    
    // 若该进程的 parent_pid 为pid, 则返回
    if(pthread->parent_pid == pid)
        pthread->parent_pid = 1;
    
    // 让list_traversal继续传递下一个元素
    // 父进程为pid的所有子进程都过继给init
    return false;
}


/* 等待子进程调用exit, 将子进程的退出状态保存到status指向的变量
 * 成功则返回子进程的pid, 失败则返回-1
 */
signed short int sys_wait(signed int *status)
{
    struct task_struct *parent_thread = running_thread();
    
    while(1)
    {
        // 优先处理已经是挂起状态的任务, 即已经exit的子进程
        struct list_elem *child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
        
        // 若有挂起的子进程
        if(child_elem != NULL)
        {
            struct task_struct *child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
            // 获取子进程的 exit_status
            *status = child_thread->exit_status;
            
            // thread_exit 之后, pcb会被回收, 因此提前获取pid
            unsigned short int child_pid = child_thread->pid;
            
            // 2) 从就绪队列和全部队列中删除进程表项
            thread_exit(child_thread, false); // 传入false, 使 thread_exit 调用后回到此处   
            /* 进程表项是进程或线程的最后保留的资源, 至此该进程彻底消失了 */
            
            return child_pid;
        }
        
        // 判断是否有子进程
        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        // 若没有子进程则出错返回
        if(child_elem == NULL)
            return -1;
        // 若子进程还未运行完, 即还未调用exit, 则将自己挂起, 直到子进程执行exit时将自己唤醒
        else
            thread_block(TASK_WAITING);
    }
} 


/* 子进程用来结束自己时调用 */
void sys_exit(signed int status)
{
    struct task_struct *child_thread = running_thread();
    child_thread->exit_status = status; // 将status存入自己的pcb    
    if(child_thread->parent_pid == -1)
        PANIC("ERROR: during sys_exit, child_thread->parent_id is -1\n");
    
    // 将进程 child_thread 的所有子进程都过继给init
    list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);
    
    // 回收进程 child_thread 的资源
    release_prog_resourece(child_thread);
    
    struct task_struct *parent_thread = pid2thread(child_thread->parent_pid);
    if(parent_thread->status == TASK_WAITING)
        thread_unblock(parent_thread);
    
    // 将自己挂起, 等待父进程获取其status, 并回收其pcb
    thread_block(TASK_HANGING);
} 
