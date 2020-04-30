#include "memory.h"
#include "string.h"
#include "print.h"
#include "debug.h"

#include "sync.h"
#include "interrupt.h"

#define PAGE_SIZE   4096

/***************  位图地址 ********************
 * 因为0xc009f000是内核主线程栈顶，0xc009e000是内核主线程的pcb.
 * 一个页框大小的位图可表示128M内存, 位图位置安排在地址0xc009a000,
 * 这样本系统最大支持4个页框的位图,即512M
 * 0xc009 e00 - 0x4000 = 0xc009 a00 */
// 一个页框大小的位图，4KB*8bits*4KB=128M
#define MEM_BITMAP_BASE     0xc009a000      // 内存位图基址
/*************************************/

/* 0xc000_0000是内核从虚拟地址3G起.
 * 0x0010_0000意指跨过低端1M内存,使虚拟地址在逻辑上连续 */
#define K_HEAP_START    0xc0100000      // 内核堆空间的起始虚拟地址


/* 二级页表的分页机制下，
 * 高10位为页目录表PDT中的索引，中间10位为页表PT中的索引 */
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)


/* 内存池结构
 * 用于管理内存池中的所有物理内存 */
struct pool{
    struct lock lock;               // 申请内存时互斥, 避免公共资源的竞争
    
    struct bitmap pool_bitmap;      // 内存池用到的位图结构，用于管理物理内存
    unsigned int phy_addr_begin;    // 内存池所管理物理内存的起始地址
    unsigned int pool_size;         // 内存池字节容量，本物理内存池的内存容量
};

struct pool kernel_pool, user_pool;     // 内核内存池和用户内存池

struct virtual_addr kernel_vaddr;       // 用于给内核分配虚拟地址




/* 内存仓库arena */
// arena元信息, 12字节
// 将会在堆中创建, 给arena结构体指针赋予1个页框以上的内存, arena则成为内存仓库
// 页框中除了此结构体外的部分都将作为arena的内存池区域, 该区域被平均拆分成多个相同规格的内存块, 即mem_block
// 这些mem_block会被添加到内存块描述符的free_list
struct arena{
    struct mem_block_desc *desc;    // 此arena关联的mem_block_desc
    // large为true时, count表示页框数; 否则, count表示空间mem_block数量
    unsigned int count;
    // large表示此arena用于处理大于1024字节以上的内存分配
    bool large;
};



// 内核内存块描述符数组
// 用户进程也有自己的内存块描述符数组, 将来定义在PCB中
struct mem_block_desc k_block_descs[MEM_DESC_COUNT];    

/* 为malloc准备 */
void block_desc_init(struct mem_block_desc *desc_array)
{
    unsigned short int desc_index, block_size = 16;
    
    /* 初始化每个mem_block_desc描述符, 即7种规格的内存块描述符 */
    // 内存块描述符数组中, 下标越低的内存块描述符, 其表示的内存块容量越小
    for(desc_index = 0; desc_index < MEM_DESC_COUNT; desc_index++)
    {
        desc_array[desc_index].block_size = block_size;
        
        // 初始化该规格arnea中的内存块数量
        desc_array[desc_index].blocks_per_arena = (PAGE_SIZE - sizeof(struct arena)) / block_size;
        
        // 每种规格都有一个链表
        list_init(&desc_array[desc_index].free_list);
        
        // 规格: 16 32 64 128 256 512 1024字节
        block_size *= 2;    // 下一种规格的内存块
    }
}




