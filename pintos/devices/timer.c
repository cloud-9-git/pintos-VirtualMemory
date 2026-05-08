#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "list.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고하라. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 누적된 타이머 tick 수. */
static int64_t ticks;

/* 타이머 tick당 루프 횟수.
   `timer_calibrate()`가 초기화한다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 Programmable Interval Timer(PIT)가 초당 `PIT_FREQ`번
   인터럽트를 발생시키도록 설정하고, 해당 인터럽트를 등록한다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 `TIMER_FREQ`로 나눈 값을 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* 제어어: 카운터 0, LSB 후 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연 구현에 쓰이는 `loops_per_tick`을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* 한 타이머 tick보다 작은 최대 2의 거듭제곱으로
	   `loops_per_tick`의 초기값을 잡는다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* `loops_per_tick`의 다음 8비트를 더 정밀하게 조정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS가 부팅된 뒤의 타이머 tick 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* `THEN` 이후 경과한 타이머 tick 수를 반환한다.
   `THEN`은 이전에 `timer_ticks()`가 반환한 값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 대략 `TICKS`만큼 실행을 멈춘다. */
/* 현재 스레드가 "지금부터 `ticks`만큼 CPU를 받지 않겠다"라고 선언하는 셈이다. */
void
timer_sleep (int64_t ticks) { // 지정한 tick 동안 잠든다.
	ASSERT (intr_get_level () == INTR_ON);
	// `alarm-zero`, `alarm-negative` 테스트를 위해 0 이하는 즉시 반환한다.
	if (ticks <= 0)
		return;
	thread_sleep (ticks);
}


/* 대략 `MS`밀리초 동안 실행을 멈춘다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 대략 `US`마이크로초 동안 실행을 멈춘다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 대략 `NS`나노초 동안 실행을 멈춘다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_awake(ticks); // 기상 시각이 지난 스레드를 꺼내 깨운다.
	thread_tick (); // 현재 스레드의 실행 tick을 누적하고, time slice를 다 썼으면 양보를 예약한다.
}

/* `LOOPS`번 반복이 한 타이머 tick보다 오래 걸리면 true,
   아니면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 tick 하나를 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* `LOOPS`번 반복한다. */
	start = ticks;
	busy_wait (loops);

	/* tick 수가 바뀌었다면 너무 오래 반복한 것이다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 단순 루프를 `LOOPS`번 반복한다.
 *
 * 코드 정렬이 타이밍에 큰 영향을 줄 수 있으므로 `NO_INLINE`을
 * 붙인다. 이 함수가 위치마다 다르게 인라인되면 결과를
 * 예측하기 어려워질 수 있다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 대략 `NUM / DENOM`초 동안 잔다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* `NUM / DENOM`초를 타이머 tick으로 변환하고 버림한다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 한 개 이상의 전체 타이머 tick을 기다리는 경우다.
		   `timer_sleep()`은 CPU를 다른 프로세스에 넘겨주므로 이를 쓴다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면 더 정확한 sub-tick 타이밍을 위해
		   busy-wait 루프를 사용한다. 오버플로를 피하려고
		   분자와 분모를 1000으로 나눠 축소한다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
