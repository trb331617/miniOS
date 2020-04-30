; FILE: c05b_loader.asm
; TITLE: 调用BIOS中断获取内存大小; 构建GDT、开启保护模式; 
;       构建页目录表和页表、开启分页机制;
; DATE: 20200215
; USAGE: nasm -I ./include/ -o loader.bin c05c_loader.asm
;        dd if=loader.bin of=hd60M.img bs=512 count=4 seek=2 conv=notrunc 

%include "boot.inc"

SECTION loader vstart=LOADER_BASE_ADDR


; total_mem_bytes用于保存内存容量,以字节为单位。
; 自定义loader.bin的加载地址是0x900,
; 故total_mem_bytes内存中的地址是0x900.将来在内核中咱们会引用此地址
mem_total_size  dd 0    ; 0x900

; FILE: user/tss.c中准备创建用户进程时，需要在GDT中添加: 
; DPL为0的TSS描述符, DPL为3的代码段描述符, DPL为3的数据段描述符
GDT_BASE        dd gdt_0    ; 0x904


; 调用BIOS中断0x15获取内存大小，会返回ARDS结构的数据
; 人工对齐:total_mem_bytes4字节+gdt_ptr6字节+ards_buf244字节+ards_nr2,共256字节
; ards_buf        times 250 db 0
ards_buf        times 246 db 0
ards_nr         dw 0    ; 0x用于记录ards结构体数量




loader_beginning:

; 调用BIOS中断0x15获取内存大小
; ---------------------------------------------------------------

; int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局
   xor ebx, ebx		      ;第一次调用时，ebx值要为0
   mov edx, 0x534d4150	      ;edx只赋值一次，循环体中不会改变
   mov di, ards_buf	      ;ards结构缓冲区
 .e820_mem_get_loop:	      ;循环获取每个ARDS内存范围描述结构
   mov eax, 0x0000e820	      ;执行int 0x15后,eax值变为0x534d4150,所以每次执行int前都要更新为子功能号。
   mov ecx, 20		      ;ARDS地址范围描述符结构大小是20字节
   int 0x15
   jc .e820_failed_so_try_e801   ;若cf位为1则有错误发生，尝试0xe801子功能
   add di, cx		      ;使di增加20字节指向缓冲区中新的ARDS结构位置
   inc word [ards_nr]	      ;记录ARDS数量
   cmp ebx, 0		      ;若ebx为0且cf不为1,这说明ards全部返回，当前已是最后一个
   jnz .e820_mem_get_loop

;在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量。
   mov cx, [ards_nr]	      ;遍历每一个ARDS结构体,循环次数是ARDS的数量
   mov ebx, ards_buf 
   xor edx, edx		      ;edx为最大的内存容量,在此先清0
 .find_max_mem_area:	      ;无须判断type是否为1,最大的内存块一定是可被使用
   mov eax, [ebx]	      ;base_add_low
   add eax, [ebx+8]	      ;length_low
   add ebx, 20		      ;指向缓冲区中下一个ARDS结构
   cmp edx, eax		      ;冒泡排序，找出最大,edx寄存器始终是最大的内存容量
   jge .next_ards
   mov edx, eax		      ;edx为总内存大小
 .next_ards:
   loop .find_max_mem_area
   jmp .mem_get_ok

;------  int 15h ax = E801h 获取内存大小,最大支持4G  ------
; 返回后, ax cx 值一样,以KB为单位,bx dx值一样,以64KB为单位
; 在ax和cx寄存器中为低16M,在bx和dx寄存器中为16MB到4G。
 .e820_failed_so_try_e801:
   mov ax,0xe801
   int 0x15
   jc .e801_failed_so_try88   ;若当前e801方法失败,就尝试0x88方法

;1 先算出低15M的内存,ax和cx中是以KB为单位的内存数量,将其转换为以byte为单位
   mov cx,0x400	     ;cx和ax值一样,cx用做乘数
   mul cx 
   shl edx,16
   and eax,0x0000FFFF
   or edx,eax
   add edx, 0x100000 ;ax只是15MB,故要加1MB
   mov esi,edx	     ;先把低15MB的内存容量存入esi寄存器备份

;2 再将16MB以上的内存转换为byte为单位,寄存器bx和dx中是以64KB为单位的内存数量
   xor eax,eax
   mov ax,bx		
   mov ecx, 0x10000	;0x10000十进制为64KB
   mul ecx		;32位乘法,默认的被乘数是eax,积为64位,高32位存入edx,低32位存入eax.
   add esi,eax		;由于此方法只能测出4G以内的内存,故32位eax足够了,edx肯定为0,只加eax便可
   mov edx,esi		;edx为总内存大小
   jmp .mem_get_ok

