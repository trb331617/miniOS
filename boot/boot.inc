; FILE: c05b_boot.inc
; TITLE: 
; DATE: 20200215

; 宏

;-------------	 loader和kernel   ----------
LOADER_BASE_ADDR    equ 0x900   ; 自定义loader被加载到物理内存位置
LOADER_START_SECTOR equ 0x02    ; 自定义loader位于硬盘的扇区号

KERNEL_BIN_BASE_ADDR equ 0x70000; 自定义kernel被加载到物理内存位置
KERNEL_START_SECTOR equ 0x09    ; 自定义kernel位于硬盘的扇区号
; KERNEL_IMAGE_BASE_ADDR  equ   0x1500 ; loader解析elf格式的kernel.bin后在内存中生成的内核映像基址
KERNEL_ENTRY_POINT equ 0xc0001500


;-------------	 页目录表基地址   ----------
PAGE_DIR_TABLE_POS  equ 0x10_0000 ; 自定义页目录表基地址，1MB  

;--------------   gdt描述符属性  -------------
DESC_G_4K   equ	  1_00000000000000000000000b    ; G位为1，4K粒度
DESC_D_32   equ	   1_0000000000000000000000b    ; D/B位
DESC_L	    equ	    0_000000000000000000000b	; L位为0。64位代码标记，此处标记为0便可。
DESC_AVL    equ	     0_00000000000000000000b	; AVL位为0。 cpu不用此位，暂置为0  
; 段界限的第2部分，第1部分在低4字节中，此处为1111B，与第1部分组成20个二进制1，即段界限为0xF_FFFF
DESC_LIMIT_CODE2  equ 1111_0000000000000000b    
DESC_LIMIT_DATA2  equ DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2  equ 0000_000000000000000b
DESC_P	    equ		  1_000000000000000b    ; P位，段存在

; 段描述符对应的内存段特权级    
DESC_DPL_0  equ		   00_0000000000000b    
DESC_DPL_1  equ		   01_0000000000000b
DESC_DPL_2  equ		   10_0000000000000b
DESC_DPL_3  equ		   11_0000000000000b
DESC_S_CODE equ		     1_000000000000b    ; 代码段的S位
DESC_S_DATA equ	  DESC_S_CODE
DESC_S_sys  equ		     0_000000000000b
DESC_TYPE_CODE  equ	      1000_00000000b	; type字段，代码段。x=1,c=0,r=0,a=0 代码段是可执行的,非依从的,不可读的,已访问位a清0.  
DESC_TYPE_DATA  equ	      0010_00000000b	;x=0,e=0,w=1,a=0 数据段是不可执行的,向上扩展的,可写的,已访问位a清0.

; 代码段高4字节，平坦模式段基址为0
DESC_CODE_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00

DESC_DATA_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00
DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0b

;--------------   选择子属性  ---------------
RPL0  equ   00b
RPL1  equ   01b
RPL2  equ   10b
RPL3  equ   11b
TI_GDT	 equ   000b
TI_LDT	 equ   100b


;----------------   页表相关属性    --------------
PG_P  equ   1b      ; present，存在位
PG_RW_R	 equ  00b   ; 只读
PG_RW_W	 equ  10b   ; 可写
PG_US_S	 equ  000b  ; US位为0，不能被特权级3的任务访问，只允许0 1 2特权级
PG_US_U	 equ  100b 