#include "bitmap.h"
#include "string.h"
#include "debug.h"

/* 位图bitmap初始化 */
void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->bitmap_bytes_len);
}


/* 判断bit_index位是否为1，若为1返回true，否则返回false */
bool bitmap_scan_bit(struct bitmap *btmp, unsigned int bit_index)
{
    unsigned int byte_index = bit_index / 8; // 向下取整，索引数组下标
    unsigned int bit_odd = bit_index % 8; // 取余，所以字节内的位
    return (btmp->bits[byte_index] & (1 << bit_odd));
}


/* 在位图中申请连续count个位，成功则返回起始位下标，失败返回-1 */
int bitmap_scan(struct bitmap *btmp, unsigned int count)
{
    unsigned int byte_index = 0;    // 记录空闲位所在的字节
    // 先逐字节比较，蛮力法
    while((0xff == btmp->bits[byte_index]) && (byte_index < btmp->bitmap_bytes_len))
        byte_index++;
    
    ASSERT(byte_index < btmp->bitmap_bytes_len);
    if(byte_index == btmp->bitmap_bytes_len)
        return -1;      // 若该内存池找不到可用空间
    
    /* 若在位图数组范围内的某字节内找到了空闲位，
     * 在该字节内逐位比对,返回空闲位的索引。*/
    int bit_index = 0;
    // 和btmp->bits[byte_index]这个字节逐位对比
    while((unsigned char)(1<<bit_index) & btmp->bits[byte_index])
        bit_index++;
    
    // 空闲位在位图内的下标
    int bit_index_begin = byte_index * 8 + bit_index;
    if(count == 1)
        return bit_index_begin;
    
    unsigned int bit_leaving = btmp->bitmap_bytes_len * 8 - bit_index_begin;    // 记录还有多少位可以判断
    unsigned int next_bit = bit_index_begin + 1;
    unsigned int counting = 1;     // 用于记录找到的空闲位的个数
    
    bit_index_begin = -1;   // 先将其置为-1，若找不到连续的位就直接返回
    while(bit_leaving-- > 0)
    {
        if(!(bitmap_scan_bit(btmp, next_bit)))
            counting++;
        else
            counting = 0;
        
        if(counting == count)   // 若找到连续的count个空位
        {
            bit_index_begin = next_bit - count + 1;
            break;
        }
        next_bit++;
    }        
    return bit_index_begin;
}


/* 将位图btmp的bit_index位设置为value */
void bitmap_set(struct bitmap *btmp, unsigned int bit_index, signed char value)
{
    ASSERT((value == 0) || (value == 1));
    unsigned int byte_index = bit_index / 8; // 向下取整索引数组下标
    unsigned int bit_odd = bit_index % 8; // 取余索引数组内的位
    
    if(value)   // 如果value为1
        btmp->bits[byte_index] |= (1<<bit_odd);
    else
        btmp->bits[byte_index] &= ~(1<<bit_odd);
}

