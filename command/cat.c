#include "stdio.h"

#include "syscall.h"
#include "string.h"

#define NULL ((void *)0)

int main(int argc, char **argv)
{
    if(argc > 2)
    {
        printf("cat: only support 1 argument.\neg: cat filename\n");
        exit(-2);
    }
    
    // 无参数时, 调用 read 系统调用从键盘获取数据
    if(argc == 1)
    {
        char buf[1024] = {0};
        // TODO: 待优化
        // BUG: 必须读取到3个字符后才退出
        read(0, buf, 1024);
        printf("%s", buf);
        exit(0);
    }
    
    
    char abs_path[512] = {0};   // 存储文件的绝对路径
    int buf_size = 1024;
    void *buf = malloc(buf_size);
    if(buf == NULL)
    {
        printf("cat: malloc memeory faild\n");
        return -1;
    }
    if(argv[1][0] != '/')
    {
        getcwd(abs_path, 512);
        strcat(abs_path, "/");
        strcat(abs_path, argv[1]);
    }
    else
    {
        strcpy(abs_path, argv[1]);
    }
    
    int fd = open(abs_path, O_RDONLY);
    if(fd == -1)
    {
        printf("cat: open %s failed\n", argv[1]);
        return -1;
    }
    
    int read_bytes = 0;
    while(1)    // 循环读取文件
    {
        read_bytes = read(fd, buf, buf_size);
        if(read_bytes == -1)    // 一直读到文件尾
            break;
        write(1, buf, read_bytes);
    }
    free(buf);
    close(fd);
    return 66;
}