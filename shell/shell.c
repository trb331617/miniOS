#include "shell.h"

#include "syscall.h"        // read
#include "stdio.h"         // printf
#include "string.h"         // memset
#include "file.h"           // stdin_id

#include "buildin_cmd.h"    // make_clear_abs_path

// #include "debug.h"          // ASSERT
// #include "global.h"         // NULL
#include "assert.h"        // assert panic NULL

// #define cmd_len     128     // 最大支持键入128个字符的命令行输入
#define MAX_ARG_NR  16      // 加上命令外, 最多支持15个参数

/* 存储输入的命令 */
static char cmd_line[MAX_PATH_LEN] = {0};

// 用于洗路径时的缓存
// 本项目只支持单控制台, 因此并不会出现 final_path 被覆盖的情况
char final_path[MAX_PATH_LEN] = {0};

/* 记录当前目录, 是当前目录的缓存
 * 每次执行 cd 命令时会更新此内容 */
char pwd_cache[MAX_PATH_LEN] = {0};

// argv 必须为全局变量, 为了以后 exec 的程序可访问参数
char *argv[MAX_ARG_NR];
signed int argc = -1;



/* 输出提示符 */
static void print_prompt(void)
{
    printf("[OS@localhost %s]$ ", pwd_cache);
}


/* 从键盘缓冲区中最多读入count个字节到buf */
static void readline(char *buf, signed int count)
{
    assert(buf != NULL && count > 0);
    char *pos = buf;
    
    while(read(stdin_id, pos, 1) != -1 && (pos - buf) < count)
    {
        
        switch(*pos){
        // 控制键: 回车换行符 退格键
        case '\n':
        case '\r':
            *pos = 0;   // 添加 cmd_line 的终止字符0
            putchar('\n');
            return;     // 正常情况下, 输入回车符就返回结果
            
        case '\b':
            if(buf[0] != '\b')  // 阻止删除非本次输入的信息
            {
                --pos;      // 退回到缓冲区 cmd_line中上一个字符
                putchar('\b');
            }
            break;
            
        // ctrl+l 清屏
        case 'l'-'a':
            *pos = 0;           // 1) 先将当前的字符'l'-'a'置为0
            clear();            // 2) 再将屏幕清空
            print_prompt();     // 3) 打印提示符    
            printf("%s", buf);  // 4) 将之前输入的内容再次打印
            break;
            
        // ctrl+u 清除输入
        case 'u'-'a':
            while(buf != pos)
            {
                putchar('\b');
                *(pos--) = 0;
            }
            break;
            
        default:    // 非控制键则输出字符
            putchar(*pos);
            pos++;
        }
    }
    printf("ERROR: during readline, cannot find enter_key in the cmd_line, \
            max num of char is 128!\n");
}



/* 分析字符串 cmd_str 中以token为分隔符的单词, 将各单词的指针存入argv数组 */
static signed int cmd_parse(char *cmd_str, char **argv, char token)
{
    assert(cmd_str != NULL);
    signed int arg_index = 0;
    while(arg_index < MAX_ARG_NR)   // 清空数组 argv
    {
        argv[arg_index] = NULL;
        arg_index++;
    }
    char *next = cmd_str;
    signed int argc = 0;
    
    // 外层循环处理整个命令行
    while(*next)
    {
        // 去除命令字或参数之间的空格
        while(*next == token)
            next++;
        
        // 处理最后一个参数后接空格的情况, 如"ls dir "
        if(*next == 0)
            break;
        
        argv[argc] = next;
        
        // 内层循环处理命令行中的每个命令字及参数
        // 在字符串结束前找单词分隔符
        while(*next && *next != token)
            next++;
        
        // 如果未结束(是token字符), 使token变为0
        if(*next)
            *next++ = 0;
        
        // 避免 argv 数组访问越界, 参数过多则返回0
        if(argc > MAX_ARG_NR)
            return -1;
        
        argc++;
    }
    return argc;
}



