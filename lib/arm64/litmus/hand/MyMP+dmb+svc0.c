#include <stdint.h>
#include <stdlib.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"

#define T 1000000            /* number of runs */
#define NAME "MP+dmb+svc0"   /* litmus test name */

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

      /* arguments passed to SVC through x0..x7 */
      "mov x0, %[i]\n\t"    /* which iteration */
      "mov x1, %[ctx]\n\t"  /* pointer to test_ctx_t object */
      "svc #0\n\t"
    : [x0] "=r" (x0[i])
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

static void* svc_handler(uint64_t esr, regvals_t* regs) {
  /* invariant: only called within an SVC */
  uint64_t iss = esr & 0xffffff;

  switch (iss) {
    case 0: {
      uint64_t i = regs->x0;
      test_ctx_t *ctx = regs->x1;
      uint64_t* x2 = ctx->out_regs[out_p1_x2];
      uint64_t* x = ctx->heap_vars[X];
      asm volatile (
        "ldr %[x2], [%[x3]]\n"
        : [x2] "=r" (x2[i])
        : [x3] "r" (&x[i])
        : "memory"
      );
      break;
    }
    case 1: {
      /* drop to EL0 */
      uint64_t old_table = regs->x0;
      asm volatile (
        "mrs x18, spsr_el1\n"

        /* zero SPSR_EL1[0:3] */
        "lsr x18, x18, #4\n"
        "lsl x18, x18, #4\n"

        /* write back to SPSR */
        "msr spsr_el1, x18\n"

        /* /1* set EL0 SP *1/ */
        /* "mov x18, sp\n" */
        /* "add x18,x18,#288\n" */
        /* "msr sp_el0, x18\n" */

        /* "msr vbar_el1, x0\n" */
        "isb\n"
      :
      : [v] "r" (old_table)
      : "x18"
      );
      break;
    }
    case 2: {
      /* raise to EL1 */
      asm volatile (
        "mrs x18, spsr_el1\n"

        /* zero SPSR_EL1[0:3] */
        "lsr x18, x18, #4\n"
        "lsl x18, x18, #4\n"
        
        /* add targetEL and writeback */
        "add x18, x18, #5\n"
        "msr spsr_el1, x18\n"
        "isb\n"
      :
      :
      : "x18"
      );
      break;
    }
    /* read CurrentEL via SPSR */
    case 3: {
      uint64_t cel;
      asm volatile (
        "mrs %[cel], SPSR_EL1\n"
        "and %[cel], %[cel], #12\n"
      : [cel] "=r" (cel)
      );
      return cel;
    }
  }

  return NULL;
}


static void go_cpus(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;

  int cpu = smp_processor_id();
  printf("CPU%d: on\n", cpu);

  /* setup exceptions */
  uint64_t* old_table = set_vector_table(&el1_exception_vector_table);
  set_handler(VEC_EL1T_SYNC, EC_SVC64, svc_handler);
  set_handler(VEC_EL0_SYNC_64, EC_SVC64, svc_handler);

  /* before we drop to EL0, ensure both EL0 and EL1 stack pointers
   * agree, and then ensure that execution at both EL1 and EL0 use SP_EL0
   */
  asm volatile (
    "mov x18, sp\n"
    "msr sp_el0, x18\n"
    "mov x18, #0\n"
    "msr spsel, x18\n"
  :
  :
  : "x18"
  );

  /* drop to EL0 */
  asm volatile (
    "svc #1\n\t"
  :
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );

  uint64_t cel;
  asm volatile (
      "svc #3\n" /* read CurrentEL */
      "mov %[el], x0\n" 
  : [el] "=r" (cel)
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",  /* dont touch parameter registers */
    "memory"
  );
  printf("CPU%d, CurrentEL = %d\n", cpu, cel >> 2);

  switch (cpu) {
    case 1:
      P0(a);
      break;
    case 2:
      P1(a);
      break;
  }

  printf("CPU%d, Finished, Restoring to EL1\n", cpu);

  /* restore EL1 */
  asm volatile (
    "svc #2\n\t"
  :
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",  /* dont touch parameter registers */
    "memory"
  );

  /* restore old vtable now tests are over */
  set_vector_table(old_table);

  bwait(cpu, 0, ctx->final_barrier);
}

void MyMP_dmb_svc0(void) {
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
  free_test_ctx(&ctx);
}
