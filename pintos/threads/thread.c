#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "devices/timer.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* `struct thread`의 `magic` 멤버에 넣는 임의 값.
   스택 오버플로 감지에 쓰인다. 자세한 내용은 `thread.h`
   상단의 긴 주석을 참고하라. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드용 임의 값.
   이 값은 수정하지 마라. */
#define THREAD_BASIC 0xd42df210

/* `THREAD_READY` 상태인 프로세스 목록.
   즉, 실행할 준비는 됐지만 실제로 실행 중은 아닌 프로세스들이다. */
static struct list ready_list;

/* 잠들어 있는 스레드들의 리스트. */
static struct list sleep_list;

/* idle 스레드. */
static struct thread *idle_thread;

/* 초기 스레드, 즉 `init.c:main()`을 실행하는 스레드. */
static struct thread *initial_thread;

/* `allocate_tid()`에서 쓰는 락. */
static struct lock tid_lock;

/* 스레드 파괴 요청 목록. */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks;   /* idle 상태로 보낸 타이머 tick 수. */
static long long kernel_ticks; /* 커널 스레드에서 보낸 타이머 tick 수. */
static long long user_ticks;   /* 사용자 프로그램에서 보낸 타이머 tick 수. */

/* 스케줄링. */
#define TIME_SLICE 4		  /* 각 스레드에 줄 타이머 tick 수. */
static unsigned thread_ticks; /* 마지막 양보 이후 경과한 타이머 tick 수. */

/* false(기본값)면 라운드 로빈 스케줄러를 사용한다.
   true면 다단계 피드백 큐 스케줄러를 사용한다.
   커널 명령줄 옵션 `-o mlfqs`로 제어한다. */
bool thread_mlfqs;
int load_avg;                  /* 실행 준비가 된 스레드 수의 이동 평균 */
static struct list all_list;   /* 전체 스레드 리스트 */

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static bool wake_up_less(const struct list_elem *, const struct list_elem *, void *aux);
static void schedule(void);

static tid_t allocate_tid(void);

static void update_priority(struct thread *current);
static void update_priority_all(void);
static void update_recent_cpu(struct thread *current);
static void update_recent_cpu_all(void);
static void update_load_avg(void);

/* `T`가 유효한 스레드를 가리키는 것처럼 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드를 반환한다.
 * CPU의 스택 포인터 `rsp`를 읽은 뒤, 이를 페이지 시작 주소까지
 * 내림한다. `struct thread`는 항상 페이지 맨 앞에 있고
 * 스택 포인터는 그 중간 어딘가에 있으므로, 이렇게 현재 스레드를
 * 찾을 수 있다. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// `thread_start`용 전역 디스크립터 테이블.
// GDT는 `thread_init` 뒤에 설정되므로, 먼저 임시 GDT를 구성해야 한다.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* 현재 실행 중인 코드를 스레드로 바꿔 스레딩 시스템을 초기화한다.
   일반적으로는 이런 방식이 불가능하지만, 여기서는 `loader.S`가
   스택 바닥을 페이지 경계에 맞춰 둔 덕분에 가능하다.

   실행 큐와 tid 락도 함께 초기화한다.

   이 함수를 호출한 뒤에는 `thread_create()`로 스레드를 만들기 전에
   반드시 페이지 할당기를 초기화해야 한다.

   이 함수가 끝나기 전에는 `thread_current()`를 호출해선 안 된다. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* 커널용 임시 GDT를 다시 로드한다.
	 * 이 GDT에는 사용자 문맥이 포함되어 있지 않다.
	 * 커널은 이후 `gdt_init()`에서 사용자 문맥을 포함해 GDT를 다시 만든다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* 전역 스레드 문맥을 초기화한다. */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list);
	list_init(&destruction_req);
	list_init(&all_list);

	/* 현재 실행 중인 스레드에 대한 구조체를 설정한다. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();

	list_push_back(&all_list, &initial_thread->allelem);
}

/* 인터럽트를 켜서 선점형 스레드 스케줄링을 시작한다.
   idle 스레드도 함께 생성한다. */
void thread_start(void)
{
	/* idle 스레드를 만든다. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);
	

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable();

	/* idle 스레드가 `idle_thread`를 초기화할 때까지 기다린다. */
	sema_down(&idle_started);
}

