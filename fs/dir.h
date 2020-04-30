#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "fs.h"
#include "inode.h"

#define MAX_FILE_NAME_LEN   16  // 文件名的最大长度

/* 目录结构 */
// 并不存在磁盘上, 只用于与目录相关的操作时, 在内存中创建, 用过之后就释放
struct dir{
    struct inode *inode;        // 指向inode指针, 该inode是在"已打开的inode队列"
    unsigned int dir_pos;       // 在目录内的偏移, 用于遍历目录的操作
    unsigned char dir_buf[512]; // 目录的数据缓存, 如读取目录时存储返回的目录项
};

/* 目录项结构 */
// 连结文件名与inode的纽带
struct dir_entry{
    char filename[MAX_FILE_NAME_LEN]; // 普通文件或目录名
    unsigned int inode_id;          // 普通文件或目录对应的inode编号
    enum file_types file_type;         // 文件类型
};

extern struct dir root_dir;    // 分区的根目录


/* 打开根目录 */
void open_root_dir(struct partition *part);

/* 在分区part上打开i节点为inode_id的目录并返回目录指针 */
struct dir *dir_open(struct partition *part, unsigned int inode_id);

/* 关闭目录 */
// 关闭目录的inode并释放目录占用的内存
void dir_close(struct dir* dir);


/* 在part分区内的pdir目录内寻找名为name的文件或目录, 
 * 找到后返回true, 并将其目录项存入dir_e, 否则返回false */
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, \
            struct dir_entry *dir_e);


/* 将目录项p_de写入父目录parent_dir中,io_buf由主调函数提供 */
// 父目录 parent_dir, m目录项 p_de
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf);


/* 在内存中初始化目录项p_de */
void create_dir_entry(char* filename, unsigned int inode_no, unsigned char file_type, struct dir_entry* p_de);


/* 把分区part目录pdir中编号为inode_no的目录项删除 */
bool delete_dir_entry(struct partition* part, struct dir* pdir, unsigned int inode_no, void* io_buf);            


/* 读取目录,成功返回1个目录项,失败返回NULL */
struct dir_entry* dir_read(struct dir* dir);


/* 判断目录是否为空 */
bool dir_is_empty(struct dir* dir);

/* 在父目录parent_dir中删除child_dir */
signed int dir_remove(struct dir* parent_dir, struct dir* child_dir);
            
#endif