/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// current points to the currently running user-mode application.
process* current[NCPU] = {NULL, NULL};
int vm_alloc_stage[NCPU] = {0, 0};

// points to the first free page in our simple heap. added @lab2_2
// uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  uint64 hartid = read_tp();
  assert(proc);
  current[hartid] = proc;

  write_csr(stvec, (uint64)smode_trap_vector);

  proc->trapframe->kernel_sp = proc->kstack;
  proc->trapframe->kernel_satp = read_csr(satp);
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  
  x |= SSTATUS_SPIE;  
  write_csr(sstatus, x);

  write_csr(sepc, proc->trapframe->epc);

  // 【必须】：恢复硬件线程 ID
  proc->trapframe->regs.tp = hartid;

  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // 标记当前核进入了用户态的执行与内存分配阶段
  vm_alloc_stage[hartid] = 1;

  return_to_user(proc->trapframe, user_satp);
}