/* 매 타이머 tick마다 타이머 인터럽트 핸들러가 호출한다.
   따라서 이 함수는 외부 인터럽트 문맥에서 실행된다. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	if (thread_mlfqs) {
		if (t != idle_thread) {
			t->recent_cpu = FP_ADD_INT(t->recent_cpu, 1);
		}

		if (timer_ticks() % TIMER_FREQ == 0) {
			update_load_avg();
			update_recent_cpu_all();
		}

		if (timer_ticks() % 4 == 0) {
			update_priority_all();
		}

		if (!list_empty(&ready_list)) {
			struct thread *current = list_entry(list_front(&ready_list), struct thread, elem);
			if (current->priority > thread_current()->priority) {
				intr_yield_on_return();
			}
		}
	}

	/* 선점을 강제한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* 스레드 통계를 출력한다. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* 주어진 초기 `PRIORITY`를 가진 이름 `NAME`의 새 커널 스레드를 만들고,
   인자로 `AUX`를 넘겨 `FUNCTION`을 실행하게 한 뒤 준비 큐에 넣는다.
   생성에 성공하면 새 스레드의 식별자를, 실패하면 `TID_ERROR`를 반환한다.

   `thread_start()`가 이미 호출된 상태라면, 새 스레드는
   `thread_create()`가 반환되기 전에 스케줄될 수 있다.
   심지어 반환 전에 종료할 수도 있다. 반대로 기존 스레드가 새 스레드가
   스케줄되기 전까지 얼마든지 더 실행될 수도 있다. 순서를 보장해야 한다면
   세마포어나 다른 동기화 수단을 사용하라.

   제공된 코드는 새 스레드의 `priority` 멤버를 `PRIORITY`로 설정하지만,
   실제 우선순위 스케줄링은 아직 구현되어 있지 않다.
   우선순위 스케줄링은 Problem 1-3의 목표다. */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* 스레드를 할당한다. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드를 초기화한다. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	if (t != thread_current()) {
		t->nice = thread_current()->nice;
		t->recent_cpu = thread_current()->recent_cpu;
		if (thread_mlfqs) {
				update_priority(t);
		}
	}

	list_push_back(&all_list, &t->allelem);

	/* 스케줄되면 `kernel_thread`를 호출하게 한다.
	 * 참고로 `rdi`는 첫 번째 인자, `rsi`는 두 번째 인자다. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 큐에 추가한다. */
	thread_unblock(t);

	if (thread_current()->priority < t->priority)
	{
		thread_yield();
	}
	return tid;
}

/* `wakeup_tick` 시각이 된 스레드들을 깨운다. */
void thread_awake(int64_t now)
{
	bool need_preempt_any = false;
	struct thread *current = thread_current();

	while (!list_empty(&sleep_list))
	{
		struct list_elem *head = list_begin(&sleep_list);
		struct thread *t = list_entry(head, struct thread, elem);
		if (t->wakeup_tick > now)
			break;
		list_pop_front(&sleep_list);
		thread_unblock(t);

		if (current->priority < t->priority)
		{
			need_preempt_any = true;
		}
	}

	if (!list_empty(&ready_list) && need_preempt_any)
	{
		intr_yield_on_return();
	}
}

/* 현재 실행 중인 스레드를 sleep 리스트에 넣고 블록한다. */
void thread_sleep(int64_t ticks)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	int64_t start = timer_ticks();
	curr->wakeup_tick = start + ticks;
	list_insert_ordered(&sleep_list, &curr->elem, wake_up_less, NULL);
	thread_block();
	intr_set_level(old_level);
}

/* 현재 스레드를 잠재운다. `thread_unblock()`이 깨우기 전까지는
   다시 스케줄되지 않는다.

   이 함수는 인터럽트가 꺼진 상태에서 호출해야 한다.
   보통은 `synch.h`의 동기화 원시 기능을 쓰는 편이 더 낫다. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* `ta`가 `tb`보다 앞에 오려면 `wakeup_tick`이 더 이르거나,
   같다면 우선순위가 더 높아야 한다.
   같은 tick에 깨는 스레드는 우선순위가 높은 쪽을 먼저 꺼낸다. */
static bool
wake_up_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	if (ta->wakeup_tick != tb->wakeup_tick)
	{
		return ta->wakeup_tick < tb->wakeup_tick;
	}
	else
	{
		return ta->priority > tb->priority;
	}
}

bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	return ta->priority > tb->priority;
}

