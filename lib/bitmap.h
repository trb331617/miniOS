#ifndef __LIB_BITMAP_H
#define __LIB_BITMAP_H

#include "global.h"

struct bitmap{
    // 遍历位图时整体上以字节为单位，细节上以位为单位
    unsigned int bitmap_bytes_len;
    unsigned char *bits;    // 位图所在内存的起始地址
};

void bitmap_init(struct bitmap *btmp);
bool bitmap_scan_bit(struct bitmap *btmp, unsigned int bit_index);
int bitmap_scan(struct bitmap *btmp, unsigned int count);
void bitmap_set(struct bitmap *btmp, unsigned int index, signed char value);

#endif