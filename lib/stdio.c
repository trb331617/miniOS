#include "stdio.h"
#include "global.h"

#include "string.h"
#include "syscall.h"

// linux中man stdarg
// 处理可变参数的3个宏

/* 令ap指向第一个固定参数v */
// 初始化指针ap, 令ap指向栈中可变参数中的第一个参数v
// ap为二级指针
#define va_start(ap, v)  ap = (va_list)&v   // 取format字符串的二级指针，得到所在栈中的地址

/* 令ap指向下一个参数并返回其值 */
// t为类型， 如int
// 令指针指向栈中下一个参数, 并根据下一个参数的类型t返回下一个参数的值
#define va_arg(ap, t)   *((t *)(ap += 4))

/* 清空指针ap */
#define va_end(ap)      ap = NULL


/* 将整型转换为字符(integer to ascii) */
// 这里为啥要用二级指针 ？？ 便于原地修改一级指针
// base, 十六进制则为16
static void itoa(unsigned int value, char **buf_ptr_addr, unsigned char base)
{
    unsigned int m = value % base;  // 求模, 最先掉下来的是最低位
    unsigned int i = value / base;  // 取整
    
    // 递归调用: 最低位最先得到, 但转换后的字符却是最后写入缓冲区的
    if(i)   // 不为0, 则递归调用
        itoa(i, buf_ptr_addr, base);
        
    if(m < 10)  // 余数为0~9
        // **ptr = m+'0';   (*ptr)++;
        *((*buf_ptr_addr)++) = m + '0';         // 将数字0~9转换为字符'0'~'9'
    else        // 余数为A~F
        *((*buf_ptr_addr)++) = m - 10 + 'A';    // 将数字A~F转换为字符'A'~'F'
}


/* 将参数ap按照格式format输出到字符串str, 并返回替换后str长度 */
unsigned int vsprintf(char *str, const char *format, va_list ap)
{
    char *buf_ptr = str;
    
    const char *index_ptr = format;
    char index_char = *index_ptr;
    
    signed int arg_int;
    char *arg_str;
    
    while(index_char)
    {
        if(index_char != '%')
        {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        index_char = *(++index_ptr);    // 先++, 跳过%, 得到%后面的字符
        switch(index_char){
        case 's':
            arg_str = va_arg(ap, char *);
            strcpy(buf_ptr, arg_str);
            buf_ptr += strlen(arg_str);
            index_char = *(++index_ptr);
            break;
            
        case 'c':
            *(buf_ptr++) = va_arg(ap, char);
            index_char = *(++index_ptr);
            break;
            
        case 'd':
            arg_int = va_arg(ap, int);
            // 若是负数, 将其转为正数后, 在整数前面输出个负号
            if(arg_int < 0)
            {
                arg_int = 0 - arg_int;
                *buf_ptr++ = '-';
            }
            itoa(arg_int, &buf_ptr, 10); // 将数字转换为十进制字符后写入buf_ptr
            index_char = *(++index_ptr);
            break;
        
        case 'x':
            arg_int = va_arg(ap, int);  // 从参数中取得 格式format中'%'对应的 参数
                                        // va_arg返回下一个参数, 即跳过了栈中的format指针, 取得栈中的可变参数
            itoa(arg_int, &buf_ptr, 16);
            index_char = *(++index_ptr);
            break;
        }
    }
    return strlen(str);
}


/* 格式化输出字符串format */
unsigned int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);     // 使args指向栈中 format
                                // c语言调用约定: 参数从右向左压入栈中
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    
    // return write(buf);
    return write(1, buf, strlen(buf));
}

/* 区别于printf, sprintf是写到buf中，而不是终端 */
unsigned int sprintf(char *buf, const char *format, ...)
{
    va_list args;
    unsigned int ret;
    va_start(args, format);
    ret = vsprintf(buf, format, args);
    return ret;
}