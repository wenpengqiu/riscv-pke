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

#include "spike_interface/spike_utils.h"
#include "riscv.h"     // 修复缺少 read_tp() 的问题
#include "config.h"   // 修复缺少 NCPU 的问题

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  uint64 hartid = read_tp();
  sprint("hartid = %ld: %s", hartid, buf);
  return 0;
}

static int core_exited[NCPU] = {0, 0};

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  uint64 hartid = read_tp();
  sprint("hartid = %ld: User exit with code:%ld.\n", hartid, code);
  
  core_exited[hartid] = 1;

  // 【关键修复】将检查逻辑放到 while 循环内部！
  // 这样无论哪个核先退出，0号核都能在循环(被中断唤醒)中敏锐地察觉并执行 shutdown。
  while (1) {
    if (core_exited[0] && core_exited[1]) {
      if (hartid == 0) {
        sprint("hartid = %ld: shutdown with code:%ld.\n", hartid, code);
        shutdown(code);
      }
    }
    // 让出CPU，等待中断唤醒，避免纯死循环占满宿主机单核性能
    asm volatile("wfi"); 
  }
  
  return 0; 
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2); // 恢复打印系统调用
    case SYS_user_exit:
      return sys_user_exit(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}