;-----------------  int 15h ah = 0x88 获取内存大小,只能获取64M之内  ----------
 .e801_failed_so_try88: 
   ;int 15后，ax存入的是以kb为单位的内存容量
   mov ah, 0x88
   int 0x15
   jc .error_hlt
   and eax, 0x0000FFFF
      
   ; 16位乘法，被乘数是ax,积为32位.积的高16位在dx中，积的低16位在ax中
   mov cx, 0x400        ; 0x400等于1024,将ax中的内存容量换为以byte为单位
   mul cx
   shl edx, 16          ; 把dx移到高16位
   or edx, eax          ; 把积的低16位组合到edx,为32位的积
   add edx, 0x100000    ;0x88子功能只会返回1MB以上的内存,故实际内存大小要加上1MB

 .mem_get_ok:
   mov [mem_total_size], edx	 ;将内存换为byte单位后存入total_mem_bytes处




    ; mov sp, LOADER_BASE_ADDR
; 打印字符串
; ---------------------------------------------------------------
; 中断int 0x10, 功能号0x13，打印字符串
; INPUT: ah 功能号0x13, al 写字符方式, 01光标跟随移动
;   es:bp 字符串地址, cx 串长度, 不包括结束符0的字符个数
;   bh 页号, bl 字符属性, 0x1f蓝底粉红字
;   (dh, dl) 坐标(行, 列)
    ; mov bp, msg_loader
    ; mov cx, 17
    ; mov ax, 0x1301 
    ; mov bx, 0x001f
    ; mov dx, 0x1800
    ; int 0x10

; 加载gdt
; lgdt指令的操作数是一个48位(6字节)的内存区域，低16位是gdt的界限值，高32位是gdt的基地址
; GDTR, 全局描述符表寄存器
    lgdt [gdt_size]

; 打开地址线A20
; 芯片ICH的处理器接口部分，有一个用于兼容老式设备的端口0x92，端口0x92的位1用于控制A20 
    in al, 0x92
    or al, 0000_0010B
    out 0x92, al

; 禁止中断
; 保护模式和实模式下的中断机制不同，在重新设置保护模式下的中断环境之前，必须关中断
    cli

; 开启保护模式
; CR0的第1位(位0)是保护模式允许位(Protection Enabel, PE)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

; 清空流水线、重新加载段选择器
    jmp dword sel_code:protcetmode_beginning
    
 .error_hlt:    ;出错则挂起
    hlt
    
    
[bits 32]
protcetmode_beginning:
    mov ax, sel_data
    mov ds, ax
    mov es, ax
    
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    
    mov ax, sel_video
    mov gs, ax
    
    ; 加载kernel，从硬盘读取到物理内存
    ; 这里为了简单，选择了在开启分页之前加载
    mov eax, KERNEL_START_SECTOR    ; kernel.bin在硬盘中的扇区号
    mov ebx, KERNEL_BIN_BASE_ADDR   ; 从磁盘读出后，写入到ebx指定的物理内存地址
    mov ecx, 200			        ; 读入的扇区数
    call read_hard_disk_0
    
    

    ; 创建页目录及页表
    call setup_page
    
    ; 将GDT中的段描述符映射到线性地址0xc000_0000，即内核全局空间
    ; 保存gdt描述符表地址及偏移量,一会用新地址重新加载
    sgdt [gdt_size]   ; 存储到原来gdt的位置
   ; 将gdt描述符中视频段描述符中的段基址+0xc000_0000，即3G
    mov ebx, [gdt_base]  
    or dword [ebx + 3*8 + 4], 0xc000_0000 ; 视频段是第3个段描述符,每个描述符是8字节,故0x18。
    ; 段描述符的高4字节的最高位是段基址的31~24位

    ; 把GDT也放到内核的地址空间
    ; 将gdt的基址加上0xc000_0000使其成为内核所在的高地址
    add dword [gdt_base], 0xc000_0000 ; 全局描述符表寄存器GDTR也用的是线性地址
    lgdt [gdt_size] ; 将修改后的GDT基地址和界限值加载到GDTR
    
    add esp, 0xc000_0000    ; 将栈指针同样映射到内核地址

    ; 令CR3寄存器指向页目录
    ; CR3寄存器的低12位除了用于控制高速缓存的PCD和PWT位，都没有使用
    mov eax, PAGE_DIR_TABLE_POS     ; 把页目录地址赋给控制寄存器cr3
    mov cr3, eax

    ; 开启分页机制
    ; 从此，段部件产生的地址就不再被看成物理地址，而是要送往页部件进行变换，以得到真正的物理地址
    mov eax, cr0
    or eax, 0x8000_0000     ; 打开cr0的pg位(第31位)，开启分页机制
    mov cr0, eax
    ; 这里切换至分页模式后，不需要重新加载各个段寄存器以刷新它们的描述符高速缓存器，因为所有这些段都是4GB的
    ; 所以，此处不刷新流水线也没问题
    ; jmp sel_code:enter_kernel