/* 初始化内存池 */
static void mem_pool_init(unsigned int mem_size)
{
    put_str("    mem_pool_init start...\n");
    
/* 页目录PDT大小为1页
 * 第0和第768(0x300)个页目录项pde指向同一个页表PT，即共享1页
 * 第769~1022个页目录项共指向254个页表
 * 最后一个页目录项(第1023个)指向页目录表PDT本身
 * 因此，共256个页，正好1M */
    unsigned int page_table_size = PAGE_SIZE * 256;     // 页目录表和页表占用的字节大小
    
    unsigned int used_mem = page_table_size + 0x100000; // 低端1MB内存
    unsigned int free_mem = mem_size - used_mem;        // 本项目中设置的总内存为32M - 2M
    
    // 1页为4k,不管总内存是不是4k的倍数,
	// 对于以页为单位的内存分配策略，不足1页的内存不用考虑了
    unsigned short int all_free_pages = free_mem / PAGE_SIZE;
    unsigned short int kernel_free_pages = all_free_pages / 2;
    unsigned short int user_free_pages = all_free_pages - kernel_free_pages;
    
    // 位图中的一位表示一页4KB,以字节为单位
    // 为简化位图操作，余数不处理，坏处是这样做会丢内存(1~7页)
    // 好处是不用做内存的越界检查,因为位图表示的内存少于实际物理内存
    unsigned int kbm_length = kernel_free_pages / 8;    // kernel bitmap长度
    unsigned int ubm_length = user_free_pages / 8;      // user bitmap长度
    
    // 物理内存池起始地址
    unsigned int kp_begin = used_mem;   // kernel pool start, 内核物理内存池的起始地址
    unsigned int up_begin = kp_begin + kernel_free_pages * PAGE_SIZE; // user pool start
    
    kernel_pool.phy_addr_begin = kp_begin;
    user_pool.phy_addr_begin = up_begin;
    
    kernel_pool.pool_size = kernel_free_pages * PAGE_SIZE;
    user_pool.pool_size = user_free_pages * PAGE_SIZE;
    
    kernel_pool.pool_bitmap.bitmap_bytes_len = kbm_length;
    user_pool.pool_bitmap.bitmap_bytes_len = ubm_length;
    
    
/*********    内核内存池和用户内存池位图   ***********
 *   位图是全局的数据，长度不固定。
 *   全局或静态的数组需要在编译时知道其长度，
 *   而我们需要在程序运行过程中根据总内存大小算出位图需要多少字节。
 *   所以改为指定一块内存来生成位图，这样就不需要固定长度了
 *   ************************************************/

// 内核使用的最高地址是0xc009f000,这是主线程的栈地址.(内核的大小预计为70K左右)
// 32M内存占用的位图是2k.内核内存池的位图先定在MEM_BITMAP_BASE(0xc009a000)处
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;

// 用户内存池的位图紧跟在内核内存池位图之后
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);
    
    /******************** 输出内存池信息 **********************/
    put_str("    kernel_pool_bitmap_start: 0x"); 
    put_int((int)kernel_pool.pool_bitmap.bits);     // 内存池所用位图的起始地址
    put_str("\n    kernel_pool_phy_addr_start: 0x"); 
    put_int(kernel_pool.phy_addr_begin);    // 内存池的起始物理地址
    
    put_str("\n    user_pool_bitmap_start: 0x");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str("\n    user_pool_phy_addr_start: 0x");
    put_int(user_pool.phy_addr_begin);
    put_char('\n');
    
/* 将位图置0初始化 
 * 位值为0表示该位对应的内存页未分配，为1表示已分配 */
    bitmap_init(&kernel_pool.pool_bitmap);      // 初始化位图
    bitmap_init(&user_pool.pool_bitmap);

    
    // 初始化锁
    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    
    /* 初始化内核虚拟地址池 */
    
    /* 初始化内核虚拟地址的位图，按实际物理内存大小生成数组 */
    // 用于维护内核堆的虚拟地址，所以要和内核内存池大小一致
    kernel_vaddr.vaddr_bitmap.bitmap_bytes_len = kbm_length;
    
    /* 位图的数组指向一块未使用的内存,目前定位在内核内存池和用户内存池之外*/
    // 这里将其安排在紧挨着内核内存池和用户内存池所用的位图之后
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    
    // 内核虚拟内存池的起始地址为K_HEAP_START，即0xc010 0000
    kernel_vaddr.vaddr_begin = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);    // 初始化内核的虚拟内存池位图
    put_str("    mem_pool_init done!\n");
}


/* 内存管理部分初始化入口 */
void mem_init()
{
    put_str("mem_init begin...\n");
    
/* loader.asm中把获取到的内存容量保存在汇编变量mem_total_size中, 其物理地址为0x900
 * mem_total_size是用伪指令dd来定义的，宽度为32位。这里先把0x900转换成32位整型指针，
 * 再通过*对该指针做取值操作 */
    unsigned int mem_bytes_total = (*(unsigned int *)(0x900));  // 以字节为单位
    mem_pool_init(mem_bytes_total);     // 初始化内存池
    
    // 初始化每个mem_block_desc描述符数组, 为malloc做准备
    block_desc_init(k_block_descs);
    
    put_str("mem_init done!\n");
}







