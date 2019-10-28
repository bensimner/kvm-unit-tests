#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"

#define T 1000000            /* number of runs */
#define NAME "MP+dmb+svc0"   /* litmus test name */

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

  uint64_t* y = ctx->y;
  uint64_t* x0 = ctx->out_p1_x0;

  for (uint64_t i = 0; i < T; i++) {
    bwait(1, i % 2, &bars[i]);
    uint64_t iout;
    asm volatile (
      "ldr %[x0], [%[x1]]\n\t"

      /* arguments passed to SVC through x0..x7 */
      "mov x0, %[i]\n\t"    /* which iteration */
      "mov x1, %[ctx]\n\t"  /* pointer to test_ctx_t object */
      "svc #0\n\t"
    : [x0] "=r" (x0[i]), [iout] "=r" (iout)
    : [x1] "r" (&y[i]), [i] "r" (i), [ctx] "r" (ctx)
    : "cc", "memory", 
      "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
    );

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
      uint64_t volatile* x2 = ctx->out_p1_x2;
      uint64_t volatile* x = ctx->x;
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

        /* set EL0 SP */
        "mov x18, sp\n"
        "msr sp_el0, x18\n"

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
  set_handler(VEC_EL1H_SYNC, EC_SVC64, svc_handler);
  set_handler(VEC_EL0_SYNC_64, EC_SVC64, svc_handler);

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
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
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

  /* restore EL1 */
  asm volatile (
    "svc #2\n\t"
  :
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );

  /* restore old vtable now tests are over */
  set_vector_table(old_table);

  bwait(cpu, 0, ctx->final_barrier);
}

void MyMP_dmb_svc0(void) {
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

  /* get pointer to start of aligned section */
  printf("New EL1 Exception Vector @ %p\n", &el1_exception_vector_table);

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
