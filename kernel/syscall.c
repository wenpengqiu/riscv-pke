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

ssize_t sys_user_print(const char* buf, size_t n) {
  uint64 hartid = read_tp();
  assert( current[hartid] );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current[hartid]->pagetable), (void*)buf);
  // 【修复】：去掉 spin_lock，允许并发顺畅打印
  sprint(pa);
  return 0;
}

static int core_exited[NCPU] = {0, 0};

ssize_t sys_user_exit(uint64 code) {
  uint64 hartid = read_tp();
  sprint("hartid = %ld: User exit with code: %ld.\n", hartid, code);
  core_exited[hartid] = 1;

  while (1) {
    if (core_exited[0] && core_exited[1]) {
      if (hartid == 0) {
        sprint("hartid = 0: shutdown with code: %ld.\n", code);
        shutdown(code);
      }
    }
    asm volatile("wfi");
  }
  return 0; 
}

uint64 sys_user_allocate_page() {
  uint64 hartid = read_tp();
  void* pa = alloc_page(); // alloc_page 内部有针对链表的锁，安全
  
  uint64 va = current[hartid]->ufree_page;
  current[hartid]->ufree_page += PGSIZE;
  
  user_vm_map((pagetable_t)current[hartid]->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
         
  // 【修复】：去掉 spin_lock，使用精准格式匹配
  sprint("hartid = %ld: vaddr 0x%08x is mapped to paddr 0x%x\n", hartid, (uint32)va, (uint32)(uint64)pa);
  return va;
}

uint64 sys_user_free_page(uint64 va) {
  uint64 hartid = read_tp();
  user_vm_unmap((pagetable_t)current[hartid]->pagetable, va, PGSIZE, 1);
  return 0;
}

long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}