#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "list.h"
#include "thread.h"

/* 信号量结构 */
struct semaphore{
    unsigned char value;
    struct list waiters; // 此信号量上等待(阻塞)的所有线程
};

/* 锁结构 */
// 锁是基于信号量实现的，因此锁结构中必须包含一个信号量成员
struct lock{
    struct task_struct *holder;     // 锁的持有者
    struct semaphore semaphore;     // 用二元信号量实现锁
                                    // 每个锁对应的信号量都会有一个阻塞队列
    unsigned int holder_repeat_number;  // 锁的持有者重复申请锁的次数
        // 累积锁的持有者重复申请锁的次数
        // 未释放锁之前，有可能会重复申请此锁
        // 释放锁时，根据这个值来执行相应的操作，
        // 避免内外层函数在释放锁时，会对同一个锁释放2次
};

void sema_init(struct semaphore *sema, unsigned char value);

/* 信号量down操作, 获取锁 */
void sema_down(struct semaphore *sema);

/* 信号量up操作，释放锁 */
void sema_up(struct semaphore *sema);

void lock_init(struct lock *lock);
void lock_acquire(struct lock *lock);
void lock_release(struct lock *lock);

#endif