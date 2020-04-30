#ifndef __LIB_KERNEL_PRINT_H
#define __LIB_KERNEL_PRINT_H

#include "stdint.h"
void put_char(uint8_t char_asci);
void put_str(char *string);
void put_int(uint32_t num);     // 以十六进制形式打印32位二进制    
void set_cursor(unsigned int cursor_pos);

void cls_screen(void);

#endif