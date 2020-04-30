#include "inode.h"

#include "ide.h"        // struct partition
#include "file.h"       // BLOCK_BITMAP

#include "string.h"     // memcpy
#include "interrupt.h"  // enum intr_status

#include "debug.h"

/* 存储inode位置 */
// inode所在的扇区地址及在扇区内的偏移量, 用于定位inode在磁盘上的位置
struct inode_position{
    bool two_sector;            // inode是否跨扇区
    unsigned int sector_lba;    // inode所在的扇区号
    unsigned int offset_size;   // inode在扇区内的字节偏移量
};


/* 获取inode所在的扇区和扇区内偏移量 */
// 分区part inode编号inode_id inode_pos, 定位inode所在的扇区和扇区内偏移量, 并写入inode_pos
static void inode_locate(struct partition *part, unsigned int inode_id, struct inode_position *inode_pos)
{
    /* inode_table在硬盘上是连续的 */
    ASSERT(inode_id < 4096);
    
    // 从分区超级块的inode_table_lba中获取inode_table的扇区地址
    unsigned int inode_table_lba = part->sbk->inode_table_lba;
    unsigned int inode_size = sizeof(struct inode);
    // 第inode_id号结点相对inode_table_lba的字节偏移量
    unsigned int offset_size = inode_id * inode_size;
    // 第inode_id号结点相对inode_table_lba的扇区偏移量
    unsigned int offset_sector = offset_size / 512;
    // 待查找的inode所在扇区中的起始地址
    unsigned int offset_size_in_sector = offset_size % 512;
    
    /* 判断此inode结点是否跨越2个扇区 */
    unsigned int left_in_sector = 512 - offset_size_in_sector;
    if(left_in_sector < inode_size)
    {
        // 若扇区内剩下的空间不足以容纳一个inode, 必然是i结点跨越了2个扇区
        // 即inode一部分在当前扇区末尾, 另一部分在下一扇区开头处
        inode_pos->two_sector = true;
    }
    else    // 否则, 所查找的inode未跨扇区
    {
        inode_pos->two_sector = false;
    }
    inode_pos->sector_lba = inode_table_lba + offset_sector;
    inode_pos->offset_size = offset_size_in_sector;
}


/* 将inode写入到分区part */
// 分区part 待同步的inode指针 操作缓冲区io_buf是用于硬盘io的缓冲区
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    unsigned char inode_id = inode->inode_id;
    struct inode_position inode_pos;
    
    // 先定位到该inode的位置, 并保存在inode_pos中
    inode_locate(part, inode_id, &inode_pos);
    ASSERT(inode_pos.sector_lba <= (part->start_lba + part->sector_count));
    
    /* 硬盘中的inode成员inode_tag和inode_open_count是不需要的, 
     * 它们只在内存中记录链表位置和被多少进程共享 */
    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));
    
    // 以下inode的三个成员只存在于内存中, 现将inode同步到硬盘, 清掉这3项即可
    // 这3个成员是用于统计inode操作状态, 只在内存中有意义
    pure_inode.inode_open_count = 0;
    pure_inode.write_deny = false;  // 置为false, 以保证在硬盘中读出时为可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;
    
    char *inode_buf = (char *)io_buf;
    // 若是跨了2个扇区, 就要读出2个扇区再写入2个扇区
    if(inode_pos.two_sector)
    {
        // 读写硬盘是以扇区为单位, 若写入的数据小于一扇区, 要将原硬盘上的内容先读出来
        // 再和新数据拼成一扇区后再写入
        // inode在format中写入硬盘时时连续写入的, 所以读入2块扇区
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 2);
        
        // 将待写入的inode拼入到这2个扇区中的相应位置
        memcpy((inode_buf + inode_pos.offset_size), &pure_inode, sizeof(struct inode));
        
        // 将拼接好的数据再写入磁盘
        ide_write(part->my_disk, inode_pos.sector_lba, inode_buf, 2);
    }
    else    // 若只是一个扇区
    {
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.offset_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sector_lba, inode_buf, 1);
    }
}


