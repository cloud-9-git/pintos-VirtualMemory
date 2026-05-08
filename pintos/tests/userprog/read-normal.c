/* 가장 일반적인 방식으로 파일 읽기를 시도한다. */

#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  check_file ("sample.txt", sample, sizeof sample - 1);
}
