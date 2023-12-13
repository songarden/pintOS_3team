#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// * USERPROG 추가
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
            
    lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
    switch (f->R.rax) {
	    case SYS_HALT:
	        halt();
	        break;
        case SYS_EXIT:
	        exit(f->R.rdi);
	        break;
        case SYS_CREATE:
	        f->R.rax = create(f->R.rdi, f->R.rsi);
	        break;
        case SYS_REMOVE:
	        f->R.rax = remove(f->R.rdi);
	        break;
        case SYS_OPEN:
	        f->R.rax = open(f->R.rdi);
	        break;
        case SYS_FILESIZE:
	        f->R.rax = filesize(f->R.rdi);
	        break;
        case SYS_READ:
	        f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
	        break; 
        case SYS_WRITE:      
	        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
	        break;
        case SYS_SEEK:
	        seek(f->R.rdi, f->R.rsi);
	        break;
        case SYS_TELL:
	        f->R.rax = tell(f->R.rdi);
	        break;
        case SYS_CLOSE:
	        close(f->R.rdi);
	        break;
        case SYS_EXEC:
	        exec(f->R.rdi);
	        break;
        case SYS_WAIT:
	        f->R.rax = wait(f->R.rdi);
	        break; 
        case SYS_FORK:
	        memcpy(&thread_current()->ptf, f, sizeof(struct intr_frame));
	        f->R.rax = fork(f->R.rdi);
	        break;
		case SYS_DUP2:
    		f->R.rax = dup2(f->R.rdi, f->R.rsi);
    		break;
        default:
			exit(-1);
			break;
    }
}

void halt(void) {
	// * power_off()를 사용하여 pintos 종료
	power_off();
}