; enter_kernel:

    ; mov byte [gs:160], 'V'  ; 80*2=160, 第2行首字符位置；默认属性黑底白字
    ; jmp $

    ; 初始化内核
    call kernel_init
    
    mov esp, 0xc009f000     ; ???
    jmp KERNEL_ENTRY_POINT  ; 用地址0x1500访问测试，结果ok




    




; ===============================================================================    
; Function: 读取硬盘的n个扇区
; Input: 1) eax 起始逻辑扇区号 2) ebx 目标缓冲区地址 3) ecx 扇区数
; 相比mbr中的此函数，区别在于所用的寄存器变成了32位
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
; 至此,硬盘控制器便从指定的lba地址(eax)处,读出连续的cx个扇区
; 下面检查硬盘状态,不忙就能把这cx个扇区的数据读出来    
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
    mov [ebx], ax
    add ebx, 2
    loop .readw

    ret






; 创建页目录及页表
; ---------------------------------------------------------------
setup_page:
; 先清空页目录表所占的内存，由于内存中可能会有大量随机数据
    mov ecx, 4096   ; 1024项*4字节
    mov esi, 0
 .clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

;开始创建页目录项(PDE)Page Directory Entry
 .create_pde:
    mov eax, PAGE_DIR_TABLE_POS ; 页目录表PDT基地址    
    add eax, 0x1000     ; 4KB，此时eax为第一张页表的基地址
    mov ebx, eax        ; 为.create_pte做准备，ebx为基址

; 下面将页目录表第0和第0x300即768项都存为第一个页表的地址，
;  一个页表可表示4MB内存,这样0xc03fffff以下的地址和0x003fffff以下的地址都指向相同的页表，
; 这是为将地址映射为内核地址做准备
    or eax, PG_US_U | PG_RW_W | PG_P         ; 页目录项的属性RW和P位为1,US为1,表示用户属性,所有特权级别都可以访问
    mov [PAGE_DIR_TABLE_POS + 0x0], eax      ; 第1个目录项,在页目录表中的第1个目录项写入第一个页表的位置(0x101000)及属性(7)
    mov [PAGE_DIR_TABLE_POS + 0x300*4], eax  ; 一个页表项占用4字节,0xc00表示第768个页表占用的目录项,0xc00以上的目录项用于内核空间,
    ; 页表的0xc0000000~0xffffffff共计1G属于内核,0x0~0xbfffffff共计3G属于用户进程
    
    sub eax, 0x1000     ; 4KB，重新指向自定义的页目录基地址
    mov [PAGE_DIR_TABLE_POS + 1023*4], eax ; 使最后一个目录项指向页目录表自己的地址

; 下面创建页表项(PTE)Page Table Entry
; 本项目的mbr、loader、内核都放置在物理内存的低端1MB内
    mov ecx, 256    ; 1M低端内存 / 每页大小4k = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P ; 属性为7,US=1,RW=1,P=1
.create_pte:
    mov [ebx+esi*4], edx	; ebx已赋值为0x10_1000, 即自定义的第一个页表基地址 
    add edx, 4096   ; 4KB
    inc esi
    loop .create_pte

; 页目录中创建内核其它页表的PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000     ; 此时eax为第二个页表的位置
    or eax, PG_US_U | PG_RW_W | PG_P  ; 页目录项的属性US,RW和P位都为1
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254        ; 第769~1022的所有目录项数量
    mov esi, 769        
.create_kernel_pde:
    mov [ebx+esi*4], eax
    inc esi
    add eax, 0x1000      ; 页大小为4KB
    loop .create_kernel_pde
    ret






