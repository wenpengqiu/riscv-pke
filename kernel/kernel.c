/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "string.h"
#include "elf.h"
#include "process.h"

#include "spike_interface/spike_utils.h"
#include "spike_interface/atomic.h"

volatile int app_load_sync = 0;
// process is a structure defined in kernel/process.h
process user_app[NCPU]; // 进程池改为数组

//
// load the elf, and construct a "process" (with only a trapframe).
// load_bincode_from_host_elf is defined in elf.c
//
void load_user_program(process *proc) {
  // 获取当前硬件线程的 ID (核号)
  uint64 hartid = read_tp(); 

  // 使用带 hartid 参数的宏，为当前核分配独立的内存空间
  proc->trapframe = (trapframe *)USER_TRAP_FRAME(hartid);
  memset(proc->trapframe, 0, sizeof(trapframe));
  proc->kstack = USER_KSTACK(hartid);
  proc->trapframe->regs.sp = USER_STACK(hartid);

  proc->trapframe->regs.tp = hartid;
  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(proc);
}

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
int s_start(void) {
  uint64 hartid = read_tp();
  sprint("hartid = %ld: Enter supervisor mode...\n", hartid);
  write_csr(satp, 0);

  // 严格串行化 ELF 加载过程，防止多个核同时底层冲突
  if (hartid == 0) {
    load_user_program(&user_app[0]);
    app_load_sync = 1; // 0号核加载完毕，通知 1 号核开始
    mb();
  } else {
    while (app_load_sync == 0) { mb(); } // 1号核等待 0 号核
    load_user_program(&user_app[1]);
  }

  sprint("hartid = %ld: Switch to user mode...\n", hartid);
  switch_to(&user_app[hartid]);
  return 0;
}
