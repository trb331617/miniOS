#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "super_block.h"     // struct super_block

#include "sync.h"

/* 分区结构 */
struct partition{
    unsigned int start_lba;     // 分区的起始扇区
    unsigned int sector_count;  // 分区的扇区数
    struct disk *my_disk;       // 分区所属的硬盘, 一个硬盘有很多分区
    struct list_elem partition_tag;     // 本分区的标记, 将分区汇总到队列中
    char name[8];               // 分区名称, 如 sda1 sda2
    struct super_block *sbk;    // 本分区的超级块
    
    // 为减少对低速磁盘的访问次数, 文件系统通常以多个扇区来读写磁盘
    // 这多个扇区就是块
    struct bitmap block_bitmap; // 块位图, 用于管理本分区所有块
                                // 这里为了简单, 咱们的块大小为一个扇区
    struct bitmap inode_bitmap; // i结点位图
    struct list open_inodes;    // 本分区打开的i结点队列
}; 

/* 硬盘结构 */
struct disk{
    char name[8];               // 本硬盘的名称, 如sda sdb等
    struct ide_channel  *my_channel;    // 此块硬盘属于哪个ide通道, 一个通道上有2块硬盘
    unsigned char device_id;            // 本硬盘是主0 还是从1
    struct partition primary_parts[4];  // 本硬盘中主分区数量, 最多是4个
    struct partition logic_parts[8];    // 逻辑分区数量无限, 这里自定义上限为8
};

/* ata通道 */
// ide通道, 即ata通道
struct ide_channel{
    char name[8];               // 本ata通道名称, 如 ata0 或 ide0
    
    // 通道1(primary通道)的命令块寄存器端口范围是0x1f0~0x1f7, 控制块寄存器端口是0x3f6
    // 通道2(secondary通道)命令块寄存器端口范围是0x170~0x177, 控制块寄存器端口是0x376
    unsigned short port_base;   // 本通道的起始端口号
    
    // 在硬盘的中断处理程序中要根据中断号来判断在哪个通道中操作
    unsigned char irq_id;       // 本通道所用的中断号 interrupt request, irq
    struct lock lock;           // 通道锁, 实现本通道的互斥
    bool expecting_intr;        // 表示等待硬盘的中断
    
    // 驱动程序向硬盘发送命令后, 在等待硬盘工作期间可以通过此信号量阻塞自己, 避免浪费CPU
    // 等硬盘工作完成后, 会发出中断, 中断处理程序通过此信号量将硬盘驱动程序唤醒
    struct semaphore disk_done; // 用于阻塞、唤醒驱动程序
    struct disk devices[2];     // 一个通道上连接2个硬盘, 一主一从
};


void ide_init(void);
void intr_hd_handler(unsigned char irq_no);



/* 从硬盘hd的扇区地址lba处读取sec_cnt个扇区到buf */
void ide_read(struct disk* hd, unsigned int lba, void* buf, unsigned int sec_cnt);

/* 将buf中sec_cnt个扇区数据写入硬盘hd的扇区地址lba */
void ide_write(struct disk* hd, unsigned int lba, void* buf, unsigned int sec_cnt);


extern unsigned char channel_count;	 // 按硬盘数计算的通道数
extern struct ide_channel channels[2];	 // 有两个ide通道
// 所有分区的列表
extern struct list partition_list;

#endif