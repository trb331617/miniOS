
if [[ ! -d "../lib" || ! -d "../build" ]]
then
    echo "dependent dir don\'t exist!"
    cwd=$(pwd)
    cwd=${cwd##*/}
    cwd=${cwd%/}
    if [[ $cwd != "command" ]]
    then
        echo -e "you\'d better in command dir\n"
    fi
    exit
fi


BIN="cat"
CFLAGS="-m32 -Wall -c -fno-builtin -W -Wstrict-prototypes \
      -Wmissing-prototypes -Wsystem-headers"
LIBS="-I ../lib/ -I ../kernel -I ../user -I ../device \
     -I ../thread -I ../fs -I ../shell"
OBJS="../build/string.o ../build/syscall.o \
      ../build/stdio.o ../build/assert.o start.o"

DD_IN=$BIN
DD_OUT="../hd60M.img" 

nasm -f elf ./start.asm -o ./start.o

# ar 命令将 string.o syscall.o stdio.o assert.o start.o 打包成静态库文件 simple_crt.a
# simple_str.a 类似于CRT的作用
ar rcs simple_crt.a $OBJS start.o

gcc $CFLAGS $LIBS -o $BIN".o" $BIN".c"
# ld -m elf_i386 -e main $BIN".o" $OBJS -o $BIN
ld -m elf_i386 $BIN".o" simple_crt.a -o $BIN    # 默认 -e _start

SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')

if [[ -f $BIN ]];then
   dd if=./$DD_IN of=$DD_OUT bs=512 \
   count=$SEC_CNT seek=300 conv=notrunc
fi



##########   以上核心就是下面这五条命令   ##########
#nasm -f elf ./start.S -o ./start.o
#ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
#   ../build/stdio.o ../build/assert.o ./start.o
#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib/ -I ../lib/user -I ../fs prog_arg.c -o prog_arg.o
#ld prog_arg.o simple_crt.a -o prog_arg
#dd if=prog_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=11 seek=300 conv=notrunc






##########   以上核心就是下面这三条命令   ##########
# gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
  # -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c
  
# ld -e main prog_no_arg.o ../build/string.o ../build/syscall.o\
  # ../build/stdio.o ../build/assert.o -o prog_no_arg


#dd if=prog_no_arg of=/home/work/my_workspace/bochs/hd60M.img bs=512 count=10 seek=300 conv=notrunc
# dd if=hello.txt of=hd60M.img bs=512 count=1 seek=350 conv=notrunc