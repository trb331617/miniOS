; FILE: core_interrupt.asm
; TITLE: 定义了33个中断处理程序，并将中断处理程序的地址保存至全局数组table_intr_entry
;   全局数组table_intr_entry将提供给kernel/interrupt.c用于构建IDT

; 每个中断处理程序都一样，就是调用字符串打印函数put_str
; 中断向量0~19是处理器内部固定的异常类型, 20~31是Intel保留
; 所以可用的中断向量号最低是32，我们在设置8259A时会把IR0的中断向量号设置为32

; DATE: 20200307
; USAGE: 


; 这里是用宏VECTOR实现的中断入口，所有的中断入口程序几乎都一样，只是中断向量号不同
; 宏展开后，中断入口程序名为intr_%1_entry。其中，%1是中断向量号
; 入口程序用这个中断向量号作为idt_table中的索引，调用最终C语言版的中断处理程序



[bits 32]

; 有的中断会产生错误码，用来指明中断是在哪个段上发生的
; 4字节的错误码会在进入中断后，处理器在栈中压入eip之后压入
; 为了保持代码模板的通用性，这里分别做了处理
%define ERROR_CODE nop ; no operation, 不操作，什么都不干
    ; 若在相关的异常中cpu已经自动压入了错误码,为保持栈中格式统一,这里不做操作.
%define ZERO push 0		 ; 若在相关的异常中cpu没有压入错误码,为了统一栈中格式,就手工压入一个0

extern idt_table    ; idt_table是interrupt.c中注册的中断处理函数数组

SECTION .data

global intr_entry_table     ; 全局数组
intr_entry_table:

%macro VECTOR 2     ; 2个参数 %1 %2
; 本文件中调用该宏33次，在预处理之后，宏中的2个section将会各出现33次
SECTION .text
intr_%1_entry:		 ; 每个中断处理程序都要压入中断向量号,所以一个中断类型一个中断处理程序，自己知道自己的中断向量号是多少
   %2       ; 这里会展开为nop或push 0
   
   ; 在汇编文件中调用C程序一定会破坏当前寄存器环境，所以要先保护现场
   push ds
   push es
   push fs
   push gs
   pushad   ; push all double word register
            ; 依次压入eax ecx edx ebx esp ebp esi edi
      
    ; 由于我们在设置8259A时会设置为手动结束，所以中断处理程序中需手动向8259A发送中断结束标志
   ; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
   mov al, 0x20                   ; 中断结束命令EOI, End Of Interrupt
   out 0xa0, al                   ; 向从片发送
   out 0x20, al                   ; 向主片发送
   
   push %1 ; 不管idt_table中的处理函数是否需要参数，这里都一律压入中断向量号

    call [idt_table + %1*4]     ; 调用idt_table中的C语言中断处理函数
    jmp intr_exit

; 编译器会将属性相同的SECTION合并到同一个大的segment中
; 这里的.data将会和上面的紧凑地排在一起
SECTION .data
   dd    intr_%1_entry	 ; 存储各个中断入口程序的地址，形成intr_entry_table数组
%endmacro

SECTION .text
global intr_exit
intr_exit:
    add esp, 4  ; 跳过栈中压入的中断号
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4  ; 跳过栈中error_code
    iretd

; 
VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO 
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO 
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE 
VECTOR 0x0c,ZERO
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO 
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO 
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO 
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO 
VECTOR 0x20,ZERO    ; 时钟中断对应的入口
VECTOR 0x21, ZERO   ; 键盘中断对应的入口
VECTOR 0x22, ZERO   ; 级联用的
VECTOR 0x23, ZERO   ; 串口2对应的入口
VECTOR 0x24, ZERO   ; 串口1对应的入口
VECTOR 0x25, ZERO   ; 并口2对应的入口
VECTOR 0x26, ZERO   ; 软盘对应的入口
VECTOR 0x27, ZERO   ; 并口1对应的入口
VECTOR 0x28, ZERO   ; 实时时钟对应的入口
VECTOR 0x29, ZERO   ; 重定向
VECTOR 0x2a, ZERO   ; 保留
VECTOR 0x2b, ZERO   ; 保留
VECTOR 0x2c, ZERO   ; ps/2鼠标
VECTOR 0x2d, ZERO   ; fpu浮点单元异常
VECTOR 0x2e, ZERO   ; 硬盘
VECTOR 0x2f, ZERO   ; 保留




