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
	list_init (&sema->sema_waiters);
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

	old_level = intr_disable ();//인터럽트 끄기
	while (sema->value == 0) {
		list_insert_ordered(&sema->sema_waiters, &thread_current()->elem, priority_higher, NULL);
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
/* 최신 우선순위 기준으로 대기자를 선택 */
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();//인터럽트 끄기
	if (!list_empty (&sema->sema_waiters)){
		//대기목록이 비어있지 않다면
	/* 현재는 목록의 첫 번째 대기자를 우선 후보로 삼는다 (가장높은 우선순위)
    뒤의 대기자들과 비교해 더 높은 우선순위가 있는지 찾을 때 기준점으로 사용 */	
	struct list_elem *best_e=list_begin(&sema->sema_waiters);

	/* 리스트 요소(best_e)가 들어 있는 thread 구조체를 찾아 포인터로 가져온다 */
	//best_t : 최선 후보
	struct thread *best_t=list_entry(best_e, struct thread, elem);
	
	/* 첫 번째 후보 다음 요소부터 목록 끝까지 확인한다. */
	struct list_elem *e = list_next(best_e);

while (e != list_end (&sema->sema_waiters)) {
    /* 현재 리스트 요소가 가리키는 대기 스레드를 가져온다. */
    struct thread *cd = list_entry(e, struct thread, elem);

    /* 후보보다 현재 우선순위가 더 높은 스레드가 있으면 최선의 후보를 변경
    우선순위가 같은 경우에는 후보를 바꾸지 않아 먼저 기다린 순서를 유지. */
    if(cd->priority > best_t->priority) {
        best_e = e;
        best_t = cd; }

    e = list_next (e);//다음 대기자를 확인
}

/* 가장 우선순위가 높은 대기자를 대기 목록에서 제거 */
list_remove(best_e);

/* 선택한 대기 스레드를 BLOCKED 상태에서 READY 상태로 변경 */
thread_unblock(best_t); }
	sema->value++;
	intr_set_level (old_level);

	thread_wake_up();
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

	// Lock 소유자가 존재하면 기부 관계를 등록하고, 필요할 때만 소유자의 우선순위를 높이는 방식. 실질적인 기부 작업이 이루어지는 곳!!!	
	if (lock->holder != NULL) {
		thread_donate_on_wait(thread_current(), lock);
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
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
	ASSERT (lock != NULL); // 락이 정말 존재하는가?
	ASSERT (lock_held_by_current_thread (lock)); // 현재 스레드가 정말 이 Lock을 소유하고 있는가?

	thread_remove_lock_donations(lock);
	// thread_clear_lock_wait(); // 현재 스레드의 락 정보를 null로 변경
	lock->holder = NULL;
	sema_up (&lock->semaphore);
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

	list_init (&cond->cond_waiters);
}

/* Compares the value of two list elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
bool cond_priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	/* condition.waiters의 저장되어 있는 sema_elem (a, b)을 elem을 통해 추출*/
	const struct semaphore_elem *sema_elem_a = list_entry(a, struct semaphore_elem, elem);
	const struct semaphore_elem *sema_elem_b = list_entry(b, struct semaphore_elem, elem);
	
	/* condition.waiters 리스트가 비어 있을 시 */
	// if(list_empty(&sema_elem_a->semaphore.sema_waiters) || list_empty(&sema_elem_b->semaphore.sema_waiters)){
	// 	return true;
	// }

	const struct list sema_wait_list_a = sema_elem_a->semaphore.sema_waiters; /* sema_elem_a의 waiters 중 최고 우선순위 스레드를 가져와 비교 */
	// const struct list_elem *s = list_begin(&sema_wait_list_a);
	// for(s; s != list_end(&sema_wait_list_a); s = list_next(s)){
	// 	struct thread *t = list_entry(s, struct thread, elem);
	// 	struct thread *next_t = list_entry(list_next(s), struct thread, elem);
	// 	if(t->priority < next_t->priority){
			
	// 	}
	// }
	const struct list sema_wait_list_b = sema_elem_b->semaphore.sema_waiters; /* sema_elem_b의 waiters 중 최고 우선순위 스레드를 가져와 비교 */
	const struct list_elem *list_elem_a = list_begin(&sema_wait_list_a);
	const struct list_elem *list_elem_b = list_begin(&sema_wait_list_b);
	const struct thread *thread_a = list_entry(list_elem_a, struct thread, elem);
	const struct thread *thread_b = list_entry(list_elem_b, struct thread, elem);

	if(thread_a->priority > thread_b->priority){
		return true;
	}else{
		return false;
	}
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

	sema_init (&waiter.semaphore, 0); /* 이게 무슨 의미지? */
	list_push_back (&cond->cond_waiters, &waiter.elem); 
	/* 
		priority-condvar.c 테스트 코드의 스레드 생성 흐름대로 살펴본다면 23,22,21
		그러면 현재 &cond->cond_waiters = {23, 22, 21.. } 순으로 삽입된다.	
	*/

	// list_insert_ordered(&cond->cond_waiters, &waiter.elem, cond_priority_higher, NULL);
	lock_release (lock); /* condition의 대기 목록에 추가되었기 때문에 Lock을 해제한다. */
	sema_down (&waiter.semaphore); /* 이게 무슨 의미지? */
	lock_acquire (lock);
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

	/* cond_waiters 리스트 내림차순 정렬 */
	list_sort(&cond->cond_waiters, cond_priority_higher, NULL);

	if (!list_empty (&cond->cond_waiters))
		sema_up (&list_entry (list_pop_front (&cond->cond_waiters),
					struct semaphore_elem, elem)->semaphore);
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

	while (!list_empty (&cond->cond_waiters))
		cond_signal (cond, lock);
}
