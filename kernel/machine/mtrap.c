#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() {
  uint64 epc = read_csr(mepc);
  uint64 cause = read_csr(mcause);
  uint64 tval = read_csr(mtval);
  sprint("Illegal instruction at mepc: %lx, mcause: %lx, mtval: %lx\n", epc, cause, tval);
  panic("Illegal instruction!");
}

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { 
  // 获取当前发生异常的精确指令地址！
  uint64 epc = read_csr(mepc);
  // 新加：捕获进入 S 态前的真正的死因与地址！！！
  uint64 s_cause = read_csr(scause);
  uint64 s_epc   = read_csr(sepc);
  // ✨再新加：获取进入 S 态时的 t6（那里保存了报错那一刻寄存器 ra 的值）
  uint64 s_ra = 0;
  // 通过汇编把 sscratch (原来指向 trapframe，但在发生错误嵌套前保存了一些东西) 当作线索。
  // 但更简单的方式是我们直接通过堆栈！因为此刻是刚刚死掉。
  // 这里其实不需要复杂的汇编，我们通过 RISC-V 內建的编译器辅助函数获得 ra 
  uint64 current_ra = (uint64)__builtin_return_address(0);

  sprint("Misaligned AMO at mepc: %lx\n", epc);
  sprint("REAL TRAP was: scause: %lx, sepc: %lx\n", s_cause, s_epc);
  // 注意，如果你能够直接在 gdb 或者 objdump 里向上看，你会发现上一个地址。
  panic("Misaligned AMO!"); 
}

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      // panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction();

      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
