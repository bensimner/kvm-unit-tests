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

static void* svc_handler_1(uint64_t esr, regvals_t* regs) {
  uint64_t* x = regs->x0;
  uint64_t* x2 = regs->x1;

  asm volatile (
    "ldr %[x2], [%[x3]]\n"
    : [x2] "=&r" (*x2)
    : [x3] "r" (x)
    : "memory"
  );

  return NULL;
}

static void* svc_handler_0(uint64_t esr, regvals_t* regs) {
  uint64_t* x = regs->x0;
  uint64_t* y = regs->x1;
  uint64_t* x0 = regs->x2;
  uint64_t* x2 = regs->x3;

  asm volatile (
    /* arguments passed to SVC through x0..x7 */
    "mov x0, %[x8]\n\t"    /* pointer to x */
    "mov x1, %[x9]\n\t"    /* pointer to out reg x2 */

    /* save important state */
    "mrs x9, SPSR_EL1\n\t"
    "mrs x10, ELR_EL1\n\t"
    "mrs x11, ESR_EL1\n\t"

    "ldr %[x0], [%[x1]]\n\t"
    "svc #1\n"

    "msr SPSR_EL1, x9\n\t"
    "msr ELR_EL1, x10\n\t"
    "msr ESR_EL1, x11\n\t"
    : [x0] "=r" (*x0)
    : [x1] "r" (y), [x8] "r" (x), [x9] "r" (x2)
    : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
      "x9", "x10", "x11",
      "memory"
  );

  return NULL;
}

static void P1(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t** out_regs) {
  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];
  uint64_t* x0 = out_regs[0];
  uint64_t* x2 = out_regs[1];

  set_svc_handler(0, svc_handler_0);
  set_svc_handler(1, svc_handler_1);

  asm volatile (
    /* arguments passed to SVC through x0..x7 */
    "mov x0, %[x4]\n\t"    /* pointer to x */
    "mov x1, %[x5]\n\t"    /* pointer to y */
    "mov x2, %[x6]\n\t"    /* pointer to out reg x0 */
    "mov x3, %[x7]\n\t"    /* pointer to out reg x2 */

    "svc #0\n"
  :
  : [x4] "r" (x), [x5] "r" (y), [x6] "r" (x0), [x7] "r" (x2)
  : "cc", "memory", 
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );

}

void MyMP_dmb_1svc1(void) {
  run_test(
    "MP+dmb+1svc1",
    2, (th_f*[]){P0,P1}, 
    2, (char*[]){"x", "y"}, 
    2, (char*[]){"p1:x0", "p1:x2"}, 
    (uint64_t[]){
      /* p1:x0 =*/ 1,
      /* p1:x2 =*/ 0,
    });
}
