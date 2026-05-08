/* 자식 프로세스로의 인자 전달을 테스트한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("I'm your father");
  exec ("child-args childarg");
}
