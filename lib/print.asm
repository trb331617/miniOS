; FILE: print.asm
; TITLE: 
; DATE: 20200229
; USAGE: 
;   nasm -f elf -o lib/print.o lib/print.asm


TI_GDT  equ 0
RPL0    equ 0
SELECTOR_VIDEO  equ (0x03<<3) + TI_GDT + RPL0


SECTION .data
buffer_put_int  dq 0    ; 定义8字节缓冲区用于存储转换后的字符


[bits 32]
SECTION .text

; ====================================================================== 
; Function: 打印以0字符结尾的字符串
; Input: 栈中参数为打印的字符串
global put_str
put_str:
    push ebx
    push ecx
    
    ; 临时关闭硬件中断
    ; 否则，字符串显示期间，随时会被中断而切换到另一个任务，将导致2个任务所显示的内容在屏幕上交替出现
    ; cli     ; 用cli指令清零EFLAGS寄存器的IF位
    
    mov ebx, [esp+12]       ; 从栈中获取待打印的字符串地址
                            ; 前面压入了2个寄存器2*4字节 + 函数返回地址4字节
    xor ecx, ecx            ; 清空
 .loop_show_string:
    mov cl, [ebx]
    or cl, cl
    jz .exit                ; 以0结尾
    
    push ecx        ; 为put_char传递参数
    call put_char
    add esp, 4      ; 回收参数所占的栈空间
    
    inc ebx
    jmp .loop_show_string
    
 .exit:
    ; 放开硬件中断
    ; sti 
 
    pop ecx
    pop ebx
    ret                    ; 段间调用返回
    

; ====================================================================== 
; Function: 把栈中的1个字符写入光标处
; Input: 
global put_char
put_char:

    ; 依次push EAX,ECX,EDX,EBX,ESP(初始值),EBP,ESI,EDI, 8个
    pushad
    
    mov ax, SELECTOR_VIDEO
    mov gs, ax
    
    ; 读取当前光标位置
    ; 索引寄存器端口0x3d4，其索引值14(0x0e)和15(0x0f)分别用于提供光标位置的高和低8位
    ; 数据端口0x3d5
    mov dx, 0x3d4   
    mov al, 0x0e   
    out dx, al
    mov dx, 0x3d5
    in al, dx
    mov ah, al
    
    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    in al, dx
    mov bx, ax      ; 此处用bx存放光标位置的16位数
    
    ; 因为访问显示缓冲区时用的是32位寻址方式，故必须使用EBX寄存器
    and ebx, 0x0000_ffff ; 清除EBX寄存器高16位
    
    ; 从栈中获取待打印的字符
    mov ecx, [esp+36]   ; pushad压入了4*8=32字节，主调函数的返回地址4字节  
    
    ; 判断是否为回车符0x0d
    cmp cl, 0x0d    ; 0x0d 为回车符CR
    jz .is_carriage_return
    ; 判断是否为回车符0x0a
    cmp cl, 0x0a    ; 0x0a 为换行符LF
    jz .is_line_feed
    ; 判断是否为退格符0x08
    cmp cl, 0x08    ; 0x08 为退格符BS (BackSpace)
    jz .is_backspace 
    
    jmp .show_normal

 ; 退格符BS, 删除一个字符，光标左移一位
 .is_backspace:
    dec bx
    shl bx, 1
    mov byte [gs:bx], 0x20  ; 0x20为空格
    inc bx
    mov byte [gs:bx], 0x07  ; 属性，黑底白字
    shr bx, 1
    jmp .set_cursor

 ; 正常显示字符
 ; 在写入其它内容之前，显存里全是黑底白字的空白字符0x0720，所以可以不重写黑底白字的属性
 .show_normal:
    shl bx, 1 ; 光标指示字符位置，显存中一个字符占2字节，光标位置乘2得到该字符在显存中得偏移地址    
    mov [gs:bx], cl ; 物理内存的低端1MB已被完整地映射到从0x8000_0000开始的高端。
    ; 显示缓冲区的线性基地址为0x800b_8000，会被处理器的页部件转换成物理地址0x000b_8000
    
    shr bx, 1       ; 恢复bx
    inc bx          ; 将光标推进到下一个位置
    
    cmp bx, 2000    ; 25行x80列, 判断是否需要向上滚动一行屏幕
    jl .set_cursor
    
 .is_line_feed:     ; 是换行符LF(\n)
 .is_carriage_return:   ; 是回车符CR(\r)
    ; 将光标移到行首
    xor dx, dx      ; dx 被除数高16位，清零
    mov ax, bx      ; ax 被除数低16位
    mov si, 80      ; 一行80个字符
    div si
    sub bx, dx      ; 光标值减去除80的余数
    
    ; 将光标移到下一行行首
    add bx, 80
    
    cmp bx, 2000    ; 25行x80列, 判断是否需要向上滚动一行屏幕
    jl .set_cursor
    
 ; 判断是否需要向上滚动一行屏幕
 .roll_screen:
    mov esi, 0xc00b_80a0
    mov edi, 0xc00b_8000
    mov ecx, 960    ; rep次数 
    cld             ; 传送方向cls std24行*每行80个字符*每个字符加显示属性占2字节 / movsd每次4字节
    rep movsd
    
    ; 清除屏幕最底一行，即写入黑底白字的空白字符0x0720
    mov bx, 3840    ; 24行*每行80个字符*每个字符加显示属性占2字节
    mov ecx, 80
 .cls:
    mov word [gs:ebx], 0x0720   ; 黑底白字的空格
    add bx, 2
    loop .cls
    
    mov bx, 1920    ; 重置光标位置为最底一行行首
 
 ; 根据bx重置光标位置
 ; 索引寄存器端口0x3d4，其索引值14(0x0e)和15(0x0f)分别用于提供光标位置的高和低8位
 ; 数据端口0x3d5
 .set_cursor:
    mov dx, 0x3d4   
    mov al, 0x0e   
    out dx, al
    mov dx, 0x3d5
    mov al, bh      ; in和out 只能用al或者ax
    out dx, al
    
    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    mov al, bl
    out dx, al    
    
    ; 依次pop EDI,ESI,EBP,EBX,EDX,ECX,EAX
    popad

    ret
    
    
    

