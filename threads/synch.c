/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {

		//기존 로직 비활성화
		//list_push_back (&sema->waiters, &thread_current ()->elem);

		//대기 리스트에 스레드를 우선순위로 삽입하는 로직 추가
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_priority_compare, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		list_sort(&sema->waiters, thread_priority_compare, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	// 스레드 리스케줄
	thread_check_reschedule();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}
/*
 * 우선 순위 기부함수
 * wait_on_lock에서 기다리고 있는 lock을 현재 점유하고 있는 
 * holder들을 순회하면서 모두에게 자신의 우선순위를 기부
 * depth는 8 이하
 * 연결된 wait_on_lock이 없으면 종료
 */
void donate_priority(void)
{
	int depth;
    struct thread *cur = thread_current();
    
    for (depth =0; depth < 8; depth++) {
    	if (!cur->wait_on_lock)
        	break;
        struct thread *holder = cur->wait_on_lock->holder;
        holder->priority = cur->priority;
        cur = holder;
    }
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	/* 
	 * 현재 lock을 소유하는 스레드가 있다면
	 * lock 소유자의 기부목록에 현재 스레드를 우선순위에 따라 삽입하고
	 * 우선순위를 기부한다
	 */
	struct thread *cur = thread_current (); 
	if (lock->holder) {	
        cur->wait_on_lock = lock; 
        list_insert_ordered(&lock->holder->donations, &cur->donation_elem, 
        thread_donate_priority_compare, 0);
        donate_priority(); 	
    }

	sema_down (&lock->semaphore);
    //락을 획득한 후 현재 스레드가 대기 중인 락을 NULL로 설정
	cur->wait_on_lock = NULL;
	lock->holder = thread_current ();
}

// 우선순위 기부 목록을 우선순위 순으로 정렬하는 함수
bool
thread_donate_priority_compare (const struct list_elem *l, 
				const struct list_elem *s, void *aux UNUSED)
{
	return list_entry (l, struct thread, donation_elem)->priority
		 > list_entry (s, struct thread, donation_elem)->priority;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* remove_with_lock 함수: 특정 락에 대한 우선순위 기부를 제거하는 함수
 * 인자로 받은 락(lock)을 기다리고 있는 모든 스레드들의 우선순위 기부를 현재 스레드의 기부 목록에서 제거합니다.
 */
void remove_with_lock (struct lock *lock) {
  struct list_elem *e;
  struct thread *cur = thread_current ();

  // 현재 스레드의 기부 목록을 순회하면서
  for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, donation_elem);
    // 해당 락을 기다리고 있는 스레드의 기부를 목록에서 제거
    if (t->wait_on_lock == lock)
      list_remove (&t->donation_elem);
  }
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
/* 세마포어를 연결 리스트의 한 요소로 만들기 위해 구조체를 생성
 * semaphore_elem 이라는 새로운 구조체 타입을 정의
 * elem은 구조체를 연결리스트에 포함시키기 위한 변수
 * semaphore는 세마포어 자체를 나타내는 변수
 */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	//기존 로직 비활성화
	//list_push_back (&cond->waiters, &waiter.elem);

	// 세마포어의 우선순위를 고려하여 리스트에 삽입
	list_insert_ordered(&cond->waiters, &waiter.elem, semaphore_priority_compare, NULL);

	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}
/*세마포어의 우선순위 비교 함수
 * 리스트의 요소 중 2개를 인자로 전달받은 후
 * 그 세마포어의 구성 원소 중 스레드의 priority 필드를 비교
 * 스레드 우선순위 비교 함수를 호출 후 결과를 반환
 */
bool semaphore_priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list_elem *ta = list_begin(&sa->semaphore.waiters);
	struct list_elem *tb = list_begin(&sb->semaphore.waiters);
	return thread_priority_compare(ta, tb, NULL);
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		//기존 로직 비활성화
		//list_sort(&cond->waiters, thread_priority_compare, NULL);

		//스레드의 우선순위가 아닌 세마포어의 우선순위로 waiters_list를 정렬
		list_sort(&cond->waiters, semaphore_priority_compare, NULL);

		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