/* 在用户(pf=2)/内核(pf=1)的虚拟内存池中申请page_count个虚拟页，
 * 成功则返回虚拟页起始地址，失败则返回NULL */
static void *vaddr_get(enum pool_flags pf, unsigned int page_count)
{
    int vaddr_begin = 0, bit_index = -1;
    unsigned int counting = 0;
    if(pf == PF_KERNEL)     // 内核虚拟内存池
    {
        bit_index = bitmap_scan(&kernel_vaddr.vaddr_bitmap, page_count);
        if(bit_index  == -1)
            return NULL;
        while(counting < page_count)
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_index + counting++, 1);
        vaddr_begin = kernel_vaddr.vaddr_begin + bit_index * PAGE_SIZE;
    }
    else    // 用户内存池
    {
        struct task_struct *current = running_thread();
        bit_index = bitmap_scan(&current->user_vaddr.vaddr_bitmap, page_count);
        if(bit_index == -1)
            return NULL;
        while(counting < page_count)
            bitmap_set(&current->user_vaddr.vaddr_bitmap, bit_index + counting++, 1);
        vaddr_begin = current->user_vaddr.vaddr_begin + bit_index * PAGE_SIZE;
        
        // (0xc000_0000 - PAGE_SIZE)作为用户3级栈已经在start_process被分配
        ASSERT((unsigned int)vaddr_begin < (0xc0000000 - PAGE_SIZE));   // ???
    }
    return (void *)vaddr_begin;
}


/* 得到虚拟地址vaddr对应的页表项PTE指针 */
unsigned int *pte_ptr(unsigned int vaddr)
{
    /* 先访问到页表自己 + 
     * 再用页目录项pde(页目录内页表的索引)做为pte的索引访问到页表 +
     * 再用pte的索引作为页内偏移 */
    unsigned int *pte = (unsigned int *)(0xffc00000 + \
        ((vaddr & 0xffc00000) >> 10) + \
        PTE_IDX(vaddr) * 4);
    return pte;
}


/* 得到虚拟地址vaddr对应的页目录项PDE的指针 */
unsigned int *pde_ptr(unsigned int vaddr)
{
    // 0xffff_f000是用来访问到页目录表本身所在的地址
    unsigned int *pde = (unsigned int *)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}


/* 在mem_pool指向的物理内存池中分配一个物理页，
 * 成功则返回页的物理地址，失败则返回NULL */
static void *palloc(struct pool *mem_pool)
{
    // 扫描或设置位图要保证原子操作
    int bit_index = bitmap_scan(&mem_pool->pool_bitmap, 1); // 找一个物理页
    if(bit_index == -1)
        return NULL;
    bitmap_set(&mem_pool->pool_bitmap, bit_index, 1);
    unsigned int page_phyaddr = bit_index * PAGE_SIZE + mem_pool->phy_addr_begin;
    return (void *)page_phyaddr;
}


/* 页表中添加虚拟地址_vaddr 与物理地址_page_phyaddr的映射 */
static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    unsigned int vaddr = (unsigned int)_vaddr, page_phyaddr = (unsigned int)_page_phyaddr;
    
    unsigned int *pde = pde_ptr(vaddr);
    unsigned int *pte = pte_ptr(vaddr);
    
/************************   注意   *************************
 * 执行*pte,会访问到空的pde。所以确保pde创建完成后才能执行*pte,
 * 否则会引发page_fault。因此在*pde为0时,*pte只能出现在下面else语句块中的*pde后面。
 * *********************************************************/
    /* 先在页目录内判断目录项的P位，若为1,则表示该表已存在 */
    if(*pde & 0x00000001) // 页目录项和页表项的第0位为P,此处判断目录项是否存在
    {
        ASSERT(!(*pte & 0x00000001));   // 断言: pte的P位为不存在
        
        if(!(*pte & 0x00000001))    // 创建页表。pte就应该不存在,多判断一下放心
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US=1 RW=1 P=1
        else
            // 调试模式下不会执行到此,上面的ASSERT会先执行.关闭调试时下面的PANIC会起作用
            PANIC("pte repeat");
    }
    else    // 页目录项不存在，所以要先申请一个物理页作为页表并创建页目录项
    {
        // 页表中用到的页一律从内核空间分配
        unsigned int pde_phyaddr = (unsigned int)palloc(&kernel_pool);
        *pde = pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1;
        
        /* 分配到的物理页地址pde_phyaddr对应的物理内存清0,
         * 避免里面的陈旧数据变成了页表项,从而让页表混乱.
         * 访问到pde对应的物理地址,用pte取高20位便可.
         * 因为pte是基于该pde对应的物理地址内再寻址,
         * 把低12位置0便是该pde对应的物理页的起始 */
        memset((void *)((int)pte & 0xfffff000), 0, PAGE_SIZE);
        
        ASSERT(!(*pte & 0x00000001));   // 断言: pte的P位为不存在
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}


