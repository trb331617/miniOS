#include "buildin_cmd.h"

#include "string.h"     // 
#include "syscall.h"    // getcwd
#include "fs.h"         // MAX_PATH_LEN
#include "dir.h"        // MAX_FILE_NAME_LEN

#include "shell.h"      // final_path[MAX_PATH_LEN]

#include "assert.h"     // assert
#include "stdio.h"      // printf

/* 将路径 old_absolute_path 中的..和.转换为实际路径后存入 new_absolute_path */
// 从左到右解析 old_absolute_path 路径中的每一层, 若解析出来的目录名不是 .. 就将其连接到new
// 若是.. 就将new的最后一层目录去掉
static void wash_path(char *old_absolute_path, char *new_absolute_path)
{
    assert(old_absolute_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0};
    char *sub_path = old_absolute_path;
    
    sub_path = path_parse(sub_path, name);
    if(name[0] == 0)    // 若只键入了"/", 直接将"/"存入 new_absolute_path 后返回
    {
        new_absolute_path[0] = '/';
        new_absolute_path[1] = 0;
        return;
    }
    
    new_absolute_path[0] = 0;   // 避免传给 new_absolute_path 的缓冲区不干净
    strcat(new_absolute_path, "/");
    while(name[0])
    {
        // 如果上一级目录是".."
        if(!strcmp("..", name))
        {
            char *slash_ptr = strrchr(new_absolute_path, '/');
            
            // 如果未到new_abs_path中的顶层目录,就将最右边的'/'替换为0,
            // 这样便去除了new_abs_path中最后一层路径,相当于到了上一级目录
            if(slash_ptr != new_absolute_path)
                // 如new_abs_path为“/a/b”,".."之后则变为“/a”
                *slash_ptr = 0;
            else
                // 若new_abs_path中只有1个'/',即表示已经到了顶层目录, 就将下一个字符置为结束符0
                *(slash_ptr + 1) = 0;
        }
        // 如果路径不是'.',就将name拼接到new_abs_path
        // 若name为当前目录".",无须处理new_abs_path
        else if(strcmp(".", name))
        {
            // 如果new_abs_path不是"/",就拼接一个"/",此处的判断是为了避免路径开头变成这样"//"
            if(strcmp(new_absolute_path, "/"))
                strcat(new_absolute_path, "/");
            strcat(new_absolute_path, name);
        }
        
        // 继续遍历下一层路径
        memset(name, 0, MAX_FILE_NAME_LEN);
        if(sub_path)
            sub_path = path_parse(sub_path, name);
    }
}


/* 将path处理为不含.. 和 . 的绝对路径, 存储在final_path */
void make_clear_abs_path(char *path, char *final_path)
{
    char abs_path[MAX_PATH_LEN] = {0};
    // 先判断是否输入的是绝对路径
    if(path[0] != '/')
    {
        // 若输入的不是绝对路径,就拼接成绝对路径
        memset(abs_path, 0, MAX_PATH_LEN);
        if(getcwd(abs_path, MAX_PATH_LEN) != NULL)
            // 若abs_path表示的当前目录不是根目录/
            if(!((abs_path[0] == '/') && (abs_path[1] == 0)))
                strcat(abs_path, "/");
    }
    strcat(abs_path, path);
    wash_path(abs_path, final_path);
}






/*** 内部函数方式实现shell命令 ***/


/* pwd命令的内建函数 */
void buildin_pwd(unsigned int argc, char **argv __attribute__((unused)))
{
    if(argc != 1)
    {
        printf("pwd: no argument support!\n");
        return;
    }
    else
    {
        if(NULL != getcwd(final_path, MAX_PATH_LEN))
            printf("%s\n", final_path);
        else
            printf("pwd: get current work-directory failed!\n");
    }
}


/* cd命令的内建函数 */
char *buildin_cd(unsigned int argc, char **argv)
{
    if(argc > 2)
    {
        printf("cd: only support 1 argument!\n");
        return NULL;
    }
    
    // 若是只键入cd而无参数, 直接返回到根目录
    if(argc == 1)
    {
        final_path[0] = '/';
        final_path[1] = 0;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
    }
    
    if(chdir(final_path) == -1)
    {
        printf("cd: no such directory %s\n", final_path);
        return NULL;
    }
    return final_path;
}


