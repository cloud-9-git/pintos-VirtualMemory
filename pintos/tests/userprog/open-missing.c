/* 존재하지 않는 파일 열기를 시도한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle = open ("no-such-file");
  if (handle != -1)
    fail ("open() returned %d", handle);
}
