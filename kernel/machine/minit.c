/*
 * Machine-mode C startup codes
 */

#include "util/types.h"
#include "kernel/riscv.h"
#include "kernel/config.h"
#include "spike_interface/spike_utils.h"
#include "spike_interface/atomic.h"

__attribute__((aligned(16))) char stack0[4096 * NCPU];

extern void s_start();
extern void mtrapvec();
extern uint64 htif;
extern uint64 g_mem_size;
riscv_regs g_itrframe;

void init_dtb(uint64 dtb) {
  query_htif(dtb);
  if (htif) sprint("HTIF is available!\r\n");
  query_mem(dtb);
  sprint("(Emulated) memory size: %ld MB\n", g_mem_size >> 20);
}

static void delegate_traps() {
  if (!supports_extension('S')) return;
  uintptr_t interrupts = MIP_SSIP | MIP_STIP | MIP_SEIP;
  uintptr_t exceptions = (1U << CAUSE_MISALIGNED_FETCH) | (1U << CAUSE_FETCH_PAGE_FAULT) |
                         (1U << CAUSE_BREAKPOINT) | (1U << CAUSE_LOAD_PAGE_FAULT) |
                         (1U << CAUSE_STORE_PAGE_FAULT) | (1U << CAUSE_USER_ECALL);
  write_csr(mideleg, interrupts);
  write_csr(medeleg, exceptions);
}

void timerinit(uintptr_t hartid) {
  *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + TIMER_INTERVAL;
  write_csr(mie, read_csr(mie) | MIE_MTIE);
}

extern volatile int app_load_sync; 

void m_start(uintptr_t hartid, uintptr_t dtb) {
  write_tp(hartid); 

  if (hartid == 0) {
    spike_file_init();
    init_dtb(dtb); 
    // 【修复】：恢复 %ld，防止无格式字符引起的缓冲区末尾字符丢弃
    sprint("In m_start, hartid:%ld\n", hartid);
  } else {
    while (app_load_sync < 1) { mb(); } 
    sprint("In m_start, hartid:%ld\n", hartid);
  }

  write_csr(mscratch, &g_itrframe);
  write_csr(mstatus, ((read_csr(mstatus) & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_S));
  write_csr(mepc, (uint64)s_start);
  write_csr(mtvec, (uint64)mtrapvec);
  write_csr(mstatus, read_csr(mstatus) | MSTATUS_MIE);
  delegate_traps();
  write_csr(sie, read_csr(sie) | SIE_SEIE | SIE_STIE | SIE_SSIE);
  timerinit(hartid);
  asm volatile("mret");
}