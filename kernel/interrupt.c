// FILE: interrupt.c
// TITLE: 构建IDT，初始化8259A

//  根据kernel/core_interrupt.asm中的全局数组intr_entry_table构建中断描述符，再写入IDT

// DATE: 20200307
// USAGE: 

#include "stdint.h"
#include "print.h"

#include "interrupt.h"
#include "global.h"
#include "io.h"

// PIC, Programmable Interrupt Controller
// 这里用的可编程中断控制器是8259A
#define PIC_M_CTRL 0x20	       // 主片的控制端口是0x20
#define PIC_M_DATA 0x21	       // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0	       // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1	       // 从片的数据端口是0xa1

// 目前总共支持的中断数
#define IDT_DESC_CNT 0x81	 // 0 ~ 0x81


// 中断门描述符结构体
// 根据中断描述符定义的结构体，8字节
// 结构体中位置越偏下的成员，其地址越高
struct gate_desc {
   uint16_t    func_offset_low_word;
   uint16_t    selector;
   uint8_t     dcount;   //此项为双字计数字段，是门描述符中的第4字节。此项固定值，不用考虑
   uint8_t     attribute;
   uint16_t    func_offset_high_word;
};

static struct gate_desc idt[IDT_DESC_CNT];   // idt是中断描述符表,本质上就是个中断门描述符数组
extern void* intr_entry_table[IDT_DESC_CNT];	    // 声明引用定义在core_interrupt.asm中的中断处理函数入口数组
void* idt_table[IDT_DESC_CNT];  // 定义中断处理程序数组
                                // 在core_interrupt.asm中定义的intr_xx_entry只是中断处理程序的入口，最终调用的是这里的处理程序

char* intr_name[IDT_DESC_CNT];      // 用于保存异常的名字


extern unsigned int syscall_handler(void);
// 测试栈传递参数版本的系统调用
// extern unsigned int syscall_stack_handler(void);

/* 初始化可编程中断控制器8259A */
// 中断处理程序中, 如果中断源是来自从片 8259A, 在发送中断结束信号EOI的时候, 主片和从片都要发送。
// 即，中断处理程序需要向两片8259A发送EOI。
// 否则，将无法继续响应新的中断。
static void pic_init(void) 
{

   /* 初始化主片 */
   outb(PIC_M_CTRL, 0x11);   // ICW1: 边沿触发,级联8259, 需要ICW4.
   outb(PIC_M_DATA, 0x20);   // ICW2: 起始中断向量号为0x20,也就是IR[0-7] 为 0x20 ~ 0x27.
   outb(PIC_M_DATA, 0x04);   // ICW3: IR2接从片. 
   outb(PIC_M_DATA, 0x01);   // ICW4: 8086模式, 正常EOI

   /* 初始化从片 */
   outb(PIC_S_CTRL, 0x11);	// ICW1: 边沿触发,级联8259, 需要ICW4.
   outb(PIC_S_DATA, 0x28);	// ICW2: 起始中断向量号为0x28,也就是IR[8-15] 为 0x28 ~ 0x2F.
   outb(PIC_S_DATA, 0x02);	// ICW3: 设置从片连接到主片的IR2引脚
   outb(PIC_S_DATA, 0x01);	// ICW4: 8086模式, 正常EOI

    /* 打开主片上IR0, 只接受时钟产生的中断, 屏蔽其它外部中断 */
    // 时钟中断将会触发0x20号中断
    // outb(PIC_M_DATA, 0xfe);
    // outb(PIC_S_DATA, 0xff);
    
    // 测试键盘和环形缓冲区，只打开键盘和时钟中断，其它全部关闭
    // 为0表示不屏蔽中断
    // outb(PIC_M_DATA, 0xfc);     // 操作主片上的中断屏蔽寄存器
                                // 只打开键盘中断，即位1为0，其它位都为1, 值为0xfd
    // outb(PIC_S_DATA, 0xff);     // 操作从片上的中断屏蔽寄存器
                                // 屏蔽了从片上的所有中断，值为0xff
                                
    // IRQ2 用于级联从片，必须打开，否则无法响应从片上的中断
    // 主片 8259A 上打开的中断有 IRQ0 的时钟、IRQ1 的键盘和级联从片的 IRQ2
    // 其它全部关闭
    outb(PIC_M_DATA, 0xf8);
    
    // 从片8259A上打开 IRQ14 的硬盘，此引脚接收硬盘控制器的中断
    outb(PIC_S_DATA, 0xbf);

   put_str("   pic_init done.\n");
}


/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, void* function) 
{ 
   p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
   p_gdesc->selector = SELECTOR_K_CODE;
   p_gdesc->dcount = 0;
   p_gdesc->attribute = attr;
   p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}


