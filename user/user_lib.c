/*
 * The supporting library for applications.
 */

#include "user_lib.h"
#include "util/types.h"
#include "util/snprintf.h"
#include "kernel/syscall.h"

uint64 do_user_call(uint64 sysnum, uint64 a1, uint64 a2, uint64 a3, uint64 a4, uint64 a5, uint64 a6,
                 uint64 a7) {
  int ret;
  asm volatile(
      "ecall\n"
      "sw a0, %0"  // returns a 32-bit value
      : "=m"(ret)
      :
      : "memory");
  return ret;
}

int printu(const char* s, ...) {
  va_list vl;
  va_start(vl, s);
  char out[256]; 
  int res = vsnprintf(out, sizeof(out), s, vl);
  va_end(vl);
  const char* buf = out;
  size_t n = res < sizeof(out) ? res : sizeof(out);
  return do_user_call(SYS_user_print, (uint64)buf, n, 0, 0, 0, 0, 0);
}

int exit(int code) {
  return do_user_call(SYS_user_exit, code, 0, 0, 0, 0, 0, 0); 
}

void* naive_malloc() {
  return (void*)do_user_call(SYS_user_allocate_page, 0, 0, 0, 0, 0, 0, 0);
}

void naive_free(void* va) {
  do_user_call(SYS_user_free_page, (uint64)va, 0, 0, 0, 0, 0, 0);
}

int fork() {
  return do_user_call(SYS_user_fork, 0, 0, 0, 0, 0, 0, 0);
}

void yield() {
  do_user_call(SYS_user_yield, 0, 0, 0, 0, 0, 0, 0);
}

// lab3_challenge2: 信号量的用户态系统调用包装
int sem_new(int val) {
  return do_user_call(SYS_user_sem_new, val, 0, 0, 0, 0, 0, 0);
}

void sem_P(int sem_id) {
  do_user_call(SYS_user_sem_P, sem_id, 0, 0, 0, 0, 0, 0);
}

void sem_V(int sem_id) {
  do_user_call(SYS_user_sem_V, sem_id, 0, 0, 0, 0, 0, 0);
}

void sem_free(int sem_id) {
  do_user_call(SYS_user_sem_free, sem_id, 0, 0, 0, 0, 0, 0);
}