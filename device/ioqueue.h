#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "sync.h"

// DEBUG_20200425
#define buffersize  1024

/* 环形队列 */
struct ioqueue{
    // 生产者消费者问题
    
    // 每次对缓冲区操作都要先申请这个锁，从而保证缓冲区操作互斥
    struct lock lock;
    
    // 生产者，缓冲区不满时就继续往里面放数据，否则就睡眠
    struct task_struct *producer;   // 记录哪个生产者在此缓冲区上睡眠
    
    // 消费者，缓冲区不空时就继续从里面拿数据，否则就睡眠
    struct task_struct *consumer;   // 记录哪个消费者在此缓冲区上睡眠

    char buf[buffersize];   // 缓冲区大小
    signed int head;        // 队首，数据往队首处写入
    signed int tail;        // 队尾，数据从队尾读出
};

void ioqueue_init(struct ioqueue *ioq);
bool ioq_full(struct ioqueue *ioq);
char ioq_getchar(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);
bool ioq_empty(struct ioqueue *ioq);


/* 返回环形缓冲区中的数据长度 */
unsigned int ioq_length(struct ioqueue *ioq);

#endif