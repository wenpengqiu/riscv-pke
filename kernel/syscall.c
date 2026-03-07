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
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "proc_file.h"
#include "memlayout.h"
#include "elf.h"

#include "spike_interface/spike_utils.h"
extern process procs[NPROC];

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  current->status = ZOMBIE;
  // 如果父进程正在等待，唤醒父进程
  if (current->parent && current->parent->status == BLOCKED) {
    current->parent->status = READY;
    insert_to_ready_queue(current->parent);
  }
  schedule();
  return 0;
}

ssize_t sys_user_wait(int pid) {
  if (pid < 0) return -1;

  int found = 0;
  for (int i = 0; i < NPROC; i++) {
    if (procs[i].pid == pid && procs[i].parent == current &&
        procs[i].status != FREE) {
      found = 1;

      if (procs[i].status == ZOMBIE) {
        free_process(&procs[i]);
        return 0;
      }
    }
  }

  if (!found) return -1;

  // PKE 的 schedule() 不会“返回到这里”继续执行 wait。
  // 父进程被子进程唤醒后，会直接回到用户态。
  // 所以必须把 epc 回退，让它重新执行 ecall，再次进入 wait，
  // 这次才能看到子进程已经 ZOMBIE，并正常 return 0。
  current->trapframe->epc -= 4;
  current->status = BLOCKED;
  schedule();

  return 0;
}

//
// implement the SYS_user_exec syscall
//
ssize_t sys_user_exec(char *command, char *parameter) {
  char *pa_cmd = (char*)user_va_to_pa((pagetable_t)(current->pagetable), command);
  char *pa_parameter = (char*)user_va_to_pa((pagetable_t)(current->pagetable), parameter);

  char cmd_buf[128];
  char param_buf[128];
  cmd_buf[0] = '\0';
  param_buf[0] = '\0';

  if (pa_cmd == 0) return -1;
  strcpy(cmd_buf, pa_cmd);

  if (pa_parameter) {
    strcpy(param_buf, pa_parameter);
  }

  // 3. 卸载当前进程旧的代码段、数据段，防止与新程序的地址空间冲突
  int valid_idx = 0;
  for (int i = 0; i < current->total_mapped_region; i++) {
    int stype = current->mapped_info[i].seg_type;
    if (stype == CODE_SEGMENT || stype == DATA_SEGMENT) {
      // 这里不能 free 旧页，否则 fork 后父子共享的代码页会被子进程 exec 误释放
      user_vm_unmap((pagetable_t)current->pagetable,
                    current->mapped_info[i].va,
                    current->mapped_info[i].npages * PGSIZE, 0);

      current->mapped_info[i].va = 0;
      current->mapped_info[i].npages = 0;
      current->mapped_info[i].seg_type = 0;
    } else {
      if (stype == HEAP_SEGMENT) {
        current->mapped_info[i].npages = 0;
        current->user_heap.heap_top = USER_FREE_ADDRESS_START;
        current->user_heap.free_pages_count = 0;
      }

      current->mapped_info[valid_idx] = current->mapped_info[i];
      if (i != valid_idx) {
        current->mapped_info[i].va = 0;
        current->mapped_info[i].npages = 0;
        current->mapped_info[i].seg_type = 0;
      }
      valid_idx++;
    }
  }
  current->total_mapped_region = valid_idx;

  // 4. 将新程序的可执行代码覆盖装载进当前的进程页表之中
  load_bincode_from_host_elf(current, cmd_buf);

  // 5. 重置用户栈并传入参数 (argc 与 argv)
  uint64 sp = USER_STACK_TOP;

  int cmd_len = strlen(cmd_buf) + 1;
  int param_len = strlen(param_buf) + 1;
  int has_param = strlen(param_buf) > 0;

  // 退栈，压入参数二的字符串内容 (如果有的话)
  uint64 va_param = 0;
  if (has_param) {
      sp -= param_len;
      char *pa_stack_param = (char *)user_va_to_pa((pagetable_t)(current->pagetable), (void *)sp);
      strcpy(pa_stack_param, param_buf);
      va_param = sp;
  }

  // 退栈，压入命令名，这是为了完整性放到用户栈，但我们可以不在 argv 中体现它
  sp -= cmd_len;
  char *pa_stack_cmd = (char *)user_va_to_pa((pagetable_t)(current->pagetable), (void *)sp);
  strcpy(pa_stack_cmd, cmd_buf);
  uint64 va_cmd = sp;

  // RISC-V 约定：栈顶指针 (sp) 必须向下进行 16 字节 (16-byte) 对齐
  sp = sp & ~(uint64)15;

  // 预留 argv[] 数组空间 (存放3个 64bit 地址)
  // 因为参数指针为 8 字节，这里预留 32 字节依然保持 16 字节对齐
  sp -= 32; 
  uint64 *pa_argv = (uint64 *)user_va_to_pa((pagetable_t)(current->pagetable), (void *)sp);

  int argc = has_param ? 1 : 0;
  if (has_param) {
      // PKE风格：直接把参数放在 argv[0] 供应用读取
      pa_argv[0] = va_param; 
      pa_argv[1] = 0; // null结尾
  } else {
      pa_argv[0] = 0; // 没有参数
  }
  
  // 6. 根据 RISC-V 约定更新相关寄存器环境 
  current->trapframe->regs.a1 = sp;         // a1 写入 argv[] 首地址指针
  current->trapframe->regs.sp = sp;         // 更新当前进程 SP 到栈底

  // 【核心修改】：直接返回 argc，作为系统调用的返回值。
  // 它会被操作系统的 Trap 机制装载到进程的 a0 寄存器，成为新程序的实际 argc！
  return argc; 
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va;
  // if there are previously reclaimed pages, use them first (this does not change the
  // size of the heap)
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    // otherwise, allocate a new page (this increases the size of the heap by one page)
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;

    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  // add the reclaimed page to the free page list
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

//
// kerenl entry point of naive_fork
//
ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}

