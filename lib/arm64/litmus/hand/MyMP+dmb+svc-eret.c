#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"

#define T 10000                 /* number of runs */
#define NAME "MP+dmb+svc-eret"  /* litmus test name */
#define N_THREADS 2             /* number of hardware threads in test */

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
    start_of_run(ctx, 0, i);
  
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

    end_of_run(ctx, 0, i);
  }
}

static void* svc_handler(uint64_t esr, regvals_t* regs) {
  /* intentionally blank */
}

static void P1(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  uint64_t* start_bars = ctx->start_barriers;
  uint64_t* end_bars = ctx->end_barriers;

  uint64_t* x = ctx->heap_vars[X];
  uint64_t* y = ctx->heap_vars[Y];
  uint64_t* x0 = ctx->out_regs[out_p1_x0];
  uint64_t* x2 = ctx->out_regs[out_p1_x2];

  for (int j = 0; j < T; j++) {
    int i = ctx->shuffled_ixs[j];
    start_of_run(ctx, 1, i);
    set_svc_handler(0, svc_handler);

    asm volatile (
      "ldr %[x0], [%[x1]]\n\t"
      "svc #0\n\t"
      "ldr %[x2], [%[x3]]\n\t"
    : [x0] "=r" (x0[i]), [x2] "=r" (x2[i])
    : [x1] "r" (&y[i]), [x3] "r" (&x[i])
    : "cc", "memory",
      "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
    );

    end_of_run(ctx, 1, i);
  }
}

static void go_cpus(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  int cpu = smp_processor_id();
  start_of_thread(ctx, cpu);

  switch (cpu) {
    case 1:
      P0(a);
      break;
    case 2:
      P1(a);
      break;
  }

  end_of_thread(ctx, cpu);
}

void MyMP_dmb_svc_eret(void) {
  test_ctx_t ctx;
  start_of_test(&ctx, NAME, N_THREADS, 2, 2, T);

  /* run test */
  trace("%s\n", "Running Tests ...");
  on_cpus(go_cpus, &ctx);

  /* collect results */
  const char* reg_names[] = {
    "p1:x0",
    "p1:x2",
  };
  const int relaxed_result[] = {
    /* p1:x0 =*/ 1,
    /* p1:x2 =*/ 0,
  };

  end_of_test(&ctx, reg_names, relaxed_result);
}