/* 分配page_count个页空间，成功则返回起始虚拟地址，失败则返回NULL
 * 虚拟地址是连续的，但物理地址可能连续，也可能不连续
 * 一次性申请page_count个虚拟页，成功申请之后，根据申请的页数，通过循环
 * 依次为每一个虚拟页申请物理页，再将它们在页表中依次映射关联 */

void *malloc_page(enum pool_flags pf, unsigned int page_count)
{
    // 本项目中设置的总内存为32M(bochsrc.cfg)，内核和用户空间各约16MB，
    // 保守起见用15MB来限制。15M/4KB=3840页
    ASSERT(page_count > 0 && page_count < 3840);
    
/***********   malloc_page的原理是三个动作的合成:   ***********
      1 通过vaddr_get在虚拟内存池中申请虚拟地址
      2 通过palloc在物理内存池中申请物理页
      3 通过page_table_add将以上得到的虚拟地址和物理地址在页表中完成映射
***************************************************************/ 

    void *vaddr_begin = vaddr_get(pf, page_count);
    if(vaddr_begin == NULL)
        return NULL;
    
    unsigned int vaddr = (unsigned int)vaddr_begin, count = page_count;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    /* 虚拟地址是连续的，但物理地址可以是不连续的，所以逐个做映射 */
    while(count-- > 0)
    {
        void *page_phyaddr = palloc(mem_pool);
        if(page_phyaddr == NULL)
        {
            // 物理页申请失败时，应将曾经已申请的虚拟地址和物理页全部回滚，
            // 如果物理内存池的位图也被修改过，还要再把物理地址回滚
            // 在将来完成内存回收时再补充
            return NULL;
        }
        page_table_add((void *)vaddr, page_phyaddr); // 在页表中做映射
        vaddr += PAGE_SIZE;     // 下一个虚拟页
    }
    return vaddr_begin;
}


/* 从内核物理内存池中申请page_count页内存，
 * 成功则返回其虚拟地址，失败则返回NULL */
void *get_kernel_pages(unsigned int page_count)
{
    lock_acquire(&kernel_pool.lock);
    
    void *vaddr = malloc_page(PF_KERNEL, page_count);
    if(vaddr != NULL)
        // 若分配的地址不为空，将页清0后返回
        memset(vaddr, 0, page_count * PAGE_SIZE);
        
    lock_release(&kernel_pool.lock);
    return vaddr;
}








