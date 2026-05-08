/* -*- c -*- */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  const char *child_cmd = "child-rox " CHILD_CNT;
  int handle;
  pid_t child;
  char buffer[16];

  /* child-rox를 열고, 읽고, 같은 데이터를 다시 써 넣는다. */
  CHECK ((handle = open ("child-rox")) > 1, "open \"child-rox\"");
  CHECK (read (handle, buffer, sizeof buffer) == (int) sizeof buffer,
         "read \"child-rox\"");
  seek (handle, 0);
  CHECK (write (handle, buffer, sizeof buffer) == (int) sizeof buffer,
         "write \"child-rox\"");

  /* child-rox를 실행하고 종료를 기다린다. */
  msg ("exec \"%s\"", child_cmd);
  if (!(child = fork ("child-rox"))){
    exec (child_cmd);
  }

  if (child < 0)
    fail ("fork() returned %d", child);
  quiet = true;
  CHECK (wait (child) == 12, "wait for child");
  quiet = false;

  /* child-rox에 다시 쓴다. */
  seek (handle, 0);
  CHECK (write (handle, buffer, sizeof buffer) == (int) sizeof buffer,
         "write \"child-rox\"");
}
