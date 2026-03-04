/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "string.h"
#include "elf.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"
#include "spike_interface/atomic.h"
#include "config.h"

process user_app[NCPU];
extern char trap_sec_start[];

volatile int app_load_sync = 0;

void enable_paging() {
  write_csr(satp, MAKE_SATP(g_kernel_pagetable));
  flush_tlb();
}

void load_user_program(process *proc) {
  uint64 hartid = read_tp();
  sprint("hartid = %ld: User application is loading.\n", hartid);

  proc->trapframe = (trapframe *)alloc_page();
  memset(proc->trapframe, 0, sizeof(trapframe));

  proc->pagetable = (pagetable_t)alloc_page();
  memset((void *)proc->pagetable, 0, PGSIZE);

  proc->kstack = (uint64)alloc_page() + PGSIZE;   
  uint64 user_stack = (uint64)alloc_page();       

  proc->trapframe->regs.sp = USER_STACK_TOP;  
  proc->ufree_page = USER_FREE_ADDRESS_START;

  sprint("hartid = %ld: user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n", 
          hartid, proc->trapframe, proc->trapframe->regs.sp, proc->kstack);

  load_bincode_from_host_elf(proc);

  user_vm_map((pagetable_t)proc->pagetable, USER_STACK_TOP - PGSIZE, PGSIZE, user_stack,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
  user_vm_map((pagetable_t)proc->pagetable, (uint64)proc->trapframe, PGSIZE, (uint64)proc->trapframe,
         prot_to_type(PROT_WRITE | PROT_READ, 0));
  user_vm_map((pagetable_t)proc->pagetable, (uint64)trap_sec_start, PGSIZE, (uint64)trap_sec_start,
         prot_to_type(PROT_READ | PROT_EXEC, 0));
}

int s_start(void) {
  uint64 hartid = read_tp();
  write_csr(satp, 0);

  if (hartid == 0) {
    sprint("hartid = %ld: Enter supervisor mode...\n", hartid);
    pmm_init();
    app_load_sync = 1; 
    mb();
    
    kern_vm_init();
    app_load_sync = 2; 
    mb();
  } else {
    while (app_load_sync < 2) { mb(); }
    sprint("hartid = %ld: Enter supervisor mode...\n", hartid);
  }

  enable_paging();
  load_user_program(&user_app[hartid]);

  sprint("hartid = %ld: Switch to user mode...\n", hartid);
  switch_to(&user_app[hartid]);

  return 0;
}