/* 블록된 스레드 `T`를 실행 가능한 준비 상태로 바꾼다.
   `T`가 블록 상태가 아니면 오류다. (실행 중인 스레드를 준비 상태로
   만들려면 `thread_yield()`를 써라.)

   이 함수는 현재 실행 중인 스레드를 선점하지 않는다.
   호출자가 직접 인터럽트를 꺼 둔 경우, 스레드를 원자적으로 깨우고
   다른 데이터도 함께 갱신할 수 있기를 기대할 수 있으므로 중요하다. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;
	ASSERT(is_thread(t));
	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);

	t->wakeup_tick = 0;
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* 현재 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* 현재 실행 중인 스레드를 반환한다.
   `running_thread()`에 몇 가지 안전성 검사를 더한 것이다.
   자세한 내용은 `thread.h` 상단의 긴 주석을 참고하라. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* `T`가 실제 스레드인지 확인한다.
	   이 assertion 둘 중 하나라도 터지면, 스레드 스택이 넘쳤을
	   가능성이 크다. 각 스레드의 스택은 4 kB보다 작으므로,
	   큰 자동 배열 몇 개나 적당한 재귀만으로도 스택 오버플로가 날 수 있다. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* 현재 실행 중인 스레드의 tid를 반환한다. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* 현재 스레드를 스케줄 대상에서 빼고 파괴한다.
   호출자에게는 돌아오지 않는다. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	list_remove(&thread_current()->allelem);

	/* 상태만 dying으로 바꾸고 다른 프로세스를 스케줄한다.
	   실제 파괴는 `schedule_tail()` 호출 중에 이뤄진다. */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* CPU를 양보한다. 현재 스레드는 잠들지 않으며,
   스케줄러 판단에 따라 곧바로 다시 실행될 수도 있다. */
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* 현재 스레드의 우선순위를 `NEW_PRIORITY`로 설정한다. */
void thread_set_priority(int new_priority)
{
	if (thread_mlfqs) {
		return;
	}

	struct thread *current = thread_current();
	bool isdonating = current->priority > current->origin_priority;

	current->origin_priority = new_priority;
	if (!isdonating)
	{
		current->priority = new_priority;
	}

	if (!list_empty(&ready_list))
	{
		struct list_elem *head_elem = list_begin(&ready_list);
		struct thread *head_thread = list_entry(head_elem, struct thread, elem);
		if (current->priority < head_thread->priority)
		{
			thread_yield();
		}
	}
}

/* 현재 스레드의 우선순위를 반환한다. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* 현재 스레드의 nice 값을 `NICE`로 설정한다. */
void thread_set_nice(int nice)
{
	enum intr_level old_level = intr_disable();
	
	struct thread *current = thread_current();
	current->nice = nice;

	update_priority(current);

	if (!list_empty(&ready_list)) {
		list_sort(&ready_list, cmp_priority, NULL);
		struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);

		if (t->priority > current->priority) {
			thread_yield();
		}
	}

	intr_set_level(old_level);
}

/* 현재 스레드의 nice 값을 반환한다. */
int thread_get_nice(void)
{
	enum intr_level old_level = intr_disable();
	int value = thread_current()->nice;
	intr_set_level(old_level);

	return value;
}

/* 시스템 load average의 100배 값을 반환한다. */
int thread_get_load_avg(void)
{
	enum intr_level old_level = intr_disable();
	int value = FP_TO_INT_ROUND(FP_MUL_INT(load_avg, 100));
	intr_set_level(old_level);

	return value;
}

/* 현재 스레드 recent_cpu 값의 100배를 반환한다. */
int thread_get_recent_cpu(void)
{
	enum intr_level old_level = intr_disable();
	int value = FP_TO_INT_ROUND(FP_MUL_INT(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);

	return value;
}

/* recent_cpu 값이 바뀜에 따라 우선순위 값을 갱신한다 */
static void update_priority(struct thread *current) {
	int priority = PRI_MAX - FP_TO_INT_ZERO(FP_DIV_INT(current->recent_cpu, 4)) - (current->nice * 2);

	if (priority < PRI_MIN) {
		priority = PRI_MIN;
	}
	else if (priority > PRI_MAX) {
		priority = PRI_MAX;
	}
	current->priority = priority;
}

static void update_priority_all(void) {
	struct list_elem *e;

	for (e=list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, allelem);

		if (t != idle_thread) {
			update_priority(t);
		}
	}
	list_sort(&ready_list, cmp_priority, NULL);
}

/* recent_cpu 값 갱신 */
static void update_recent_cpu(struct thread *current) {
	int coef = FP_DIV(FP_MUL_INT(load_avg, 2), FP_ADD_INT(FP_MUL_INT(load_avg, 2), 1));
	current->recent_cpu = FP_ADD_INT(FP_MUL(coef, current->recent_cpu), current->nice);
}

/* 전체 스레드 recent_cpu_all 값 갱신 */
static void update_recent_cpu_all(void) {
	struct list_elem *e;

	for (e=list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, allelem);

		if (t != idle_thread) {
			update_recent_cpu(t);
		}
	}
}

/* load_avg 값 갱신 */
static void update_load_avg(void) {
	int ready_threads = list_size(&ready_list);

	if (thread_current() != idle_thread) {
		ready_threads++;
	}

	load_avg = FP_ADD(FP_MUL(FP_DIV_INT(INT_TO_FP(59), 60), load_avg), FP_MUL_INT(FP_DIV_INT(INT_TO_FP(1), 60), ready_threads));
}

