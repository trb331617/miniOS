#include "pipe.h"

#include "fs.h"     // fd_local2global
#include "file.h"   // file_table
#include "ioqueue.h"

/* 判断文件描述符 local_fd 是否为管道 */
bool is_pipe(unsigned int local_fd)
{
    unsigned int global_fd = fd_local2global(local_fd);
    // DEBUG_2020-04-18
    return file_table[global_fd].fd_flag == PIPE_FLAG;
}


/* 创建管道, 成功返回0, 失败返回-1 */
signed int sys_pipe(signed int pipefd[2])
{
    signed int global_fd = get_free_slot_in_global_filetable();
    
    // 申请一页内核内存做环形缓冲区
    file_table[global_fd].fd_inode = get_kernel_pages(1);
    
    // 初始化环形缓冲区
    ioqueue_init((struct ioqueue *)file_table[global_fd].fd_inode);
    if(file_table[global_fd].fd_inode == NULL)
        return -1;
    
    // 将fd_flag复用为管道标志
    file_table[global_fd].fd_flag = PIPE_FLAG;
    
    // 将fd_pos复用为管道打开数
    file_table[global_fd].fd_pos = 2;
    pipefd[0] = pcb_fd_install(global_fd);  // 安装文件描述符
    pipefd[1] = pcb_fd_install(global_fd);
    
    return 0;
}


/* 从管道中读数据 */
// 从文件描述符fd中读取count字节到buf
unsigned int pipe_read(signed int fd, void *buffer, unsigned int count)
{
    char *buf = buffer;
    unsigned int bytes_read = 0;
    unsigned int global_fd = fd_local2global(fd);
    
    // 获取管道的环形缓冲区
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;
    
    // 选择较小的数据读取量, 避免阻塞
    unsigned int ioq_len = ioq_length(ioq);
    unsigned int size = ioq_len > count ? count : ioq_len;
    while(bytes_read < size)
    {
        *buf = ioq_getchar(ioq);
        bytes_read++;
        buf++;
    }
    return bytes_read;
}


/* 往管道中写数据 */
unsigned int pipe_write(signed int fd, const void *buffer, unsigned int count)
{
    unsigned int bytes_write = 0;
    unsigned int global_fd = fd_local2global(fd);
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;
    
    // 选择较小的数据写入量, 避免阻塞
    unsigned int ioq_left = buffersize - ioq_length(ioq);
    unsigned int size = ioq_left > count ? count : ioq_left;
    
    const char *buf = buffer;
    while(bytes_write < size)
    {
        ioq_putchar(ioq, *buf);
        bytes_write++;
        buf++;
    }
    return bytes_write;
}



/* 将文件描述符 old_local_fd 重定向为 new_local_fd */
// 文件描述符为PCB中数组 fd_table 的下标, 数组元素的值是全局文件表 file_table 的下标
// 文件描述符重定向的原理为: 将数组fd_table中下标为 old_local_fd 的元素值 用下标为 new_local_fd 的元素值替换
void sys_fd_redirect(unsigned int old_local_fd, unsigned int new_local_fd)
{
    struct task_struct *current = running_thread();
    // 针对恢复标准描述符, 不需要从 fd_table 中获取元素值, 直接把 new_local_fd 赋值给 fd_table[old_local_fd]
    if(new_local_fd < 3)    // 标准输入 输出 错误
        current->fd_table[old_local_fd] = new_local_fd;
    else
    {
        unsigned int new_global_fd = current->fd_table[new_local_fd];
        current->fd_table[old_local_fd] = new_global_fd;
    }
}



