/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "spike_interface/spike_utils.h"

// 准备一个静态数组用于内核维护用户态内存块节点
#define MAX_HEAP_BLOCKS 1024
static memory_block block_pool[MAX_HEAP_BLOCKS];
static int block_pool_count = 0;

// 从静态池中获取一个新的控制节点
memory_block* alloc_block_node() {
    if (block_pool_count < MAX_HEAP_BLOCKS) {
        return &block_pool[block_pool_count++];
    }
    return NULL;
}

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  shutdown(code);
}

//
// modified for lab2_challenge2_singlepageheap & cross page
// memory allocation within dynamically expanding pages
//
uint64 sys_user_allocate_page(uint64 size) {
  // 【修复】：将变量声明提前到 label 之前
  memory_block *curr;

retry:
  // 第一步：First-Fit 策略：遍历链表寻找满足大小的空闲块
  curr = current->heap_list; // 标签后面紧跟赋值语句（合法）
  
  while (curr != NULL) {
      if (curr->is_free && curr->size >= size) {
          // 找到了足够的空闲块，看看是否需要切分
          if (curr->size > size) { 
              memory_block *new_block = alloc_block_node();
              if (new_block) {
                  new_block->va = curr->va + size;
                  new_block->size = curr->size - size;
                  new_block->is_free = 1;
                  new_block->next = curr->next;
                  
                  curr->size = size;
                  curr->next = new_block;
              }
          }
          curr->is_free = 0; // 标记被占用
          return curr->va;   // 返回给应用精细化的虚拟首地址
      }
      curr = curr->next;
  }
  
  // 第二步：如果没有找到合适的空闲块（当前空间耗尽或碎片化），申请新的一页物理内存
  void* pa = alloc_page();
  if (pa == NULL) {
      panic("Physical memory exhausted!\n");
  }
  
  uint64 va = g_ufree_page;
  g_ufree_page += PGSIZE;
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
  
  memory_block *init_block = alloc_block_node();
  if (!init_block) panic("Max heap block nodes reached!\n");
  
  init_block->va = va;
  init_block->size = PGSIZE;
  init_block->is_free = 1;
  init_block->next = NULL;

  // 将新的一页挂载到链表尾部
  if (current->heap_list == NULL) {
      current->heap_list = init_block;
  } else {
      memory_block *tail = current->heap_list;
      while (tail->next != NULL) {
          tail = tail->next;
      }
      // 【关键合并】：如果尾部块也是空闲的，且虚拟地址上与新申请的页相连，直接合并扩容
      if (tail->is_free && tail->va + tail->size == init_block->va) {
          tail->size += PGSIZE;
          block_pool_count--; // 回收未使用的分配节点
      } else {
          tail->next = init_block;
      }
  }
  
  // 扩充了一页空间后，跳回开头重新尝试分配
  goto retry;
}

//
// modified for lab2_challenge2_singlepageheap
// reclaim a memory block and coalesce adjacent free blocks.
//
uint64 sys_user_free_page(uint64 va) {
  memory_block *curr = current->heap_list;
  // 查找对应地址并标记为空闲
  while (curr != NULL) {
      if (curr->va == va) {
          curr->is_free = 1;
          break;
      }
      curr = curr->next;
  }

  // 合并（Coalesce）链表中相邻且皆为空闲的内存块
  memory_block *temp = current->heap_list;
  while (temp != NULL) {
      if (temp->is_free && temp->next != NULL && temp->next->is_free) {
          temp->size += temp->next->size;
          temp->next = temp->next->next;
      } else {
          temp = temp->next;
      }
  }
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_allocate_page:
      return sys_user_allocate_page(a1); // 传入应用所需的分配字节大小
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}