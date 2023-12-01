#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "fixed_point.h"
/* mlfqs */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

int load_avg;

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list blocked_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
static bool sorting_ticks (const struct list_elem *a_, const struct list_elem *b_, void* aux);
static bool sorting_priority (const struct list_elem *a_, const struct list_elem *b_, void* aux);
/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. 
   스레딩 시스템을 초기화한다. 초기 스레드 설정, 준비 리스트와 tid를 초기화함.
   */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};

	// 글로벌 디스크립터 테이블(Global Descriptor Table, GDT)의 주소를 특별한 CPU 레지스터에 로드
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	// lock 객체 초기화
	lock_init (&tid_lock);

	// 리스트 초기화
	list_init (&blocked_list);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	// 현재 실행 중인 스레드의 포인터를 저장
	initial_thread = running_thread ();
	// 스레드 초기화
	init_thread (initial_thread, "main", PRI_DEFAULT);
	// enum형으로 스레드 상태를 나타냄.
	initial_thread->status = THREAD_RUNNING;
	// allocated_tid로 스레드 식별자 생성 후 스레드 구조체에 tid 설정
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. 
   idle 상태 스레드를 생성하고 활성화.
   */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg = LOAD_AVG_DEFAULT;
	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. 
   타이머 인터럽트를 받을 때마다 호출되며, 인터럽트에 의해 자동으로 실행됨.
*/
void
thread_tick (void) {
	// 실행 중인 스레드를 불러옴.
	struct thread *t = thread_current ();

	/* Update statistics. 
	idle 스레드일 경우 아이들 스레드가 사용한 틱 수를 증가시킴.
	*/
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG // 유저 프로그램이면.
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else // 커널로 실행되었다면.
		kernel_ticks++;

	/* Enforce preemption. 
	틱을 증가시켜 선점후 틱 값이 TIME_SLICE(스레드에 할당단 최대 실행 시간)을 초과하면 중단하고 yield를 시킴.
	*/
	// thread_ticks++;
	if (++thread_ticks >= TIME_SLICE);
		intr_yield_on_return ();
}

/* Prints thread statistics. 
스레드 상태 출력.
*/
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. 
   
   주어진 초기 우선순위(PRIORITY)로 이름이 지정된(NAME) 새 커널 스레드를 생성합니다.
   이 스레드는 함수(FUNCTION)를 인자(AUX)와 함께 실행하고 준비 큐에 추가합니다.
   새 스레드의 스레드 식별자를 반환하거나, 생성 실패 시 TID_ERROR를 반환합니다.

   thread_start()가 호출되었다면, 새 스레드는 thread_create()가 반환되기 전에
   스케줄될 수 있습니다. 심지어 thread_create()가 반환되기 전에 종료될 수도 있습니다.
   반면에, 원래 스레드는 새 스레드가 스케줄될 때까지 어느 정도 시간 동안 실행될 수 있습니다.
   실행 순서를 보장하기 위해서는 세마포어 또는 다른 형태의 동기화를 사용해야 합니다.

   제공된 코드는 새 스레드의 `priority` 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되어 있지 않습니다.
   우선순위 스케줄링은 문제 1-3의 목표입니다.
   */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. 
	 커널 스레드가 스케줄될 경우 호출.
	 참고) rdi는 첫 번째 인자, rsi는 두 번째 인자.
	 */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);
	if(t->priority > thread_get_priority()) thread_yield();
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. 
   현재 스레드를 잠재웁니다. 이 스레드는 thread_unblock()에 의해 깨어날 때까지 
   다시 스케줄되지 않습니다.

   이 함수는 인터럽트가 꺼진 상태에서 호출되어야 합니다. 
   일반적으로 synch.h에 있는 동기화 프리미티브 중 하나를 사용하는 것이 
   더 좋은 생각입니다.
   */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

static bool
sorting_ticks (const struct list_elem* a_, const struct list_elem* b_, void* aux) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  if(!aux) return a->for_ticks < b->for_ticks;
  else return a->for_ticks > b->for_ticks;
}

void blocking_for_ticks(int64_t ticks){
	struct thread* cur = thread_current();
	ASSERT (!intr_context ());
	ASSERT(cur != idle_thread);
	cur->for_ticks = ticks;
	list_insert_ordered(&blocked_list, &cur->elem, sorting_ticks, NULL);
	thread_block();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. 
   차단된 스레드 T를 준비-실행 상태로 전환합니다. T가 차단 상태가 아니라면 이는 오류입니다.
   (실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를 사용하세요.)

   이 함수는 실행 중인 스레드를 선점하지 않습니다. 이는 중요할 수 있습니다: 
   호출자가 직접 인터럽트를 비활성화한 경우, 스레드를 원자적으로 차단 해제하고 
   다른 데이터를 업데이트할 수 있기를 기대할 수 있습니다.
   */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	if(list_empty(&ready_list))	list_push_back (&ready_list, &t->elem);
	else list_insert_ordered(&ready_list, &t->elem, sorting_priority, (void*)1);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

static bool sorting_priority (const struct list_elem* a_, const struct list_elem* b_, void* aux) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  if(!aux) return a->priority < b->priority;
  else return a->priority > b->priority;
}

