# make自动化变量
# $@, aim at, 规则中目标文件名集合
# $<, 规则中依赖文件中的第1个文件
# $^, 规则中所有依赖文件的集合

BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500

AS = nasm
CC = gcc
LD = ld
LIB = -I lib/ -I kernel/ -I device/ -I thread/ -I user/ -I fs/ -I shell/

ASFLAGS = -f elf

# -fno-buildin 告诉编译器不要采用内部函数，因为咱们的实现中会自定义与内部函数同名的函数
# -Wstrict-prototypes 要求函数声明中必须有参数类型，否则编译时发出告警
# -Wmissing-prototypes 要求函数必须有声明，否则编译时发出告警
CFLAGS = -m32 -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes \
            -Wmissing-prototypes
LDFLAGS = -m elf_i386 -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map

# 注意，最好不要用%.o来匹配，这样不能保证链接顺序。链接时的目标文件，位置顺序上最好是调用在前，实现在后
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
       $(BUILD_DIR)/timer.o $(BUILD_DIR)/core_interrupt.o $(BUILD_DIR)/print.o \
       $(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
       $(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
       $(BUILD_DIR)/switch.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/console.o \
       $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o \
       $(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall-init.o \
       $(BUILD_DIR)/stdio.o $(BUILD_DIR)/stdio_kernel.o $(BUILD_DIR)/ide.o \
       $(BUILD_DIR)/fs.o $(BUILD_DIR)/inode.o $(BUILD_DIR)/file.o \
       $(BUILD_DIR)/dir.o $(BUILD_DIR)/fork.o $(BUILD_DIR)/shell.o \
       $(BUILD_DIR)/assert.o $(BUILD_DIR)/buildin_cmd.o $(BUILD_DIR)/exec.o \
       $(BUILD_DIR)/wait_exit.o $(BUILD_DIR)/pipe.o
       
##############     c代码编译     ###############
$(BUILD_DIR)/main.o: kernel/main.c lib/print.h lib/stdint.h \
                     kernel/init.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/print.h lib/stdint.h \
                     kernel/interrupt.h device/timer.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h lib/stdint.h \
                          kernel/global.h lib/io.h lib/print.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/stdint.h \
                      lib/io.h lib/print.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/debug.o: kernel/debug.c kernel/debug.h lib/print.h lib/stdint.h \
                      kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/string.o: lib/string.c lib/string.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/bitmap.o: lib/bitmap.c lib/bitmap.h lib/string.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h lib/bitmap.h lib/print.h \
                       lib/string.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c thread/thread.h kernel/memory.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/list.o: lib/list.c lib/list.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/sync.o: thread/sync.c thread/sync.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/console.o: device/console.c device/console.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/keyboard.o: device/keyboard.c device/keyboard.h    
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/ioqueue.o: device/ioqueue.c device/keyboard.h    
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/tss.o: user/tss.c user/tss.h    
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: user/process.c user/process.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/syscall.c lib/syscall.h    
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall-init.o: user/syscall-init.c user/syscall-init.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c lib/stdio.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio_kernel.o: lib/stdio_kernel.c lib/stdio_kernel.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/ide.o: device/ide.c device/ide.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/fs.o: fs/fs.c fs/fs.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/inode.o: fs/inode.c fs/inode.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/file.o: fs/file.c fs/file.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/dir.o: fs/dir.c fs/dir.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/fork.o: user/fork.c user/fork.h
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/shell.o: shell/shell.c
	$(CC) $(CFLAGS) $< -o $@ 
    
$(BUILD_DIR)/assert.o: lib/assert.c
	$(CC) $(CFLAGS) $< -o $@
    
$(BUILD_DIR)/buildin_cmd.o: shell/buildin_cmd.c
	$(CC) $(CFLAGS) $< -o $@ 
    
$(BUILD_DIR)/exec.o: user/exec.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/wait_exit.o: user/wait_exit.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/pipe.o: shell/pipe.c
	$(CC) $(CFLAGS) $< -o $@
    
    
##############    汇编代码编译    ###############
# mbr
$(BUILD_DIR)/mbr.bin: boot/mbr.asm
	$(AS) $< -o $@

$(BUILD_DIR)/loader.bin: boot/loader.asm
	$(AS) -I ./boot/ $< -o $@

$(BUILD_DIR)/core_interrupt.o: kernel/core_interrupt.asm
	$(AS) $(ASFLAGS) $< -o $@
    
$(BUILD_DIR)/print.o: lib/print.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: thread/switch.asm
	$(AS) $(ASFLAGS) $< -o $@
    
    
    
##############    链接所有目标文件    #############
$(BUILD_DIR)/kernel.bin: $(OBJS)    
	$(LD) $(LDFLAGS) $^ -o $@
    
.PHONY: mk_dir hd clean all

mk_dir:
	if [[ ! -d $(BUILD_DIR) ]]; then mkdir $(BUILD_DIR); fi
    
write_img:
	dd if=build/mbr.bin of=hd60M.img bs=512 count=1 conv=notrunc

	dd if=build/loader.bin of=hd60M.img bs=512 count=4 seek=2 conv=notrunc

	dd if=$(BUILD_DIR)/kernel.bin of=hd60M.img \
    bs=512 count=200 seek=9 conv=notrunc
    
clean:
	cd $(BUILD_DIR) && rm -f ./*
    
    
build: $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.bin

all: mk_dir build write_img
    