/* ls命令的内建函数 */
void buildin_ls(unsigned int argc, char **argv)
{
    char *pathname = NULL;
    
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(struct stat));
    
    bool long_info = false;         // 标识是否输入参数 -l
    unsigned int arg_path_count = 0;
    unsigned int arg_index = 1;     // 跨国 argv[0], 即字符串 ls
    
    while(arg_index < argc)
    {
        // 如果是选项, 单词的首字符是 -
        if(argv[arg_index][0] == '-')
        {
            if(!strcmp("-l", argv[arg_index]))      // 参数 -l
                long_info = true;
            else if(!strcmp("-h", argv[arg_index])) // 参数 -h
            {
                printf("usage: -l list all infomation about the file.\n-h for help\nlist all files in the current dirctory if no option\n"); 
                return;                
            }
            else    // 本项目中 ls 只支持 -h -l 两个选项
            {
                printf("ls: invalid option %s\nTry `ls -h' for more information.\n", argv[arg_index]);
                return;                
            }
        }
        // ls 的路径参数
        else
        {
            if(arg_path_count == 0)
            {
                pathname = argv[arg_index];
                arg_path_count = 1;
            }
            else
            {
                printf("ls: only support one path\n");
                return;                
            }
        }
        arg_index++;
    }
    
    // 若只输入了ls 或 ls -l,没有输入操作路径,默认以当前路径的绝对路径为参数
    if(pathname == NULL)
    {
        if(NULL != getcwd(final_path, MAX_PATH_LEN))
            pathname = final_path;
        else
        {
            printf("ls: getcwd for default path failed\n");
            return;            
        }
    }
    else
    {
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }
    
    if(stat(pathname, &file_stat) == -1)
    {
        printf("ls: cannot access %s: No such file or directory\n", pathname);
        return;        
    }
    
    if(file_stat.st_type == FT_DIRECTORY)
    {
        struct dir *dir = opendir(pathname);
        struct dir_entry *dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0};
        
        unsigned int pathname_len = strlen(pathname);
        unsigned int last_char_index = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);
        if(sub_pathname[last_char_index] != '/')
        {
            sub_pathname[pathname_len] = '/';
            pathname_len++;
        }
        rewinddir(dir);
        
        if(long_info)
        {
            char filetype;
            printf("total: %d\n", file_stat.st_size);
            while((dir_e = readdir(dir)))
            {
                filetype = 'd';
                if(dir_e->file_type == FT_REGULAR)
                {
                    filetype = '-';
                }
                sub_pathname[pathname_len] = 0;
                strcat(sub_pathname, dir_e->filename);
                
                memset(&file_stat, 0 , sizeof(struct stat));
                if(stat(sub_pathname, &file_stat) == -1)
                {
                   printf("ls: cannot access %s: No such file or directory\n", dir_e->filename);
                   return;                    
                }
                printf("%c  %d  %d  %s\n", filetype, dir_e->inode_id, file_stat.st_size, dir_e->filename);
            }
        }
        else
        {
            while((dir_e = readdir(dir)))
                printf("%s ", dir_e->filename);
            printf("\n");
        }
        closedir(dir);
    }
    else
    {
        if(long_info)
            printf("-  %d  %d   %s\n", file_stat.st_inode_id, file_stat.st_size, pathname);
        else
            printf("%s\n", pathname);
    }
}


/* ps命令内建函数 */
void buildin_ps(unsigned int argc, char **argv __attribute__((unused)))
{
    if(argc != 1)
    {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}


/* clear命令内建函数 */
void buildin_clear(unsigned int argc, char **argv __attribute__((unused)))
{
    if (argc != 1)
    {
        printf("clear: no argument support!\n");
        return;
    }
    clear();    
}


/* mkdir命令内建函数 */
signed int buildin_mkdir(unsigned int argc, char **argv)
{
    signed int ret = -1;
    if(argc != 2)
        printf("mkdir: only support 1 argument!\n");
    else
    {
        make_clear_abs_path(argv[1], final_path);
        // 若创建的不是根目录
        if(strcmp("/", final_path))
        {
            if(mkdir(final_path) == 0)
                ret = 0;
            else
                printf("mkdir: create directory %s failed.\n", argv[1]);
        }
    }
    return ret;
}


/* rmdir命令内建函数 */
signed int buildin_rmdir(unsigned int argc, char **argv)
{
    signed int ret = -1;
    if(argc != 2)
        printf("rmdir: only support 1 argument!\n");
    else
    {
        make_clear_abs_path(argv[1], final_path);
        // 若删除的不是根目录
        if(strcmp("/", final_path))
        {
            if(rmdir(final_path) == 0)
                ret = 0;
            else
                printf("rmdir: remove %s failed.\n", argv[1]);
        }
    }
    return ret;
}


/* rm命令内建函数 */
signed int buildin_rm(unsigned int argc, char **argv)
{
    signed int ret = -1;
    if(argc != 2)
        printf("rm: only support 1 argument!\n");
    else
    {
        make_clear_abs_path(argv[1], final_path);
        // 若删除的不是根目录
        if(strcmp("/", final_path))
        {
            if(unlink(final_path) == 0)
                ret = 0;
            else
                printf("rm: delete %s failed.\n", argv[1]);
        }
    }
    return ret;
}

/* 显示内建命令列表 */
void buildin_help(unsigned int argc __attribute__((unused)), char** argv __attribute__((unused)))
{
    help();
}
