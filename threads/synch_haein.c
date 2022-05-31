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
/* 세마포어 유형 및 작업 */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": "wait" for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and "wake up" one waiting
   thread, if any). */
/* semaphore를 주어진 value로 초기화 */
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
/* semaphore를 요청하고 획득했을 때 value를 1 낮춤 */
/* semaphore를 얻고 waiters 리스트 삽입 시, 우선순위대로 삽입되도록 수정 */
/* sema_down에 lock이 반환되자마자 들어온 스레드는 sema→value가 현재 1인 상태이므로
   바로 공유자원을 사용하는 것 (while문 안에 안 들어가고)
   다른 if문 안에 들어있던 스레드는 sema_down에 들어가서 
   while문 안에 갖혀 있는 것(lock을 반환할 때까지) */
void
sema_down (struct semaphore *sema) { 
   enum intr_level old_level;

   ASSERT (sema != NULL);
   ASSERT (!intr_context ());

   old_level = intr_disable ();
   while (sema->value == 0) { // sema up 될 때까지 waiters 리스트에 묶여있음
                              // (스레드별)만약 공유자원이 사용 중이라면 while문 안을 돌음
                              // 현재 running 중인 스레드가 사용하고자 하는 공유자원에 대한
                              // semaphore의 value가 1로 풀어지면(sema_up이 일어나면) 공유자원을 사용할 수 있게 되므로
                              // waiting list에 있는 다음 스레드가 sema_down 내의 while문을 나와 공유자원을 사용
                              // 더이상 block하지 않고 while문을 벗어나 value를 0으로 바꾸고 잠궈준 다음에 사용 
      // list_push_back(&sema->waiters, &thread_current ()->elem);
      // semaphore를 얻고 waiters 리스트 삽입 시, 우선순위대로 삽입되도록 수정
      list_insert_ordered(&sema->waiters, &thread_current ()->elem, cmp_priority, NULL);
      thread_block (); // 해당 스레드를 잠에 재움. thread_unblock()될 때까지 스케쥴링되지 않음
                       // (스레드별)block 당하면서 CPU 주도권을 뺏김
   }
   // UP이 되어 while문을 빠져나온 다음 공유 자원을 차지했다. 
	// 자신이 공유자원을 사용중이므로 value를 DOWN한다. 
   sema->value--; 
   // sema의 value가 1이어서 해당 공유자원을 활용할 수 있는 상태가 되었을 때 잠그고 활용해야 하므로
   // sema의 value를 0으로 만들어줌
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
/* waiting list에서 그 다음 스레드가 공유 자원을 사용할 수 있도록 unblock */
/* semaphore를 반환하고 value를 1 높임 */
/* semaphore를 반환했으므로 sema_down()의 while문 안에 묶여있던 스레드가
   block 상태에서 unblock상태 (즉, ready 상태)로 만들어져야 함
   sema_up( )에선 &sema->waiters에 잠들어 있던 스레드를 ready 상태로 만들어주고
   ready list에 우선순위로 정렬해줌 */
/* waiter list에 있는 쓰레드의 우선순위가 변경 되었을 경우를 고려하여 waiter list를 정렬 (list_sort)
   세마포어 해제 후 priority preemption 기능 추가 
   == test_max_priority()를 통해 현재 수행중인 스레드와 ready list 맨 앞 스레드의 우선순위 비교 */
void
sema_up (struct semaphore *sema) {
   enum intr_level old_level;

   ASSERT (sema != NULL);

   old_level = intr_disable ();
   if (!list_empty (&sema->waiters)) {
      list_sort(&sema->waiters, cmp_priority, NULL); 
      // Nested donation으로 인해 변경된 우선순위 정렬
      // waiter list에 있는 쓰레드의 우선순위가 변경 되었을 경우를 고려하여 waiter list를 정렬 (list_sort)
      // 스레드 H의 우선순위는 스레드 L,M에게 모두 도네이션된 상태이므로 각 lock에서 waiting list에 있는 
      // sema->waiters 리스트엔 기부 받은 priority가 정렬되지 않은 상태로 되어 있음
      thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
      // waiters 리스트에 있던 것들 중 가장 앞에 있는 스레드(최대 우선순위)를 "ready list에" 정렬해서 넣어주고 ready 상태로 만들어줌
      // 그리고 ready list에 우선순위 순으로 정렬해줌
   }
   sema->value++;
   test_max_priority();
   // 잠들어 있던 스레드 중 waiter list의 맨 앞에 있던 스레드를 깨워서 ready list에 넣어줌
   // 이때 ready list가 한 번 갱신되었으므로, 선점형 스케쥴링이 가능하도록
   // 현재 수행중인 스레드와 ready list에서 가장 높은 우선순위를 가진 스레드의 우선순위를 비교하여 스케줄링 
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
/*
	LOCK
	세마포어의 특별한 버전, 1로 초기화된 값을 가지고 있음
	잠금을 한 thread만 잠금 해제 가능
	"down" (P) operation is called "acquire (잠그기 - 감소, 0이면 잠겨짐)".
	"up"(V) is called "release (풀어주기 - 1 증가)"

	세마포어 vs. 락
	1. 세마포어는 1보다 큰 값을 가질 수 있음. 그러나 락은 오직 하나의 스레드에 의해서만 소유될 수 있음.
	2. 세마포어는 오너를 가지고 있지 않음 == 하나의 스레드가 잠그면 다른 스레드가 풀 수 있음.
	   그러나 락은 동일한 스레드가 잠그고 풀어줘야 함.
*/
/* lock의 value(세마포어)를 1로 초기화해 준다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL; // holder는 지금 lock으로 제한되어 있는 공유자원을 사용하고 있는 스레드. 초기화이므로 아직 없음
	sema_init (&lock->semaphore, 1); // lock이 풀린 상태로 초기화해줌. value의 값이 0이라면 해당 공유자원은 사용 중인 것
									         // 1이라면 사용할 수 있는 공유 자원
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 해당 공유자원을 활용할 때 쓴다. 
   현재 공유자원을 사용하기 위해 lock의 sema의 value를 0으로 낮춰주고 (= 잠그고)
   그 다음 코드로 진행한다. */
/* 현재 락을 소유하고 있지 않은 스레드(semaphore의 value가 1)만 가능 */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ()); // 외부 인터럽트를 처리하는 중엔 true 아니면 false (외부 인터럽트가 처리되는 중엔 다른 작업 일어나면 안됨)
	ASSERT (!lock_held_by_current_thread (lock)); // 현재 running 중인 스레드가 락을 소유한 스레드와 같지 않아야 통과

   /* mlfqs인 경우 아래 return;까지만 진행 (priority donation 을 mlfqs 에서는 비활성화) */
   if (thread_mlfqs) { //mlfqs는 시간에 따라 priority가 재조정되므로 priority donation 사용 X
      sema_down (&lock->semaphore);
      lock->holder = thread_current ();
      return; 
   }

   struct thread* curr = thread_current();

   /* 만약 해당 lock을 누가 사용하고 있으면 lock이 반환될 때까지 기다려야
      lock holder가 lock을 반환해주면 if문을 빠져나오고 sema_down으로 lock을 획득함
    */
   if (lock->holder != NULL) {   // lock_release가 되면 lock->holder == NULL이 되어서 if문 빠져나옴
      curr->wait_on_lock = lock; // 현재 스레드의 wait_on_lock에 해당 lock을 저장
      
      list_insert_ordered(&lock->holder->donations, &curr->donation_elem, thread_compare_donate_priority, 0); 
      // 지금 lock을 소유하고 있는 스레드의 donations 에 현재 스레드를 우선순위순으로 저장
      // lock을 소유하고 있는 스레드에 donate한 스레드 원소들의 우선순위 vs. 현재 스레드의 우선순위

      donate_priority();
      // 우선순위 기부
   }

	sema_down (&lock->semaphore); // 해당 lock의 waiting list에서 기다리다 자신의 차례가 되면 CPU를 점유하고, lock을 획득
	
   curr->wait_on_lock = NULL; // lock을 획득했으므로 반환받기를 기다리고 있는 lock이 없음
   
   lock->holder = thread_current (); // 현재 lock의 권한을 갖고 있는 스레드를 명시
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
/* 해당 공유자원을 모두 사용했다면 holder를 NULL로 만들고 value를 1 올려준다.
   현재 공유자원을 사용하기 위해 lock의 sema의 value를 0으로 낮춰주고 (= 잠그고)
   그 다음 코드로 진행한다.
   이제 공유자원을 활용할 수 있으므로 waiting list에 있는 다음 스레드가 sema_down 내의 while문을 나와 공유자원을 사용한다. */
/* 현재 락을 소유하고 있는 스레드(semaphore의 value가 0)만 가능 */
// void
// lock_release (struct lock *lock) {
// 	ASSERT (lock != NULL);
// 	ASSERT (lock_held_by_current_thread (lock)); // 무조건 락을 소유하고 있는 스레드만 락을 풀어줄 수 있음

// 	lock->holder = NULL;
// 	sema_up (&lock->semaphore); // 현재 공유자원을 풀어주기 위해 semaphore를 요청하고 획득했을 때 value를 1 낮춤
// }

/* lock을 해제한 후, 현재 스레드의 donations 리스트에서
   해당 lock을 필요로 하는 스레드를 지워준다. → remove_with_lock() */
void lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	
   /* mlfqs인 경우 아래 return;까지만 진행 (priority donation 을 mlfqs 에서는 비활성화) */
   lock->holder = NULL;
   if (thread_mlfqs) {
      sema_up (&lock->semaphore);
      return ;
   }

	remove_with_lock(lock); // donations 리스트에서 해당 lock을 필요로 하는 스레드를 없애준다. (더이상 걔의 우선순위로 갱신되면 안 됨)
	refresh_priority();  // lock이 해제되었을 때, running 쓰레드의 priority를 갱신하는 작업 

	lock->holder = NULL;  // lock의 holder를 NULL로.
	sema_up (&lock->semaphore);  // sema를 UP 시켜 해당 lock에서 기다리고 있는 스레드 하나를 깨운다.
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
/* 현재 running 중인 스레드와 lock을 소유한 스레드가 같다면 True 반환 */
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

/* 첫번째 인자로 주어진 세마포어를 위해 대기 중인 가장 높은 우선순위의
스레드와 두번째 인자로 주어진 세마포어를 위해 대기 중인 가장 높은
우선순위의 스레드와 비교 */
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct semaphore_elem* sema_a = list_entry(a, struct semaphore_elem, elem); // sema_a: 각 스레드의 elem의 구조체인 waiter
	struct semaphore_elem* sema_b = list_entry(b, struct semaphore_elem, elem);

	struct list_elem* waiting_list_a = &(sema_a->semaphore.waiters); // waiter->semaphore->waiters: 즉 해당 스레드의 semaphore의 waiters
	struct list_elem* waiting_list_b = &(sema_b->semaphore.waiters);

	struct thread* thread_a = list_entry(list_begin(waiting_list_a), struct thread, elem);
	struct thread* thread_b = list_entry(list_begin(waiting_list_b), struct thread, elem);

	return thread_a->priority > thread_b->priority;
}

