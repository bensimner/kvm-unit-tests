#include <stdint.h>

#include "MyLitmusTests.h"
#include "MyCommon.h"


static void P0(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t** out_regs) {
  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];
  uint64_t* x0 = out_regs[0];
  uint64_t* x2 = out_regs[1];

  asm volatile (
    "mov x0, #1\n\t"
    "str x0, [%[x1]]\n\t"
    "dmb sy\n\t"
    "mov x2, #1\n\t"
    "str x2, [%[x3]]\n\t"
  :
  : [x1] "r" (x), [x3] "r" (y)
  : "cc", "memory", "x0", "x2"
  );
}

static void* svc_handler(uint64_t esr, regvals_t* regs) {
  uint64_t* y = regs->x0;
  uint64_t* x0 = regs->x1;

  asm volatile (
    "ldr %[x0], [%[x1]]\n"
    : [x0] "=r" (*x0)
    : [x1] "r" (y)
    : "memory"
  );
  return NULL;
}

static void P1(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t** out_regs) {
  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];
  uint64_t* x0 = out_regs[0];
  uint64_t* x2 = out_regs[1];
  
  set_svc_handler(0, svc_handler);

  asm volatile (
    /* arguments passed to SVC through x0..x7 */
    "mov x0, %[x4]\n\t"    /* pointer to y */
    "mov x1, %[x5]\n\t"    /* pointer to out reg x0 */

    "svc #0\n\t"
    "ldr %[x2], [%[x3]]\n\t"
  : [x2] "=&r" (*x2)
  : [x3] "r" (x), [x4] "r" (y), [x5] "r" (x0)
  : "cc", "memory", 
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );
}

void MyMP_dmb_eret0(void) {
  run_test(
    "MP+dmb+eret0",
    2, (th_f*[]){P0,P1}, 
    2, (char*[]){"x", "y"}, 
    2, (char*[]){"p1:x0", "p1:x2"}, 
    (uint64_t[]){
      /* p1:x0 =*/ 1,
      /* p1:x2 =*/ 0,
    });
}
