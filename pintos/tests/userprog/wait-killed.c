/* 잘못된 동작으로 종료될 프로세스를 기다린다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t child;
  if ((child = fork ("child-bad"))){
    msg ("wait(exec()) = %d", wait (child));
  } else {
    exec ("child-bad");
  }
}
