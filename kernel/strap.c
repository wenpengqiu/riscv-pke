/*
 * Utility functions for trap handling in Supervisor mode.
 */
#ifndef PTE_A
#define PTE_A (1L << 6)
#endif
#ifndef PTE_D
#define PTE_D (1L << 7)
#endif

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "util/functions.h"
#include "util/string.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // TODO (lab1_1): remove the panic call below, and call do_syscall (defined in
  // kernel/syscall.c) to conduct real operations of the kernel side for a syscall.
  // IMPORTANT: return value should be returned to user app, or else, you will encounter
  // problems in later experiments!
  // panic( "call do_syscall to accomplish the syscall and lab1_1 here.\n" );
  tf->regs.a0 = do_syscall(tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3, tf->regs.a4, tf->regs.a5, tf->regs.a6, tf->regs.a7);

}

//
// global variable that store the recorded "ticks". added @lab1_3
static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  // panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
  g_ticks++;
  write_csr(sip, 0);

}

//
// the page fault handler. added @lab2_3. parameters:
// sepc: the pc when fault happens;
// stval: the virtual address that causes pagefault when being accessed.
//
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
  sprint("handle_page_fault: %lx\n", stval);
  switch (mcause) {
    case CAUSE_STORE_PAGE_FAULT: {
      pte_t *pte = lookup_pte(current->pagetable, stval);
      
      // 如果是 COW 触发的 Store Fault
      if (pte && (*pte & PTE_COW)) {
          void *pa = (void *)PTE2PA(*pte);
          int ref = get_page_ref(pa);
          
          if (ref > 1) {
              // 有其他进程共享该页，分配新页并拷贝数据
              void *new_pa = alloc_page();
              memcpy(new_pa, pa, PGSIZE);
              
              // 释放对原物理页的引用
              free_page(pa);
              
              // 重新构造 PTE：指向新物理页，恢复可写(PTE_W)，移除 COW
              // 【核心修复】：必须同时打上 Accessed (PTE_A) 和 Dirty (PTE_D) 标记！
              uint64 flags = *pte & 0x3FF; // 获取原本权限位
              flags |= PTE_W | PTE_A | PTE_D; 
              flags &= ~PTE_COW;
              *pte = PA2PTE((uint64)new_pa) | flags;
          } else {
              // 引用计数为 1，说明别的共享者都已经退出了，此时只需恢复权限
              *pte |= PTE_W | PTE_A | PTE_D;
              *pte &= ~PTE_COW;
          }
          flush_tlb();
      } else {
          // 常规按需缺页分配
          user_vm_map((pagetable_t) current->pagetable, 
                      stval - stval % PGSIZE, PGSIZE, 
                      (uint64) alloc_page(), 
                      prot_to_type(PROT_WRITE | PROT_READ, 1));
      }
      break;
    }
    default:
      sprint("unknown page fault.\n");
      break;
  }
}

//
// implements round-robin scheduling. added @lab3_3
//
void rrsched() {
  // TODO (lab3_3): implements round-robin scheduling.
  // hint: increase the tick_count member of current process by one, if it is bigger than
  // TIME_SLICE_LEN (means it has consumed its time slice), change its status into READY,
  // place it in the rear of ready queue, and finally schedule next process to run.
  // panic( "You need to further implement the timer handling in lab3_3.\n" );
  if (++current->tick_count >= TIME_SLICE_LEN) {
    current->tick_count = 0;       
    current->status = READY;      
    insert_to_ready_queue(current);
    schedule();
  }

}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  // save user process counter.
  current->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      // invoke round-robin scheduler. added @lab3_3
      rrsched();
      break;
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      // the address of missing page is stored in stval
      // call handle_user_page_fault to process page faults
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }

  // continue (come back to) the execution of current process.
  switch_to(current);
}
