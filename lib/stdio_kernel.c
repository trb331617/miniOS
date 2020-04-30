#include "stdio_kernel.h"
#include "stdio.h"      // vsprintf
#include "global.h"
#include "console.h"


// linux中man stdarg
// 处理可变参数的3个宏

/* 令ap指向第一个固定参数v */
// 初始化指针ap, 令ap指向栈中可变参数中的第一个参数v
// ap为二级指针
#define va_start(ap, v)  ap = (char *)&v   // 取format字符串的二级指针，得到所在栈中的地址

/* 令ap指向下一个参数并返回其值 */
// t为类型， 如int
// 令指针指向栈中下一个参数, 并根据下一个参数的类型t返回下一个参数的值
// #define va_arg(ap, t)   *((t *)(ap += 4))

/* 清空指针ap */
#define va_end(ap)      ap = NULL



// 供内核使用, 即内核版 printf
/* 格式化输出字符串format */
void printk(const char *format, ...)
{
    char *args;
    va_start(args, format);     // 使args指向栈中 format
                                // c语言调用约定: 参数从右向左压入栈中
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    
    // return write(buf);
    console_put_str(buf);
}