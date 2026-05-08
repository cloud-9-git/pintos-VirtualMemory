/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었다.
   Nachos 저작권 고지는 아래에 전문을 그대로 실었다. */

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

/* 세마포어 `SEMA`를 `VALUE`로 초기화한다. 세마포어는
   음수가 아닌 정수 값과, 이를 조작하는 두 개의 원자적 연산으로
   이루어진다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤
     값을 감소시킨다.

   - up 또는 "V": 값을 증가시키고, 대기 중인 스레드가 있으면
     하나를 깨운다. */

static bool wake_up_less(const struct list_elem *, const struct list_elem *, void *aux);
static bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static void donate_priority(struct thread *current);
static void remove_donations(struct thread *current, struct lock *lock);
static void refresh_priority(struct thread *current);
static bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산.
   `SEMA` 값이 양수가 될 때까지 기다렸다가 원자적으로 감소시킨다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면
   안 된다. 인터럽트를 끈 상태로 호출할 수는 있지만, 실제로
   잠들게 되면 다음에 스케줄된 스레드가 인터럽트를 다시 켤 수 있다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산이지만,
   세마포어 값이 이미 0이 아닐 때만 수행한다.
   감소에 성공하면 true, 아니면 false를 반환한다.

   이 함수는 인터럽트 핸들러 안에서 호출할 수 있다. */
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

/* 세마포어에 대한 up 또는 "V" 연산.
   `SEMA` 값을 증가시키고, 대기 중인 스레드가 있으면 하나를 깨운다.

   이 함수는 인터럽트 핸들러 안에서 호출할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	bool should_yield = false;

	struct thread *cur = thread_current();
	if (!list_empty (&sema->waiters)) {
		list_sort(&sema->waiters, cmp_priority, NULL);
		struct thread *waiter = list_entry(list_pop_front(&sema->waiters),
										   struct thread, elem);
		thread_unblock(waiter);
		if (cur->priority < waiter->priority) {
			if (intr_context()) {
				intr_yield_on_return();
			} else {
				should_yield = true;
			}
		}
	}
	sema->value++;
	intr_set_level(old_level);

	if (should_yield) {
		thread_yield();
	}
}

static void sema_test_helper (void *sema_);

/* 두 스레드가 번갈아 제어권을 주고받도록 만들어 보는
   세마포어 자체 테스트다. 동작을 보고 싶으면 `printf()`를
   추가로 넣어 보면 된다. */
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

/* `sema_self_test()`에서 사용하는 스레드 함수. */
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

/* `LOCK`를 초기화한다. 락은 어떤 시점에도 최대 한 스레드만
   보유할 수 있다. Pintos의 락은 "재귀적(recursive)"이지 않으므로,
   이미 락을 쥔 스레드가 같은 락을 다시 획득하려 하면 오류다.

   락은 초기값이 1인 세마포어의 특수한 형태다. 다만 차이가
   두 가지 있다. 첫째, 세마포어 값은 1보다 클 수 있지만 락은
   한 번에 한 스레드만 소유할 수 있다. 둘째, 세마포어에는
   소유자가 없어서 어떤 스레드가 down한 뒤 다른 스레드가 up할
   수 있지만, 락은 같은 스레드가 획득과 해제를 모두 수행해야 한다.
   이런 제약이 부담스럽다면 락 대신 세마포어를 써야 한다는 신호다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* 필요하다면 잠들어서라도 `LOCK`를 획득한다.
   현재 스레드는 이미 이 락을 쥐고 있으면 안 된다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면
   안 된다. 인터럽트를 끈 상태에서 호출할 수는 있지만, 잠들어야
   하면 인터럽트가 다시 켜진다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *current = thread_current();
	struct thread *holder = lock->holder;
	if (lock->holder != NULL) {
		current->wait_on_lock = lock;
		list_insert_ordered(&holder->donations, &current->donation_elem, cmp_donation_priority, NULL);
		donate_priority(current);
	}

	sema_down(&lock->semaphore);   // 요청 스레드 블락(여기에서 잠듦)
	current->wait_on_lock = NULL;  // 현재 스레드 끝나서 락에서 나감
	lock->holder = current;        // 현재 스레드가 락을 잡음
}

/* `LOCK` 획득을 시도하고, 성공하면 true 아니면 false를 반환한다.
   현재 스레드는 이미 이 락을 쥐고 있으면 안 된다.

   이 함수는 잠들지 않으므로 인터럽트 핸들러 안에서 호출할 수 있다. */
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