/* 在用户空间中申请4K内存，并返回其虚拟地址 */
void *get_user_pages(unsigned int page_count)
{
    lock_acquire(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, page_count);
    memset(vaddr, 0, page_count * PAGE_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}


/* 将地址vaddr与内存池中的物理地址关联，仅支持一页空间分配 */
// 参数vaddr用于指定要绑定的虚拟地址。
// 函数功能：申请一页内存，并用vaddr映射到该页
// 函数get_user_pages和get_kernel_pages不能指定虚拟地址，只能由内存管理模块自动分配虚拟地址
void *get_a_page(enum pool_flags pf, unsigned int vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    lock_acquire(&mem_pool->lock);
    
    /* 先将虚拟地址对应的位图置1 */
    struct task_struct *current = running_thread();
    signed int bitmap_index = -1;
    
    // 如果是用户进程申请用户内存，则修改用户进程自己的虚拟地址位图
    if(current->pgdir != NULL && pf == PF_USER)
    {
        bitmap_index = (vaddr - current->user_vaddr.vaddr_begin) / PAGE_SIZE;
        ASSERT(bitmap_index >= 0);
        bitmap_set(&current->user_vaddr.vaddr_bitmap, bitmap_index, 1);
    }
    // 如果是内核线程申请内核内存，则修改kernel_vaddr
    else if(current->pgdir == NULL && pf == PF_KERNEL)
    {
        bitmap_index = (vaddr - kernel_vaddr.vaddr_begin) / PAGE_SIZE;
        ASSERT(bitmap_index > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bitmap_index, 1);
    }
    else
        PANIC("[ERROR]get_a_page: not allow kernel alloc userspace or user alloc kernelspace\n");
    
    void *page_phyaddr = palloc(mem_pool);
    if(page_phyaddr == NULL)
        return NULL;
    page_table_add((void *)vaddr, page_phyaddr);
    
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}



/* 安装1页大小的vaddr, 专门针对fork时虚拟地址位图无需操作的情况 */
// 为指定的vaddr分配一物理页, 但无需从虚拟地址内存池中设置位图
void *get_a_page_without_operate_vaddrbitmap(enum pool_flags pf, unsigned int vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    lock_acquire(&mem_pool->lock);
    void *page_phyaddr = palloc(mem_pool);
    if(page_phyaddr == NULL)
    {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}



/* 得到虚拟地址所映射到物理地址 */
// 先得到虚拟地址vaddr所映射到的物理页框起始地址，也就是页表中vaddr所在的pte中记录的那个物理页地址
// 再将vaddr的低12位与此值相加
unsigned int addr_v2p(unsigned int vaddr)
{
    unsigned int *pte = pte_ptr(vaddr);
    
    // pte为该虚拟地址对应的页表项所在地址，(*pte)是该虚拟地址指向的物理页框基地址
    // 去掉其低12位的页表属性值 + 虚拟地址vaddr低12位(偏移地址)
    return (*pte & 0xfffff000) + (vaddr & 0x00000fff);
}






/***** 实现 sys_malloc 小内存申请 16 32 64 128 256 512 1024*****/


/* 返回arena中第index个内存块的地址 */
static struct mem_block *arena2block(struct arena *ar, unsigned int index)
{
    return (struct mem_block *) \
           ((unsigned int)ar + sizeof(struct arena) + index * ar->desc->block_size);
}


/* 返回内存块block所在的arena地址 */
static struct arena *block2arena(struct mem_block *bk)
{
    // mem_block 是在页框的基础上做的细分, &0xffff_f000 即得到对应页框的基地址
    return (struct arena *) ((unsigned int)bk & 0xfffff000);
}




/* 在堆中申请size字节内存 */
void *sys_malloc(unsigned int size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    unsigned int pool_size;
    struct mem_block_desc *descs;
    
    struct task_struct *current_thread = running_thread();
    
    // 判断用哪个内存池
    if(current_thread->pgdir == NULL)   // 若为内核线程
    {
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    }
    else    // 用户进程PCB中的pgdir会在为其分配页表时创建
    {
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = current_thread->u_block_desc;
    }
    
    // 若申请的内存不在内存池容量范围内, 则直接返回NULL
    if(!(size > 0 && size < pool_size))
        return NULL;
    
    struct arena *ar;
    struct mem_block *bk;
    lock_acquire(&mem_pool->lock);
    
    // 超过最大内存块1024, 就直接分配4KB页
    // 本项目中支持7种规格: 16 32 64 128 256 512 1024
    if(size > 1024){
        // 向上取整需要的页框数
        unsigned page_count = \
            DIV_ROUND_UP(size + sizeof(struct arena), PAGE_SIZE);
            
        ar= malloc_page(PF, page_count);
        if(ar != NULL)  // 前12字节的元信息结构也将被清0
            memset(ar, 0, page_count * PAGE_SIZE);
        
        // 对于分配的大页框, 将desc置为NULL, count为页框数, large置为true
        ar->desc = NULL;
        ar->count = page_count;
        ar->large = true;
        lock_release(&mem_pool->lock);
        
        // 跨过arena大小, 把剩下的内存返回
        return (void *)(ar + 1);    // +1, 跨过一个struct arena大小, 即跨过arena元信息
    }
    else    // 若申请的内存<=1024, 则在各种规格的 mem_block_desc 中适配
    {
        unsigned char desc_index;
        
        // 在内存块描述符中匹配合适的内存块规格
        for(desc_index=0; desc_index < MEM_DESC_COUNT; desc_index++)
        {
            if(size <= descs[desc_index].block_size)
                break;
        }
        
        // 若 mem_block_desc 的 free_list中没有可用的 mem_block, 则创建新的arena提供mem_block
        if(list_empty(&descs[desc_index].free_list))
        {
            ar = malloc_page(PF, 1);    // 分配一页框作为arena
            if(ar == NULL)
            {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(ar, 0, PAGE_SIZE);
            
            // 对于分配的小块内存, 将desc置为相应内存块描述符, 
            // count置为此arena可用的内存块数, large置为false
            ar->desc = &descs[desc_index];
            ar->large = false;
            ar->count = descs[desc_index].blocks_per_arena;
            unsigned int block_index;
            
            enum intr_status old_status = intr_disable();   // 关中断
            
            // 开始将arena拆分成内存块, 并添加到内存块描述符的free_list
            for(block_index = 0; block_index < descs[desc_index].blocks_per_arena; block_index++)
            {
                bk = arena2block(ar, block_index);
                ASSERT(!elem_find(&ar->desc->free_list, &bk->free_elem));
                list_append(&ar->desc->free_list, &bk->free_elem);
            }
            intr_set_status(old_status);
        }
        
        // 开始分配内存块
        bk = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_index].free_list)));
        memset(bk, 0, descs[desc_index].block_size);    // 这里也会覆盖内存块中 struct list_elem free_elem 变量
        
        ar = block2arena(bk);   // 获取内存块bk所在的arena
        ar->count--;            // 将此arena中的空闲内存块数减1
        
        lock_release(&mem_pool->lock);
        return (void *)bk;
    }
}





