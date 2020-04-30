#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "dir.h"        // struct dir

/* 文件结构 */
// 本项目中的管道实现复用了此结构: 若是管道, fd_flag 为 0xFFFF, fd_inode 指向管道的内存缓冲区, fd_pos管道的打开数
struct file{
    unsigned int fd_pos;    // 当前文件操作的偏移地址, 值最小为0 最大为文件大小-1
    unsigned int fd_flag;   // 文件操作标识, 如 O_RDONLY O_WRONLY
    struct inode *fd_inode; // inode指针, 用来指向inode队列 part->open_inodes 中的inode
};


/* 标准输入输出描述符 */
// 本项目中只实现了标准输入/输出
enum std_fd{
    stdin_id,       // 0 标准输入
    stdout_id,      // 1 标准输出
    stderr_id       // 2 标准错误
};


/* 位图类型 */
enum bitmap_type{
    INODE_BITMAP,   // inode位图
    BLOCK_BITMAP    // 块位图
};

// 系统可打开的最大文件数
#define MAX_FILE_OPEN   32


/* 文件表, 即文件结构数组 */
// 一个文件可以被多次打开, 甚至把file_table占满
extern struct file file_table[MAX_FILE_OPEN];


/* 创建文件, 若成功则返回文件描述符, 否则返回-1 */
// 创建文件i结点 -> 文件描述符fd -> 目录项
signed int file_create(struct dir *parent_dir, char *filename, unsigned char flag);

/* 分配一个扇区, 返回其扇区地址 */
signed int block_bitmap_alloc(struct partition *part);


/* 将内存中bitmap第bit_index位所在的512字节同步到硬盘 */
void bitmap_sync(struct partition *part, unsigned int bit_index, unsigned char bitmap_type);


/* 打开编号为 inode_id 的inode对应的文件 */
// 成功则返回文件描述符, 失败返回-1
signed int file_open(unsigned int inode_id, unsigned char flag);

/* 关闭文件 */
// 成功返回0, 失败返回-1
signed int file_close(struct file *file);


/* 把buf中的count个字节写入file,成功则返回写入的字节数,失败则返回-1 */
signed int file_write(struct file* file, const void* buf, unsigned int count);


/* 从文件file中读取count个字节写入buf, 返回读出的字节数,若到文件尾则返回-1 */
signed int file_read(struct file* file, void* buf, unsigned int count);


/* 分配一个i结点, 返回i结点号 */
signed int inode_bitmap_alloc(struct partition *part);


/* 从文件表file_table中获取一个空闲位, 成功返回下标, 失败返回-1 */
signed int get_free_slot_in_global_filetable(void);



/* 将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中
 * 成功返回下标, 失败返回-1 */
signed int pcb_fd_install(signed int global_fd_index);

#endif