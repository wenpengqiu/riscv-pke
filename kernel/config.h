#ifndef _CONFIG_H_
#define _CONFIG_H_

// 我们在 challenge3 中使用双核
#define NCPU 2

// interval of timer interrupt. added @lab1_3
#define TIMER_INTERVAL 1000000

// the maximum memory space that PKE is allowed to manage. added @lab2_1
#define PKE_MAX_ALLOWABLE_RAM 128 * 1024 * 1024

// the ending physical address that PKE observes. added @lab2_1
#define PHYS_TOP (DRAM_BASE + PKE_MAX_ALLOWABLE_RAM)

// 用于多核内存隔离的宏定义
#define USER_STACK(hartid) (0x81100000 + (hartid) * 0x500000)
#define USER_KSTACK(hartid) (0x81200000 + (hartid) * 0x500000)
#define USER_TRAP_FRAME(hartid) (0x81300000 + (hartid) * 0x500000)

#endif