#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H

// #include "stdint.h"

void idt_init(void);
void register_handler(unsigned char vector_id, void *function);


/*
 * 定义中断的2种状态
 * INTR_ON 值为1，表示开中断
 * INTR_OFF 值为0，表示关中断
 */
enum intr_status{
    INTR_OFF,       // 中断关闭
    INTR_ON         // 中断打开
};

enum intr_status intr_get_status(void);
enum intr_status intr_set_status(enum intr_status);
enum intr_status intr_enable(void);
enum intr_status intr_disable(void);


#endif