#ifndef __LIB_ASSERT_USER_H
#define __LIB_ASSERT_USER_H

#define NULL ((void *)0)

void user_spin(char* filename, int line, const char* func, const char* condition);

#define panic(...)  user_spin(__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef NDEBUG
    #define assert(CONDITION) ((void)0)
#else
    #define assert(CONDITION) \
        if(!(CONDITION)) \
        { \
            /* 符号# 让预处理器将宏的参数转化为字符串常量 */ \
            panic(#CONDITION); \
        }

#endif


#endif