/* 根据i结点号返回相应的i结点 */
// inode是存储在磁盘上, 为减少频繁访问磁盘, 在内存中为各分区创建了inode队列, 即 part->open_inodes
// open_inodes为已打开的inode队列, 是inode的缓存, 以后每打开一个inode, 先在此缓存中查找
// 若未找到, 再从磁盘上加载该inode到此缓存中
struct inode *inode_open(struct partition *part, unsigned int inode_id)
{
    // 先在已打开inode链表中找inode, 此链表时为提速创建的缓冲区
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *inode_found;
    
    // 遍历查找内存中保存的"已打开的inode队列"
    while(elem != &part->open_inodes.tail)
    {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if(inode_found->inode_id == inode_id)
        {
            inode_found->inode_open_count++;
            return inode_found;
        }
        elem = elem->next;
    }
    
    // 由于open_inodes链表中找不到, 下面从硬盘上读入此inode并加入到此链表
    struct inode_position inode_pos;
    
    // inode位置信息会存入inode_pos, 包括inode所在扇区地址和扇区内的字节偏移量
    inode_locate(part, inode_id, &inode_pos);
    
    /* 为使通过sys_malloc创建的新inode被所有任务共享, 需要将inode置于内核空间
     * 故需要临时将current_pbc->pgdir置为NULL */
    struct task_struct *current = running_thread();
    unsigned int *current_pagedir_bak = current->pgdir;
    current->pgdir = NULL;
    // 此时分配的内存将位于内核区
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    // 恢复pgdir
    current->pgdir = current_pagedir_bak;
    
    char *inode_buf;
    if(inode_pos.two_sector)    // 考虑跨扇区的情况
    {
        inode_buf = (char *)sys_malloc(1024);
        // i结点表时被partition_format函数连续写入扇区的, 所以可以连续读出来
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 2);
    }
    else    // 否则, 所查找的inode未跨扇区, 一个扇区大小的缓冲区足够
    {
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.offset_size, sizeof(struct inode));
    
    // 因为一会很可能要用到此inode, 故将其插入到队首便于提前检索到
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->inode_open_count = 1;
    
    sys_free(inode_buf);
    return inode_found;
}


/* 关闭inode或减少inode的打开数 */
void inode_close(struct inode *inode)
{
    enum intr_status old_status = intr_disable();
    
    // 若没有进程再打开此文件, 将此inode去掉并释放空间
    if(--inode->inode_open_count == 0)
    {
        list_remove(&inode->inode_tag);     // 将i结点从part->open_inodes中去掉
        // inode_open时为实现inode被所有进程共享, 已经在sys_malloc为inode分配了内核空间
        // 释放inode时也要确保释放的是内核内存池
        struct task_struct *current = running_thread();
        unsigned int *current_pagedir_bak = current->pgdir;
        current->pgdir = NULL;
        sys_free(inode);    // 这样释放的便是内核空间内存
        
        current->pgdir = current_pagedir_bak;
    }
    intr_set_status(old_status);
}


/* 初始化new_inode */
// 创建inode时不为其分配扇区, 真正写文件时才分配扇区
void inode_init(unsigned int inode_id, struct inode *new_inode)
{
    new_inode->inode_id = inode_id;
    new_inode->inode_size = 0;
    new_inode->inode_open_count = 0;
    new_inode->write_deny = false;
    
    // 初始化块索引数组inode_sector
    // inode中0~11 为直接块指针, 12 为一级间接块索引表指针
    unsigned char sector_index = 0;
    while(sector_index < 13)
    {
        // inode_blocks[12]为一级间接块地址
        new_inode->inode_blocks[sector_index] = 0;
        sector_index++;
    }
}














/* 将硬盘分区part上的inode清空
 * 把inode从inode_table中擦除, 也就是把inode_table中把该inode对应的空间清0 */
 