//
// kerenl entry point of yield. added @lab3_2
//
ssize_t sys_user_yield() {
  // TODO (lab3_2): implment the syscall of yield.
  // hint: the functionality of yield is to give up the processor. therefore,
  // we should set the status of currently running process to READY, insert it in
  // the rear of ready queue, and finally, schedule a READY process to run.
  // panic( "You need to implement the yield syscall in lab3_2.\n" );
  current->status = READY;
  insert_to_ready_queue(current);
  schedule();

  return 0;
}

//
// open file
//
ssize_t sys_user_open(char *pathva, int flags) {
  char* pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  char safe_path[128]; // 使用内核栈或分配页来保证安全
  // 加一把防弹锁，只拷有效字节并确保结束
  int max_len = 127;
  int i = 0;
  while (i < max_len && pathpa[i] != '\0') {
    safe_path[i] = pathpa[i];
    i++;
  }
  safe_path[i] = '\0';
  return do_open(safe_path, flags);
}

//
// read file
//
ssize_t sys_user_read(int fd, char *bufva, uint64 count) {
  int i = 0;
  while ((uint64)i < count) {
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;

    ssize_t r = do_read(fd, (char *)pa + off, len);
    if (r < 0) return i == 0 ? -1 : i;

    i += r;
    if ((uint64)r < len) return i;
  }
  return count;
}

//
// write file
//
ssize_t sys_user_write(int fd, char *bufva, uint64 count) {
  int i = 0;
  while ((uint64)i < count) {
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;

    ssize_t r = do_write(fd, (char *)pa + off, len);
    if (r < 0) return i == 0 ? -1 : i;

    i += r;
    if ((uint64)r < len) return i;
  }
  return count;
}

//
// lseek file
//
ssize_t sys_user_lseek(int fd, int offset, int whence) {
  return do_lseek(fd, offset, whence);
}

//
// read vinode
//
ssize_t sys_user_stat(int fd, struct istat *istat) {
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
  return do_stat(fd, pistat);
}

//
// read disk inode
//
ssize_t sys_user_disk_stat(int fd, struct istat *istat) {
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
  return do_disk_stat(fd, pistat);
}

//
// close file
//
ssize_t sys_user_close(int fd) {
  return do_close(fd);
}

//
// lib call to opendir
//
ssize_t sys_user_opendir(char *pathva){
  char *pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  char safe_path[128];
  int i = 0;
  while (i < 127 && pathpa[i] != '\0') { safe_path[i] = pathpa[i]; i++; }
  safe_path[i] = '\0';
  return do_opendir(safe_path);
}

//
// lib call to readdir
//
ssize_t sys_user_readdir(int fd, struct dir *vdir){
  struct dir * pdir = (struct dir *)user_va_to_pa((pagetable_t)(current->pagetable), vdir);
  return do_readdir(fd, pdir);
}

//
// lib call to mkdir
//
ssize_t sys_user_mkdir(char *pathva){
  char *pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  char safe_path[128];
  int i = 0;
  while (i < 127 && pathpa[i] != '\0') { safe_path[i] = pathpa[i]; i++; }
  safe_path[i] = '\0';
  return do_mkdir(safe_path);
}

//
// lib call to closedir
//
ssize_t sys_user_closedir(int fd){
  return do_closedir(fd);
}

//
// lib call to link
//
ssize_t sys_user_link(char * vfn1, char * vfn2){
  char * pfn1 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn1);
  char * pfn2 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn2);
  return do_link(pfn1, pfn2);
}

//
// lib call to unlink
//
ssize_t sys_user_unlink(char *vfn){
  char *pfn = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn);
  char safe_path[128];
  int i = 0;
  while (i < 127 && pfn[i] != '\0') { safe_path[i] = pfn[i]; i++; }
  safe_path[i] = '\0';
  return do_unlink(safe_path);
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
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    // added @lab4_1
    case SYS_user_open:
      return sys_user_open((char *)a1, a2);
    case SYS_user_read:
      return sys_user_read(a1, (char *)a2, a3);
    case SYS_user_write:
      return sys_user_write(a1, (char *)a2, a3);
    case SYS_user_lseek:
      return sys_user_lseek(a1, a2, a3);
    case SYS_user_stat:
      return sys_user_stat(a1, (struct istat *)a2);
    case SYS_user_disk_stat:
      return sys_user_disk_stat(a1, (struct istat *)a2);
    case SYS_user_close:
      return sys_user_close(a1);
    // added @lab4_2
    case SYS_user_opendir:
      return sys_user_opendir((char *)a1);
    case SYS_user_readdir:
      return sys_user_readdir(a1, (struct dir *)a2);
    case SYS_user_mkdir:
      return sys_user_mkdir((char *)a1);
    case SYS_user_closedir:
      return sys_user_closedir(a1);
    // added @lab4_3
    case SYS_user_link:
      return sys_user_link((char *)a1, (char *)a2);
    case SYS_user_unlink:
      return sys_user_unlink((char *)a1);
    case SYS_user_wait:
      return sys_user_wait((int)a1);
    case SYS_user_exec:
      return sys_user_exec((char*)a1, (char*)a2);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
