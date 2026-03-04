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
#include "spike_interface/spike_utils.h"

// lab3_challenge2: 内核态信号量数据结构
#define MAX_SEMAPHORES 16

typedef struct semaphore {
    int value;
    int is_used;
    // 等待该信号量的进程队列的头尾指针，复用 process 结构里的 queue_next
    process* wait_queue_head;
    process* wait_queue_tail;
} semaphore_t;

semaphore_t semaphores[MAX_SEMAPHORES] = {0};

ssize_t sys_user_print(const char* buf, size_t n) {
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  free_process( current );
  schedule();
  return 0;
}

uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va;
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;
    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}

ssize_t sys_user_yield() {
  current->status = READY;
  insert_to_ready_queue(current);
  schedule();
  return 0;
}

// lab3_challenge2: sys_user_sem_new
ssize_t sys_user_sem_new(int val) {
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if (!semaphores[i].is_used) {
            semaphores[i].is_used = 1;
            semaphores[i].value = val;
            semaphores[i].wait_queue_head = NULL;
            semaphores[i].wait_queue_tail = NULL;
            return i;
        }
    }
    return -1; // 信号量耗尽
}

// lab3_challenge2: sys_user_sem_P
ssize_t sys_user_sem_P(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEMAPHORES || !semaphores[sem_id].is_used) {
        panic("Invalid semaphore ID in P operation.\n");
    }
    
    semaphores[sem_id].value--;
    
    if (semaphores[sem_id].value < 0) {
        // 将当前进程挂起放入等待队列
        current->status = BLOCKED;
        current->queue_next = NULL;
        
        if (semaphores[sem_id].wait_queue_tail == NULL) {
            semaphores[sem_id].wait_queue_head = current;
            semaphores[sem_id].wait_queue_tail = current;
        } else {
            semaphores[sem_id].wait_queue_tail->queue_next = current;
            semaphores[sem_id].wait_queue_tail = current;
        }
        // 调度其他就绪进程
        schedule();
    }
    return 0;
}

// lab3_challenge2: sys_user_sem_V
ssize_t sys_user_sem_V(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEMAPHORES || !semaphores[sem_id].is_used) {
        panic("Invalid semaphore ID in V operation.\n");
    }
    
    semaphores[sem_id].value++;
    
    if (semaphores[sem_id].value <= 0) {
        // 从等待队列中唤醒一个进程
        process* p = semaphores[sem_id].wait_queue_head;
        if (p != NULL) {
            semaphores[sem_id].wait_queue_head = p->queue_next;
            if (semaphores[sem_id].wait_queue_head == NULL) {
                semaphores[sem_id].wait_queue_tail = NULL;
            }
            // 唤醒该进程，重新插入就绪队列
            insert_to_ready_queue(p);
        }
    }
    return 0;
}

// lab3_challenge2: sys_user_sem_free
ssize_t sys_user_sem_free(int sem_id) {
    if (sem_id >= 0 && sem_id < MAX_SEMAPHORES) {
        semaphores[sem_id].is_used = 0;
        semaphores[sem_id].wait_queue_head = NULL;
        semaphores[sem_id].wait_queue_tail = NULL;
    }
    return 0;
}

long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    // lab3_challenge2: 加入信号量对应的 do_syscall 分支
    case SYS_user_sem_new:
      return sys_user_sem_new(a1);
    case SYS_user_sem_P:
      return sys_user_sem_P(a1);
    case SYS_user_sem_V:
      return sys_user_sem_V(a1);
    case SYS_user_sem_free:
      return sys_user_sem_free(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}