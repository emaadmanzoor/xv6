#include "types.h"
#include "user.h"

// A more thorough allocation counter test
// Allocates and deallocates pages using sbrk,
// and verifies the counter arithmetic.
void
allocCounterTest(void) {

  int pagesToAllocate = 10;
  int pid, i;

  if ((pid = fork()) == 0) {

    int startNumPages = getmemusage();

    // Allocate pages
    for (i = 0; i < pagesToAllocate; i++)
      sbrk(4096);

    if (startNumPages + pagesToAllocate != getmemusage())
      printf(1, "Allocation test failed: %d %d %d\n", startNumPages, pagesToAllocate, getmemusage());

    // Deallocate pages
    for (i = 0; i < pagesToAllocate; i++)
      sbrk(-4096);

    if (getmemusage() != startNumPages)
      printf(1, "Deallocation test failed: %d %d %d\n", startNumPages, pagesToAllocate, getmemusage());

    // Tests passed
    exit();
  } else {
    wait();
  }
}

int
main(void)
{
  printf(1, "Pages allocated: %d\n", getmemusage());
  allocCounterTest();
  exit();
}
