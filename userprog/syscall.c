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
			lock_acquire(&filesys_lock);
			f->R.rax = open(f->R.rdi);
			lock_release(&filesys_lock);
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
		
		case SYS
		

	}
	// printf ("system call!\n");
	// thread_exit ();
}

void check_addr(void *addr){
	if(!is_user_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4 , addr) == NULL){
		exit(-1);
	}
}

int open(const char *file_name){
	//here is open function code
	check_addr(file_name);
	struct file *file = filesys_open(file_name);
	if (file == NULL) {
		return -1;
	}
	int fd = process_add_fd(file);
	if (fd == -1){
		file_close(file);
	}
	return fd;
}

int exec(const char *cmd_line){
	check_addr(cmd_line);
	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
		return TID_ERROR;
	strlcpy (cmd_line_copy,cmd_line,PGSIZE);
	if(process_exec(cmd_line) == -1){
		exit(-1);
	}
	
}

void exit(int status){
	struct thread *curr = thread_current ();
	printf("%s: exit(%d)\n",curr->name,status);
	thread_exit();
}

void halt(void){
	power_off();
}

int write(int fd, void *buffer, unsigned size){
	if (fd == 1){
		putbuf((char*)buffer,(size_t)size);
		return size;
	}
}
