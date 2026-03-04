#include "pmm.h"
#include "util/functions.h"
#include "riscv.h"
#include "config.h"
#include "util/string.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

// _end is defined in kernel/kernel.lds, it marks the ending (virtual) address of PKE kernel
extern char _end[];
extern uint64 g_mem_size;

static uint64 free_mem_start_addr;  //beginning address of free memory
static uint64 free_mem_end_addr;    //end address of free memory (not included)

typedef struct node {
  struct node *next;
} list_node;

static list_node g_free_mem_list;

// 【新增】COW 物理页引用计数管理（最高支持128MB内存，按4KB分块约32768页）
#define MAX_PHYS_PAGES 32768
int pa_ref[MAX_PHYS_PAGES] = {0};

static inline int get_pa_index(void *pa) {
    return ((uint64)pa - DRAM_BASE) / PGSIZE;
}

// 增加物理页的引用计数
void add_page_ref(void *pa) {
    pa_ref[get_pa_index(pa)]++;
}

// 获取物理页的引用计数
int get_page_ref(void *pa) {
    return pa_ref[get_pa_index(pa)];
}

//
// actually creates the freepage list.
//
static void create_freepage_list(uint64 start, uint64 end) {
  g_free_mem_list.next = 0;
  for (uint64 p = ROUNDUP(start, PGSIZE); p + PGSIZE < end; p += PGSIZE)
    free_page( (void *)p );
}

//
// place a physical page at *pa to the free list of g_free_mem_list
//
void free_page(void *pa) {
  if (((uint64)pa % PGSIZE) != 0 || (uint64)pa < free_mem_start_addr || (uint64)pa >= free_mem_end_addr)
    panic("free_page 0x%lx \n", pa);

  // 【COW 修改】先递减引用计数，只有当引用计数归零时，才真正回收到空闲链表
  int idx = get_pa_index(pa);
  pa_ref[idx]--;
  
  if (pa_ref[idx] <= 0) {
      list_node *n = (list_node *)pa;
      n->next = g_free_mem_list.next;
      g_free_mem_list.next = n;
  }
}

//
// takes the first free page from g_free_mem_list, and returns (allocates) it.
//
void *alloc_page(void) {
  list_node *n = g_free_mem_list.next;
  if (n) {
      g_free_mem_list.next = n->next;
      // 【COW 修改】初始分配时，引用计数设为 1
      pa_ref[get_pa_index((void*)n)] = 1;
  }

  return (void *)n;
}

//
// pmm_init() establishes the list of free physical pages.
//
void pmm_init() {
  uint64 g_kernel_start = KERN_BASE;
  uint64 g_kernel_end = (uint64)&_end;

  uint64 pke_kernel_size = g_kernel_end - g_kernel_start;
  sprint("PKE kernel start 0x%lx, PKE kernel end: 0x%lx, PKE kernel size: 0x%lx .\n",
    g_kernel_start, g_kernel_end, pke_kernel_size);

  free_mem_start_addr = ROUNDUP(g_kernel_end , PGSIZE);

  g_mem_size = MIN(PKE_MAX_ALLOWABLE_RAM, g_mem_size);
  if( g_mem_size < pke_kernel_size )
    panic( "Error when recomputing physical memory size (g_mem_size).\n" );

  free_mem_end_addr = g_mem_size + DRAM_BASE;
  sprint("free physical memory address: [0x%lx, 0x%lx] \n", free_mem_start_addr,
    free_mem_end_addr - 1);

  sprint("kernel memory manager is initializing ...\n");
  create_freepage_list(free_mem_start_addr, free_mem_end_addr);
}