/* 현재 스레드가 소유한 `LOCK`를 해제한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 그 안에서 락을
   해제하려는 시도도 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_donations(thread_current(), lock);
	refresh_priority(thread_current());
	lock->holder = NULL;

	sema_up (&lock->semaphore);
}

/* 현재 스레드가 `LOCK`를 보유하고 있으면 true, 아니면 false를
   반환한다. 다른 스레드가 락을 쥐고 있는지 검사하는 것은
   경쟁 상태를 일으킬 수 있다는 점에 주의하라. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트 안의 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소. */
	struct semaphore semaphore;         /* 해당 세마포어. */
	int priority;
};

/* 조건 변수 `COND`를 초기화한다. 조건 변수는 한 코드 조각이
   조건을 신호로 보내고, 협력하는 다른 코드가 그 신호를 받아
   동작하도록 해 준다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}
/* cond 정렬 비교함수
*/
bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	return sa->priority > sb->priority;
}

/* donation 정렬 비교함수 */
static bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct thread *da = list_entry(a, struct thread, donation_elem);
	struct thread *db = list_entry(b, struct thread, donation_elem);

	return da->priority > db->priority;
}

	/* `LOCK`를 원자적으로 해제하고, 다른 코드가 `COND`를 신호할 때까지
	   기다린다. `COND`가 신호되면 반환하기 전에 `LOCK`를 다시 획득한다.
	   이 함수를 호출하기 전에는 반드시 `LOCK`를 보유하고 있어야 한다.

	   이 함수가 구현하는 모니터는 "Hoare" 스타일이 아니라 "Mesa"
	   스타일이다. 즉, 신호를 보내는 것과 받는 것은 원자적 연산이 아니다.
	   따라서 보통은 대기가 끝난 뒤 조건을 다시 확인하고, 필요하면
	   다시 기다려야 한다.

	   하나의 조건 변수는 하나의 락에만 연결되지만, 하나의 락은
	   여러 조건 변수와 연결될 수 있다. 즉 락에서 조건 변수로는
	   일대다 관계다.

	   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면
	   안 된다. 인터럽트를 끈 상태에서 호출할 수는 있지만, 잠들어야
	   하면 인터럽트가 다시 켜진다. */
void
cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init(&waiter.semaphore, 0);
	waiter.priority = thread_current()->priority;
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sema_priority, NULL);
	lock_release(lock); // sema_down으로 잠들기 전에 락을 풀어줘야 다른 스레드가 락을 잡아 실행할 수 있음?
	sema_down (&waiter.semaphore);
	lock_acquire(lock); // signal을 받고 깨어나면 wait()가 리턴하기 전에 lock을 다시 잡는다.
}

/* `COND`에서 기다리는 스레드가 하나라도 있으면,
   그중 하나를 깨우도록 신호한다. 이 함수를 호출하기 전에는
   반드시 `LOCK`를 보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 그 안에서
   조건 변수에 신호를 보내려는 시도도 의미가 없다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty(&cond->waiters))
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
}

/* `COND`에서 기다리는 모든 스레드를 깨운다.
   이 함수를 호출하기 전에는 반드시 `LOCK`를 보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 그 안에서
   조건 변수에 신호를 보내려는 시도도 의미가 없다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/* 우선순위 기부 */
static void donate_priority(struct thread *current) {
	int depth = 0;

	while (current->wait_on_lock != NULL && depth < 8) {
		struct thread *holder = current->wait_on_lock->holder;

		if (holder == NULL) {
			break;
		}

		if (holder->priority >= current->priority) {
			break;
		}

		holder->priority = current->priority;
		current = holder;
		depth++;
	}
}

/* 우선순위 기부 제거 */
static void remove_donations(struct thread *current, struct lock *lock) {
	struct list_elem *le = list_begin(&current->donations);

	while (le != list_end(&current->donations)) {
		struct thread *donor = list_entry(le, struct thread, donation_elem);
		struct list_elem *next = list_next(le);

		if (donor->wait_on_lock == lock) {
			list_remove(le);
		}
		le = next;
	}
}

/* 우선순위 초기화 */
static void refresh_priority(struct thread *current) {
	current->priority = current->origin_priority;

	if (!list_empty(&current->donations)) {
		list_sort(&current->donations, cmp_donation_priority, NULL);
		struct thread *donor = list_entry(list_front(&current->donations), struct thread, donation_elem);

		if (current->priority < donor->priority) {
			current->priority = donor->priority;
		}
	}
}