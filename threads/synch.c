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
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters,&thread_current ()->elem, cmp_priority,NULL);
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
		list_sort(&sema->waiters, cmp_priority, NULL); // Nested donation으로 인해 변경된 우선순위를 정렬
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	// priority preemption
	test_max_priority(); // 다시 스케줄링 해주기
	
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
	// sema_down (&lock->semaphore);
	// lock->holder = thread_current ();

	/* advanced .. mlfqs인 경우 아래 return까지만 진행 */
	if (thread_mlfqs) {
		sema_down(&lock->semaphore);
		lock->holder = thread_current();
		return;
	}

	struct thread *curr = thread_current();

	/* 만약 해당 lock을 누가 사용하고 있다면 */
	if (lock->holder != NULL){
		curr->wait_on_lock = lock; // 현재 쓰레드의 wait_on_block 필드에 해당 lock을 저장.
		
		/* 현재 lock을 소유하고 있는 쓰레드의 donations에 현재 쓰레드를 저장 */
		list_insert_ordered(&lock->holder->donations, &curr->donation_elem, cmp_donation_priority, NULL);

		donate_priority();
	}
	/* 해당 lock의 waiting list에서 기다리가 자신의 차례가 되면, 
	CPU를 점유하고 나머지를 실행하여 lock을 획득한다. */
	sema_down(&lock->semaphore); 

	curr->wait_on_lock = NULL; // lock을 획득했으니 대기하고 있는 lock이 없음.

	lock->holder = thread_current(); // 
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

	/* advanced .. mlfqs인 경우 아래 return까지만 진행 */
	lock->holder = NULL;
	if (thread_mlfqs) {
		sema_up(&lock->semaphore);
		return;
	}

	remove_with_lock(lock); // donations 리스트에서 해당 lock을 필요로하는 쓰레드를 없애준다.
	refresh_priority(); 	// 현재 쓰레드의 우선순위를 업데이트

	lock->holder = NULL; // lock의 holder를 NULL로 만들어줌
	sema_up (&lock->semaphore); // semaphore를 UP시켜, 해당 lock에서 기다리고 있는 쓰레드 하나를 깨워준다.
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

	ASSERT (cond != NULL); // 전역변수 condition이 비어있다면 fail
	ASSERT (lock != NULL); // lock 
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
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

	if (!list_empty (&cond->waiters)) {
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}	
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

// cond waiters 안에는 세마포어를 담고 있는 semaphore_elem 구조체가 있다.
// 그 구조체 안에는 semaphore가 있고, semaphore 안에는 해당 semaphore를 기다리는 
// waiting_list가 있다. 그리고 이 안에는 쓰레드가 존재한다.
bool cmp_sem_priority (const struct list_elem *a, const struct list_elem *b, void *aux) {
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	struct list *list_a = &(sema_a->semaphore.waiters);
	struct list *list_b = &(sema_b->semaphore.waiters);

	struct thread *t_a = list_entry(list_begin(list_a), struct thread, elem);
	struct thread *t_b = list_entry(list_begin(list_b), struct thread, elem);

	return (t_a->priority > t_b->priority) ? 1 : 0 ;
}

/* - priority donation을 수행하는 함수 
현재 쓰레드가 기다리고 있는 lock과 연결된 모든 쓰레드를 순회하며 
현재 쓰레드의 우선순위를 Lock을 보유하고 있는 쓰레드에게 기부한다. */
void donate_priority(void) 
{
	int depth;
	struct thread *curr = thread_current();

	/* nested depth를 8로 제한 */
	for (depth=0; depth < 8; depth++){
		if (!curr->wait_on_lock) // running 쓰레드가 필요한 lock이 없다면 donation을 해줄 필요가 없음
			break;
		
		struct thread *holder = curr->wait_on_lock->holder; // 내가 필요한 lock을 잡고있는 쓰레드의 정보
		holder->priority = curr->priority; // 내가 필요한 lock을 잡고 있는 쓰레드에게 현재 쓰레드의 우선 순위를 준다.
										   // 즉, 현재 쓰레드가 원하는 lock을 갖기 위해 처리되어야하는 쓰레드들에게 우선순위를 넘겨줌
		curr = holder; // 다음 depth로 가기 위해 curr 갱신
	}
}
/* lock을 해제 했을 때, donations 리스트에서 해당 엔트리를 삭제하기 위한 역할 
현재 쓰레드의 donations 리스트를 확인하여 해지 할 lock을 보유하고 있는 엔트리를 삭제한다. */
void remove_with_lock(struct lock *lock) 
{
	struct list_elem *e;
	struct thread *curr = thread_current();

	for (e = list_begin(&curr->donations); e != list_end(&curr->donations); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock) { // donation_elem를 가진 쓰레드가 lock을 가지고 있다면
			list_remove(&t->donation_elem);
		}
	}
}
/* lock이 해제되었을 때, running 쓰레드의 priority를 갱신하는 작업 */
void refresh_priority(void) 
{ 
	struct thread *curr = thread_current(); // 현재 쓰레드의 정보

	curr->priority = curr->init_priority; // 우선 순위를 원복

	/* donation을 받고 있다면 */
	if (!list_empty(&curr->donations)) {
		list_sort(&curr->donations, cmp_donation_priority,NULL);
		
		struct thread *front = list_entry(list_front(&curr->donations), struct thread, donation_elem); // donations의 가장 높은 우선순위
		
		if (front->priority > curr->priority) // 만약 초기 우선 순위보다 더 큰 값이라면.
			curr->priority = front->priority; // donation 진행
	}
}

bool cmp_donation_priority (const struct list_elem *a, const struct list_elem *b, void *aux) {
	struct thread *thread_a = list_entry(a, struct thread, donation_elem);
	struct thread *thread_b = list_entry(b, struct thread, donation_elem);

	return (thread_a->priority > thread_b->priority) ? 1 : 0;
}
