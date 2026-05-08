/* 일반적인 빈 파일을 생성한다. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("quux.dat", 0), "create quux.dat");
}