/*初始化中断描述符表*/
static void idt_desc_init(void) 
{
   int i;
   for (i = 0; i < IDT_DESC_CNT; i++) {
      make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]); 
   }
   
   // 单独处理系统调用 0x80
   // 系统调用对应的中断门dpl为3, 中断处理程序为syscall_handler
   // 若dpl为0, 则在3级环境下执行int指令会产生GP异常
   make_idt_desc(&idt[0x80], IDT_DESC_ATTR_DPL3, syscall_handler);
   
   // 测试栈传递参数版本的系统调用
   // make_idt_desc(&idt[0x80], IDT_DESC_ATTR_DPL3, syscall_stack_handler);
   
   put_str("   idt_desc_init done.\n");
}


/* 通用的中断处理函数 */
static void general_intr_handler(uint8_t intr_id)
{
    // IRQ7和IRQ15会产生伪中断(spurious interrupt)，无需处理
    // 0x2f是从片8259A上的最后一个IRQ引脚，保留项
    if(intr_id == 0x27 || intr_id == 0x2f)
        return;
    
    // 将光标置为0，从屏幕左上角请出一片打印异常信息的区域，方便阅读
    set_cursor(0);
    int cursor_pos = 0;
    while(cursor_pos < 320)     // 一行80个字符，先清空4行内容
    {
        put_char(' ');
        cursor_pos++;
    }
    
    set_cursor(0);      // 重置光标为屏幕左上角
    put_str("!!!!!!!      excetion message begin  !!!!!!!!\n");
    set_cursor(88);     // 从第2行第8个字符开始打印
    
    // put_str("interrupt vector: 0x");
    // put_int(intr_id);
    // put_char('\n');
    put_str(intr_name[intr_id]);
    if(intr_id == 14)   // 若为pagefault，将缺失的地址打印出来并悬停
    {
        int page_fault_vaddr = 0;
        asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));   // cr2 是存放造成page_fault的地址
        
        put_str("\npage fault addr is ");
        put_int(page_fault_vaddr);
    }
    put_str("\n!!!!!!!      excetion message end    !!!!!!!!\n");
    
    // 处理器进入中断后，会自动把标志寄存器eflags中的IF位置0，即中断处理程序在关中断的情况下运行
    // 能进入中断处理程序就表示已经处在关中断情况下
    // 不会出现调度进程的情况，故下面的死循环不会再被中断
    while(1);
}


/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void)
{
    int i;
    for(i=0; i<IDT_DESC_CNT; i++)
    {
        // idt_table数组中的函数是进入中断后根据中断向量号调用的
        // 见kernel/core_interrupt.asm的call [idt_table + %1*4]
        idt_table[i] = general_intr_handler;    // 先统一默认为general_intr_handler
        intr_name[i] = "unknown";               // 先统一赋值为unknown
    }
    
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
    
}



/*完成有关中断的所有初始化工作*/
void idt_init() 
{
   put_str("idt_init begin...\n");
   idt_desc_init();	   // 初始化中断描述符表
   exception_init();   // 异常名称初始化并注册通用的中断处理函数
   pic_init();		   // 初始化8259A

   /* 加载idt */
   // idt地址左移16位，以防原地址高16位不是0而造成数据错误，这里将idt转成64位后再左移
   // 由于指针只能转换成相同大小的整型，所以先将其转换成32位，再64位
   uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
   
   // 内联汇编，lidt 把IDT的界限值16位、基地址32位加载到IDTR寄存器
   // lidt的操作数是从内存地址处获取， m 表示内存约束
   asm volatile("lidt %0" : : "m" (idt_operand));
   
   put_str("idt_init done!\n");
}





/* 在中断处理程序数组第vcetor_id个元素中，注册安装中断处理程序 */
void register_handler(unsigned char vector_id, void *function)
{
    // idt_table数组中的函数是进入中断后根据中断向量号调用的
    // 见kernel/core_interrupt.asm的call [idt_table + %1*4]
    idt_table[vector_id] = function;
}






/* 获取当前中断状态 */
enum intr_status intr_get_status()
{
    uint32_t eflags = 0;
    // 获取当前中断状态
    // 寄存器约束g 用于约束eflags可以放在内存中或寄存器中
    asm volatile("pushfl; popl %0" : "=g"(eflags));         
    // 0x0000_0200, eflags寄存器中的if位为1，IF位于eflags中第9位
    return (0x00000200 & eflags) ? INTR_ON : INTR_OFF;
}


/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status)
{
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 开中断，并返回开中断前的状态 */
enum intr_status intr_enable()
{
    enum intr_status old_status;
    if(INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        asm volatile("sti");        // 开中断，sti指令将IF位置1
        return old_status;
    }
}


/* 关中断，并返回关中断前的状态 */
enum intr_status intr_disable()
{
    enum intr_status old_status;
    if(INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;
        asm volatile("cli" : : : "memory");     // 关中断，cli指令将IF位置0
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        return old_status;
    }
}
