#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/stdio.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_addr(void *addr);
int open(const char *file_name);
int exec(const char *cmd_line);
void exit(int status);
void halt(void);
int write(int fd, void *buffer, unsigned size);
int fork(const char *file, struct intr_frame *f);
int wait(tid_t child_tid);
bool create_file(const char *file, unsigned initial_size);
bool remove_file(const char *file);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	lock_init(&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int syscall_num = f->R.rax; // rax에 존재하는 시스템콜 넘버 추출
	switch (syscall_num){
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		
		case SYS_HALT:
			halt();
			break;

		case SYS_WRITE:
			f->R.rax = write(f->R.rdi,f->R.rsi,f->R.rdx);
			break;
		
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi,f);
			break;
		
		case SYS_CREATE:
			f->R.rax = create_file(f->R.rdi,f->R.rsi);
			break;
		
		case SYS_REMOVE:
			f->R.rax = remove_file(f->R.rdi);

		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;

		

	}
}

void check_addr(void *addr){
	if(!is_user_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4 , addr) == NULL){
		exit(-1);
	}
}

int open(const char *file_name){
	check_addr(file_name);
	lock_acquire(&filesys_lock);
	struct file *file = filesys_open(file_name);
	if (file == NULL) {
		return -1;
	}
	int fd = process_add_fd(file);
	if (fd == -1){
		file_close(file);
	}
	lock_release(&filesys_lock);
	return fd;
}

int exec(const char *cmd_line){
	check_addr(cmd_line);

	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
		return TID_ERROR;
	strlcpy (cmd_line_copy,cmd_line,PGSIZE);

	if(process_exec(cmd_line_copy) == -1){
		exit(-1);
	}
}

void exit(int status){
	struct thread *curr = thread_current ();
	printf("%s: exit(%d)\n",curr->name,status);
	if(curr->parent){
		curr->parent->child_exist_status = status;
	}
	thread_exit();
}

void halt(void){
	power_off();
}

int write(int fd, void *buffer, unsigned size){
	check_addr(buffer);
	if (fd == 1){
		putbuf((char*)buffer,(size_t)size);
		return size;
	}
}

int fork(const char *file, struct intr_frame *f){
	check_addr(file);
	return process_fork(file,f);
}

int wait(tid_t child_tid){
	struct thread *curr = thread_current();
	return process_wait(child_tid);
}


bool create_file(const char *file, unsigned initial_size){
	check_addr(file);
	// char *file_name_copy = palloc_get_page(0);
	// if (file_name_copy == NULL)
	// 	return TID_ERROR;
	// strlcpy (file_name_copy,file,PGSIZE);

	bool success = filesys_create(file,initial_size);
	// bool success = filesys_create(file_name_copy,initial_size);
	// palloc_free_page(file_name_copy);

	return success;
}

/* 아직 구현 미완 -> 열려있는지 fd 전체 확인한 뒤 열려있으면 닫힐때 까지 wait하기 구현해야 함 */
bool remove_file(const char *file){
	check_addr(file);
	bool success = filesys_remove(file);
	return success;
}