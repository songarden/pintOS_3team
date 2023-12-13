#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
struct lock sysLock;

void syscall_init (void);
int wait(int pid);
int exec(const char* cmd_line);
// int write(int fd, const void* buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
int read(int fd, void* buffer, unsigned size);
void close(int fd);
unsigned tell(int fd);
void seek(int fd, unsigned position);
int filesize(int fd);
int open(const char* file_name);
bool remove(const char* file);
bool create(const char* file, unsigned initial_size);
void exit(int status);
void halt(void);
void check_address(void* addr);
#endif /* userprog/syscall.h */