/* idle 스레드. 다른 어떤 스레드도 실행 준비가 안 되었을 때 실행된다.

   idle 스레드는 처음에 `thread_start()`가 준비 리스트에 넣는다.
   최초 한 번 스케줄되면 `idle_thread`를 초기화하고,
   넘겨받은 세마포어를 up해서 `thread_start()`가 계속 진행되게 한 뒤
   즉시 블록된다. 그 이후 idle 스레드는 준비 리스트에 나타나지 않는다.
   준비 리스트가 비어 있을 때 `next_thread_to_run()`이 특별히 반환한다. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* 다른 누군가가 실행되게 한다. */
		intr_disable();
		thread_block();

		/* 인터럽트를 다시 켜고 다음 인터럽트를 기다린다.

		   `sti` 명령은 다음 명령이 끝날 때까지 인터럽트를 막아 두므로,
		   이 두 명령은 원자적으로 실행된다. 이 원자성은 중요하다.
		   그렇지 않으면 인터럽트를 다시 켠 시점과 다음 인터럽트를
		   기다리는 시점 사이에서 인터럽트가 처리되어, 최대 한 클록 tick
		   정도의 시간이 낭비될 수 있다.

		   [IA32-v2a]의 "HLT", [IA32-v2b]의 "STI",
		   [IA32-v3a] 7.11.1 "HLT Instruction"을 참고하라. */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 본체로 쓰이는 함수. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* 스케줄러는 인터럽트를 끈 채 실행된다. */
	function(aux); /* 스레드 함수를 실행한다. */
	thread_exit(); /* `function()`이 반환하면 스레드를 끝낸다. */
}

/* `T`를 이름 `NAME`을 가진 블록 상태 스레드로 기본 초기화한다. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->origin_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);
	#ifdef USERPROG
	/* initial thread도 initd를 만든 뒤 process_wait()를 호출한다.
	 * 모든 thread에서 process 관련 리스트를 미리 초기화해 둔다. */
	list_init(&t->child_list);
	t->my_status = NULL;
	list_init(&t->fd_list);
	t->next_fd = 2;
	t->exec_file = NULL;
	#endif

	t->nice = 0;
	t->recent_cpu = 0;

	t->magic = THREAD_MAGIC;
}

/* 다음에 스케줄할 스레드를 골라 반환한다.
   실행 큐가 비어 있지 않다면 실행 큐에서 스레드를 반환해야 한다.
   (현재 실행 중인 스레드가 계속 실행 가능하다면 실행 큐에 들어 있다.)
   실행 큐가 비어 있으면 `idle_thread`를 반환한다. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* `iretq`를 사용해 스레드를 시작한다. */
void do_iret(struct intr_frame *tf)
{
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
		: : "g"((uint64_t)tf) : "memory");
}

/* 새 스레드의 페이지 테이블을 활성화해 스레드를 전환하고,
   이전 스레드가 dying 상태라면 파괴한다.

   이 함수가 호출될 시점에는 방금 `PREV` 스레드에서 전환이 일어났고,
   새 스레드는 이미 실행 중이며, 인터럽트는 여전히 꺼져 있다.

   스레드 전환이 끝나기 전에는 `printf()`를 호출하면 안전하지 않다.
   실무적으로는 함수 맨 끝에만 `printf()`를 두라는 뜻이다. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* 핵심 전환 로직.
	 * 먼저 전체 실행 문맥을 `intr_frame`에 복원하고,
	 * 그다음 `do_iret`를 호출해 다음 스레드로 전환한다.
	 * 전환이 끝날 때까지 여기서 스택을 써서는 안 된다. */
	__asm __volatile(
		/* 사용할 레지스터를 저장한다. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* 입력을 한 번만 읽어 둔다. */
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
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* 새 프로세스를 스케줄한다. 진입 시점에 인터럽트는 꺼져 있어야 한다.
 * 이 함수는 현재 스레드 상태를 `status`로 바꾸고,
 * 실행할 다른 스레드를 찾아 전환한다.
 * `schedule()` 안에서 `printf()`를 호출하는 것은 안전하지 않다. */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* 다음 스레드를 실행 중 상태로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 time slice를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새 주소 공간을 활성화한다. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* 방금 전환된 이전 스레드가 dying 상태라면 그 `struct thread`를
		   파괴해야 한다. 다만 `thread_exit()`가 자기 발밑을 걷어차지
		   않도록 이 작업은 늦게 해야 한다.
		   지금은 해당 페이지가 스택으로 쓰이고 있으므로, 여기서는
		   페이지 해제 요청만 큐에 넣는다.
		   실제 파괴 로직은 `schedule()` 시작 부분에서 실행된다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* 스레드를 전환하기 전에 현재 실행 중인 정보부터 저장한다. */
		thread_launch(next);
	}
}

/* 새 스레드에 사용할 tid를 반환한다. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}
