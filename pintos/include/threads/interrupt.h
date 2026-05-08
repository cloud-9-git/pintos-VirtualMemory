#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* 인터럽트가 켜져 있는가, 꺼져 있는가? */
enum intr_level {
	INTR_OFF,             /* 인터럽트 비활성화. */
	INTR_ON               /* 인터럽트 활성화. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* 인터럽트 스택 프레임. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;  // 네번째 인자
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;  // 두번째 인자
	uint64_t rdi;  // 첫번째 인자
	uint64_t rbp;
	uint64_t rdx;  // 세번째 인자
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

struct intr_frame {
	/* `intr-stubs.S`의 `intr_entry`가 푸시한다.
	   인터럽트된 작업의 레지스터를 저장한 값이다. */
	struct gp_registers R;
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;
	uint16_t __pad3;
	uint32_t __pad4;
	/* `intr-stubs.S`의 `intrNN_stub`가 푸시한다. */
	uint64_t vec_no; /* 인터럽트 벡터 번호. */
/* 경우에 따라 CPU가 푸시하고,
   그렇지 않으면 일관성을 위해 `intrNN_stub`가 0을 푸시한다.
   CPU는 원래 이를 `eip` 바로 아래에 두지만, 여기로 옮겨 둔다. */
	uint64_t error_code;
/* CPU가 푸시한다.
   인터럽트된 작업의 레지스터를 저장한 값이다. */
	uintptr_t rip;    // 인터럽트가 걸리기 직전에 실행하던 명령어 주소. 복귀하면 여기부터 실행
	uint16_t cs;      // 코드 세그먼트 선택자. 사용자 모드 코드인지, 커널 코드인지 구분하는데 쓰임
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;  // CPU 상태 플래그 레지스터, 인터럽트 허용 여부, 산술 결과 플래그, 방향 플래그 등이 들어있음
	uintptr_t rsp;    // 인터럽트 직전의 스택 포인터, 복귀 시 원래 위치로 돌아가기 위해 사용
	uint16_t ss;      // 스택 세그먼트 선택자, 사용자 모드와 커널 모드를 오갈 때 사용
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
