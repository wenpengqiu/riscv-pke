#include "util/string.h"
#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "spike_interface/spike_file.h"
extern process* current;

void print_source_line(const char* file_path, int target_line) {
  spike_file_t* f = spike_file_open(file_path, O_RDONLY, 0);
  if (IS_ERR_VALUE(f)) return;

  char buf[256];
  int current_line = 1;
  int pos = 0;
  char ch;

  sprint("  "); // 输出缩进

  // 逐字节读取文件，计算换行符，找到目标行
  while (spike_file_read(f, &ch, 1) > 0) {
    if (ch == '\n') {
      if (current_line == target_line) {
        buf[pos] = '\0';
        sprint("%s\n", buf);
        break;
      }
      current_line++;
      pos = 0; 
    } else {
      if (current_line == target_line && pos < sizeof(buf) - 1) {
        buf[pos++] = ch;
      }
    }
  }
  spike_file_close(f);
}

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() {
  uint64 epc = read_csr(mepc);
  
  // 请根据你分支中 process 结构体的具体字段名（如 line_ind, file, dir 等）进行替换
  int match_index = -1;
  // 遍历寻找包含该指令地址的代码行
  // 假设 current->line_length 是 line 数组的长度，或者以 addr == 0 结尾
  for (int i = 0; current->line[i].addr != 0; i++) {
    if (current->line[i].addr == epc || 
       (current->line[i].addr < epc && current->line[i+1].addr > epc)) {
       match_index = i;
       break;
    }
  }

  if (match_index != -1) {
    int file_idx = current->line[match_index].file;
    int dir_idx = current->file[file_idx].dir;
    
    char* dir_name = current->dir[dir_idx];
    char* file_name = current->file[file_idx].file;
    int line_num = current->line[match_index].line;

    // 格式化出文件完整路径
    char full_path[256];
    // 手动拼接 dir_name 和 file_name
    strcpy(full_path, dir_name);
    int dir_len = strlen(dir_name);
    full_path[dir_len] = '/';
    strcpy(full_path + dir_len + 1, file_name);

    sprint("Runtime error at %s:%d\n", full_path, line_num);
    
    // 打印具体的代码内容
    print_source_line(full_path, line_num);
  }

  sprint("Illegal instruction!\n");
  shutdown(-1);
}

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

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