void thread_ticks_end(void){
	if(list_empty(&blocked_list)) return;
	struct list_elem* e = list_begin(&blocked_list);
	while(e != list_end(&blocked_list)) {
		struct thread* isAwake = list_entry(e, struct thread, elem);
		if(isAwake->for_ticks <= timer_ticks()) {
			e = list_remove(e);
			thread_unblock(isAwake);
			isAwake->for_ticks = 0;
		} else {
			e = list_next(e);
		}
	}
	
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. 
   실행 중인 스레드 주소를 반환함.
   */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. 
   현재 스레드의 스케줄을 해제하고 파괴합니다. 호출자에게는 절대 반환하지 않습니다.
   */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. 
   CPU를 양보합니다. 현재 스레드는 잠들지 않으며 스케줄러의 재량에 따라 
   즉시 다시 스케줄될 수 있습니다. 
   */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, sorting_priority, (void*)1);
		// list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if(thread_mlfqs) return;

	struct thread* curr = thread_current();
	thread_current ()->priority = new_priority;
	thread_current()->original_priority = new_priority;
	refresh_priority(curr);
	struct thread* next = list_entry(list_begin(&ready_list), struct thread, elem);
	if(next->priority > curr->priority) {
		thread_yield();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	struct thread* curr = thread_current();
	curr->nice = nice;
	intr_set_level (old_level);
	thread_yield();
}


/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	int nice = thread_current()->nice;
	intr_set_level (old_level);
	return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	int get_load_avg = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level (old_level);
	printf("%d",load_avg);
	return get_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	int get_recent_cpu = fp_to_int_round(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level (old_level);
	return get_recent_cpu;
}

void priority_calculator(struct thread* t){
	if(t==idle_thread) return;
	int priority = PRI_MAX - fp_to_int(div_mixed(t->recent_cpu,4)) - t->nice*2;
	t->priority = priority;
}

int decay(){
	return div_fp((int64_t)int_to_fp(2*load_avg),(int64_t)(add_mixed(2*load_avg,1)));
}

void recent_cpu_calculator (struct thread *t){
	if(t==idle_thread) return;
	int recent_cpu = add_mixed(mult_fp(decay(), recent_cpu),t->nice);
	t->recent_cpu = recent_cpu;
}

void load_avg_calculator (void){
	int ready_thread=0;
	if(thread_current()!= idle_thread) ready_thread = 1 + list_size(&ready_list);
	load_avg = add_fp(
		mult_fp(div_fp(int_to_fp(59),int_to_fp(60)), load_avg), 
		div_mixed(int_to_fp(ready_thread),(60))
	); 
	// load_avg = add_fp(mult_fp(div_mixed(int_to_fp(59),60), load_avg), div_mixed(int_to_fp(ready_thread),60));
}

void recent_cpu_increment (void)
{
	struct thread* curr = thread_current();
	if(curr==idle_thread) return;
	curr->recent_cpu = add_mixed(curr->recent_cpu,1);
}

void recalc_all (void)
{
/* 모든 thread의 recent_cpu와 priority값 재계산 한다. */
	enum intr_level old_level;
	struct list_elem* e;
	struct thread* target;

	recent_cpu_calculator(thread_current());
	priority_calculator(thread_current());

	for(e=list_begin(&blocked_list); e!=list_end(&blocked_list); e=list_next(e)){
		target = list_entry(e, struct thread, elem);
		recent_cpu_calculator(target);
		priority_calculator(target);
	}

	for(e=list_begin(&ready_list); e!=list_end(&ready_list); e=list_next(e)){
		target = list_entry(e, struct thread, elem);
		recent_cpu_calculator(target);
		priority_calculator(target);
	}
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. 
   아이들 스레드. 다른 스레드가 실행 준비가 되지 않았을 때 실행됩니다.

   아이들 스레드는 처음에 thread_start()에 의해 준비 리스트에 추가됩니다.
   처음에 한 번 스케줄되며, 이 때 idle_thread를 초기화하고,
   thread_start()가 계속 진행할 수 있도록 전달된 세마포어에 "up" 연산을 수행한 후
   즉시 차단(block) 상태가 됩니다. 그 후, 아이들 스레드는 준비 리스트에
   나타나지 않습니다. 준비 리스트가 비어 있을 때 next_thread_to_run()에 의해
   특별한 경우로 반환됩니다.
   */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". 
		   인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

		   `sti` 명령어는 다음 명령어의 완료까지 인터럽트를 비활성화하므로,
		   이 두 명령어는 원자적으로 실행됩니다. 이 원자성은 중요합니다;
		   그렇지 않으면 인터럽트가 다시 활성화된 후 다음 인터럽트가 발생하기를 기다리는
		   사이에 인터럽트가 처리될 수 있으며, 최대 한 클록 틱 가치의 시간을
		   낭비할 수 있습니다.
		   s*/
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->original_priority = priority;
	t->waiting_lock = NULL;
	t->magic = THREAD_MAGIC;
	t->for_ticks=0;
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	list_init(&t->holding_locks);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
