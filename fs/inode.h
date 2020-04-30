#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "global.h"     // bool
#include "list.h"       // struct list_elem
#include "ide.h"        // struct partition

/* inode结构 */
struct inode{
    unsigned int inode_id;      // inode编号, 即在inode数组中的下标
    
    // inode指向的文件的大小, 字节为单位
    // 当此inode是文件时, inode_size为文件大小
    // 当此inode是目录时, inode_size为该目录下所有目录项大小之和
    unsigned int inode_size;
    
    unsigned int inode_open_count;  // 记录此文件被打开的次数
    bool write_deny;    // 写文件不能并行, 进程写文件前检查此标识
    
    // 0~11 为直接块指针, 12 为一级间接块索引表指针
    /* 本项目中为了简单, 数据块大小与扇区大小一致, 即1块等于1扇区 */
    // 本项目中支持一级间接块, 总共支持 12 + 512/4 = 140个块
    unsigned int inode_blocks[13];
    
    // 已打开的inode队列, 作为缓存
    // inode是保存在硬盘上的, 文件被打开时, 肯定先要从硬盘上载入其inode, 但硬盘比较慢
    // 为了避免下次再打开该文件时还要从硬盘上重复载入inode
    // 此inode的标识, 用于加入"已打开的inode列表"
    struct list_elem inode_tag;
};



/* 根据i结点号返回相应的i结点 */
// inode是存储在磁盘上, 为减少频繁访问磁盘, 在内存中为各分区创建了inode队列, 即 part->open_inodes
// open_inodes为已打开的inode队列, 是inode的缓存, 以后每打开一个inode, 先在此缓存中查找
// 若未找到, 再从磁盘上加载该inode到此缓存中
struct inode *inode_open(struct partition *part, unsigned int inode_id);

/* 关闭inode或减少inode的打开数 */
void inode_close(struct inode *inode);

/* 将inode写入到分区part */
// 分区part 待同步的inode指针 操作缓冲区io_buf是用于硬盘io的缓冲区
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);

/* 初始化new_inode */
// 创建inode时不为其分配扇区, 真正写文件时才分配扇区
void inode_init(unsigned int inode_id, struct inode *new_inode);

/* 回收inode的数据块和inode本身 */
void inode_release(struct partition* part, unsigned int inode_no);

#endif