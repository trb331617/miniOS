
; switch_to的操作对象是线程栈struct thread_stack
; core_interrupt.asm interrupt.c timer.c thread.c 它们之间是密切配合的


[bits 32]

SECTION .text

; void switch_to(struct task_struct* current, struct task_struct* next);
; 参数：当前线程、下一个上处理器的线程，自动保存在栈中
; 此函数的功能是 保存current线程的寄存器映像，将下一个线程next的寄存器映像装载到处理器

; 任务调度
; 切换过程中需要保护好任务2层执行流的上下文
;   1) 进入中断时的保护。保存的是任务的全部寄存器映像，也就是进入中断前任务所属第一层的状态，
;      这些寄存器相当于任务中用户代码的上下文。
;      这部分是由kernel/core_interrupt.asm中定义的中断处理入口程序intr_%1_entry来保护的，里面是一些push寄存器的指令
;   2) 保护内核环境上下文。根据ABI，除esp外，只保护esi edi ebx ebp这4个寄存器就够了。这4个寄存器映像相当于任务中的内核代码的上下文，
;      也就是第2层执行流。此部分只负责恢复第二层的执行流，即恢复为在内核的中断出力程序中继续执行的状态。
;      当任务开始执行内核代码后，任务在内核代码中的执行路径由这4个寄存器决定，将来恢复这4个寄存器，也只是让处理器继续执行任务中内核代码，
;      并不是让任务恢复到中断前，依然还是在内核中。其实，这4个寄存器主要是用来恢复主调函数的环境
  

; 保护esi edi ebx ebp寄存器，并切换栈
global switch_to
switch_to:
    ; 栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp                ; 遵循ABI原则，保护好esi edi ebx ebp寄存器
                            ; 保存到current_thread的栈中
    
    mov eax, [esp + 5*4]    ; 得到栈中的参数current_thread
    mov [eax], esp          ; 保存栈顶指针esp，task_struct的self_kstack字段
                            ; self_kstack在task_struct中的偏移为0
    
    ; 以上是备份当前线程的环境，下面是恢复下一个线程的环境
    
    mov eax, [esp + 6*4]    ; 得到栈中的参数next_thread
                            ; 这里是next线程PCB中self_kstack的地址
    mov esp, [eax]          ; PCB第一个成员是self_kstack成员
                            ; 它用于记录0级栈顶指针，被换上CPU时用来恢复0级栈
                            ; 0级栈中保存了进程或线程所有信息，包括3级栈指针
                            
                            
    pop ebp                 ; 栈已切换为next_thread
    pop ebx
    pop edi
    pop esi
    
    ; 将当前栈顶处的值作为返回地址加载到处理器的eip寄存器中，使next线程的代码恢复执行
    
    ; 骚trick
    ; 利用ret的特性实现执行流的切换(内核线程的切换)
    ; 执行ret时，会自动从栈中弹出给eip
    ret     ; 返回到上面switch_to下面的那句注释的返回地址(自动保存在栈中的)
            ; 未由中断进入，第一次执行时会返回到kernel_thread
            
            
; 如果此时的next线程之前尚未执行过，
; 马上开始的是第一次执行，此时栈顶的值是函数kernel_thread的地址，这是由thread_create函数设置的，执行ret指令后
; 处理器将去执行函数kernel_thread


; 如果next之前已经执行过了，
; 这次是再次将其调度到处理器的话，此时栈顶的值是由调用函数swit_to的主调函数 schedule 留下的，这会继续执行schedule后面的流程。
; 而swit_to是schedule最后一句代码，因此执行流程马上回到schedule的调用者intr_timer_handler中。
; schedule同样也是intr_timer_handler中最后一句代码，因此会回到core_interrupt.asm中的jmp intr_exit，从而恢复任务的全部寄存器映像，
; 之后通过iretd指令退出中断，任务被完全彻底恢复
            