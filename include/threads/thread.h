#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING,        /* About to be destroyed. */
	THREAD_SLEEPING,    /* 일정기간동안 쓰레드 재우기*/
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define F (1<<14)                       /* 17.14 소수점 표현의 1*/
#define FDT_PAGES 1
#define FDT_CNT_LIMIT (1<<8)


/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int64_t wake_time;
	/* synch member */
	struct list donation_list;  //기다리고 있는 쓰레드들 (donate-elem으로 연결)
	struct list_elem donate_elem;
	struct lock *wait_on_lock;  //기다리고 있는 락 (현재는 블록상태여야 함)
	int real_priority;  //초기값 = priority
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/* MLFQS 멤버 추가*/
	int recent_cpu;
	int nice;

	/* file descripter 멤버 */
	struct file **fdt;
	struct file **fdt_dup;
	int next_dup;
	int next_fd;

	struct intr_frame parent_if;
	int exit_status;
	struct list child_list;
	struct list_elem child_elem;

	struct semaphore dupl_sema;
	struct semaphore child_wait_sema;
	struct semaphore exit_sema;

	struct file *loading_file;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */

#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;
void set_load_avg (void);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_sleep_and_yield(void);
void time_to_wake (void);

int thread_get_priority (void);
void thread_set_priority (int);

bool thread_more_priority(const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED);

bool
thread_more_lock_priority (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED); 

void list_sort_high_priority (struct list *list);
void check_running_priority(void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
void thread_set_mlfqs_priority(struct thread *curr);
void set_recent_cpu_and_priority(void);
void thread_set_recent_cpu(struct thread *t);

void do_iret (struct intr_frame *tf);

/* 고정 소수점 */

int fp_to_int_round(int x);
int fp_multiple(int x,int y);
int fp_divide(int x, int y);
int fp_to_int_toward_zero(int x);

#endif /* threads/thread.h */