;;;;;;;;;;;;;;;;   0x80号中断   ;;;;;;;;;;;;;;;;
[bits 32]

extern syscall_table

SECTION .text
global syscall_handler
syscall_handler:
; step 1 保存上下文环境
    ; 为了复用intr_exit, 这里的前半部分和其它中断处理函数一样
    ; 其实这部分的push在这里并没有其它作用, 只是占位用
    push 0      ; 压入0, 使栈中格式统一
    
    push ds
    push es
    push fs
    push gs
    pushad  ; 依次压入EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    
    push 0x80   ; 这里压入0x80也是为了保持统一的栈格式, 只是占位用
    
; step 2 为系统调用子功能传入参数
    ; C调用约定, 最右边的参数先入栈
    ; C调用约定, 栈顶的4字节为函数的返回地址, 往上依次为参数
    ; 编译时，编译器会根据函数声明在栈中匹配出正确数量的参数
    push edx    ; 系统调用中第3个参数
    push ecx    ; 系统调用中第2个参数
    push ebx    ; 系统调用中第1个参数
    
; step 3 调用子功能处理函数
    call [syscall_table + eax*4]
    add esp, 12     ; 跨过上面的三个参数
                    ; 即, 调用者自己回收栈空间
    
; step 4 将返回值传给用户进程
    ; 当从内核态退出时，会从内核栈中恢复寄存器上下文
    ; 将返回值存入待当前内核栈中eax的位置，从内核返回时popad指令，用户便获得了系统调用函数的返回值
    ; 8=1+7, 1为push 0x80, 7为pushad
    mov [esp + 8*4], eax        ; 将eax中的返回值存入栈中的对应位置
    jmp intr_exit   ; intr_exit返回，恢复上下文
    




;;;;;;;;;;;;;;;;   0x80号中断 之栈传递参数版本  ;;;;;;;;;;;;;;;;
[bits 32]
SECTION .text
global syscall_stack_handler

syscall_stack_handler:
; 系统调用传入的参数在用户栈中，此时是内核栈

; step 1 保存上下文环境
    push 0
    push ds
    push es
    push fs
    push gs
    pushad  ; 依次压入这8个32位寄存器 eax ecx edx ebx esp ebp esi edi
    push 0x80
; step 2 从内核栈中获取cpu自动压入的用户栈指针esp的值
; 4对应push 0x80  4*12对应pushad 8个加上面4个  4对应push 0
; 4*3 中断发生后, 处理器由低特权级进入高特权级, 它会把ss3 esp3 eflags cs eip依次压入栈中
    mov ebx, [esp + 4 + 4*12 + 4 + 4*3]
; step 3 再把参数重新压入内核栈中, 此时ebx为用户栈指针
; 由于此处只压入了三个参数，所以目前系统调用最多支持3个参数
; lib/syscall.c中的约定先压入参数, 再压入子功能号
    push dword [ebx + 12]   ; 系统调用中第3个参数
    push dword [ebx + 8]    ; 系统调用中第2个参数
    push dword [ebx + 4]    ; 系统调用中第1个参数
    mov edx, [ebx]          ; 系统调用子功能号
; step 4 调用子功能处理函数
; 编译器会在栈中根据C函数声明匹配正确数量的参数
    call [syscall_table + edx*4]
    add esp, 12             ; 跨过上面的三个参数
; step 5 将call调用后的返回值存入当前内核栈中eax的位置
    mov [esp + 8*4], eax
    jmp intr_exit           ; intr_exit返回，恢复上下文
    