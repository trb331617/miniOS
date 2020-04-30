#ifndef __SHELL_BUILDIN_CMD_H
#define __SHELL_BUILDIN_CMD_H


/* 将path处理为不含.. 和 . 的绝对路径, 存储在final_path */
void make_clear_abs_path(char *path, char *final_path);

void buildin_ls(unsigned int argc, char** argv);
char* buildin_cd(unsigned int argc, char** argv);
signed int buildin_mkdir(unsigned int argc, char** argv);
signed int buildin_rmdir(unsigned int argc, char** argv);
signed int buildin_rm(unsigned int argc, char** argv);
void buildin_pwd(unsigned int argc, char** argv);
void buildin_ps(unsigned int argc, char** argv);
void buildin_clear(unsigned int argc, char** argv);


/* 显示内建命令列表 */
void buildin_help(unsigned int argc __attribute__((unused)), char** argv __attribute__((unused)));

#endif