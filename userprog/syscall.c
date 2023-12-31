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
#ifdef VM
#include "vm/vm.h"
void check_invalid_write(void *addr);
#endif


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
int open(const char *file_name);
int exec(const char *cmd_line);
void exit(int status);
void halt(void);
int write(int fd, void *buffer, unsigned size);
int fork(const char *file, struct intr_frame *f);
int wait(tid_t child_tid);
bool create_file(const char *file, unsigned initial_size);
bool remove_file(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
#ifndef VM
int dup2(int oldfd, int newfd);
#else
void munmap (void *addr);
void * mmap (void *addr, size_t length, int writable, int fd, off_t offset);
#endif

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
#ifdef VM
	thread_current()->curr_rsp = f->rsp;
#endif
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
#ifndef VM
		case SYS_DUP2:
			f->R.rax = dup2(f->R.rdi,f->R.rsi);
			break;
#else
		case SYS_MMAP:
			f->R.rax = mmap(f->R.rdi,f->R.rsi,f->R.rdx,f->R.r10,f->R.r8);
			break;
		case SYS_MUNMAP:
			munmap(f->R.rdi);
			break;
	}
#endif
}

void check_addr(void *addr){
	if(!is_user_vaddr(addr) || addr == NULL){
		exit(-1);
	}
#ifndef VM
	if(pml4_get_page(thread_current()->pml4 , addr) == NULL){
		exit(-1);
	}
#endif
}

#ifdef VM
void check_invalid_write(void *addr){
	if (addr <= USER_STACK && addr >= USER_STACK - (1<<20)){
		return;
	}
	struct page *page = spt_find_page(&thread_current()->spt,addr);
	if(page == NULL){
		exit(-1);
	}
	else if(page->writable == false){
		exit(-1);
	}
}
#endif

int open(const char *file_name){
	check_addr(file_name);
	lock_acquire(&filesys_lock);
	struct file *file = filesys_open(file_name);
	lock_release(&filesys_lock);
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
	if (cmd_line_copy == NULL){
		exit(-1);
	}
	strlcpy (cmd_line_copy,cmd_line,PGSIZE);
	if(process_exec(cmd_line_copy) == -1){
		exit(-1);
	}
}

void exit(int status){
	struct thread *curr = thread_current ();
	printf("%s: exit(%d)\n",curr->name,status);
	curr->exit_status = status;
	sema_up(&curr->child_wait_sema);
	thread_exit();
}

void halt(void){
	power_off();
}

int write(int fd, void *buffer, unsigned size){
	check_addr(buffer);

	struct thread *curr = thread_current();
	if(fd < 1){
		return -1;
	}
	lock_acquire(&filesys_lock);
	if (fd == 1){
		putbuf((char*)buffer,(size_t)size);
		lock_release(&filesys_lock);
		return size;
	}
	if(curr->fdt[fd] == NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	
	int write_size = (int)file_write(curr->fdt[fd],buffer,(off_t)size);
	lock_release(&filesys_lock);
	return write_size;

}

int fork(const char *file, struct intr_frame *f){
	lock_acquire(&filesys_lock);
	int fork_pid = process_fork(file,f);
	lock_release(&filesys_lock);
	return fork_pid;
}

int wait(tid_t child_tid){
	struct thread *curr = thread_current();
	return process_wait(child_tid);
}


bool create_file(const char *file, unsigned initial_size){
	check_addr(file);
	lock_acquire(&filesys_lock);
	bool success = filesys_create(file,initial_size);
	lock_release(&filesys_lock);
	return success;
}


bool remove_file(const char *file){
	check_addr(file);
	bool success = filesys_remove(file);
	return success;
}

int filesize(int fd){
	struct thread *curr = thread_current ();
	if (fd < 2 || curr->fdt[fd] == NULL){
		return -1;
	}
	
	return file_length(curr->fdt[fd]);
}

int read(int fd, void *buffer, unsigned size){
	check_addr(buffer);
#ifdef VM
	check_invalid_write(buffer);
#endif
	if(fd == 1 || fd < 0){
		return -1;
	}
	struct thread *curr = thread_current ();
	lock_acquire(&filesys_lock);
	if (fd == 0){
		int read_size = strlen(input_getc());
		lock_release(&filesys_lock);
		return read_size;
	}
	
	if(curr->fdt[fd] == NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	struct file *file = curr->fdt[fd];
	int read_size = (int)file_read(file,buffer,(off_t)size);
	lock_release(&filesys_lock);
	return read_size;
}

void seek (int fd, unsigned position){
	struct thread *curr = thread_current();
	if(position < 0){
		return;
	}
	if (curr->fdt[fd] != NULL){
		file_seek(curr->fdt[fd],(off_t)position);
	}
}

unsigned tell (int fd){
	struct thread *curr = thread_current();
	if (fd >1 && curr->fdt[fd] != NULL){
		unsigned point = (unsigned)file_tell(curr->fdt[fd]);
		return point;
	}
	else{
		return -1; // 어떻게 예외처리?
	}
}
#ifndef VM
void close (int fd){
	struct thread *curr = thread_current ();
	struct file *closing_file = curr->fdt[fd];
	if(closing_file != NULL){
		if(fd >1){
			for(int i = 2; i< curr->next_dup;i++){
				if(curr->fdt_dup[i] == curr->fdt[fd]){
					curr->fdt_dup[i] == NULL;
					curr->fdt[fd] == NULL;
					return;
				}
			}
		}
		file_close(curr->fdt[fd]);
		curr->fdt[fd] = NULL;
	}
}

int dup2(int oldfd, int newfd){
	if(oldfd < 2){
		return -1;
	}

	if(oldfd == newfd){
		return newfd;
	}

	struct thread *curr = thread_current ();

	if (curr->fdt[newfd] != NULL){
		return -1;
	}

	lock_acquire(&filesys_lock);
	curr->fdt[newfd] = curr->fdt[oldfd];
	curr->fdt_dup[curr->next_dup] = curr->fdt[newfd];
	curr->next_dup++;
	lock_release(&filesys_lock);

	return newfd;
}
#else

void close (int fd){
	struct thread *curr = thread_current ();
	if (fd < 2){
		return;
	}
	struct file *closing_file = curr->fdt[fd];
	if(closing_file != NULL){
		file_close(curr->fdt[fd]);
		curr->fdt[fd] = NULL;
	}
}

void *
mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	if(!is_user_vaddr(addr) || addr == NULL){
		return NULL;
	}
	if(offset % PGSIZE != 0){
		return NULL;
	}
	struct thread *curr = thread_current ();
	if(fd < 2){
		return NULL;
	}
	if(curr->fdt[fd] == NULL){
		return NULL;
	}
	struct file *file = curr->fdt[fd];
	if (file_length(file) == 0 ){
		return NULL;
	}
	if (length == 0 || (long)length < 0){
		return NULL;
	}
	void* mmap_addr = do_mmap(addr,length,writable,file,offset);


	return mmap_addr;
}

void
munmap (void *addr) {
	check_addr(addr);
	do_munmap(addr);
}

#endif