void exit(int status) {
	/*
	* 실행중인 스레드 구조체를 가져옴
	* 프로세스 종료 메시지 출력
	* 출력 양식: "프로세스 이름: exit(종료상태)"
	* thread 종료
	*/ 
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

bool create (const char *file, unsigned initial_size) {
	/* 
	* 파일 이름과 크기에 해당하는 파일 생성
	* 파일 생성 성공 시 true 반환, 실패 시 false 반환
	*/
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove (const char *file) {
	/* 
	* 파일 이름에 해당하는 파일을 제거
	* 파일 제거 성공 시 true 반환, 실패 시 false 반환
	*/
	check_address(file);
	return filesys_remove(file);
}

int open (const char *file) {
	check_address(file);
	struct thread *cur = thread_current();
	struct file *fd = filesys_open(file);
	if (fd) {
		for (int i = 2; i < 128; i++) {
			if (!cur->fdt[i]) {
				cur->fdt[i] = fd;
				cur->next_fd = i + 1;
				return i;
			}
		}
		file_close(fd);
	}
	return -1;
}

int filesize (int fd) {
	struct file *file = thread_current()->fdt[fd];
	if (file)
		return file_length(file);
	return -1;
}

int read (int fd, void *buffer, unsigned size) {
	check_address(buffer);
	if (fd == 1) {
		return -1;
	}

	if (fd == 0) {
		lock_acquire(&filesys_lock);
		int byte = input_getc();
		lock_release(&filesys_lock);
		return byte;
	}
	struct file *file = thread_current()->fdt[fd];
	if (file) {
		lock_acquire(&filesys_lock);
		int read_byte = file_read(file, buffer, size);
		lock_release(&filesys_lock);
		return read_byte;
	}
	return -1;
}

// int write (int fd UNUSED, const void *buffer, unsigned size) {
// 	check_address(buffer);

// 	if (fd < 1 ) // STDIN일때 -1
// 		return -1;

// 	if (fd == 1) {
// 		lock_acquire(&filesys_lock);
// 		putbuf(buffer, size);
// 		lock_release(&filesys_lock);
// 		return size;
// 	}

// 	struct file *file = thread_current()->fdt[fd];
// 	if (file) {
// 		lock_acquire(&filesys_lock);
// 		int write_byte = file_write(file, buffer, size);
// 		lock_release(&filesys_lock);
// 		return write_byte;
// 	}
// 	else return -1;
// }

int write(int fd, const void *buffer, unsigned size) {
    check_address(buffer); // Validate the buffer address.

    struct thread *cur = thread_current(); // Get the current thread context.
    struct file *file_fd = cur->fdt[fd];   // Retrieve the file object for the given file descriptor.

    // If the file descriptor is for STDIN, return -1 as STDIN is not writable.
    if (fd < 1 || file_fd == STDIN) { 
        return -1;
    }

    // Handling standard output (STDOUT)
    if (fd == 1 || file_fd == STDOUT) {
        if (cur->stdout_count == 0) { // Check if STDOUT is available.
            // STDOUT is not available. Handle the error.
            NOT_REACHED(); // This is typically used to indicate an unexpected error state.
            remove_file_from_fdt(fd); // Remove the file descriptor from the file descriptor table.
            return -1;
        }

        // Write to STDOUT.
        lock_acquire(&filesys_lock);
        putbuf(buffer, size); // Write the buffer to standard output.
        lock_release(&filesys_lock);
        return size; // Return the number of bytes written.
    }

    // Writing to a regular file.
    if (file_fd) {
        lock_acquire(&filesys_lock);
        int write_byte = file_write(file_fd, buffer, size); // Write to the file.
        lock_release(&filesys_lock);
        return write_byte; // Return the number of bytes written to the file.
    }

    // If the file descriptor does not correspond to a valid file, return -1.
    return -1;
}

void seek (int fd, unsigned position) {
	struct file *curfile = thread_current()->fdt[fd];
	if (curfile)
		file_seek(curfile, position);
}

unsigned tell (int fd) {
	struct file *curfile = thread_current()->fdt[fd];
	if (curfile)
		return file_tell(curfile);
}

// void close (int fd) {
// 	struct file * file = thread_current()->fdt[fd];
// 	if (file) {
// 		lock_acquire(&filesys_lock);
// 		thread_current()->fdt[fd] = NULL;
// 		file_close(file);
// 		lock_release(&filesys_lock);
// 	}
// }

void close(int fd) {
    struct thread *cur = thread_current();
    struct file *file_fd = cur->fdt[fd];

    if (file_fd == NULL) {
        return; // If there's no file associated with fd, there's nothing to close.
    }

    // Handling closing of standard input (STDIN)
    if (fd == 0 || file_fd == STDIN) {
        cur->stdin_count--;
        if (cur->stdin_count > 0) {
            return; // Don't close if there are still references to STDIN.
        }
    }

    // Handling closing of standard output (STDOUT)
    else if (fd == 1 || file_fd == STDOUT) {
        cur->stdout_count--;
        if (cur->stdout_count > 0) {
            return; // Don't close if there are still references to STDOUT.
        }
    }

    // For regular files
    else {
        if (file_fd->dup_count > 0) {
            file_fd->dup_count--;
            if (file_fd->dup_count > 0) {
                return; // Don't close if there are still duplicated references.
            }
        }
    }

    // Closing the file
    lock_acquire(&filesys_lock);
    cur->fdt[fd] = NULL; // Remove the file descriptor from the file descriptor table.
    file_close(file_fd); // Close the file.
    lock_release(&filesys_lock);
}

int exec (const char *file_name) {
	check_address(file_name);

	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (!fn_copy) {
		exit(-1);
		return -1;
	}
	strlcpy(fn_copy, file_name, file_size);
	if (process_exec(fn_copy) == -1) {
		exit(-1);
		return -1;
	}
}

int wait (tid_t pid) {
  	return process_wait(pid);
}

int fork (const char *thread_name) {
	check_address(thread_name);
	return process_fork(thread_name, &thread_current()->ptf);
}

void check_address(void *addr) {
	struct thread *cur = thread_current();
	if (addr == NULL || is_kernel_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL)
		exit(-1);
}

int dup2(int oldfd, int newfd) {
    struct thread *cur = thread_current();
    if (oldfd < 2 || oldfd >= 128 || cur->fdt[oldfd] == NULL)
        return -1;
    if (oldfd == newfd)
        return newfd;
    if (newfd >= 0 && newfd < 128 && cur->fdt[newfd] != NULL)
        close(newfd);
    cur->fdt[newfd] = file_duplicate(cur->fdt[oldfd]);
    return newfd;
}

