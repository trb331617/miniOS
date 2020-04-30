#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H

typedef char* va_list;

unsigned int vsprintf(char *str, const char *format, va_list ap);

unsigned int printf(const char *str, ...);
// unsigned int vsprintf(char *str, const char *format, va_list ap);
unsigned int sprintf(char *buf, const char *format, ...);


#endif

