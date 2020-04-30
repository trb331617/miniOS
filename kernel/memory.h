#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "bitmap.h"
#include "list.h"

/* 内存池标记,用于判断用哪个内存池 */
enum pool_flags {
   PF_KERNEL = 1,    // 内核内存池
   PF_USER = 2	     // 用户内存池
};


#define	 PG_P_1	  1	// 页表项或页目录项存在属性位，1存在，0不存在
#define	 PG_P_0	  0	
#define	 PG_RW_R  0	// R/W 属性位值, 读/执行
#define	 PG_RW_W  2	// R/W 属性位值, 读/写/执行
#define	 PG_US_S  0	// U/S 属性位值, 系统级，只允许特权级0 1 2程序访问此页
#define	 PG_US_U  4	// U/S 属性位值, 用户级，允许所有特权级程序访问此页



/* 虚拟地址池，用于管理虚拟地址 */
// 相比于struct pool，这里没有pool_size。虚拟地址空间4GB，
// 相对来说是无限的，不需要指定地址空间大小
struct virtual_addr{
    struct bitmap vaddr_bitmap;     // 虚拟地址用到位图结构
    unsigned int vaddr_begin;       // 虚拟地址起始值
};

// extern struct pool kernel_pool, user_pool;

void mem_init(void);


void *get_kernel_pages(unsigned int page_count);
void *malloc_page(enum pool_flags pf, unsigned int page_count);
void *get_a_page(enum pool_flags pf, unsigned int vaddr);

void *get_user_pages(unsigned int page_count);

unsigned int *pte_ptr(unsigned int vaddr);
unsigned int *pde_ptr(unsigned int vaddr);

unsigned int addr_v2p(unsigned int vaddr);



/* 内存块 */
struct mem_block{
    struct list_elem free_elem;
};

/* 内存块描述符 */
struct mem_block_desc{
    unsigned int block_size;        // 内存块大小
    unsigned int blocks_per_arena;  // 本arena中可容纳此mem_blcok的数量
    struct list free_list;          // 空闲内存块mem_block链表, 此链表中只添加规格为block_size的内存块
                                    // 此链表的长度是无限的，可由多个arena提供内存块
};

// 本项目中的内存规格大小是以2为底的指数方程来设计的
// 从16字节起, 分别是16 32 64 128 256 512 1024
#define MEM_DESC_COUNT  7   // 内存块描述符个数, 即7种规格的内存块

void block_desc_init(struct mem_block_desc *desc_array);
void *sys_malloc(unsigned int size);
void sys_free(void *vaddr);


/* 释放以虚拟地址vaddr为起始的count个页框 */
void mfree_page(enum pool_flags pf, void *_vaddr, unsigned int page_count);


/* 安装1页大小的vaddr, 专门针对fork时虚拟地址位图无需操作的情况 */
// 为指定的vaddr分配一物理页, 但无需从虚拟地址内存池中设置位图
void *get_a_page_without_operate_vaddrbitmap(enum pool_flags pf, unsigned int vaddr);




/* 根据物理页框地址 page_phy_addr 在相应的内存池的位图清0, 不改动页表 */
void free_a_phy_page(unsigned int page_phy_addr);


#endif