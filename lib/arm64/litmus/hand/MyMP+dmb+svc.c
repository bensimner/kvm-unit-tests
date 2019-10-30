#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"

#define T 100000           /* number of runs */
#define NAME "MP+dmb+svc"  /* litmus test name */

#define X 0
#define Y 1

#define out_p1_x0 0
#define out_p1_x2 1

static void P0(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  uint64_t* x = ctx->heap_vars[X];
  uint64_t* y = ctx->heap_vars[Y];
  uint64_t* start_bars = ctx->start_barriers;
  uint64_t* end_bars = ctx->end_barriers;

  for (int j = 0; j < T; j++) {
    int i = ctx->shuffled_ixs[j];

    start_of_run(ctx, i);
    bwait(0, i % 2, &start_bars[i]);

    asm volatile (
      "mov x0, #1\n\t"
      "str x0, [%[x1]]\n\t"
      "dmb sy\n\t"
      "mov x2, #1\n\t"
      "str x2, [%[x3]]\n\t"
    :
    : [x1] "r" (&x[i]), [x3] "r" (&y[i])
    : "cc", "memory", "x0", "x2"
    );

    bwait(0, i % 2, &end_bars[i]);
    end_of_run(ctx, i);
  }
}

static void P1(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  uint64_t* start_bars = ctx->start_barriers;
  uint64_t* end_bars = ctx->end_barriers;

  uint64_t* y = ctx->heap_vars[Y];
  uint64_t* x0 = ctx->out_regs[out_p1_x0];

  for (int j = 0; j < T; j++) {
    int i = ctx->shuffled_ixs[j];
    start_of_run(ctx, i);
    bwait(1, i % 2, &start_bars[i]);

    asm volatile (
      "ldr %[x0], [%[x1]]\n\t"

      /* arguments passed to SVC through x0,x1,x2 */
      "mov x0, %[i]\n\t"    /* which iteration */
      "mov x1, %[ctx]\n\t"  /* pointer to test_ctx_t object */
      "svc #0\n\t"
    : [x0] "=&r" (x0[i])
    : [x1] "r" (&y[i]), [i] "r" (i), [ctx] "r" (ctx)
    : "cc", "memory",
      "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
    );

    bwait(1, i % 2, &end_bars[i]);
    if (i % T/10 == 0) {
      printf("%s", ".\n");
    }
  }
}

static void svc_handler(uint64_t esr, regvals_t* regs) {
  /* invariant: only called within an SVC */
  uint64_t i = regs->x0;
  test_ctx_t *ctx = regs->x1;
  uint64_t* x2 = ctx->out_regs[out_p1_x2];
  uint64_t* x = ctx->heap_vars[X];
  asm volatile (
    "ldr %[x2], [%[x3]]\n"
    : [x2] "=&r" (x2[i])
    : [x3] "r" (&x[i])
    : "memory"
  );
}

static void go_cpus(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;

  int cpu = smp_processor_id();
  printf("CPU%d: on\n", cpu);

  /* setup exceptions */
  uint64_t* old_table = set_vector_table(&el1_exception_vector_table);
  set_handler(VEC_EL1H_SYNC, EC_SVC64, svc_handler);

  switch (cpu) {
    case 1:
      P0(a);
      break;
    case 2:
      P1(a);
      break;
  }

  /* restore old vtable now tests are over */
  set_vector_table(old_table);

  bwait(cpu, 0, ctx->final_barrier);
}

void MyMP_dmb_svc(void) {
  test_ctx_t ctx;
  init_test_ctx(&ctx, NAME, 2, 2, T);

  printf("====== %s ======\n", NAME);

  printf("New EL1 Exception Vector @ %p\n", &el1_exception_vector_table);

  /* run test */
  printf("%s\n", "Running Tests ...");
  on_cpus(go_cpus, &ctx);

  /* collect results */
  printf("%s\n", "Ran Tests.");

  /* collect results */
  const char* reg_names[] = {
    "p1:x0",
    "p1:x2",
  };
  const int relaxed_result[] = {
    /* p1:x0 =*/ 1,
    /* p1:x2 =*/ 0,
  };

  printf("%s\n", "Printing Results...");
  print_results(ctx.hist, &ctx, reg_names, relaxed_result);
}
