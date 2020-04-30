#include "exec.h"

#include "thread.h"     // struct intr_stack TASK_NAME_LEN
#include "fs.h"         // sys_close

#include "string.h"     // memcpy

// DEBUG
#include "stdio_kernel.h"

extern void intr_exit(void);

// 结构字段中的变量大小分别是4字节和2字节
typedef unsigned int Elf32_Word, Elf32_Addr, Elf32_Off;
typedef unsigned short int Elf32_Half;


// 文件头
/* 32位elf文件头 */
struct Elf32_Ehdr {
   unsigned char e_ident[16];
   Elf32_Half    e_type;
   Elf32_Half    e_machine;
   Elf32_Word    e_version;
   Elf32_Addr    e_entry;
   Elf32_Off     e_phoff;
   Elf32_Off     e_shoff;
   Elf32_Word    e_flags;
   Elf32_Half    e_ehsize;
   Elf32_Half    e_phentsize;
   Elf32_Half    e_phnum;
   Elf32_Half    e_shentsize;
   Elf32_Half    e_shnum;
   Elf32_Half    e_shstrndx;
};

// 程序头表, 即段头表
/* 程序头表Program header.就是段描述头 */
struct Elf32_Phdr {
   Elf32_Word p_type;		 // 见下面的enum segment_type
   Elf32_Off  p_offset;
   Elf32_Addr p_vaddr;
   Elf32_Addr p_paddr;
   Elf32_Word p_filesz;
   Elf32_Word p_memsz;
   Elf32_Word p_flags;
   Elf32_Word p_align;
};

// 可识别的段类型
// 这里我们只关注类型为 PT_LOAD 的段, 即可加载的段, 也就是程序本身的程序体
/* 段类型 */
enum segment_type {
   PT_NULL,            // 忽略
   PT_LOAD,            // 可加载程序段
   PT_DYNAMIC,         // 动态加载信息 
   PT_INTERP,          // 动态加载器名称
   PT_NOTE,            // 一些辅助信息
   PT_SHLIB,           // 保留
   PT_PHDR             // 程序头表
};


/* 将文件描述符 fd 指向的文件中, 偏移为offset, 大小为filesz的段加载到虚拟地址为vaddr的内存 */
// filesz 为段的大小, 程序头中的段大小为 p_filesz
// 程序是由多个段组成, 分别为每个可加载的段分配内存, 内存分配时采用页框粒度
static bool segment_load(signed int fd, unsigned int offset, unsigned int filesz, unsigned int vaddr)
{
    unsigned int vaddr_first_page = vaddr & 0xfffff000;     // vaddr 地址所在的页框
    unsigned int size_in_first_page = PAGE_SIZE - (vaddr & 0x00000fff); // 加载到内存后, 文件在第一个页框中占用的字节大小
    unsigned int occupy_pages = 0;
    
    // 第一个页框容不下该段
    if(filesz > size_in_first_page)
    {
        unsigned int left_size = filesz - size_in_first_page;
        occupy_pages = DIV_ROUND_UP(left_size, PAGE_SIZE) + 1;  // 1是指 vaddr_first_page
    }
    else
    {
        occupy_pages = 1;
    }
    
    // 为进程分配内存
    unsigned int page_index = 0;
    unsigned int vaddr_page = vaddr_first_page;
    while(page_index < occupy_pages)
    {
        unsigned int *pde = pde_ptr(vaddr_page);
        unsigned int *pte = pte_ptr(vaddr_page);
        
        // 如果pde不存在,或者pte不存在就分配内存.
        // pde的判断要在pte之前,否则pde若不存在会导致判断pte时缺页异常
        if(!(*pde & 0x00000001) || !(*pte & 0x00000001))
        {
            if(get_a_page(PF_USER, vaddr_page) == NULL)
                return false;
        }
        
        // 如果原进程的页表已经分配了, 利用现有的物理页, 直接覆盖进程体
        vaddr_page += PAGE_SIZE;
        page_index++;
    }
    
    sys_lseek(fd, offset, SEEK_SET);
    
    sys_read(fd, (void *)vaddr, filesz);
    
    return true;
}


