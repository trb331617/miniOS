#ifndef __FS_FS_H
#define __FS_FS_H



#define MAX_FILES_PER_PART  4096    // 每个分区所支持最大创建的文件数, 即inode数量
#define BITS_PER_SECTOR     4096    // 每扇区的位数
#define SECTOR_SIZE         512     // 扇区字节大小
#define BLOCK_SIZE  SECTOR_SIZE     // 块字节大小

#define MAX_PATH_LEN    512         // 路径最大长度

/* 文件类型 */
enum file_types{
    FT_UNKNOWN,     // 0, 不支持的文件类型
    FT_REGULAR,     // 1, 普通文件
    FT_DIRECTORY    // 2, 目录
};


/* 打开文件的选项 */
enum oflags{
    O_RDONLY,   // 只读
    O_WRONLY,   // 只写
    O_RDWR,     // 读写
    O_CREAT = 4     // 创建
};



/* 文件读写位置偏移量 */
enum whence{
    SEEK_SET = 1,   // lseek 中offset的参照物为文件开始处
    SEEK_CUR,       // lseek 中offset的参照物是当前读写位置
    SEEK_END        // lseek 中offset的参照物是文件尺寸大小, 即文件最后一个字节的下一个字节
};



/* 用来记录查找文件过程中已找到的上级路径, 也就是查找文件过程中"走过的地方" */
struct path_search_record{
    char searched_path[MAX_PATH_LEN];   // 查找过程中的父路径
    struct dir *parent_dir;             // 文件或目录所在的直接父目录
    enum file_types file_type;          // 找到的文件类型, 若找不到则为UNKNOWN
};


/* 文件属性结构 */
// 用于ls命令 sys_stat函数
struct stat{
    unsigned int st_inode_id;   // inode编号
    unsigned int st_size;       // 文件大小
    enum file_types st_type;    // 文件类型
};




// 默认情况下操作的是哪个分区
extern struct partition *current_part;


void filesys_init(void);

/* 打开或创建文件成功后,返回文件描述符,否则返回-1 */
signed int sys_open(const char* pathname, unsigned char flags);

/* 关闭文件描述符fd指向的文件, 成功返回0, 失败返回-1 */
signed int sys_close(signed int fd);


/* 将buf中连续count个字节写入文件描述符fd,
 * 成功则返回写入的字节数, 失败返回-1 */
signed int sys_write(signed int fd, const void *buf, unsigned int count);


/* 从文件描述符fd指向的文件中读取count个字节到buf,
 * 若成功则返回读出的字节数, 到文件尾则返回-1  */
signed int sys_read(signed int fd, void *buf, unsigned int count);


/* 重置用于文件读写操作的偏移指针,成功时返回新的偏移量,出错时返回-1 */
// 文件的读写位置是由文件结构中的 fd_pos 决定的, 因此sys_lseek的原理是将 whence+offset 转换为 fd_pos
signed int sys_lseek(signed int fd, signed int offset, unsigned char whence);


/* 删除文件(非目录),成功返回0,失败返回-1 */
signed int sys_unlink(const char* pathname);


/* 创建目录pathname,成功返回0,失败返回-1 */
signed int sys_mkdir(const char* pathname);

/* 目录打开成功后返回目录指针,失败返回NULL */
struct dir* sys_opendir(const char* name);

/* 成功关闭目录dir返回0,失败返回-1 */
signed int sys_closedir(struct dir* dir);


/* 读取目录dir的1个目录项,成功后返回其目录项地址,到目录尾时或出错时返回NULL */
struct dir_entry* sys_readdir(struct dir* dir);


/* 把目录dir的指针dir_pos置0 */
void sys_rewinddir(struct dir* dir);

/* 删除空目录,成功时返回0,失败时返回-1*/
signed int sys_rmdir(const char* pathname);


/* 把当前工作目录绝对路径写入buf, size是buf的大小. 
 * 当buf为NULL时,由操作系统分配存储工作路径的空间并返回地址
 * 失败则返回NULL */
char* sys_getcwd(char* buf, unsigned int size);


/* 更改当前工作目录为绝对路径path,成功则返回0,失败返回-1 */
// 任务的工作目录记录在PCB中 current_work_dir_inode_id, 因此更改目录的核心原理就是修改该值
signed int sys_chdir(const char* path);


/* 在buf中填充文件结构相关信息,成功时返回0,失败返回-1 */
// 待获取属性的文件路径
signed int sys_stat(const char* path, struct stat* buf);


/* 向屏幕输出一个字符 */
void sys_putchar(char char_asci);



/* 将最上层路径名称解析出来 */
// name_store 是主调函数提供的缓冲区, 用于存储最上层路径名
char *path_parse(char *pathname, char *name_store);


/* 将文件描述符转化为文件表的下标 */
unsigned int fd_local2global(unsigned int local_fd);


/* help, 显示系统支持的内部命令 */
void sys_help(void);


#endif