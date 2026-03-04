/*
 * header file to be used by applications.
 */

int printu(const char *s, ...);
int exit(int code);
void* naive_malloc();
void naive_free(void* va);
int fork();
void yield();

// lab3_challenge2: 信号量用户态接口声明
int sem_new(int val);
void sem_P(int sem_id);
void sem_V(int sem_id);
void sem_free(int sem_id);