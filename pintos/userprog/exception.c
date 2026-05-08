#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 처리한 페이지 폴트 수. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램이 일으킬 수 있는 인터럽트에 대한 핸들러를 등록한다.

   실제 유닉스 계열 운영체제라면 이런 인터럽트 대부분은 [SV-386] 3-24와
   3-25에서 설명하듯 시그널 형태로 사용자 프로세스에 전달된다. 하지만
   여기서는 시그널을 구현하지 않으므로, 대신 사용자 프로세스를 단순히
   종료시킨다.

   페이지 폴트는 예외적인 경우다. 여기서는 다른 예외와 똑같이 처리하지만,
   가상 메모리를 구현하려면 이 동작을 바꿔야 한다.

   각 예외에 대한 설명은 [IA32-v3a] 5.15절 "Exception and Interrupt
   Reference"를 참고하라. */
void
exception_init (void) {
	/* 이 예외들은 사용자 프로그램이 INT, INT3, INTO, BOUND 같은 명령어로
	   직접 일으킬 수 있다. 따라서 DPL==3으로 설정해서 사용자 프로그램이
	   이런 명령어로 호출할 수 있게 한다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이 예외들은 DPL==0이므로 사용자 프로세스가 INT 명령어로 직접 호출할
	   수 없다. 그래도 간접적으로는 발생할 수 있다. 예를 들어 #DE는 0으로
	   나누면 발생한다. */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트를 켠 상태로 처리할 수 있다. 페이지 폴트는
	   폴트 주소가 CR2에 저장되므로, 그 값을 보존하려면 인터럽트를 꺼야
	   한다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력한다. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스가 일으킨 것으로 보이는 예외를 처리한다. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 사용자 프로세스가 일으킨 것으로 보인다. 예를 들어
	   매핑되지 않은 가상 메모리에 접근해서 페이지 폴트가 났을 수 있다.
	   현재는 사용자 프로세스를 단순히 종료시킨다. 나중에는 커널에서
	   페이지 폴트를 처리하고 싶을 것이다. 실제 유닉스 계열 운영체제는
	   대부분의 예외를 시그널로 프로세스에 돌려보내지만, 여기서는 그걸
	   구현하지 않는다. */

	/* 인터럽트 프레임의 코드 세그먼트 값으로 예외가 어디서 시작됐는지 알 수
	   있다. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* 사용자 코드 세그먼트이므로 예상대로 사용자 예외다. 사용자
			   프로세스를 종료한다. */
			thread_current()->exit_status = -1;
			thread_exit ();

		case SEL_KCSEG:
			/* 커널 코드 세그먼트이므로 커널 버그를 뜻한다. 커널 코드는
			   예외를 일으키면 안 된다. (페이지 폴트가 커널 예외를 만들 수는
			   있지만, 그 경우에도 여기까지 오면 안 된다.) 이를 분명히
			   하기 위해 커널 패닉을 낸다. */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 코드 세그먼트인가? 일어나면 안 된다. 커널을 패닉시킨다. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러다. 가상 메모리를 구현하려면 이 골격 코드를 채워야
   한다. project 2의 일부 해법도 이 코드를 수정해야 할 수 있다.

   진입 시점에 폴트를 일으킨 주소는 CR2(Control Register 2)에 들어 있고,
   exception.h의 PF_* 매크로 형식에 맞춘 폴트 정보는 F의 error_code
   멤버에 들어 있다. 아래 예제 코드는 그 정보를 어떻게 해석하는지
   보여준다. 두 정보 모두에 대한 자세한 설명은 [IA32-v3a] 5.15절
   "Exception and Interrupt Reference"의 "Interrupt 14--Page Fault
   Exception (#PF)" 항목을 참고하라. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* 참이면 페이지 부재, 거짓이면 읽기 전용 페이지에 쓰기. */
	bool write;        /* 참이면 쓰기 접근, 거짓이면 읽기 접근. */
	bool user;         /* 참이면 사용자 접근, 거짓이면 커널 접근. */
	void *fault_addr;  /* 폴트가 난 주소. */

	/* 폴트를 일으킨 주소, 즉 접근하다가 오류가 난 가상 주소를 얻는다.
	   이 값은 코드나 데이터를 가리킬 수 있다. 반드시 폴트를 일으킨
	   명령어의 주소(f->rip)와 같지는 않다. */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 켠다. 인터럽트를 껐던 이유는 CR2가 바뀌기 전에
	   확실히 읽기 위해서뿐이었다. */
	intr_enable ();


	/* 원인을 판별한다. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* project 3 이후에서 사용한다. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* 페이지 폴트 수를 센다. */
	page_fault_cnt++;

	/* 실제 폴트라면 종료한다. */
	kill (f);
}
