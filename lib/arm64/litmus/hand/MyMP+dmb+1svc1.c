#include <stdint.h>

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
  uint64_t i = regs->x0;
  test_ctx_t *ctx = regs->x1;
  uint64_t* x2 = ctx->out_regs[1];
  uint64_t* x = ctx->heap_vars[0];

  asm volatile (
    "ldr %[x2], [%[x3]]\n"
    : [x2] "=&r" (x2[i])
    : [x3] "r" (&x[i])
    : "memory"
  );

  return NULL;
}

static void* svc_handler_0(uint64_t esr, regvals_t* regs) {
  uint64_t i = regs->x0;
  test_ctx_t *ctx = regs->x1;
  uint64_t* x = ctx->heap_vars[0];
  uint64_t* y = ctx->heap_vars[1];
  uint64_t* x0 = ctx->out_regs[0];
  uint64_t* x2 = ctx->out_regs[1];

  asm volatile (
    /* arguments passed to SVC through x0..x7 */
    "mov x0, %[i]\n\t"    /* which iteration */
    "mov x1, %[ctx]\n\t"  /* pointer to test_ctx_t object */
    /* save important state */
    "mrs x9, SPSR_EL1\n\t"
    "mrs x10, ELR_EL1\n\t"
    "mrs x11, ESR_EL1\n\t"

    "ldr %[x0], [%[x1]]\n\t"
    "svc #1\n"

    "msr SPSR_EL1, x9\n\t"
    "msr ELR_EL1, x10\n\t"
    "msr ESR_EL1, x11\n\t"
    : [x0] "=r" (x0[i])
    : [x1] "r" (&y[i]), [x3] "r" (&x[i]), [i] "r" (i), [ctx] "r" (ctx)
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
    "mov x0, %[i]\n\t"    /* which iteration */
    "mov x1, %[ctx]\n\t"  /* pointer to test_ctx_t object */

    "svc #0\n"
  :
  : [i] "r" (i), [ctx] "r" (ctx)
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
