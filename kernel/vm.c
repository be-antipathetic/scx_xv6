#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
   重写 kvminit ，将 UART、virtio等内核映射抽离出来，为每个进程的内核页表
   重新映射一次
 */
void
kvminit()
{
  // 全局内核页表使用 kvminit 来初始化
  kernel_pagetable = scx_kvminit_newpgtbl();

}
//抽离出来的 UART、virtio 等映射代码
void
scx_kvm_map_pagetable(pagetable_t pagetable)
{
  // uart registers
  kvmmap(pagetable,UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(pagetable,VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(pagetable,CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(pagetable,PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(pagetable,KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(pagetable,(uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(pagetable,TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

}
// 创建内核页表,其他进程也可以使用此函数创建自己的内核页表
pagetable_t
scx_kvminit_newpgtbl()
{
   //分配一个物理内存页来保存根页表页
   pagetable_t pagetable = (pagetable_t)kalloc();
   memset(pagetable,0,PGSIZE); // 将根页表页全部填充为0,填充4096个字节的0
   
   scx_kvm_map_pagetable(pagetable); //向内核页表加载

   return pagetable;
}
// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  //cpu使用 kernel_pagetable 做地址翻译
  w_satp(MAKE_SATP(kernel_pagetable));
  //每个cpu在TLB中缓存了pte，
  //当xv6系统需要更换页表时，必须使cpu禁止tlb中的pte，risv有一个指令
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  //如果超过虚拟内存可以表达的最大值则发生报错
  if(va >= MAXVA)
    panic("walk");

  //读取虚拟地址中的 level 指示位，循环两次
  for(int level = 2; level > 0; level--) {
    // PX 宏用来提取对应 level 的 9bit 数据
    // pagetable[n] 代表从起始位置偏移n个 uint64 大小的内存单元取出的内容
    pte_t *pte = &pagetable[PX(level, va)];
    // 如果 pte 存在
    if(*pte & PTE_V) {
      //提取对应的物理地址,将页表指针指向更新
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      //如果pte不存在，alloc 为标志位，指定是否允许分配新页表
      //kalloc() 分配一个新的物理内存页. pagetable 指向下一级页表的第一个页表项
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      //新页表初始化
      memset(pagetable, 0, PGSIZE);
      //更新旧页表项中的内容，将其中的ppn更新为新页表的地址
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  //此时 pagetable 指针指向最后一级页表首, pagetable[PX(0, va)]取最后一级页表的页表项
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages. 
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
// 修改 kvmmap ，将本来只用于内核页表的 map 更改为可以对任何页表进行
void
kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  // 调用 mappages 函数在 pagetable 页表中建立虚拟地址到物理地址的映射
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
// 修改 kvmpa ，原来只处理内核进程的页表，现在可以处理其他进程的内核页表
uint64
kvmpa(pagetable_t pagetable , uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(pagetable, va, 0);  // kernel_pagetable 改为参数 pagetable
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  //无条件循环
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    // 当前 pte 已经有对应的映射关系
    if(*pte & PTE_V)
      panic("remap");
    // 将物理地址填充至虚拟地址对应页表项
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    // 继续下一页的映射
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0); //直接使用 srcva 也能得到同一块物理内存
    if(pa0 == 0)
      return -1;
    //计算本页从 scrva 开始还剩多少字节.确保复制的 n 在同一页内存里
    n = PGSIZE - (srcva - va0);
    //需要复制的字节数 len 做比较，取较小的值作为本次实际要复制的字节数 
    if(n > len)
      n = len;
    // 由于内核页表的直接映射特性，dst 不需要转换成物理地址.
    memmove(dst, (void *)(pa0 + (srcva - va0)), n); // 从 dst 开始拷贝数据

    len -= n; // len 减去已经拷贝好的值
    dst += n; // dst 更新
    srcva = va0 + PGSIZE; //切换到下一页
  } 
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      // 如果判断字符为 '\0'，则结束复制
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

//递归打印页表
int
scx_pgtblprint( pagetable_t pagetable , int depth)
{
  //遍历每个页表，每个页表有512项 pte
  for ( int i = 0 ; i<512 ; i++)
  {
    pte_t pte = pagetable[i]; // 获取页表项, pagetable[i]=*(pagetable+i)
    
    if(pte & PTE_V){ // 如果获取的页表项有效，则打印出来
      printf("..") ; //根页表打印出一个 .. 符号
      //递归调用时，根据深度打印
      for (int j =0 ; j< depth ; j++)
      printf("..");
      // 打印出页表项中的内容
      printf("%d: pte %p pa %p\n" , i, pte,PTE2PA(pte)) ;
      //如果该节点不是叶节点，递归打印叶节点
      //当一个页表项指向的是一个最终的内存页时，会设置相应的访问权限
      if((pte &(PTE_R | PTE_W | PTE_X)) == 0 )
      {
        //代表还有下一级页表， PTE2PA(pte) 已经是一个地址值
        pagetable_t nextpagetable =(pagetable_t)PTE2PA(pte);
        scx_pgtblprint(nextpagetable,depth+1);
      }
    }

  }
  return 0 ;
}
//打印页表
int  scx_vmprint(pagetable_t pagetable)
{
  printf("page table %p\n",pagetable);
  return scx_pgtblprint(pagetable,0);

}

//递归释放一个内核页表中的所有映射，但是不释放其指向的物理页
void 
scx_kvm_free_kernelpgtbl(pagetable_t pagetable){
  for(int i= 0 ; i<512 ; i++)
  {
    pte_t pte = pagetable[i]; //获取页表项目
    uint64 child = PTE2PA(pte);
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)
    {
      scx_kvm_free_kernelpgtbl((pagetable_t)child);
      pagetable[i]=0;  // 递归调用结束后，将页表项也置0
    }
  }
    kfree((void*)pagetable); // 释放当前级别页表所占用空间
  
}