; 将ELF文件中的段segment拷贝到各段自己被编译的虚拟地址处，将这些段单独提取到内存中，这就是所谓的内存中的程序映像
; 分析程序中的每个段segment，如果段类型不是PT_NULL(空程序类型)，就将该段拷贝到编译的地址中
; ---------------------------------------------------------------
kernel_init:
    xor eax, eax
    xor ebx, ebx    ; ebx 记录程序头表地址
    xor ecx, ecx    ; cx 记录程序头表中的program header数量
    xor edx, edx    ; dx 记录program header尺寸, 即e_phentsize

    ; 遍历段时，每次增加一个段头的大小e_phentsize
    mov dx, [KERNEL_BIN_BASE_ADDR + 42] ; 偏移42字节处是属性e_phentsize, 表示program header大小
    ; 为了找到程序中所有的段，必须先获取程序头表(程序头program header的数组)
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28] ; 偏移28字节是e_phoff, 表示第1个program header在文件中的偏移量
	; 其实该值是0x34, 不过还是谨慎一点，这里来读取实际值
    add ebx, KERNEL_BIN_BASE_ADDR       ; 加上内核的加载地址，得程序头表的物理地址
    ; 程序头的数量e_phnum，即段的数量，
    mov cx, [KERNEL_BIN_BASE_ADDR + 44] ; 偏移44字节是e_phnum, 表示有几个program header
  
; 遍历段  
 .each_segment:
    cmp byte [ebx + 0], 0 ; 若p_type等于0, 说明此program header未使用
    je .PTNULL

    ; 为函数memcpy压入参数, 参数是从右往左依次压入
    ; 函数原型类似于 memcpy(dst, src, size)
    push dword [ebx + 16]   ; 压入memcpy的第3个参数size
                            ; program header中偏移16字节的地方是p_filesz
    mov eax, [ebx + 4]      ; program header中偏移4字节的位置是p_offset
    add eax, KERNEL_BIN_BASE_ADDR   ; 加上kernel.bin被加载到的物理地址, eax为该段的物理内存地址
    push eax                ; 压入memcpy的第2个参数src源地址
    push dword [ebx + 8]    ; 压入memcpy的第1个参数dst目的地址
                            ; program header中偏移8字节的位置是p_vaddr，这就是目的地址
                            
    call mem_cpy            ; 调用mem_cpy完成段复制
   
    add esp, 12				; 清理栈中压入的三个参数
   
 .PTNULL:
    add ebx, edx			; ebx指向下一个program header
                            ; dx为program header大小,  即e_phentsize
    loop .each_segment
    ret






; ===============================================================================    
; Function: 逐字节拷贝
; Input: 栈中三个参数(dst, src, size)，从右往左依次压入的
mem_cpy:		      
    push ebp
    mov ebp, esp
    push ecx		   ; rep指令用到了ecx，但ecx对于外层段的循环还有用，故先入栈备份
   
    mov edi, [ebp + 8]	   ; dst
    mov esi, [ebp + 12]	   ; src
    mov ecx, [ebp + 16]	   ; size，即rep次数
    cld             ; 传送方向为正向
    ; clean direction, 将eflags寄存器中的方向标志位DF置0
    rep movsb       ; 逐字节拷贝

    pop ecx		
    pop ebp
    ret





; 自定义栈顶，位于loader起始位置之下
LOADER_STACK_TOP    equ LOADER_BASE_ADDR

; 构建gdt及其描述符
; 0号段描述符
gdt_0:      dd 0x0000_0000
            dd 0x0000_0000
; 1号代码段
gdt_code:   dd 0x0000_ffff
            dd DESC_CODE_HIGH4
; 2号数据段和栈段
gdt_stack:  dd 0x0000_ffff
            dd DESC_DATA_HIGH4
; 3号显存段
; 基地址0xb_8000，段大小0xb_ffff-0xb_8000=0x7fff，粒度4KB
; 段界限0x7fff/4k=7
gdt_video:  dd 0x8000_0007  ; limit=(0xb_ffff-0xb_8000)/4k=0x7
            dd DESC_VIDEO_HIGH4     ; dpl为0
            
gdt_size    dw $-gdt_0-1
gdt_base    dd gdt_0


times 60 dq 0   ; 预留60个描述符的空位，每个描述符8字节
                ; dq, define quad-word，4字即8字节

sel_code    equ (0x0001<<3) + TI_GDT + RPL0
; 相当于(gdt_code-gdt_0)/8+TI_GDT+RPL0

sel_data    equ (0x0002<<3) + TI_GDT + RPL0
sel_video   equ (0x0003<<3) + TI_GDT + RPL0

msg_loader  db '2 loader in real.'



            