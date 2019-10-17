#include <libcflat.h>
#include <asm/smp.h>

#define T 1000  /* number of runs */

static void bwait(int cpu, int i, int volatile* barrier) {
  if (i == cpu) {
    *barrier = 1;
    asm ("dsb st");
  } else {
    while (*barrier == 0);
  }
}

typedef struct {
  int* x;
  int* y;
  int volatile* barriers;
  void* vtable;
  int* out_p1_x0;
  int* out_p1_x2;
  int volatile* final_barrier;
} test_ctx_t;

static void P0(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  int* x = ctx->x;
  int* y = ctx->y;
  int volatile* bars = ctx->barriers;
  for (int i = 0; i < T; i++) {
    bwait(0, i % 2, &bars[i]);
    asm volatile (
      "mov x0, #1\n\t"
      "str x0, [%[x1]]\n\t"
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
  int* x = ctx->x;
  int* y = ctx->y;
  int volatile* bars = ctx->barriers;
  int* x0 = ctx->out_p1_x0;
  int* x2 = ctx->out_p1_x2;
  for (int i = 0; i < T; i++) {
    bwait(1, i % 2, &bars[i]);
    asm volatile (
      "ldr %[x0], [%[x1]]\n\t"
      "ldr %[x2], [%[x3]]\n\t"
    : [x0] "=r" (x0[i]), [x2] "=r" (x2[i])
    : [x1] "r" (&y[i]), [x3] "r" (&x[i])
    : "cc", "memory"
    );
    /* printf("%d,%d\n", x0[i], x2[i]); */
  }
}

void go_cpus(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  void* vtable = ctx->vtable;

  uint64_t old_table;
  int cpu = smp_processor_id();
  printf("CPU%d: on\n", cpu);

  /* setup exceptions */
  asm volatile ("mrs %[p], vbar_el1\n" : [p] "=r" (old_table) : : "memory");

  if (vtable != NULL) {
    asm volatile ("msr vbar_el1, %[p]\n" : : [p] "r" (vtable) : "memory");
  }

  asm volatile ("isb" ::: "memory");

  void* r = NULL;
  switch (cpu) {
    case 2:
      P0(a);
      break;
    case 3:
      P1(a);
      break;
  }

  /* restore old vtable now tests are over */
  asm volatile ("msr vbar_el1, %[p]\n" : : [p] "r" (old_table) : "memory");

  printf("cpu%d waiting...\n", cpu);
  bwait(cpu, 1, ctx->final_barrier);
  printf("cpu%d go\n", cpu);
}

void MyMP(void) {
  int x[T];
  int y[T];
  int volatile bars[T];
  int x0[T];
  int x2[T];
  int volatile final_barrier = 0;


  for (int i = 0; i < T; i++) {
    x[i] = 0;
    y[i] = 0;
    bars[i] = 0;
    x0[i] = 0;
    x2[i] = 0;
  }

  test_ctx_t ctx;
  ctx.x = x;
  ctx.y = y;
  ctx.barriers = bars;
  ctx.vtable = NULL;
  ctx.out_p1_x0 = x0;
  ctx.out_p1_x2 = x2;
  ctx.final_barrier = &final_barrier;

  asm ("dsb sy");

  /* run test */
  printf("%s\n", "Running Tests ...");
  on_cpus(go_cpus, &ctx);

  printf("%s\n", "Collecting Results ...");
  /* collect results */
  int outs[2][2];

  for (int i = 0; i < T; i++) {
    /* printf("out, x0=%d, x2=%d\n", x0[i], x2[i]); */
    outs[x0[i]][x2[i]]++;
  }

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (outs[i][j] != 0) {
        if (i == 1 && j == 0)  /* the relaxed outcome ! */
          printf("*> x0=%d, x2=%d  -> %d\n", i, j, outs[i][j]);
        else
          printf(" > x0=%d, x2=%d  -> %d\n", i, j, outs[i][j]);
      }
    }
  }
}
