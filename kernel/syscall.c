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
#include "vfs.h"
#include "elf.h"
#include "memlayout.h"

#include "spike_interface/spike_utils.h"

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

// added @lab4_challenge2
ssize_t sys_user_exec(char *command) {
    // 1. 从用户空间深拷贝命令，【核心修复】：强行过滤掉所有的回车和换行符
    char kcommand[256];
    int i = 0;
    while(i < 255) {
        char *pa = (char*)user_va_to_pa((pagetable_t)current->pagetable, command + i);
        if (!pa || *pa == '\0') break;
        if (*pa == '\n' || *pa == '\r') {
            kcommand[i] = ' '; // 将换行替换为空格，方便后续分割
        } else {
            kcommand[i] = *pa;
        }
        i++;
    }
    kcommand[i] = '\0';

    // 2. 原地按空格分割字符串，构造 kargv 指针数组
    char *kargv[32];
    int argc = 0;
    char *p = kcommand;
    while (*p) {
        while (*p == ' ') p++; 
        if (*p == '\0') break;
        kargv[argc++] = p;     
        while (*p != ' ' && *p != '\0') p++; 
        if (*p == ' ') {
            *p = '\0';         
            p++;
        }
    }
    if (argc == 0) return -1;  

    char *kpath = kargv[0];    

    // 3. 智能路径转换 (去除 ./ 前缀，直接使用 obj/)
    char actual_path[256];
    if (strncmp(kpath, "/bin/", 5) == 0) {
        strcpy(actual_path, "hostfs_root/bin/");
        strcat(actual_path, kpath + 5);
    } else {
        strcpy(actual_path, kpath);
    }

    sprint("Application: %s\n", kpath);

    // 4. 【核心修复】：更安全地释放旧的 CODE 和 DATA 段 (通过类型精准打击)
    for (int k = 0; k < current->total_mapped_region; k++) {
        if (current->mapped_info[k].seg_type == CODE_SEGMENT ||
            current->mapped_info[k].seg_type == DATA_SEGMENT) {
            user_vm_unmap((pagetable_t)current->pagetable, 
                          current->mapped_info[k].va, 
                          current->mapped_info[k].npages * PGSIZE, 1);
            current->mapped_info[k].va = 0;
            current->mapped_info[k].npages = 0;
            current->mapped_info[k].seg_type = 0;
        }
    }

    // 5. 【核心修复】：清空进程的 HEAP 段并清除映射表记录
    for (uint64 heap_va = USER_FREE_ADDRESS_START; heap_va < current->user_heap.heap_top; heap_va += PGSIZE) {
        uint64 pa = lookup_pa((pagetable_t)current->pagetable, heap_va);
        if (pa) {
            user_vm_unmap((pagetable_t)current->pagetable, heap_va, PGSIZE, 1);
        }
    }
    current->user_heap.heap_top = USER_FREE_ADDRESS_START;
    current->user_heap.heap_bottom = USER_FREE_ADDRESS_START;
    current->user_heap.free_pages_count = 0;
    for (int k = 0; k < current->total_mapped_region; k++) {
        if (current->mapped_info[k].seg_type == HEAP_SEGMENT) {
            current->mapped_info[k].npages = 0;
            current->mapped_info[k].va = 0;
        }
    }

    // 6. 载入新程序的 ELF 文件
    load_bincode_from_host_elf_path(current, actual_path);

    // 7. 重置并精细化布置用户栈 
    uint64 user_stack_pa = lookup_pa((pagetable_t)current->pagetable, USER_STACK_TOP - PGSIZE);
    char *sp_pa = (char*)(user_stack_pa + PGSIZE);
    uint64 sp_va = USER_STACK_TOP;

    uint64 argv_va[32];
    for (int k = argc - 1; k >= 0; k--) {
        int len = strlen(kargv[k]) + 1;
        sp_pa -= len;
        sp_va -= len;
        strcpy(sp_pa, kargv[k]);
        argv_va[k] = sp_va; 
    }
    argv_va[argc] = 0; 

    int align = sp_va % 16;
    sp_pa -= align;
    sp_va -= align;

    sp_pa -= sizeof(uint64) * (argc + 1);
    sp_va -= sizeof(uint64) * (argc + 1);
    
    align = sp_va % 16;
    sp_pa -= align;
    sp_va -= align;

    memcpy(sp_pa, argv_va, sizeof(uint64) * (argc + 1));

    // 8. 魔法操作：在 trapframe 中布置入口参数！
    current->trapframe->regs.a1 = sp_va;
    current->trapframe->regs.sp = sp_va;

    return argc; 
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // reclaim the current process, and reschedule. added @lab3_1
  free_process( current );
  schedule();
  return 0;
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
  return do_open(pathpa, flags);
}

//
// read file
//
ssize_t sys_user_read(int fd, char *bufva, uint64 count) {
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_read(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
  }
  return count;
}

//
// write file
//
ssize_t sys_user_write(int fd, char *bufva, uint64 count) {
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_write(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
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
ssize_t sys_user_opendir(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_opendir(pathpa);
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
ssize_t sys_user_mkdir(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_mkdir(pathpa);
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
ssize_t sys_user_unlink(char * vfn){
  char * pfn = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn);
  return do_unlink(pfn);
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
    case SYS_user_exec:
      return sys_user_exec((char *)a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
