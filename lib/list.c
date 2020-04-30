#include "list.h"
#include "interrupt.h"

#include "global.h"

/* 初始化双向链表 */
void list_init(struct list *list)
{
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

/* 链表元素elem插入到元素before之前 */
void list_insert_before(struct list_elem *before, struct list_elem *elem)
{
    // 队列是公共资源，对其修改一定要保证原子操作
    enum intr_status old_status = intr_disable();   // 关中断，并保存旧中断状态
    
    before->prev->next = elem;
    
    elem->prev = before->prev;
    elem->next = before;
    
    before->prev = elem;
    
    intr_set_status(old_status);    // 恢复旧中断状态
}

/* 添加元素到列表队首，类似栈push操作 */
void list_push(struct list *plist, struct list_elem *elem)
{
    list_insert_before(plist->head.next, elem);     // 在队头插入elem
}

/* 追加元素到链表队尾，类似队列的先进先出操作 */
void list_append(struct list *plist, struct list_elem *elem)
{
    list_insert_before(&plist->tail, elem);     // 在队尾的前面插入
}

/* 使元素pelem脱离链表 */
void list_remove(struct list_elem *pelem)
{
    enum intr_status old_status = intr_disable();
    
    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;
    
    intr_set_status(old_status);
}

/* 将链表第一个元素弹出并返回该元素，类似栈的pop操作 */
struct list_elem *list_pop(struct list *plist)
{
    struct list_elem *elem = plist->head.next;
    list_remove(elem);
    return elem;
}

/* 从链表中查找元素obj_elem，成功返回true，失败返回false */
bool elem_find(struct list *plist, struct list_elem *obj_elem)
{
    struct list_elem *elem = plist->head.next;
    while(elem != &plist->tail)
    {
        if(elem == obj_elem)
            return true;
        elem = elem->next;
    }
    return false;
}

/* 遍历列表内所有元素，逐个判断是否有符合条件的元素
 * 找到符合条件的元素返回元素指针，否则返回NULL
 * 把列表plist中的每个元素elem和arg传给回调函数func，arg给func用来判断elem是否符合条件*/
struct list_elem *list_traversal(struct list *plist, function func, int arg)
{
    struct list_elem *elem = plist->head.next;
    
    if(list_empty(plist))
        return NULL;
    while(elem != &plist->tail)
    {
        // func返回true则认为该元素在回调中符合条件，命中，故停止遍历
        if(func(elem, arg))
            return elem;
        elem = elem->next;
    }
    return NULL;
}

/* 返回链表长度 */
unsigned int list_len(struct list *plist)
{
    struct list_elem *elem = plist->head.next;
    unsigned int length = 0;
    
    while(elem != &plist->tail)
    {
        length++;
        elem = elem->next;
    }
    return length;
}

/* 判断链表是否为空，空返回true，否则返回false */
bool list_empty(struct list *plist)
{
    return (plist->head.next == &plist->tail ? true:false);
}
