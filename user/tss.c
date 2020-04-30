#include "tss.h"
#include "print.h"
#include "string.h"

// TSS是由程序员提供，由CPU维护

struct tss{
    unsigned int backlink;
    unsigned int *esp0;
    unsigned int ss0;
    unsigned int *esp1;
    unsigned int ss1;
    unsigned int *esp2;
    unsigned int ss2;
    
    unsigned int cr3;
    unsigned int (*eip)(void);
    
    unsigned int eflags;
    unsigned int eax;
    unsigned int ecx;
    unsigned int edx;
    unsigned int ebx;
    unsigned int esp;
    unsigned int ebp;
    unsigned int esi;
    unsigned int edi;
    unsigned int es;
    unsigned int cs;
    unsigned int ss;
    unsigned int ds;
    unsigned int fs;
    unsigned int gs; 
    
    unsigned int ldt;
    unsigned int trace;
    unsigned int io_base;
};

static struct tss tss;

/* 更新TSS中esp0字段的值为pthread的0级栈(即线程PCB所在页的最顶端) 
 * 此栈地址是用户进程由用户态进入内核态时所用的栈 */
 
// 模仿Linux任务切换的方式：一个CPU上的所有任务共享同一个TSS，之后不断修改同一个TSS的内容
void update_tss_esp(struct task_struct *pthread)
{
    tss.esp0 = (unsigned int *)((unsigned int)pthread + PAGE_SIZE);
}



/* 创建GDT描述符
 * 按照段描述符的格式来拼数据 */
static struct gdt_desc make_gdt_desc(unsigned int *desc_addr, unsigned int limit, unsigned char attr_low, unsigned char attr_high)
{
    unsigned int desc_base = (unsigned int)desc_addr;
    struct gdt_desc desc;
    
    desc.limit_low_word = limit & 0x0000FFFF;
    desc.base_low_word = desc_base & 0x0000FFFF;
    desc.base_mid_byte = (desc_base & 0x00FF0000) >> 16;
    desc.attr_low_byte = (unsigned char)attr_low;
    desc.limit_high_attr_high = ((limit & 0x000F0000) >> 16) + (unsigned char)attr_high;
    desc.base_high_byte = desc_base >> 24;
    
    return desc;
}



/* 在GDT中创建TSS并重新加载GDT */
// 初始化TSS并将其安装到GDT中，
// 还在GDT中安装2个供用户进程使用的描述符：DPL为3的数据段、DPL为3的代码段
void tss_init()
{
    put_str("tss_init begin...");
    unsigned int tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);
    
    tss.ss0 = SELECTOR_K_STACK; // 将TSS的ss0字段赋值为0级栈段的选择子
    tss.io_base = tss_size;     // 将TSS的io_base字段置为TSS的大小, 表示此TSS中没有IO位图
                                // 当IO位图的偏移地址大于等于TSS大小减1时，就表示没有IO位图
    
    // 曾经在loader.asm中进入保护模式前，是用汇编直接生成GDT
    // 第0个段描述符不可用，第1个位代码段，第2个位数据段和栈，第3个为显存段
    // GDT段基址为0x900, 把TSS放到第4个位置，即0x900 + 0x20
    
    // FILE: boot/loader.asm 文件开头处
    unsigned int gdt_base = *((unsigned int *)0xc0000904);

    // GDT第4号描述符
    // 在GDT中添加DPL为0的TSS描述符
    // 本项目中把低端1MB空间的页表映射为同物理地址相同，并且把内核开始使用的第768个
    // 页表指向了同低端1MB空间相同的物理页
    // 因此，这里的0xc000_0920可以用0x920代替
    // *((struct gdt_desc *)0xc0000920)
    *((struct gdt_desc *)(gdt_base + 8*4)) = make_gdt_desc((unsigned int *)&tss, \
                                                    tss_size - 1, \
                                                    TSS_ATTR_LOW, \
                                                    TSS_ATTR_HIGH);
    
    // GDT第5号描述符
    // 在GDT中添加DPL为3的代码段描述符
    // *((struct gdt_desc *)0xc0000928)
    *((struct gdt_desc *)(gdt_base + 8*5)) = make_gdt_desc((unsigned int *)0, \
                                                    0xfffff, \
                                                    GDT_CODE_ATTR_LOW_DPL3, \
                                                    GDT_ATTR_HIGH);

    // GDT第6号描述符
    // 在GDT中添加DPL为3的数据段描述符 
    // *((struct gdt_desc *)0xc0000930)
    *((struct gdt_desc *)(gdt_base + 8*6)) = make_gdt_desc((unsigned int *)0, \
                                                    0xfffff, \
                                                    GDT_DATA_ATTR_LOW_DPL3, \
                                                    GDT_ATTR_HIGH);
    
    // GDT 16位的limit 和 32位的段基址，即lgdt 操作数
    // 不可一步到位转为64位
    unsigned long long int gdt_operand = ((8*7-1) | ((unsigned long long int)(0xc0000000 + gdt_base) << 16));  // 7个描述符大小

    asm volatile ("lgdt %0" : : "m" (gdt_operand));     // lgdt指令重新加载GDTR
    asm volatile ("ltr %w0" : : "r" (SELECTOR_TSS));    // ltr指令加载TR
    
    put_str(" tss_init and ltr done!\n");
}