; ====================================================================== 
; Function: 将小端字节序的数字变成对应的ascii后，倒置 
; Input: 栈中参数为待打印的数字
; Output: 在屏幕上打印16进制数字,并不会打印前缀0x
;       如打印10进制15时，只会直接打印f，不会是0xf

global put_int
put_int:
    pushad

    mov ebp, esp
    mov eax, [ebp+4*9]   ; call的返回地址占4字节+pushad的8个4字节
    mov edx, eax         ; 用edx来逐位处理             

    mov edi, 7   ; 指定在put_int_buffer中初始的偏移量
    mov ecx, 8	; 32位数字中,16进制数字的位数是8个
    mov ebx, buffer_put_int

; 将32位数字按照16进制的形式从低到高位逐个处理,共处理8个16进制数字
; 每4位二进制是16进制数字的1位,遍历每一位16进制数字
.16based_4bits:			       
   and edx, 0x0000_000f ; 解析16进制数字的每一位。and与操作后,edx只有低4位有效
   cmp edx, 9   ; 数字0～9和a~f需要分别处理成对应的字符
   jg .is_A2F 
   add edx, '0' ; ascii码是8位大小。add求和操作后,edx低8位有效。
   jmp .store
.is_A2F:
   sub edx, 10
   add edx, 'A'

;将每一位数字转换成对应的字符后,按照类似“大端”的顺序存储到缓冲区put_int_buffer
;高位字符放在低地址,低位字符要放在高地址,这样和大端字节序类似,只不过咱们这里是字符序.
.store:
   mov [ebx+edi], dl ; 此时dl为数字对应的字符ascii码		       
   dec edi
   shr eax, 4
   mov edx, eax 
   loop .16based_4bits

; 开始打印
;现在put_int_buffer中已全是字符,打印之前,
;把高位连续的字符去掉,比如把字符000123变成123
   inc edi  ; 此时edi退减为-1(0xffffffff),加1使其为0
.skip_prefix_0:  
   cmp edi,8 ; 若已经比较第9个字符了，表示待打印的字符串为全0 
   je .full0 
;找出连续的0字符, edi做为非0的最高位字符的偏移
.go_on_skip:   
   mov cl, [buffer_put_int+edi]
   inc edi
   cmp cl, '0' 
   je .skip_prefix_0		       ; 继续判断下一位字符是否为字符0(不是数字0)
   dec edi			       ;edi在上面的inc操作中指向了下一个字符,若当前字符不为'0',要恢复edi指向当前字符		       
   jmp .put_each_num

.full0:
   mov cl,'0'			       ; 输入的数字为全0时，则只打印0
.put_each_num:
   push ecx			       ; 此时cl中为可打印的字符
   call put_char
   add esp, 4
   inc edi			       ; 使edi指向下一个字符
   mov cl, [buffer_put_int+edi]	       ; 获取下一个字符到cl寄存器
   cmp edi,8
   jl .put_each_num
   popad
   ret
   
   
; ====================================================================== 
; Function: 设置光标位置
; Input: 栈中参数为待打印的数字
; Output: 在屏幕上打印16进制数字,并不会打印前缀0x
;       如打印10进制15时，只会直接打印f，不会是0xf
global set_cursor
set_cursor:
 ; 根据bx重置光标位置
 ; 索引寄存器端口0x3d4，其索引值14(0x0e)和15(0x0f)分别用于提供光标位置的高和低8位
 ; 数据端口0x3d5
    pushad
    mov bx, [esp+9*4]

    ; 先设置高8位
    mov dx, 0x3d4   
    mov al, 0x0e   
    out dx, al
    mov dx, 0x3d5
    mov al, bh      ; in和out 只能用al或者ax
    out dx, al
    
    ; 再设置低8位
    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    mov al, bl
    out dx, al    
    
    ; 依次pop EDI,ESI,EBP,EBX,EDX,ECX,EAX
    popad

    ret
    
    
    
   
   
; ====================================================================== 
; Function: 清屏
; Input: 
; Output: 
global cls_screen
cls_screen:
    pushad
    
; 由于用户程序的cpl为3, 显存段dpl为0
; 故用于显存段的选择子gs在低于自己特权的环境中为0
; 导致用户程序再次进入中断后, gs为0
; 故直接在put_str中每次都为gs赋值
    mov ax, SELECTOR_VIDEO
    mov gs, ax
    
    mov ebx, 0
    mov ecx, 80 * 25
    
 .cls:
    mov word [gs:ebx], 0x0720   ; 0x0720 是黑底白字的空格键
    add ebx, 2
    loop .cls
    
    mov ebx, 0
    
 .set_cursor:
; 根据bx重置光标位置
; 索引寄存器端口0x3d4，其索引值14(0x0e)和15(0x0f)分别用于提供光标位置的高和低8位
; 数据端口0x3d5

    ; 先设置高8位
    mov dx, 0x3d4   
    mov al, 0x0e   
    out dx, al
    mov dx, 0x3d5
    mov al, bh      ; in和out 只能用al或者ax
    out dx, al
    
    ; 再设置低8位
    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    mov al, bl
    out dx, al    
    
    ; 依次pop EDI,ESI,EBP,EBX,EDX,ECX,EAX
    popad

    ret