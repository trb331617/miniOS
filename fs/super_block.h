#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

/* 本项目中为了简单, 数据块大小与扇区大小一致, 即1块等于1扇区 */
// 磁盘操作要以扇区为单位, 交给硬盘的数据必须是扇区大小的整数倍

/* 超级块 */
struct super_block{
    // 支持多文件系统的操作系统通过此标志来识别文件系统类型
    unsigned int magic;     // 用于标识文件系统类型
    
    // 分区
    unsigned int sector_count;          // 本分区总共的扇区数
    unsigned int inode_count;           // 本分区中inode数量
    unsigned int partition_lba_base;    // 本分区的起始lba地址
    
    // 块位图
    unsigned int block_bitmap_lba;      // 块位图本身起始扇区地址
    unsigned int block_bitmap_sectors;  // 扇区位图本身占用的扇区数量
    
    // i结点位图
    unsigned int inode_bitmap_lba;      // i结点位图起始扇区lba地址
    unsigned int inode_bitmap_sectors;  // i结点位图占用的扇区数量
    
    // i结点表
    unsigned int inode_table_lba;       // i结点表起始扇区lba地址
    unsigned int inode_table_sectors;   // i结点表占用的扇区数量

    // 
    unsigned int data_start_lba;        // 数据区开始的第一个扇区号
    unsigned int root_inode_id;         // 根目录所在的i结点号
    unsigned int directory_entry_size;  // 目录项大小
    
    unsigned char empty[460];           // 加上460字节, 凑满512字节1扇区大小
}__attribute__((packed));


#endif