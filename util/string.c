// See LICENSE for license details.

#include "string.h"

#include <ctype.h>
#include <stdint.h>
#include "snprintf.h"

void* memcpy(void* dest, const void* src, size_t len) {
  const char* s = src;
  char* d = dest;

  // if ((((uintptr_t)dest | (uintptr_t)src) & (sizeof(uintptr_t) - 1)) == 0) {
  //   while ((void*)d < (dest + len - (sizeof(uintptr_t) - 1))) {
  //     *(uintptr_t*)d = *(const uintptr_t*)s;
  //     d += sizeof(uintptr_t);
  //     s += sizeof(uintptr_t);
  //   }
  // }

  while (d < (char*)(dest + len)) *d++ = *s++;

  return dest;
}

// void* memset(void* dest, int byte, size_t len) {
//   if ((((uintptr_t)dest | len) & (sizeof(uintptr_t) - 1)) == 0) {
//     uintptr_t word = byte & 0xFF;
//     word |= word << 8;
//     word |= word << 16;
//     word |= word << 16 << 16;
// 
//     uintptr_t* d = dest;
//     while (d < (uintptr_t*)(dest + len)) *d++ = word;
//   } else {
//     char* d = dest;
//     while (d < (char*)(dest + len)) *d++ = byte;
//   }
//   return dest;
// }

void* memset(void* dest, int byte, size_t len) {
  char* d = dest;
  while (d < (char*)(dest + len)) {
    *d++ = byte;
  }
  return dest;
}

size_t strlen(const char* s) {
  // ======= 新增拦截 =======
  if (s == NULL) return 0;
  // =======================
  
  const char* p = s;
  while (*p) p++;
  return p - s;
}

int strcmp(const char* s1, const char* s2) {
  if (s1 == NULL || s2 == NULL) {
    if (s1 == s2) return 0;
    return -1; 
  }

  unsigned char c1, c2;
  do {
    c1 = *s1++;
    c2 = *s2++;
  } while (c1 != 0 && c1 == c2);

  return c1 - c2;
}

char* strcpy(char* dest, const char* src) {
  // ======= 新增拦截 =======
  if (dest == NULL || src == NULL) return dest;
  // =======================
  
  char* d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strchr(const char *p, int ch)
{
    char c;
    c = ch;
    for (;; ++p) {
        if (*p == c)
            return ((char *)p);
        if (*p == '\0')
            return (NULL);
    }
}

char* strtok(char* str, const char* delim) {
  static char* current;
  if (str != NULL) current = str;
  if (current == NULL) return NULL;

  char* start = current;
  while (*start != '\0' && strchr(delim, *start) != NULL) start++;

  if (*start == '\0') {
    current = NULL;
    return current;
  }

  char* end = start;
  while (*end != '\0' && strchr(delim, *end) == NULL) end++;

  if (*end != '\0') {
    *end = '\0';
    current = end + 1;
  } else
    current = NULL;
  return start;
}

char *strcat(char *dst, const char *src) {
  if (dst == NULL || src == NULL) return dst;
  strcpy(dst + strlen(dst), src);
  return dst;
}

long atol(const char* str) {
  long res = 0;
  int sign = 0;

  while (*str == ' ') str++;

  if (*str == '-' || *str == '+') {
    sign = *str == '-';
    str++;
  }

  while (*str) {
    res *= 10;
    res += *str++ - '0';
  }

  return sign ? -res : res;
}

void* memmove(void* dst, const void* src, size_t len) {
  const char* s = src;
  char* d = dst;
  
  if (d < s) {
    // 同样的，屏蔽掉任何可能强行提速带来的 AMO 未对齐隐患，用单体字节循环
    while (len--) {
      *d++ = *s++;
    }
  } else {
    // Backward copy
    const char* lasts = s + len - 1;
    char* lastd = d + len - 1;
    while (len--) {
      *lastd-- = *lasts--;
    }
  }

  return dst;
}

// Like strncpy but guaranteed to NUL-terminate.
char* safestrcpy(char* s, const char* t, int n) {
  char* os;

  os = s;
  if (n <= 0) return os;
  while (--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}