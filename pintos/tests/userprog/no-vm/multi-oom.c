/* 자식이 더 이상 fork하지 못할 때까지 재귀적으로 fork한다.
   적어도 28개 복사본은 실행될 것으로 기대한다.
   
   새 프로세스를 시작하지 못하기 전까지 커널이 몇 개의 자식을
   실행할 수 있었는지 센다. 실제로 프로세스가 시작되지 못했다면
   exec()은 유효한 PID가 아니라 -1을 반환해야 한다.

   이 과정을 10번 반복하며, 커널이 매번 같은 수준의 깊이를
   허용하는지 확인한다.

   또한 일부 프로세스는 자원을 조금 할당한 뒤 비정상 종료하는
   자식들을 생성한다.

   EXPECTED_DEPTH_TO_PASS는 우리 구현에서 나온 값에 *충분히 큰*
   여유를 두어 경험적으로 정했다.
   코드에 메모리 누수가 정말 없다고 생각하는데도
   EXPECTED_DEPTH_TO_PASS에서 실패한다면,
   값을 조정해 실제 출력을 알려 달라.
   
   원본 작성: Godmar Back <godmar@gmail.com>
   수정: Minkyu Jung, Jinyoung Oh <cs330_ta@casys.kaist.ac.kr>
*/

#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syscall.h>
#include <random.h>
#include "tests/lib.h"

static const int EXPECTED_DEPTH_TO_PASS = 10;
static const int EXPECTED_REPETITIONS = 10;

int make_children (void);

/* 여러 파일을 열고(닫지 않은 채 둔다).
   커널은 이 파일 디스크립터와 연결된 커널 자원을 모두
   해제해야 한다. */
static void
consume_some_resources (void)
{
  int fd, fdmax = 126;

  /* fdmax에 도달할 때까지 가능한 한 많은 파일을 연다.
	 커널 내부에서 파일 디스크립터를 어떻게 할당하느냐에 따라,
	 메모리가 부족하면 open()이 실패할 수 있다.
	 open()에서 메모리 부족이 발생해도 프로세스가 종료되어서는
	 안 된다. */
  for (fd = 0; fd < fdmax; fd++) {
#ifdef EXTRA2
	  if (fd != 0 && (random_ulong () & 1)) {
		if (dup2(random_ulong () % fd, fd+fdmax) == -1)
			break;
		else
			if (open (test_name) == -1)
			  break;
	  }
#else
		if (open (test_name) == -1)
		  break;
#endif
  }
}

/* 자원을 조금 소비한 뒤, 이 프로세스를 비정상적인 방식으로
   종료시킨다. */
static int NO_INLINE
consume_some_resources_and_die (void)
{
  consume_some_resources ();
  int *KERN_BASE = (int *)0x8004000000;

  switch (random_ulong () % 5) {
	case 0:
	  *(int *) NULL = 42;
    break;

	case 1:
	  return *(int *) NULL;

	case 2:
	  return *KERN_BASE;

	case 3:
	  *KERN_BASE = 42;
    break;

	case 4:
	  open ((char *)KERN_BASE);
	  exit (-1);
    break;

	default:
	  NOT_REACHED ();
  }
  return 0;
}

int
make_children (void) {
  int i = 0;
  int pid;
  char child_name[128];
  for (; ; random_init (i), i++) {
    if (i > EXPECTED_DEPTH_TO_PASS/2) {
      snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "X");
      pid = fork(child_name);
      if (pid > 0 && wait (pid) != -1) {
        fail ("crashed child should return -1.");
      } else if (pid == 0) {
        consume_some_resources_and_die();
        fail ("Unreachable");
      }
    }

    snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "O");
    pid = fork(child_name);
    if (pid < 0) {
      exit (i);
    } else if (pid == 0) {
      consume_some_resources();
    } else {
      break;
    }
  }

  int depth = wait (pid);
  if (depth < 0)
	  fail ("Should return > 0.");

  if (i == 0)
	  return depth;
  else
	  exit (depth);
}

int
main (int argc UNUSED, char *argv[] UNUSED) {
  test_name = "multi-oom";

  msg ("begin");

  int first_run_depth = make_children ();
  CHECK (first_run_depth >= EXPECTED_DEPTH_TO_PASS, "Spawned at least %d children.", EXPECTED_DEPTH_TO_PASS);

  for (int i = 0; i < EXPECTED_REPETITIONS; i++) {
    int current_run_depth = make_children();
    if (current_run_depth < first_run_depth) {
      fail ("should have forked at least %d times, but %d times forked", 
              first_run_depth, current_run_depth);
    }
  }

  msg ("success. Program forked %d iterations.", EXPECTED_REPETITIONS);
  msg ("end");
}
