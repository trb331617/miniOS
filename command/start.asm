
[bits 32]
extern main
extern exit

SECTION .text
; 用户程序真正的第一个函数, 是程序真正入口
GLOBAL _start   ; _start 链接器默认的入口符号, 也可以 ld -e 指定入口符号
_start:
    ; 下面这2个要和execv和load之后指定的寄存器一致
    ; user/exec.c   sys_execv()
    push ebx    ; 压入 argv
    push ecx    ; 压入 argc
    call main
    
    ; ABI规定, 函数返回值是在eax寄存器中
    ; 将main的返回值通过栈传给exit, gcc用eax存储返回值, ABI规定
    push eax    ; 把main的返回值eax压栈
                ; 为下面调用exit系统调用压入的参数, 相当于exit(eax)
    call exit   ; exit 将不会返回
                ; 到这里用户进程即将结束, 后面是
                ; 内核回收其资源, 父进程获取子进程的返回值, 然后回收其PCB
    
    
; sys_execv 执行完成从 intr_exit 返回后, 此时的栈是用户栈
; 在sys_execv 中, 往0特权级核中哪个寄存器写入参数，此处就从哪个寄存器中获取参数
; 然后再压入用户栈为用户进程准备参数。