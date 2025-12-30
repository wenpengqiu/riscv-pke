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
#include "elf.h"

#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"

void print_func_name(uint64 addr) {
    spike_file_t *f = current->elf_file;
    elf_header ehdr;
    
    // 1. 读取 ELF Header
    spike_file_pread(f, &ehdr, sizeof(ehdr), 0);

    // 2. 读取 Section Header String Table (shstrtab) 的信息
    // shstrndx 是存储“段名字符串表”的段索引
    elf_sect_header shstrtab_header;
    spike_file_pread(f, &shstrtab_header, sizeof(shstrtab_header), 
                     ehdr.shoff + ehdr.shstrndx * sizeof(elf_sect_header));

    // 准备缓冲区读取段名
    char shstrtab_buf[shstrtab_header.size];
    spike_file_pread(f, shstrtab_buf, shstrtab_header.size, shstrtab_header.offset);

    // 3. 寻找 .symtab 和 .strtab 段的位置
    elf_sect_header symtab_sh, strtab_sh;
    int found_sym = 0, found_str = 0;
    elf_sect_header temp_sh;

    for (int i = 0; i < ehdr.shnum; i++) {
        spike_file_pread(f, &temp_sh, sizeof(temp_sh), ehdr.shoff + i * sizeof(elf_sect_header));
        // shstrtab_buf + temp_sh.name 就是该段的名字
        if (strcmp(shstrtab_buf + temp_sh.name, ".symtab") == 0) {
            symtab_sh = temp_sh;
            found_sym = 1;
        } else if (strcmp(shstrtab_buf + temp_sh.name, ".strtab") == 0) {
            strtab_sh = temp_sh;
            found_str = 1;
        }
    }

    if (!found_sym || !found_str) {
        sprint("Symbols not found.\n");
        return;
    }

    // 4. 遍历 Symbol Table 查找地址对应的符号
    int num_syms = symtab_sh.size / sizeof(elf_symbol);
    elf_symbol sym;
    
    for (int i = 0; i < num_syms; i++) {
        spike_file_pread(f, &sym, sizeof(sym), symtab_sh.offset + i * sizeof(elf_symbol));
        
        if (sym.value <= addr && addr < sym.value + sym.size) {
            char func_name[64];
            spike_file_pread(f, func_name, 64, strtab_sh.offset + sym.name);
            sprint("%s\n", func_name);
            return;
        }
    }
    sprint("Unknown function at %p\n", addr);
}


ssize_t sys_user_backtrace(int64 depth) {
    uint64 current_fp = current->trapframe->regs.s0;
    
    // 1. 检查文件句柄
    if (current->elf_file == NULL) {
        sprint("Backtrace Fail: ELF file handle is NULL.\n");
        return -1;
    }

    if (current_fp != 0) {
         current_fp = *(uint64*)(current_fp - 8);
    }

    for (int i = 0; i < depth; i++) {
        if (current_fp == 0) break;
        
        uint64 current_ra = *(uint64*)(current_fp - 8);
        uint64 next_fp    = *(uint64*)(current_fp - 16);

        if (current_ra == 0) break;

        // 打印函数名
        print_func_name(current_ra);

        // 更新指针
        current_fp = next_fp;
    }

    return 0;
}


//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
