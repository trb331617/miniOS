#ifndef __SHELL_SHELL_H
#define __SHELL_SHELL_H

#include "fs.h"         // MAX_PATH_LEN

// 用于洗路径时的缓存
// 本项目只支持单控制台, 因此并不会出现 final_path 被覆盖的情况
extern char final_path[MAX_PATH_LEN];


/* 简单的shell */
void my_shell(void);

#endif