// 这个函数可有可无, 只是为了帮助调试和添加的, 并不是必须的功能
// inode的使用情况是由inode位图来控制的, 回收inode时只要在inode位图中的相应位置0即可
// 没必要在inode_table中真正擦除该inode, 就像删除文件时不需要真正把文件数据块中的数据擦除一样
static void inode_delete(struct partition* part, unsigned int inode_no, void* io_buf)
{
    ASSERT(inode_no < 4096);
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);     // inode位置信息会存入inode_pos
    ASSERT(inode_pos.sector_lba <= (part->start_lba + part->sector_count));

    char* inode_buf = (char*)io_buf;
    if (inode_pos.two_sector)   // inode跨扇区,读入2个扇区
    {
        /* 将原硬盘上的内容先读出来 */
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 2);
        /* 将inode_buf清0 */
        memset((inode_buf + inode_pos.offset_size), 0, sizeof(struct inode));
        /* 用清0的内存数据覆盖磁盘 */
        ide_write(part->my_disk, inode_pos.sector_lba, inode_buf, 2);
    }
    else    // 未跨扇区,只读入1个扇区就好
    {
        /* 将原硬盘上的内容先读出来 */
        ide_read(part->my_disk, inode_pos.sector_lba, inode_buf, 1);
        /* 将inode_buf清0 */
        memset((inode_buf + inode_pos.offset_size), 0, sizeof(struct inode));
        /* 用清0的内存数据覆盖磁盘 */
        ide_write(part->my_disk, inode_pos.sector_lba, inode_buf, 1);
    }
}





/* 回收inode的数据块和inode本身 */
void inode_release(struct partition* part, unsigned int inode_no)
{
    struct inode* inode_to_del = inode_open(part, inode_no);
    ASSERT(inode_to_del->inode_id == inode_no);

    /* 1 回收inode占用的所有块 */
    unsigned char block_idx = 0, block_cnt = 12;
    unsigned int block_bitmap_idx;
    unsigned int all_blocks[140] = {0};	  //12个直接块+128个间接块

    /* a 先将前12个直接块存入all_blocks */
    while (block_idx < 12)
    {
        all_blocks[block_idx] = inode_to_del->inode_blocks[block_idx];
        block_idx++;
    }

    /* b 如果一级间接块表存在,将其128个间接块读到all_blocks[12~], 并释放一级间接块表所占的扇区 */
    if (inode_to_del->inode_blocks[12] != 0)
    {
        ide_read(part->my_disk, inode_to_del->inode_blocks[12], all_blocks + 12, 1);
        block_cnt = 140;

        /* 回收一级间接块表占用的扇区 */
        block_bitmap_idx = inode_to_del->inode_blocks[12] - part->sbk->data_start_lba;
        ASSERT(block_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
        bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    /* c inode所有的块地址已经收集到all_blocks中,下面逐个回收 */
    // 如果该inode是普通文件, 不需要遍历140个块, 因为文件的数据是一个整体, 存储数据时是挨个、连续存放在块中的
    // 因此while循环中可以把块地址为0作为结束条件
    // 但如果该inode是目录, 目录中的数据是目录项, 每一个目录项都可以单独操作
    block_idx = 0;
    while (block_idx < block_cnt)
    {
        if (all_blocks[block_idx] != 0)
        {
            block_bitmap_idx = 0;
            block_bitmap_idx = all_blocks[block_idx] - part->sbk->data_start_lba;
            ASSERT(block_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);
        }
        block_idx++; 
    }

    /*2 回收该inode所占用的inode */
    bitmap_set(&part->inode_bitmap, inode_no, 0);  
    bitmap_sync(current_part, inode_no, INODE_BITMAP);  // 将内存中的位图同步到硬盘

    /******     以下inode_delete是调试用的    ******
    * 此函数会在inode_table中将此inode清0,
    * 但实际上是不需要的,inode分配是由inode位图控制的,
    * 硬盘上的数据不需要清0,可以直接覆盖*/
    void* io_buf = sys_malloc(1024);
    
    inode_delete(part, inode_no, io_buf);
    
    sys_free(io_buf);
    /***********************************************/

    inode_close(inode_to_del);
}

