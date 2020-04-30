#ifndef __DEVICE_CONSOLE_H
#define __DEVICE_CONSOLE_H

void console_init(void);
void console_acquire(void);
void console_release(void);
void console_put_str(char *str);
void console_put_char(unsigned char char_ascii);
void console_put_int(unsigned int num);

#endif