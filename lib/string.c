#include "string.h"
// #include "global.h"
// #include "debug.h"
#include "assert.h"

/* 将dst_起始的size个字节置为value */
// 内存区域的数据初始化
void memset(void *dst_, unsigned char value, unsigned int size)
{
    assert(dst_ != NULL);
    unsigned char *dst = (unsigned char *)dst_;
    while(size--)
        *dst++ = value;
}

/* 将src_起始的size个字节复制到dst_ */
void memcpy(void *dst_, const void *src_, unsigned int size)
{
    assert(dst_ != NULL && src_ != NULL);
    unsigned char *dst = (unsigned char *)dst_;
    const unsigned char *src = (unsigned char *)src_;
    while(size--)
        *dst++ = *src++;
}

/* 连续比较以地址a_和地址b_开头的size个字节，相等返回0，a_大于b_返回1，否则返回-1 */
int memcmp(const void *a_, const void *b_, unsigned int size)
{
    assert(a_ != NULL || b_ != NULL);
    const char *a = a_, *b = b_;
    while(size--)
    {
        if(*a != *b)
            return *a > *b ? 1 : -1;
        a++; b++;
    }
    return 0;
}

/* 将字符串从src_复制到dst_ */
char *strcpy(char *dst_, const char *src)
{
    assert(dst_ != NULL || src != NULL);
    while((*dst_++ = *src++));
    return dst_;    // 返回目的字符串起始地址
}

/* 返回字符串长度 */
unsigned int strlen(const char *str)
{
    assert(str != NULL);
    const char *p = str;
    while(*p++);
    return (p-str-1);
}

/* 比较a_和b_中的字符串，相等返回0，a_大于b_返回1，否则返回-1 */
signed char strcmp(const char *a, const char *b)
{
    assert(a != NULL && b != NULL);
    while(*a != 0 && *a == *b)
    {a++; b++;}
    return *a > *b ? 1: *a < *b;
}

/* 从左到右查找字符串str中首次出现字符ch的地址(不是下标，是地址) */
char *strchr(const char *str, const unsigned char ch)
{
    assert(str != NULL);
    while(*str != 0)
    {
        if(*str == ch)
            // 需要强制转化成和返回值类型一样,否则编译器会报const属性丢失
            return (char *)str;
        str++;
    }
    return NULL;
}

/* 从后往前查找字符串str中首次出现字符ch的地址(不是下标,是地址) */
char *strrchr(const char *str, const unsigned char ch)
{
    assert(str != NULL);
    const char *last_char = NULL;
    // 从头到尾遍历一次,若存在ch字符,last_char总是该字符最后一次出现在串中的地址(不是下标,是地址)
    while(*str != 0)
    {
        if(*str == ch)
            last_char = str;
        str++;
    }
    return (char *)last_char;
}

/* 将字符串src_拼接到dst_后，返回拼接的字符串地址 */
char *strcat(char *dst_, const char *src_)
{
    assert(dst_ != NULL && src_ != NULL);
    char *str = dst_;
    while(*str++);
    --str;
    // 当*str被赋值为0时,此时表达式不成立,正好添加了字符串结尾的0
    while((*str++ = *src_++)); 
    return dst_;
}

unsigned int strchrs(const char *str, unsigned char ch)
{
    assert(str != NULL);
    unsigned int ch_cnt = 0;
    const char *p = str;
    while(*p != 0)
    {
        if(*p == ch)
            ch_cnt++;
        p++;
    }
    return ch_cnt;
}
