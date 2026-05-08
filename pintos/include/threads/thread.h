#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef USERPROG
struct child_status;
#endif
#ifdef VM
#include "vm/vm.h"
#endif


/* 스레드 생명주기의 상태들. */
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 스레드. */
	THREAD_READY,       /* 실행 중은 아니지만 실행 준비가 된 상태. */
	THREAD_BLOCKED,     /* 어떤 이벤트가 발생하기를 기다리는 상태. */
	THREAD_DYING        /* 곧 파괴될 상태. */
};

/* 스레드 식별자 타입.
   원하는 타입으로 다시 정의해도 된다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* `tid_t`의 오류 값. */

/* 스레드 우선순위. */
#define PRI_MIN 0                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63                      /* 가장 높은 우선순위. */

/* 커널 스레드 또는 사용자 프로세스.
 *
 * 각 스레드 구조체는 자신만의 4 kB 페이지에 저장된다.
 * 스레드 구조체 자체는 페이지 맨 아래(오프셋 0)에 놓이고,
 * 나머지 영역은 스레드의 커널 스택으로 예약된다. 이 스택은
 * 페이지 맨 위(오프셋 4 kB)에서 아래 방향으로 자란다.
 * 그림으로 나타내면 다음과 같다.
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
 * 여기서 중요한 점은 두 가지다.
 *
 *    1. 첫째, `struct thread`가 너무 커지면 안 된다.
 *       너무 커지면 커널 스택을 둘 공간이 부족해진다.
 *       기본 `struct thread`는 몇 바이트밖에 되지 않으므로,
 *       1 kB보다 훨씬 작게 유지하는 편이 좋다.
 *
 *    2. 둘째, 커널 스택도 너무 커지면 안 된다.
 *       스택이 넘치면 스레드 상태를 망가뜨린다. 따라서
 *       커널 함수는 큰 구조체나 배열을 비정적 지역 변수로
 *       잡지 말아야 한다. 대신 `malloc()`이나
 *       `palloc_get_page()` 같은 동적 할당을 사용하라.
 *
 * 이런 문제의 첫 징후는 보통 `thread_current()`에서 나는
 * assertion 실패다. 이 함수는 실행 중인 스레드의
 * `struct thread` 안 `magic` 멤버가 `THREAD_MAGIC`인지
 * 검사한다. 스택 오버플로가 나면 대개 이 값이 바뀌어
 * assertion이 발생한다. */
/* `elem` 멤버는 두 가지 용도로 쓰인다. `thread.c`의 실행
 * 큐 원소가 될 수도 있고, `synch.c`의 세마포어 대기 리스트
 * 원소가 될 수도 있다. 이 두 용도가 동시에 겹치지 않는 이유는
 * 준비 상태의 스레드만 실행 큐에 있고, 블록 상태의 스레드만
 * 세마포어 대기 리스트에 있기 때문이다. */
struct file;
struct fd_entry {
	int fd;                    /* fd 번호 */
	struct file *file;         /* 실제 열린 파일 객체 */
	struct list_elem elem;     /* fd_list에 연결되기 위한 노드 */
};

struct  thread {
	/* `thread.c`가 관리한다. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름(디버깅용). */
	int priority;                       /* 우선순위. */
	int origin_priority;                /* donate 받기 전 우선순위 */

	struct lock *wait_on_lock;          /* 현재 스레드에서 기다리고 있는 lock */
	struct list donations;              /* 우선순위 기부해준 리스트 목록 */
	struct list_elem donation_elem;     /* 타 스레드 donation 리스트의 원소 */

	int nice;                           /* nice 값 */
	int recent_cpu;                     /* 스레드가 최근에 사용한 CPU양 */

	/* 스레드가 깨어나야 하는 절대 tick 시각. */
	int64_t wakeup_tick;

	/* `thread.c`와 `synch.c`가 함께 사용한다. */
	struct list_elem elem;              /* 리스트 원소. */
	struct list_elem allelem;           /* 전체 리스트용 원소 */

	int exit_status;               /* 종료 코드 */

#ifdef USERPROG
	/* `userprog/process.c`가 관리한다. */
	uint64_t *pml4;                     /* 4단계 페이지 맵. */
 	struct list child_list;            /* 자식 스레드 리스트 */
	struct child_status *my_status;      /* 본인의 스레드 상태 */
	struct list fd_list;	// 현재 프로세스가 열어둔 파일 목록
	int next_fd;	// Open 때 줄 번호
	struct file *exec_file;             /* 스레드가 지금 실행 중인 파일 */
#endif

#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리용 테이블. */
	struct supplemental_page_table spt;
#endif

	/* `thread.c`가 관리한다. */
	struct intr_frame tf;               /* 문맥 전환용 정보. */
	unsigned magic;                     /* 스택 오버플로를 감지한다. */
};

/* 고정소수점 */
#define F (1 << 14)
#define INT_TO_FP(n) ((n) * F)                  /* 정수를 고정소수점으로 변환 */
#define FP_TO_INT_ZERO(x) ((x) / F)             /* 고정소수점을 정수로 변환, 0을 향해 반올림 */
#define FP_TO_INT_ROUND(x) \            
	((x) >= 0 ? (((x) + F / 2) / F) : (((x) - F / 2) / F)) /* 고정소수점을 정수로 변환, 가장 가까운 정수로 반올림 */
#define FP_ADD(x, y) ((x) + (y))                /* x와 y 더하기 */
#define FP_SUB(x, y) ((x) - (y))                /* x에서 y 빼기 */
#define FP_ADD_INT(x, n) ((x) + (n) * F)        /* x와 n 더하기 */
#define FP_SUB_INT(x, n) ((x) - (n) * F)        /* x에서 n 빼기 */
#define FP_MUL(x, y) ((int64_t)(x) * (y) / F)   /* x와 y 곱하기 */
#define FP_MUL_INT(x, n) ((x) * (n))            /* x와 n 곱하기 */
#define FP_DIV(x, y) ((int64_t)(x) * F / (y))   /* x를 y로 나누기 */
#define FP_DIV_INT(x, n) ((x) / (n))            /* x를 n으로 나누기 */

/* false(기본값)면 라운드 로빈 스케줄러를 사용한다.
   true면 다단계 피드백 큐 스케줄러를 사용한다.
   커널 명령줄 옵션 `-o mlfqs`로 제어한다. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_sleep (int64_t);
void thread_block (void);
void thread_awake (int64_t);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
bool cmp_priority(const struct list_elem *a,
				  const struct list_elem *b,
				  void *aux);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