/* lock을 사용하고 싶을 때 스레드 A에 의해 이미 점유되어 있다면, 
   lock_acquire() 호출한 스레드들이 우선순위 내림차순으로
   스레드 A의 thread 구조체 안의 donations 리스트에 들어가야 함
 */
bool thread_compare_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux){
	struct thread* thread_a = list_entry(a, struct thread, donation_elem); 
	struct thread* thread_b = list_entry(b, struct thread, donation_elem);

	return thread_a->priority > thread_b->priority;
}

/* 나의 우선순위를 holder에게 donate
   nested donation을 고려하여 구현 */
void donate_priority(){
	int depth;
	struct thread* curr = thread_current();

	/* 최대 depth는 8이다. */
	for (depth = 0; depth < 8; depth++){
		if (!curr->wait_on_lock)   // 더 이상 nested가 없을 때.
			break;
		
		struct thread* holder = curr->wait_on_lock->holder;
		holder->priority = curr->priority;   // 우선 순위를 donation한다.
		curr = holder;  //  그 다음 depth로 들어간다.
	}
}

/* lock을 해제 했을 때, donations 리스트에서 해당 엔트리를 삭제하기 위한 역할 
현재 쓰레드의 donations 리스트를 확인하여 해지 할 lock을 보유하고 있는 엔트리를 삭제한다. */
void remove_with_lock(struct lock* lock){
	struct list_elem* e;
	struct thread* curr = thread_current();

	for (e = list_begin(&curr->donations); e != list_end(&curr->donations); e = list_next(e)){
		struct thread* t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock){ 
			// donations에 있는 스레드 중 요청하고 반환받기를 기다리고 있는 lock과
			// release해주려고 하는 lock이 일치하면 donations 리스트에서 donation_elem 지워줌
			list_remove(&t->donation_elem);
		}
	}
}

