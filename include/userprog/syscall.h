#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init (void);
void check_addr(void *addr);
struct lock filesys_lock; //파일 접근 동기화 lock
#endif /* userprog/syscall.h */

