#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include "lib/stdio.h"
#include "lib/kernel/console.h"
#include "lib/kernel/stdio.h"
#include "devices/input.h"
#include <syscall-nr.h>
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/flags.h"
#include "intrinsic.h"
void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void* addr);
void get_argument(void* rsp, int* arg, int count);
tid_t fork(const char* thread_name, struct intr_frame* f);
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
	lock_init(&sysLock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_address(void* addr){
	if(addr==NULL || !is_user_vaddr(addr) || pml4_get_page(thread_current()->pml4,addr) == NULL)
		exit(-1);
}

void halt(void){
	power_off();
}

void exit(int status){
	struct thread* curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	sema_up(&curr->wait_sema);
	thread_exit();
}

bool create(const char* file, unsigned initial_size){
	lock_acquire(&sysLock);
	check_address(file);
	bool success = filesys_create(file, initial_size);
	lock_release(&sysLock);
	return success;
}

bool remove(const char* file){
	check_address(file);
	return filesys_remove(file);
}

int open(const char* file_name){
	check_address(file_name);
	lock_acquire(&sysLock);
	struct file* file = filesys_open(file_name);
	if(file == NULL) {
		lock_release(&sysLock);
		return -1;
	}
	int fd = process_add_file(file);
	if(fd == -1) file_close(file);

	lock_release(&sysLock);
	return fd;
}

int filesize(int fd){
	struct file* file = process_get_file(fd);
	if(file == NULL) return -1;
	return file_length(file);
}

void seek(int fd, unsigned position){
	struct file* file = process_get_file(fd);
	if(file == NULL) return;
	file_seek(file, position);
}

unsigned tell(int fd){
	struct file* file = process_get_file(fd);
	if(file == NULL) return;
	return file_tell(file);
}

void close(int fd){
	if(fd <2) return;
	struct file* file = process_get_file(fd);
	if(file == NULL) return;
	file_close(file);
	process_close_file(fd);
}

int read(int fd, void* buffer, unsigned size){
	check_address(buffer);

	int bytes_read = 0;

	lock_acquire(&sysLock);
	if(fd == STDIN_FILENO){
		for(int i =0; i<size; i++){
			bytes_read = strlen(input_getc());
		}
		
	}else{
		if(fd<2){
			lock_release(&sysLock);
			return -1;
		}
		struct file* file = process_get_file(fd);
		if(file == NULL){
			lock_release(&sysLock);
			return -1;
		}

		bytes_read = file_read(file, buffer, size);
	}
	lock_release(&sysLock);
	return bytes_read;
}

int write(int fd, void *buffer, unsigned size){
	check_address(buffer);
	int bytes_write = 0;
	struct thread *curr = thread_current();
	if( fd == STDOUT_FILENO){
		putbuf(buffer,size);
		bytes_write = size;
	}else{
		if(fd<1 || fd> FDT_COUNT_LIMIT) return -1;
		if(curr->fdt[fd] == NULL)return -1;

		struct file* file = process_get_file(fd);
		if(file == NULL) return -1;
		lock_acquire(&sysLock);
		bytes_write = file_write(file,buffer,size);
		lock_release(&sysLock);
	}
	return bytes_write;

}

tid_t fork(const char* thread_name, struct intr_frame* f){
	return process_fork(thread_name, f);
}

int exec(const char* cmd_line){
	check_address(cmd_line);
	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL){
		exit(-1);
	}
	strlcpy (cmd_line_copy,cmd_line,PGSIZE);
	if(process_exec(cmd_line_copy) == -1){
		exit(-1);
	}
}

int wait(int pid){
	return process_wait(pid);
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
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
			f->R.rax = create(f->R.rdi,f->R.rsi);
			break;
		
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;

		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;

		case SYS_READ:
			f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
			break;
		
		case SYS_SEEK:
			seek(f->R.rdi,f->R.rsi);
			break;

		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;

		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
}