/* 从文件系统上加载用户程序pathname, 
 * 成功则返回程序的起始地址, 否则返回-1 */
static signed int load(const char *pathname)
{
    signed int ret = -1;
    struct Elf32_Ehdr elf_header;
    struct Elf32_Phdr prog_header;
    memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));
       
    signed int fd = sys_open(pathname, O_RDONLY);
    if(fd == -1)
        return -1;
    if(sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr))
    {
        ret = -1;
        goto done;
    }
    
    // 校验elf头
    // 判断加载的文件是否是elf格式
    // elf格式的魔数, 文件类型 ET_EXEC 为2, 体系结构 EM_386 为3, 版本信息 为1
    // 程序头表中条目的数量, 即段的个数   程序头表中每个条目的字节大小
    if(memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) \
        || elf_header.e_type != 2 \
        || elf_header.e_machine != 3 \
        || elf_header.e_version != 1 \
        || elf_header.e_phnum > 1024 \
        || elf_header.e_phentsize != sizeof(struct Elf32_Phdr))
    {
        ret = -1;
        goto done;
    }
    
    Elf32_Off prog_header_offset = elf_header.e_phoff;      // 程序头的起始地址
    Elf32_Half prog_header_size = elf_header.e_phentsize;   // 程序头的条目大小
    
    // 遍历所有程序头
    unsigned int prog_index = 0;
    while(prog_index < elf_header.e_phnum)  // 段的数量
    {
        memset(&prog_header, 0, prog_header_size);
        
        // 将文件的指针定位到程序头
        sys_lseek(fd, prog_header_offset, SEEK_SET);
        
        // 只获取程序头
        if(sys_read(fd, &prog_header, prog_header_size) != prog_header_size)
        {
            ret = -1;
            goto done;
        }

        
        // 如果是可加载段就调用segment_load加载到内存
        if(PT_LOAD == prog_header.p_type)
        {
            // segment_load 为该段分配内存, 从文件系统中加载到内存
            if(!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr))
            {
                ret = -1;
                goto done;
            }
        }
        
        // 更新下一个程序头的偏移
        prog_header_offset += elf_header.e_phentsize;
        prog_index++;
    }
    // 处理完所有的段后, 将程序的入口赋值给ret
    ret = elf_header.e_entry;
    
done:
    sys_close(fd);
    return ret;
}

/* 用path指向的程序替换当前进程 */
signed int sys_execv(const char *path, const char *argv[])
{   
    unsigned int argc = 0;
    while(argv[argc])   // 统计出参数个数
        argc++;
   
    // 加载文件
    signed int entry_point = load(path);
    if(entry_point == -1)   // 若加载失败则返回-1
        return -1;
       
    struct task_struct *current = running_thread();

    
    // 修改进程名
    memcpy(current->name, path, TASK_NAME_LEN);
    current->name[TASK_NAME_LEN-1] = 0;
    
    // 内核栈
    // 接下来需要利用该栈从 intr_exit 返回
    struct intr_stack *intr_0_stack = (struct intr_stack *)((unsigned int)current + PAGE_SIZE - sizeof(struct intr_stack));
    // 参数传递给用户进程
    intr_0_stack->ebx = (signed int)argv;       // 参数数组argv的地址
    intr_0_stack->ecx = argc;                   // 参数个数
                                // 新进程从 intr_exit 返回后才是第一次运行, 因此运行之处通用寄存器中的值都是无效的
                                // 只有运行之后寄存器中的值才是有意义的
    intr_0_stack->eip = (void *)entry_point; // 将可执行文件的入口地址赋值给eip    
    
    intr_0_stack->esp = (void *)0xc0000000; // 使新用户进程的栈地址为最高用户空间地址
                                            // 老进程用户栈中的数据对新进程没用
                                            // 用户空间的最高处用于存储命令行参数
    
    // exec 不同于fork, 为使新进程更快被执行, 直接从中断返回
    // 将新进程内核栈地址赋值给esp, 跳转到 intr_exit, 假装从中断返回, 实现了新进程的运行
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g" (intr_0_stack) : "memory");
    
    return 0;   // exec使程序一去不回头, 这里根本没机会执行, 只是为了满足编译器语法要求
}




