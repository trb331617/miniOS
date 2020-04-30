#ifndef __LIB_STRING_H
#define __LIB_STRING_H

void memset(void*dst_, unsigned char value, unsigned int size);
void memcpy(void *dst_, const void *src_, unsigned int size);
int memcmp(const void *a_, const void *b_, unsigned int size);

char *strcpy(char *dst_, const char *src_);
unsigned int strlen(const char *str);
signed char strcmp (const char *a, const char *b); 
char *strchr(const char *string, const unsigned char ch);
char *strrchr(const char *string, const unsigned char ch);
char *strcat(char *dst_, const char *src_);
unsigned int strchrs(const char *filename, unsigned char ch);

#endif