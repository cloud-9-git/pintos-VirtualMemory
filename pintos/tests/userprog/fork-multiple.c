/* 여러 자식 프로세스를 fork하고 기다린다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void fork_and_wait (void);
int magic = 3;

void
fork_and_wait (void){
  int pid;
  magic++;
  if ((pid = fork("child"))){
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  } else {
    msg ("child run");
    exit(magic);
  }
}

void
test_main (void) 
{
  fork_and_wait();
  fork_and_wait();
  fork_and_wait();
  fork_and_wait();
}
