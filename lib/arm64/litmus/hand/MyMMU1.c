#include <stdint.h>

#include "MyLitmusTests.h"
#include "MyCommon.h"


/* test payload */

static void P0(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t** out_regs) {
  raise_to_el1(ctx);  /* MMU1 runs at EL1 */

  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];

  uint64_t* xpte = ptes[0];
  uint64_t* ypte = ptes[1];

  uint64_t* x2 = out_regs[0];

  asm volatile(
    "ldr x0, [%[x1]]\n"
    "str x0, [%[x2]]\n"
    "dsb sy\n"
    "tlbi vmalle1is\n"
    "dsb sy\n"
    "isb\n"
    "mov x3, #1\n\t"
    "str x3, [%[x4]]\n\t"
    "ldr %[x5], [%[x6]]\n\t"
  : [x5] "=&r" (*x2)
  : [x1] "r" (xpte), [x2] "r" (ypte), [x4] "r" (x), [x6] "r" (y)
  : "cc", "memory", "x0", "x2", "x3"
  );
}

void MyMMU1(void) {
  run_test(
    "MMU1",
    1, (th_f*[]){P0,}, 
    2, (char*[]){"x", "y"}, 
    1, (char*[]){"x2"}, 
    (uint64_t[]){
    /* x2 =*/ 0,
    }
  );
}