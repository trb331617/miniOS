#include "sync.h"
#include "interrupt.h"
#include "list.h"
#include "debug.h"

/* 初始化信号量 */
void sema_init(struct semaphore *sema, unsigned char value)
{
    sema->value = value;        // 为信号量赋值
    list_init(&sema->waiters);  // 初始化信号量的等待队列
}

/* 初始化锁 */
void lock_init(struct lock *lock)
{
    lock->holder = NULL;
    lock->holder_repeat_number = 0;
    sema_init(&lock->semaphore, 1);     // 信号量初值为1，即锁中的信号量为二元信号量
}


/* 信号量down操作, 获取锁 */
void sema_down(struct semaphore *sema)
{
    // 关中断来保证原子操作
    enum intr_status old_status = intr_disable();
    
    // while
    // 这里必须用while, 不能用if
    // 当阻塞线程被唤醒后，也不一定就能获得资源，只是再次获得了去竞争锁的机会
    // e.g.: 1) t_1线程持有锁, t_2阻塞; 2) t_1释放锁, t_2Ready; 3) t_2Ready, t_3Running持有锁
    
    // 这里可以用if代替while
    // 信号量up操作时，通过thread_unblock唤醒线程，而thread_unblock会把阻塞线程放在就绪队列的队首
    // 也就是当前锁的持有者释放锁之后，它会第一个获得锁
    // 不对。万一，在该持有者释放锁的时间片内，又重新持有锁
    while(sema->value == 0)     // value为0, 表明已经被别人持有
    {
        // 当前线程不应该已在信号量的waiter队列中
        ASSERT(!elem_find(&sema->waiters, &running_thread()->general_tag));
        if(elem_find(&sema->waiters, &running_thread()->general_tag))
            PANIC("[ERROR]sema_down: thread blocked has been in waiters_list\n");
        
        // 若信号量的值为0，则当前线程把自己加入该锁的等待队列，然后阻塞自己
        list_append(&sema->waiters, &running_thread()->general_tag);
        thread_block(TASK_BLOCKED); // 线程阻塞自己，并触发调度，切换线程    
    }
    
    // 若value为1或被唤醒后，会执行下面的代码，也就是获得了锁
    sema->value--;
    ASSERT(sema->value == 0);
    intr_set_status(old_status);
}


/* 信号量up操作，释放锁 */
void sema_up(struct semaphore *sema)
{
    enum intr_status old_status = intr_disable();   // 关中断来保证原子操作
    
    ASSERT(sema->value == 0);
    if(!list_empty(&sema->waiters))     // 锁的等待队列不为空时
    {
        struct task_struct *thread_blocked = elem2entry(struct task_struct, \
                                    general_tag, list_pop(&sema->waiters));
        thread_unblock(thread_blocked); // 将阻塞线程加入就绪队列，并修改状态为READY
    }
    sema->value++;
    ASSERT(sema->value == 1);
    intr_set_status(old_status);
}


/* 获取锁 */
void lock_acquire(struct lock *lock)
{
    // 排除自己已经持有锁，但还未将其释放的情况
    // 否则，如果线程嵌套申请同一把锁时，就会形成死锁，自己等待自己释放锁
    if(lock->holder != running_thread())
    {
        sema_down(&lock->semaphore);    // 对信号量P操作，原子操作
        lock->holder = running_thread();
        
        ASSERT(lock->holder_repeat_number == 0);
        lock->holder_repeat_number = 1;
    }
    else
        lock->holder_repeat_number++;
}


/* 释放锁 */
void lock_release(struct lock *lock)
{
    ASSERT(lock->holder == running_thread());
    if(lock->holder_repeat_number > 1)  // 说明自己持有锁期间，多次申请该锁，此时还不能释放锁
    {
        lock->holder_repeat_number--;
        return;
    }
    
    ASSERT(lock->holder_repeat_number == 1); // 说明现在可以释放锁了
    
    // 本函数释放锁的操作没有在关中断下进行，锁的持有者置空为NULL必须放在V操作之前
    // 否则，如果t_a线程刚执行完sema_up，就被调度为t_b持有锁，然后再次调度为t_a执行holder=NULL
    lock->holder = NULL;    // 锁的持有者置空放在V操作之前
    lock->holder_repeat_number = 0;
    sema_up(&lock->semaphore);  // 信号量的V操作，原子操作
}
