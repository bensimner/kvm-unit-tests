#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"

#define T 10000                 /* number of runs */
#define NAME "MP+dmb+svc-eret"  /* litmus test name */

typedef struct {
  uint64_t* x;
  uint64_t* y;
  uint64_t volatile* barriers;
  uint64_t* out_p1_x0;
  uint64_t* out_p1_x2;
  uint64_t volatile* final_barrier;
} test_ctx_t;

static void P0(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  uint64_t volatile* x = ctx->x;
  uint64_t volatile* y = ctx->y;
  uint64_t volatile* bars = ctx->barriers;
  for (int i = 0; i < T; i++) {
    bwait(0, i % 2, &bars[i]);
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
  }
}

static void P1(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  uint64_t* bars = ctx->barriers;

  uint64_t* x = ctx->x;
  uint64_t* y = ctx->y;
  uint64_t* x0 = ctx->out_p1_x0;
  uint64_t* x2 = ctx->out_p1_x2;

  for (uint64_t i = 0; i < T; i++) {
    bwait(1, i % 2, &bars[i]);
    asm volatile (
      "ldr %[x0], [%[x1]]\n\t"
      "svc #0\n\t"
      "ldr %[x2], [%[x3]]\n\t"
    : [x0] "=r" (x0[i]), [x2] "=r" (x2[i])
    : [x1] "r" (&y[i]), [x3] "r" (&x[i])
    : "cc", "memory",
      "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
    );

    if (i % T/10 == 0) {
      printf("%s", ".\n");
    }
  }
}

static void* svc_handler(uint64_t esr, regvals_t* regs) {
    dsb();
    /* intentionally empty */
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

void MyMP_dmb_svc_eret(void) {
  uint64_t* x = malloc(sizeof(uint64_t)*T);
  uint64_t* y = malloc(sizeof(uint64_t)*T);
  uint64_t* x0 = malloc(sizeof(uint64_t)*T);
  uint64_t* x2 = malloc(sizeof(uint64_t)*T);
  uint64_t* bars = malloc(sizeof(uint64_t)*T);
  uint64_t final_barrier = 0;

  printf("====== %s ======\n", NAME);

  /* zero the memory */
  for (int i = 0; i < T; i++) {
    x[i] = 0;
    y[i] = 0;
    x0[i] = 0;
    x2[i] = 0;
    bars[i] = 0;
  }

  test_ctx_t ctx;
  ctx.x = x;
  ctx.y = y;
  ctx.barriers = bars;
  ctx.out_p1_x0 = x0;
  ctx.out_p1_x2 = x2;
  ctx.final_barrier = &final_barrier;

  dsb();

  /* run test */
  printf("%s\n", "Running Tests ...");
  on_cpus(go_cpus, &ctx);

  /* collect results */
  printf("%s\n", "Collecting Results ...");
  uint64_t outs[2][2] = {{0}};
  uint64_t skipped_results = 0;

  for (int i = 0; i < T; i++) {
    if (x0[i] > 2 || x2[i] > 2) {
      skipped_results++;
      continue;
    }

    outs[x0[i]][x2[i]]++;
  }

  /* print output */
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (outs[i][j] != 0 || (i < 2 && j < 2)) {
        if (i == 1 && j == 0 && outs[i][j] > 0)  /* the relaxed outcome ! */
          printf("*> x0=%d, x2=%d  -> %d\n", i, j, outs[i][j]);
        else
          printf(" > x0=%d, x2=%d  -> %d\n", i, j, outs[i][j]);
      }
    }
  }

  printf("Observation %s: %d\n", NAME, outs[1][0]);
  if (skipped_results)
    printf("(warning: %d results skipped for being out-of-range)\n", skipped_results);
}
