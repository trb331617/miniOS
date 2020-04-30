; FILE: c05a_mbr.asm
; TITLE: 通过显存在显示器上打印字符
; DATE: 20200215
; USAGE: nasm -o mbr.bin c05a_mbr.asm
;        dd if=mbr.bin of=hd60M.img bs=512 count=1 conv=notrunc 

; 宏
LOADER_BASE_ADDR    equ 0x900   ; 自定义loader被加载到物理内存位置
LOADER_START_SECTOR equ 0x02    ; 自定义loader位于硬盘的扇区号

; 主引导程序
; ---------------------------------------------------------------
SECTION mbr vstart=0x7c00     ; mbr位于0x7c00
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00
    
    mov ax, 0xb800
    mov gs, ax
    
; 清屏。利用0x16号功能，上卷全部行，则可清屏
; ---------------------------------------------------------------
; 中断int 0x10, 功能号0x06，上卷窗口
; INPUT: ah 功能号0x06, al 上卷的行数(0为全部), bh 上卷行属性, 
;       (cl,ch) 窗口左上角(x,y)位置 (dl,dh) 窗口右下角(x,y)位置
; OUTPUT:
    mov ax, 0x0600
    mov bx, 0x0700
    mov cx, 0       ; 左上角(0,0)
    mov dx, 0x184f  ; 右下角(80,25) 0x18=24 0x4f=79
    
    int 0x10
    
; 打印字符串
; ---------------------------------------------------------------
    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xa4
    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xa4    
    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xa4
    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xa4
    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xa4

; 读取loader程序的起始部分(1个扇区)
; ---------------------------------------------------------------
    mov eax, LOADER_START_SECTOR    ; 起始逻辑扇区号，LBA28编址
    mov bx, LOADER_BASE_ADDR        ; 要写入的目标地址
    mov cx, 4   ; 要读入的扇区数。为避免再次修改，这里设为4个扇区              
    call read_hard_disk_0
    
    jmp LOADER_BASE_ADDR + 0x100    ; loader.bin文件头部偏移256字节


; ===============================================================================    
; Function: 读取硬盘的n个扇区
; Input: 1) eax 起始逻辑扇区号 2) bx 目标缓冲区地址 3) cx 扇区数
read_hard_disk_0:

    mov esi, eax 
    mov di, cx      ; 备份
    
    ; 1) 设置要读取的扇区数
    ; ==========================
    ; 向0x1f2端口写入要读取的扇区数。每读取一个扇区，数值会减1；
    ; 若读写过程中发生错误，该端口包含着尚未读取的扇区数
    mov dx, 0x1f2           ; 0x1f2为8位端口
    mov al, cl               ; 1个扇区
    out dx, al    

    mov eax, esi    ; 恢复ax
    
    ; 2) 设置起始扇区号
    ; ===========================
    ; 扇区的读写是连续的。这里采用早期的LBA28逻辑扇区编址方法，
    ; 28个比特表示逻辑扇区号，每个扇区512字节，所以LBA25可管理128G的硬盘
    ; 28位的扇区号分成4段，分别写入端口0x1f3 0x1f4 0x1f5 0x1f6，都是8位端口
    inc dx                  ; 0x1f3
    ; pop eax
    out dx, al              ; LBA地址7~0
    
    inc dx                  ; 0x1f4
    mov cl, 8
    shr eax, cl
    out dx, al              ; in和out 操作寄存器只能是al或者ax
                            ; LBA地址15~8
                            
    inc dx                  ; 0x1f5
    shr eax, cl
    out dx, al              ; LBA地址23~16

    ; 8bits端口0x1f6，低4位存放28位逻辑扇区号的24~27位;
    ; 第4位指示硬盘号，0为主盘，1为从盘；高3位，111表示LBA模式
    inc dx                  ; 0x1f6
    shr eax, cl             
    or al, 0xe0             ; al 高4位设为 1110
                            ; al 低4位设为 LBA的的高4位
    out dx, al    

    ; 3) 请求读硬盘
    ; ==========================
    ; 向端口0x1f7写入0x20，请求硬盘读
    inc dx                  ; 0x1f7
    mov al, 0x20
    out dx, al
    
 .wait:
    ; 4) 检测硬盘状态，等待硬盘读写操作完成
    ; ===========================
    ; 端口0x1f7既是命令端口，又是状态端口
    ; 通过这个端口发送读写命令之后，硬盘就忙乎开了。
    ; 0x1f7端口第7位，1为忙，0忙完了同时将第3位置1表示准备好了，
    ; 即0x08时，主机可以发送或接收数据
    nop     ; 空操作，只是为了增加延迟，相当于sleep了一小下，目的是减少打扰硬盘的工作
    in al, dx               ; 0x1f7, 同一个端口写时表示写入命令字，读时表示读入硬盘状态
    and al, 0x88            ; 取第7位和第3位
    cmp al, 0x08            
    jnz .wait
    
    ; 5) 连续取出数据
    ; ============================
    ; 0x1f0是硬盘接口的数据端口，16bits
    ; mov ecx, 256             ; loop循环次数，每次读取2bytes
    mov ax, di
    mov dx, 256
    mul dx
    mov cx, ax
    
    mov dx, 0x1f0           ; 0x1f0
 .readw:
    in ax, dx
    mov [bx], ax
    add bx, 2
    loop .readw

    ret



    
times 510-($-$$) db 0
db 0x55, 0xaa