/* lock이 해제되었을 때, running 쓰레드의 priority를 갱신하는 작업 */
void refresh_priority(){
	struct thread* curr = thread_current();

	curr -> priority = curr->init_priority;  // 우선 원복해준다.

	/* donation을 받고 있다면 */
	if (!list_empty(&curr->donations)){
			// curr의 donations에 있는 스레드 priority 내림차순 정렬
			list_sort(&curr->donations, thread_compare_donate_priority, 0);

			// 맨 앞에 있는 것을 뽑아냄
			struct thread* front = list_entry(list_front(&curr->donations), struct thread, donation_elem);
			
			// donations의 맨 앞 > 처음 우선순위
			if(front->priority > curr->priority) 
				curr->priority = front->priority;
	}
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* condition variable 자료구조를 초기화 */
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

/*
   원자적으로 LOCK을 해제하고 COND가 다른 코드 조각에 의해 신호를 받을 때까지 기다립니다. 
   COND가 신호를 받은 후 반환되기 전에 LOCK이 다시 획득됩니다. 이 함수를 호출하기 전에 LOCK을 유지해야 합니다.
   이 기능으로 구현된 모니터는 "Hoare" 스타일이 아닌 "Mesa" 스타일입니다. 
   즉, 신호 송수신이 원자적 연산이 아닙니다. 
   따라서 일반적으로 호출자는 대기가 완료된 후 조건을 다시 확인하고 필요한 경우 다시 기다려야 합니다.
   주어진 조건 변수는 단일 잠금에만 연결되지만 하나의 잠금은 여러 조건 변수와 연결될 수 있습니다. 
   즉, 잠금에서 조건 변수로의 일대다 매핑이 있습니다.
   이 함수는 휴면 상태일 수 있으므로 인터럽트 처리기 내에서 호출하면 안 됩니다. 
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만 잠자기 상태가 필요한 경우 인터럽트가 다시 켜집니다.
*/
/* condition variable을 통해 signal이 오는지 가림 */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0); // waiter의 멤버변수의 semaphore의 value를 0으로 초기화
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
	lock_release (lock); 
	sema_down (&waiter.semaphore); 
   // 0으로 초기화되어 있어 잠겨있는 상태이므로
   // 10개 스레드 다 잠재워줌 cond_signal 받아서 sema_up() 시켜줄 때까지 block 상태
   // 여기서 막혀있음 -> thread_block() 시키고 schedule () 해줄 때 block되면 스케쥴링되지 않으므로 next 스레드가 idle밖에 없음
   // idle로 주도권이 전환됨 -> 그래서 다음 for문으로 넘어가는 것
	lock_acquire (lock);
   // cond_signal 되면 비로소 실행되는 코드
   // 이 코드 이후, cond_wait (&condition, &lock);에 막혀있던 코드에서 다음 코드가 실행됨
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* condition variable에서 기다리는 가장 높은 우선순위의 스레드에 signal을 보냄 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){ // 기다리는 스레드가 있을 때
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* condition variable에서 기다리는 모든 스레드에 signal을 보냄 */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}