/***** 释放内存 *****/

/* 将物理地址 pg_phy_addr 回收到物理内存池 */
static void pfree(unsigned int pg_phy_addr)
{
    struct pool *mem_pool;
    unsigned int bit_index = 0;
    
    // 本项目中物理内存配置为32M, 减去起始的2M, 内核/用户各占15M物理内存
    // 其中低15M分配给内核, 高15M给用户
    if(pg_phy_addr >= user_pool.phy_addr_begin)     // 用户物理内存池
    {
        mem_pool = &user_pool;
        bit_index = (pg_phy_addr - user_pool.phy_addr_begin) / PAGE_SIZE;
    }
    else    // 内核物理内存池
    {
        mem_pool = &kernel_pool;
        bit_index = (pg_phy_addr - kernel_pool.phy_addr_begin) / PAGE_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_index, 0);   // 将位图中该位清0
}


/* 去掉页表中虚拟地址 vaddr 的映射, 只去掉 vaddr 对应的pte */
static void page_table_pte_remove(unsigned int vaddr)
{
    unsigned int *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;    // 将页表项pte的P位置0
    
    // 刷新快表TLB
    // 快表 TLB, 页表的高速缓存, TLB是处理器提供的、用于加速虚拟地址到物理地址的转换过程
    // 更新TLB有2种方式: 1) invlpg指令更新单条虚拟地址条目; 2) 重新加载 cr3, 这将直接清空TLB, 相当于更新整个页表
    // "m" 内存约束
    asm volatile("invlpg %0" : : "m" (vaddr) : "memory");   // 更新TLB
}


/* 在虚拟地址池中释放以 vaddr 起始的连续 page_count 个虚拟页地址 */
static void vaddr_remove(enum pool_flags pf, void *_vaddr, unsigned int page_count)
{
    unsigned int bit_index_begin = 0, vaddr = (unsigned int)_vaddr, counting = 0;
    
    if(pf == PF_KERNEL)     // 内核虚拟内存池
    {
        bit_index_begin = (vaddr - kernel_vaddr.vaddr_begin) / PAGE_SIZE;
        while(counting < page_count)
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_index_begin + counting++, 0);
    }
    else    // 用户虚拟内存池
    {
        struct task_struct *current_thread = running_thread();
        bit_index_begin = (vaddr - current_thread->user_vaddr.vaddr_begin) / PAGE_SIZE;
        while(counting < page_count)
            bitmap_set(&current_thread->user_vaddr.vaddr_bitmap, bit_index_begin + counting++, 0);
    }
}


