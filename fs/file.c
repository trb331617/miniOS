#include "file.h"
#include "fs.h"             // FT_REGULAR current_part
#include "inode.h"          // struct inode
// #include "dir.h"

#include "ide.h"            // struct partition

#include "thread.h"         // running_thread
#include "interrupt.h"      // enum intr_status
#include "string.h"         // memset

#include "stdio_kernel.h"   // printk

#include "global.h"         // NULL

#include "debug.h"          // ASSERT

#define DEFAULT_SECTORS     1

/* 文件表, 即文件结构数组 */
// 一个文件可以被多次打开, 甚至把file_table占满
struct file file_table[MAX_FILE_OPEN];

/* 从文件表file_table中获取一个空闲位, 成功返回下标, 失败返回-1 */
signed int get_free_slot_in_global_filetable(void)
{
    unsigned int fd_index = 3;
    while(fd_index < MAX_FILE_OPEN)
    {
        if(file_table[fd_index].fd_inode == NULL)
        { break; }
        fd_index++;
    }
    if(fd_index == MAX_FILE_OPEN)
    {
        printk("ERROR: exceed max open files\n");
        return -1;
    }
    return fd_index;
}


/* 将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中
 * 成功返回下标, 失败返回-1 */
signed int pcb_fd_install(signed int global_fd_index)
{
    struct task_struct *current = running_thread();
    unsigned char local_fd_index = 3;   // 跨过 stdin stdout stderr
    while(local_fd_index < MAX_FILES_OPEN_PER_PROC)
    {
        if(current->fd_table[local_fd_index] == -1)
        {
            current->fd_table[local_fd_index] = global_fd_index;
            break;
        }
        local_fd_index++;
    }
    if(local_fd_index == MAX_FILES_OPEN_PER_PROC)
    {
        printk("ERROR: exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_index;
}


/* 分配一个i结点, 返回i结点号 */
signed int inode_bitmap_alloc(struct partition *part)
{
    signed int bit_index = bitmap_scan(&part->inode_bitmap, 1);
    if(bit_index == -1)
    { return -1; }
    bitmap_set(&part->inode_bitmap, bit_index, 1);
    return bit_index;
}


/* 分配一个扇区, 返回其扇区地址 */
signed int block_bitmap_alloc(struct partition *part)
{
    signed int bit_index = bitmap_scan(&part->block_bitmap, 1);
    if(bit_index == -1)
    { return -1; }
    bitmap_set(&part->block_bitmap, bit_index, 1);
    // 和 inode_bitmap_alloc 不同, 此处返回的不是位图索引, 而是具体可用的扇区地址
    // data_start_lba 数据区开始的第一个扇区号
    return (part->sbk->data_start_lba + bit_index);
}


/* 将内存中bitmap第bit_index位所在的512字节同步到硬盘 */
void bitmap_sync(struct partition *part, unsigned int bit_index, unsigned char bitmap_type)
{
    // 本inode结点索引相对于位图的扇区偏移量
    // 相对于位图的以扇区为单位的偏移量
    unsigned int offset_sector = bit_index / 4096;  
    // 本i结点索引相对于位图的字节偏移量
    unsigned int offset_size = offset_sector * BLOCK_SIZE; 
    unsigned int sector_lba;
    unsigned char *bitmap_offset;
    
    // 需要被同步到硬盘的位图只有 inode_bitmap 和 block_bitmap
    switch(bitmap_type){
    case INODE_BITMAP:
        sector_lba = part->sbk->inode_bitmap_lba + offset_sector;
        bitmap_offset = part->inode_bitmap.bits + offset_size;
        break;
    case BLOCK_BITMAP:
        sector_lba = part->sbk->block_bitmap_lba + offset_sector;
        bitmap_offset = part->block_bitmap.bits + offset_size;
        break;
    }
    // 将位图写入硬盘
    ide_write(part->my_disk, sector_lba, bitmap_offset, 1);
}


/* 创建文件, 若成功则返回文件描述符, 否则返回-1 */
// 创建文件i结点 -> 文件描述符fd -> 目录项
signed int file_create(struct dir *parent_dir, char *filename, unsigned char flag)
{
    // 后续操作的公共资源
    // 考虑到有数据会跨扇区的情况, 故申请2个扇区大小的缓冲区
    void *io_buf = sys_malloc(1024);
    if(io_buf == NULL)
    {
        printk("ERROR: during file_create, sys_malloc for io_buf failed\n");
        return -1;
    }
    
    // 记录回滚步骤
    unsigned char rollback_step = 0;    // 用于操作失败时回滚各资源状态
    
    // 为新文件分配inode
    signed int inode_id = inode_bitmap_alloc(current_part);
    if(inode_id == -1)
    {
        printk("ERROR: during file_create: allocate inode failed\n");
        return -1;
    }
    
    // 此inode要从堆中申请内存, 不可生成局部变量(函数退出时会释放)
    // 因为file_table数组中的文件描述符的inode指针要指向它
    struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    if(new_file_inode == NULL)
    {
        printk("ERROR: during file_create, sys_malloc for inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_id, new_file_inode);   // 初始化i结点
    
    // 返回的是file_table数组的下标
    int fd_index = get_free_slot_in_global_filetable();
    if(fd_index == -1)
    {
        printk("ERROR: exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }
    
    file_table[fd_index].fd_inode = new_file_inode;
    file_table[fd_index].fd_pos = 0;
    file_table[fd_index].fd_flag = flag;
    file_table[fd_index].fd_inode->write_deny = false;
    
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    
    // create_dir_entry 只是内存操作, 不出意外不会返回失败
    create_dir_entry(filename, inode_id, FT_REGULAR, &new_dir_entry);
    
    /* 同步数据到硬盘 */
    
    // a) 在目录parent_dir下安装目录项new_dir_entry, 写入硬盘后返回true, 否则false
    if(!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    {
        printk("ERROR: sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }
    
    memset(io_buf, 0, 1024);
    // b) 将父目录i结点的内容同步到硬盘
    inode_sync(current_part, parent_dir->inode, io_buf);
    
    memset(io_buf, 0, 1024);
    // c) 将新创建文件的i结点内容同步到硬盘
    inode_sync(current_part, new_file_inode, io_buf);
    
    // d) 将inode_bitmap位图同步到硬盘
    bitmap_sync(current_part, inode_id, INODE_BITMAP);
    
    // e) 将创建的文件i结点添加到open_inodes链表
    list_push(&current_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->inode_open_count = 1;
    
    sys_free(io_buf);
    return pcb_fd_install(fd_index);
    
/* 创建文件需要创建相关的多个资源, 若某步失败则会执行到下面的回滚步骤 */
// 各case之间没有break, 他们是一种累加的回滚
rollback:
    switch(rollback_step){
    case 3:
        // 失败时, 将file_table中的相应位清空
        memset(&file_table[fd_index], 0, sizeof(struct file));
    case 2:
        sys_free(new_file_inode);
    case 1:
        // 如果新文件的i结点创建失败, 之前位图中分配的inode_id也要恢复
        bitmap_set(&current_part->inode_bitmap, inode_id, 0);
        break;
    }
    sys_free(io_buf);
    return -1;
}





/* 打开编号为 inode_id 的inode对应的文件 */
// 成功则返回文件描述符, 失败返回-1
signed int file_open(unsigned int inode_id, unsigned char flag)
{
    // 从文件表file_table中获取一个空闲位
    int fd_index = get_free_slot_in_global_filetable();
    if(fd_index == -1)
    {
        printk("ERROR: exceed max open files\n");
        return -1;
    }
    
    // 初始化
    file_table[fd_index].fd_inode = inode_open(current_part, inode_id);
    file_table[fd_index].fd_pos = 0;    // 每次打开文件, 要将fd_pos还原为0, 即让文件内的指针指向开头
    file_table[fd_index].fd_flag = flag;
    bool *write_deny = &file_table[fd_index].fd_inode->write_deny;
    
    // 只要是关于写文件, 判断是否有其他进程写此文件
    // 若是读文件, 不考虑 write_deny
    if(flag & O_WRONLY || flag & O_RDWR)    // 以写文件的方式打开(只写/读写)
    {
        // 进入临界区前先关中断
        enum intr_status old_status = intr_disable();
        // 若当前没有其他进程写该文件, 将其占用置为true, 避免多个进程同时写此文件
        if(!(*write_deny))
        {
            *write_deny = true;     // 置为true, 避免多个进程同时写此文件
            intr_set_status(old_status);    // 恢复中断
        }
        else    // 直接返回失败
        {
            intr_set_status(old_status);
            printk("ERROR: file cannot be write now, try again later\n");
            return -1;
        }
    }
    // 若是读文件或创建文件, 不用理会write_deny, 保持默认
    // O_RDONLY  O_CREAT
    // 将fd_index安装到fd_table并返回结果, 若成功返回文件描述符。否则返回-1
    return pcb_fd_install(fd_index);
}



/* 关闭文件 */
// 成功返回0, 失败返回-1
signed int file_close(struct file *file)
{
    if(file == NULL)
        return -1;
    
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;  // 使文件结构可用
    return 0;
}







/* 把buf中的count个字节写入file,成功则返回写入的字节数,失败则返回-1 */
signed int file_write(struct file* file, const void* buf, unsigned int count)
{
    // 本项目中为了简单, 一个块的大小即一个扇区, 文件最大尺寸为140个块
    // 文件目前最大只支持512*140=71680字节
    if((file->fd_inode->inode_size + count) > (BLOCK_SIZE * 140))   
    {   
        printk("exceed max file_size 71680 bytes(140 blocks), write file failed\n");
        return -1;
    }
    
    // 后面我们的磁盘操作都以1个扇区为单位
    unsigned char* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }
    
    // 写硬盘时为了方便获取块地址, 这里把文件所有的块地址收集到 all_blocks 中
    // 128个间接块+ 12个直接块
    unsigned int* all_blocks = (unsigned int*)sys_malloc(BLOCK_SIZE + 48);	  // 用来记录文件所有的块地址
    if (all_blocks == NULL)
    {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }

    const unsigned char* src = buf;	    // 用src指向buf中待写入的数据
    unsigned int size_left = count;	    // 用来记录未写入数据大小
    signed int block_lba = -1;	    // 块地址
    unsigned int block_bitmap_idx = 0;   // 用来记录block对应于block_bitmap中的索引,做为参数传给bitmap_sync

    signed int indirect_block_table;      // 用来获取一级间接表地址
    unsigned int block_idx;		      // 块索引

/* 文件中的数据是个整体，因此是顺序、连续写入块中, 并且是从最低块向高块开始写
 * 数据从前往后写, 在文件中也不会出现空洞、跨块的情况
 * 而目录中的目录项是单独的个体, 可以分散在不同的块中
 */

    /* 判断文件是否是第一次写,如果是,先为其分配一个块 */
    // 文件第一次写入数据时需要分配块地址。若未分配块地址, 块地址则为0, inode_init中初始化的
    if (file->fd_inode->inode_blocks[0] == 0)
    {
        block_lba = block_bitmap_alloc(current_part);   // 分配扇区
        if (block_lba == -1)
        {
            printk("file_write: block_bitmap_alloc failed\n");
            return -1;
        }
        file->fd_inode->inode_blocks[0] = block_lba;

        /* 每分配一个块就将位图同步到硬盘 */
        block_bitmap_idx = block_lba - current_part->sbk->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    // 是否需要为这count个字节分配新块, 也就是现有的扇区是否够用

    /* 写入count个字节前,该文件已经占用的块数 */
    unsigned int file_has_used_blocks = file->fd_inode->inode_size / BLOCK_SIZE + 1;

    /* 存储count字节后该文件将占用的块数 */
    unsigned int file_will_use_blocks = (file->fd_inode->inode_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    /* 通过此增量判断是否需要分配扇区,如增量为0,表示原扇区够用 */
    unsigned int add_blocks = file_will_use_blocks - file_has_used_blocks;

    /* 开始将文件所有块地址收集到all_blocks,(系统中块大小等于扇区大小)
     * 后面都统一在all_blocks中获取写入扇区地址 */
    if (add_blocks == 0) 
    { 
        /* 在同一扇区内写入数据,不涉及到分配新扇区 */
        if (file_has_used_blocks <= 12 )            // 文件数据量将在12个直接块之内
        {
            block_idx = file_has_used_blocks - 1;  // 指向最后一个已有数据的扇区
            all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx];
        }
        else
        { 
            /* 未写入新数据之前已经占用了间接块,需要将间接块地址读进来 */
            ASSERT(file->fd_inode->inode_blocks[12] != 0);
            indirect_block_table = file->fd_inode->inode_blocks[12];
            ide_read(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    } 
    else    // 需要分配新数据块
    {
        /* 若有增量,便涉及到分配新扇区及是否分配一级间接块表,下面要分三种情况处理 */
        
        /* 第一种情况:12个直接块够用
         *     直接分配所需的块并把块地址写入 inode_blocks 数组中 */
        if (file_will_use_blocks <= 12 )
        {
            /* 先将有剩余空间的可继续用的(剩余的)块地址写入all_blocks */
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->inode_blocks[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx];

            /* 再将未来要用的扇区分配好后写入all_blocks */
            block_idx = file_has_used_blocks;      // 指向第一个要分配的新扇区
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(current_part);
                if (block_lba == -1)
                {
                   printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                   return -1;
                }

                /* 写文件时,不应该存在块未使用但已经分配扇区的情况,当文件删除时,就会把块地址清0 */
                ASSERT(file->fd_inode->inode_blocks[block_idx] == 0);     // 确保尚未分配扇区地址
                file->fd_inode->inode_blocks[block_idx] = all_blocks[block_idx] = block_lba;

                /* 每分配一个块就将位图同步到硬盘 */
                block_bitmap_idx = block_lba - current_part->sbk->data_start_lba;
                bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;   // 下一个分配的新扇区
            }
        }
        /* 第二种情况: 旧数据在12个直接块内,新数据将使用间接块
         *     分配所需的块并把块地址写入 inode_blocks 数组中, 
         *     还要创建一级间接块表并写入块地址 */
        else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12)
        { 
            /* 先将有剩余空间的可继续用的扇区地址收集到all_blocks */
            block_idx = file_has_used_blocks - 1;      // 指向旧数据所在的最后一个扇区
            all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx];

            /* 创建一级间接块表 */
            block_lba = block_bitmap_alloc(current_part);
            if (block_lba == -1)
            {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }

            ASSERT(file->fd_inode->inode_blocks[12] == 0);  // 确保一级间接块表未分配
            /* 分配一级间接块索引表 */
            indirect_block_table = file->fd_inode->inode_blocks[12] = block_lba;

            block_idx = file_has_used_blocks;	// 第一个未使用的块,即本文件最后一个已经使用的直接块的下一块
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(current_part);
                if (block_lba == -1)
                {
                   printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                   return -1;
                }

                if (block_idx < 12)     // 新创建的0~11块直接存入all_blocks数组
                { 
                    ASSERT(file->fd_inode->inode_blocks[block_idx] == 0);      // 确保尚未分配扇区地址
                    file->fd_inode->inode_blocks[block_idx] = all_blocks[block_idx] = block_lba;
                }
                else     // 间接块只写入到all_block数组中,待全部分配完成后一次性同步到硬盘
                { all_blocks[block_idx] = block_lba; }

                /* 每分配一个块就将位图同步到硬盘 */
                block_bitmap_idx = block_lba - current_part->sbk->data_start_lba;
                bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;   // 下一个新扇区
            }
     
            ide_write(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);      // 同步一级间接块表到硬盘
        }
        /* 第三种情况: 已使用的扇区数超过了12块, 新数据占据间接块
         *     在一级间接块表中创建间接块项, 间接块项就是在一级间接块表中记录间接块地址的条目 */
        else if (file_has_used_blocks > 12)
        {
            ASSERT(file->fd_inode->inode_blocks[12] != 0); // 已经具备了一级间接块表
            indirect_block_table = file->fd_inode->inode_blocks[12];	 // 获取一级间接表地址

            /* 已使用的间接块也将被读入all_blocks,无须单独收录 */
            ide_read(current_part->my_disk, indirect_block_table, all_blocks + 12, 1); // 获取所有间接块地址

            block_idx = file_has_used_blocks;	  // 第一个未使用的间接块,即已经使用的间接块的下一块
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(current_part);
                if (block_lba == -1)
                {
                   printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                   return -1;
                }
                all_blocks[block_idx++] = block_lba;

                /* 每分配一个块就将位图同步到硬盘 */
                block_bitmap_idx = block_lba - current_part->sbk->data_start_lba;
                bitmap_sync(current_part, block_bitmap_idx, BLOCK_BITMAP);
            }
            ide_write(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);   // 同步一级间接块表到硬盘
        } 
    }

    // 通常情况下, 该块中都有些数据, 要将该块中的数据读出来, 将新数据写入该块中空闲区域, 再将新老数据一同写入
    bool first_write_block = true;      // 含有剩余空间的扇区标识
   /* 块地址已经收集到all_blocks中,下面开始写数据 */
    file->fd_pos = file->fd_inode->inode_size - 1;   // 置fd_pos为文件大小-1,下面在写数据时随时更新
    
    unsigned int bytes_written = 0;	    // 用来记录已写入数据大小
    unsigned int sec_idx;	      // 用来索引扇区
    unsigned int sec_lba;	      // 扇区地址
    unsigned int sec_off_bytes;    // 扇区内字节偏移量
    unsigned int sec_left_bytes;   // 扇区内剩余字节量
    unsigned int chunk_size;	      // 每次写入硬盘的数据块大小
    
    while (bytes_written < count)      // 直到写完所有数据
    {
        memset(io_buf, 0, BLOCK_SIZE);  // 往文件数据块写入数据的缓冲区, 必须要保证干净
        sec_idx = file->fd_inode->inode_size / BLOCK_SIZE;  // 根据文件大小计算出的块索引
        sec_lba = all_blocks[sec_idx];                  // 得到块地址
        sec_off_bytes = file->fd_inode->inode_size % BLOCK_SIZE;    // 数据在最后一个块中的偏移量
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;            // 块中的可用字节空间

        /* 判断此次写入硬盘的数据大小
         * 剩余的数据不足一个块, 往最后一个块中写剩余数据时的情况 */
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block)  // 要读写的块是本次操作中的第一个块, 通常该块中都已存在数据
        {
            ide_read(current_part->my_disk, sec_lba, io_buf, 1);    // 先读出该块中的数据
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);    // 拼接数据
        ide_write(current_part->my_disk, sec_lba, io_buf, 1);   // 写入硬盘
        
        printk("file write at lba 0x%x\n", sec_lba);    //调试,完成后去掉

        src += chunk_size;   // 将指针推移到下个新数据
        file->fd_inode->inode_size += chunk_size;  // 更新文件大小
        file->fd_pos += chunk_size;   
        bytes_written += chunk_size;
        size_left -= chunk_size;
   }
   
   inode_sync(current_part, file->fd_inode, io_buf);    // 同步inode
   sys_free(all_blocks);
   sys_free(io_buf);
   return bytes_written;
}









/* 从文件file中读取count个字节写入buf, 返回读出的字节数,若到文件尾则返回-1 */
signed int file_read(struct file* file, void* buf, unsigned int count)
{
    unsigned char* buf_dst = (unsigned char*)buf;
    unsigned int size = count, size_left = size;

    /* 若要读取的字节数超过了文件可读的剩余量, 就用剩余量做为待读取的字节数 */
    if ((file->fd_pos + count) > file->fd_inode->inode_size)
    {
        size = file->fd_inode->inode_size - file->fd_pos;
        size_left = size;
        if (size == 0)	   // 若到文件尾则返回-1
        { return -1; }
    }

    unsigned char* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    { printk("file_read: sys_malloc for io_buf failed\n"); }

	// 用来记录文件所有的块地址
    unsigned int* all_blocks = (unsigned int*)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL)
    {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }

    unsigned int block_read_start_idx = file->fd_pos / BLOCK_SIZE;          // 数据所在块的起始地址
    unsigned int block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;   // 数据所在块的终止地址
    unsigned int read_blocks = block_read_start_idx - block_read_end_idx;   // 如增量为0,表示数据在同一扇区
    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

    signed int indirect_block_table;    // 用来获取一级间接表地址
    unsigned int block_idx;             // 获取待读的块地址 

    /* 以下开始构建all_blocks块地址数组,专门存储用到的块地址(本程序中块大小同扇区大小) */
    if (read_blocks == 0)      // 在同一扇区内读数据,不涉及到跨扇区读取
    {
        ASSERT(block_read_end_idx == block_read_start_idx);
        if (block_read_end_idx < 12 )   // 待读的数据在12个直接块之内
        {
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx];
        }
        else    // 若用到了一级间接块表,需要将表中间接块读进来
        {		
            indirect_block_table = file->fd_inode->inode_blocks[12];
            // 读取该表, 获取所有间接块地址
            ide_read(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else      // 若要读多个块
    {
        /* 第一种情况: 起始块和终止块属于直接块
         *     直接读入 inode_blocks 数组中即可 */
        if (block_read_end_idx < 12 )	  // 数据结束所在的块属于直接块
        {
            block_idx = block_read_start_idx; 
            while(block_idx <= block_read_end_idx)  
            {
                all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx]; 
                block_idx++;
            }
        } 
        /* 第二种情况: 待读入的数据跨越直接块和间接块两类
         *     除了要读入 inode_blocks 数组, 还要从一级间接块索引表中读取间接块地址 */
        else if(block_read_start_idx < 12 && block_read_end_idx >= 12) 
        {
            /* 先将直接块地址写入all_blocks */
            block_idx = block_read_start_idx;
            while (block_idx < 12)      // 收集直接块地址
            {
                all_blocks[block_idx] = file->fd_inode->inode_blocks[block_idx];
                block_idx++;
            }
            ASSERT(file->fd_inode->inode_blocks[12] != 0);	    // 确保已经分配了一级间接块表

            /* 再将间接块地址写入all_blocks */
            indirect_block_table = file->fd_inode->inode_blocks[12];
            // 将一级间接块表读进来写入到第13个块的位置之后
            ide_read(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } 
        /* 第三种情况: 数据在间接块中
         *     在一级间接块索引表中读取间接块 */
        else
        {	
            ASSERT(file->fd_inode->inode_blocks[12] != 0);	    // 确保已经分配了一级间接块表
            indirect_block_table = file->fd_inode->inode_blocks[12];    // 获取一级间接表地址
            // 将一级间接块表读进来写入到第13个块的位置之后
            ide_read(current_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } 
    }

    /* 用到的块地址已经收集到all_blocks中,下面开始读数据 */
    // 选出合适的操作数大小 chunk_size
    unsigned int sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    unsigned int bytes_read = 0;
    while (bytes_read < size)	      // 直到读完为止
    {
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes; // 待读入的数据大小

        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(current_part->my_disk, sec_lba, io_buf, 1);    // 每次读取一个扇区
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);// 拷贝

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }
    
    sys_free(all_blocks);
    sys_free(io_buf);    
    return bytes_read;
}