/* 执行命令 */
static void cmd_execute(unsigned int argc, char **argv)
{
    if (!strcmp("ls", argv[0]))
    {
        buildin_ls(argc, argv);
    }
    else if(!strcmp("cd", argv[0]))
    {
        if (buildin_cd(argc, argv) != NULL)
        {
            // cd 命令会改变当前工作目录
            // 要将最新的路径final_path复制到pwd_cache, 以更新命令提示符中的路径
            memset(pwd_cache, 0, MAX_PATH_LEN);
            strcpy(pwd_cache, final_path);
        }
    }
    else if(!strcmp("pwd", argv[0]))
    {
        buildin_pwd(argc, argv);
    }
    else if (!strcmp("ps", argv[0]))
    {
        buildin_ps(argc, argv);
    }
    else if (!strcmp("clear", argv[0]))
    {
        buildin_clear(argc, argv);
    }
    else if (!strcmp("mkdir", argv[0]))
    {
        buildin_mkdir(argc, argv);
    }
    else if (!strcmp("rmdir", argv[0]))
    {
        buildin_rmdir(argc, argv);
    }
    else if (!strcmp("rm", argv[0]))
    {
        buildin_rm(argc, argv);
    }
    else if(!strcmp("help", argv[0]))
    {
        buildin_help(argc, argv);
    }
    
    else    // 如果是外部命令, 需要从磁盘上加载
    {
        signed int pid = fork();
        if(pid)     // 父进程
        {
        // 这个while必须要有, 否则父进程一般情况下会比子进程先执行, 因此会进行下一轮循环将
        // final_path 清空, 这样子进程将无法从 final_path 中获得参数
            // while(1);
            
            signed int status;
            signed int child_pid = wait(&status);
            
            // 子进程若没有执行exit, my_shell会被阻塞, 不再响应键入的命令
            if(child_pid == -1)
            // 按理说程序正确的话, 不会执行到这句, fork出的进程便是shell子进程
                panic("my_shell: no child\n");
            printf("\nDEBUG: child_pid %d, it's status: %d\n", child_pid, status);
        }
        else        // 子进程
        {
            make_clear_abs_path(argv[0], final_path);
            argv[0] = final_path;
            
            // 先判断下文件是否存在
            struct stat file_stat;
            memset(&file_stat, 0, sizeof(struct stat));
            if(stat(argv[0], &file_stat) == -1)
            {
                printf("ERROR: my_shell cannot access %s: No such file or directory\n", argv[0]);
                exit(-1);
            }
            else
            {
                execv(argv[0], argv);
            }
            // while(1);
        }
    }    
}



/* 简单的shell */
void my_shell(void)
{
    pwd_cache[0] = '/';
    // pwd_cache[1] = 0;
    while(1)
    {
        print_prompt();
        
        memset(final_path, 0, MAX_PATH_LEN);
        
        memset(cmd_line, 0, MAX_PATH_LEN);
        readline(cmd_line, MAX_PATH_LEN);
        
        if(cmd_line[0] == 0)    // 若只键入了一个回车
            continue;

        // 针对管道的处理
        char *pipe_symbol = strchr(cmd_line, '|');
        if(pipe_symbol)
        {
           /* 支持多重管道操作,如cmd1|cmd2|..|cmdn,
            * cmd1的标准输出和cmdn的标准输入需要单独处理 */
            
            // 1. 生成管道
            signed int fd[2] = {-1};
            pipe(fd);   // 创建管道
            // 将标准输出重定向到 fd[1], 使后面的输出信息重定向到内核环形缓冲区
            fd_redirect(1, fd[1]);
            
            // 2. 第一个命令
            char *each_cmd = cmd_line;
            pipe_symbol = strchr(each_cmd, '|');
            *pipe_symbol = 0;
            
            // 执行第一个命令, 命令的输出会写入环形缓冲区
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);
            
            // 跨过'|', 处理下一个命令
            each_cmd = pipe_symbol + 1;
            
            // 将标准输入重定向到fd[0], 使之指向内核环形缓冲区
            fd_redirect(0, fd[0]);
            
            // 3. 中间的命令, 命令的输入和输出都是指向环形缓冲区
            while((pipe_symbol = strchr(each_cmd, '|')))
            {
                *pipe_symbol = 0;
                argc = -1;
                argc = cmd_parse(each_cmd, argv, ' ');
                cmd_execute(argc, argv);
                each_cmd = pipe_symbol + 1;
            }
            
            // 4. 处理管道中最后一个命令
            // 先将标准输出恢复屏幕
            fd_redirect(1, 1);
            
            // 执行最后一个命令
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);
            
            // 5. 将标准输入恢复为键盘
            fd_redirect(0, 0);
            
            // 6. 关闭管道
            close(fd[0]);
            close(fd[1]);
        }
        else    // 无管道操作的一般命令
        {
            argc = -1;
            argc = cmd_parse(cmd_line, argv, ' ');
            if(argc == -1)
            {
                printf("num of arguments exceed %d\n", MAX_ARG_NR);
                continue;
            }
            cmd_execute(argc, argv);
        }
    }
    panic("ERROR: during my_shell, should not be here");
}