/* 释放以虚拟地址vaddr为起始的count个页框 */
void mfree_page(enum pool_flags pf, void *_vaddr, unsigned int page_count)
{
    unsigned int vaddr = (unsigned int)_vaddr, counting = 0;
    ASSERT(page_count >= 1 && vaddr % PAGE_SIZE == 0);
    
    unsigned int pg_phy_addr = addr_v2p(vaddr);     // 获取虚拟地址 vaddr 对应的物理地址
    
    /* 确保待释放的物理内存在 ( 低端1MB内存 + 1KB的页目录 + 1KB的页表 ) 地址范围外 */
    ASSERT((pg_phy_addr % PAGE_SIZE) == 0 && pg_phy_addr >= 0x102000 ); // 1MB + 1KB + 1KB
    
    // 判断 pg_phy_addr 属于用户物理内存池还是内核物理内存池
    // 本项目中物理内存配置为32M, 减去起始的2M, 内核/用户各占15M物理内存
    // 其中低15M分配给内核, 高15M给用户
    if(pg_phy_addr >= user_pool.phy_addr_begin)     // 用户物理内存池, 即 user_pool
    {
        // 循环处理 page_count 个物理页
        while(counting < page_count)
        {
            // 确保物理地址属于用户物理内存地址
            ASSERT((pg_phy_addr % PAGE_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_begin);
            
            // 先将对应的物理页框归还到内存池
            pfree(pg_phy_addr);
            
            // 再从页表中清除此虚拟地址所在的页表项pte
            page_table_pte_remove(vaddr);
            
            vaddr += PAGE_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            
            counting++;
        }
        // 清空虚拟地址的位图中的相应位
        vaddr_remove(pf, _vaddr, page_count);
    }
    else    // 位于kernel_pool内存池
    {
        while(counting < page_count)
        {
            // 确保待释放的物理内存只属于内核物理内存池
            ASSERT((pg_phy_addr % PAGE_SIZE) == 0 && 
                    pg_phy_addr >= kernel_pool.phy_addr_begin && 
                    pg_phy_addr < user_pool.phy_addr_begin);
                    
            // 先将对应的物理页框归还到内存池
            pfree(pg_phy_addr);
            
            // 再从页表中清除此虚拟地址所在的页表项pte
            page_table_pte_remove(vaddr);
            
            vaddr += PAGE_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            
            counting++;
        }
        // 清空虚拟地址的位图中的相应位
        vaddr_remove(pf, _vaddr, page_count);
    }
}


/* 回收内存, 释放vaddr指向的内存 */
// 释放大内存, 把页框在虚拟内存池和物理内存池的位图中将相应位置0
// 回收小内存，将arena中的内存块重新放回到内存块描述符中的空闲块链表free_list
void sys_free(void *vaddr)
{
    ASSERT(vaddr != NULL);
    if(vaddr != NULL)
    {
        enum pool_flags PF;
        struct pool *mem_pool;
        
        // 判断是线程还是进程
        if(running_thread()->pgdir == NULL)     // 线程
        {
            ASSERT((unsigned int)vaddr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        }
        else
        {
            PF = PF_USER;
            mem_pool = &user_pool;
        }
        
        lock_acquire(&mem_pool->lock);
        
        // 把 mem_block 转换为 arena, 获取元信息
        struct mem_block *bk = vaddr;
        struct arena *ar = block2arena(bk);
        
        ASSERT(ar->large == 0 || ar->large == 1);
        
        if(ar->desc == NULL && ar->large == true)   // 大于1024的大内存
            mfree_page(PF, ar, ar->count);
        else    // 小于等于1024的小内存块
        {
            // 先将内存块回收到 free_list
            list_append(&ar->desc->free_list, &bk->free_elem);
            
            // 再判断此 arena 中的内存块是否都是空闲, 如果是就释放arena
            if(++ar->count == ar->desc->blocks_per_arena)
            {
                unsigned int block_index;
                // for循环, 将此arena中所有的内存块从对应的内存块描述符的free_list中去掉
                for(block_index = 0; block_index < ar->desc->blocks_per_arena; block_index++)
                {
                    struct mem_block *bk = arena2block(ar, block_index);
                    ASSERT(elem_find(&ar->desc->free_list, &bk->free_elem));
                    list_remove(&bk->free_elem);
                }
                mfree_page(PF, ar, 1);  // 释放此arena
            }
        }
        lock_release(&mem_pool->lock);
    }
}


/* 根据物理页框地址 page_phy_addr 在相应的内存池的位图清0, 不改动页表 */
void free_a_phy_page(unsigned int page_phy_addr)
{
    struct pool *mem_pool;
    unsigned int bit_index = 0;
    if(page_phy_addr >= user_pool.phy_addr_begin)
    {
        mem_pool = &user_pool;
        bit_index = (page_phy_addr - user_pool.phy_addr_begin) / PAGE_SIZE;
    }
    else
    {
        mem_pool = &kernel_pool;
        bit_index = (page_phy_addr - kernel_pool.phy_addr_begin) / PAGE_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_index, 0);
}
