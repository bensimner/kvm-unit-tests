#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include <MyCommon.h>

/* Test Data */

void init_test_ctx(test_ctx_t* ctx, char* test_name, int no_heap_vars, int no_out_regs, int no_runs) {
  uint64_t** heap_vars = malloc(sizeof(uint64_t*)*no_heap_vars);
  uint64_t** out_regs = malloc(sizeof(uint64_t*)*no_out_regs);
  uint64_t* bars = malloc(sizeof(uint64_t)*no_runs);
  uint64_t* end_bars = malloc(sizeof(uint64_t)*no_runs);
  uint64_t* final_barrier = malloc(sizeof(uint64_t));
  uint64_t* shuffled = malloc(sizeof(uint64_t)*no_runs);

  for (int v = 0; v < no_heap_vars; v++) {
      uint64_t* heap_var = malloc(sizeof(uint64_t*)*no_runs);
      heap_vars[v] = heap_var;
  }

  for (int r = 0; r < no_out_regs; r++) {
      uint64_t* out_reg = malloc(sizeof(uint64_t*)*no_runs);
      out_regs[r] = out_reg;
  }


  for (int i = 0; i < no_runs; i++) {
    for (int v = 0; v < no_heap_vars; v++)
        heap_vars[v][i] = 0;

    for (int r = 0; r < no_out_regs; r++)
        out_regs[r][i] = 0;

    bars[i] = 0;
    end_bars[i] = 0;
    shuffled[i] = i;
  }
  *final_barrier = 0;

  /* shuffle shuffled */
  rand_seed(read_clk());
  shuffle(shuffled, no_runs);

  test_hist_t* hist = malloc(sizeof(test_hist_t)+sizeof(test_result_t)*100);
  hist->limit = 100;
  test_result_t** lut = malloc(sizeof(test_result_t*)*100);
  hist->lut = lut;

  for (int t = 0; t < 100; t++) {
    test_result_t* new_res = malloc(sizeof(test_result_t)+sizeof(uint64_t)*no_out_regs);
    hist->results[t] = new_res;
    lut[t] = NULL;
  }

  ctx->heap_vars = heap_vars;
  ctx->no_heap_vars = no_heap_vars;
  ctx->out_regs = out_regs;
  ctx->no_out_regs = no_out_regs;
  ctx->start_barriers = bars;
  ctx->end_barriers = end_bars;
  ctx->final_barrier = final_barrier;
  ctx->shuffled_ixs = shuffled;
  ctx->no_runs = no_runs;
  ctx->hist = hist;
  ctx->test_name = test_name;
}

void free_test_ctx(test_ctx_t* ctx) {

  test_hist_t* hist = ctx->hist;
  for (int t = 0; t < 100; t++) {
    free(hist->results[t]);
  }
  free(hist->lut);
  free(hist);

  for (int r = 0; r < ctx->no_out_regs; r++) {
    free(ctx->out_regs[r]);
  }

  for (int v = 0; v < ctx->no_heap_vars; v++) {
    free(ctx->heap_vars[v]);
  }

  free(ctx->shuffled_ixs);
  free(ctx->final_barrier);
  free(ctx->end_barriers);
  free(ctx->start_barriers);
  free(ctx->out_regs);
  free(ctx->heap_vars);

  /* we allocate ctx's on stack */
  /* free(ctx); */
}

static int matches(test_result_t* result, test_ctx_t* ctx, int run)  {
  for (int reg = 0; reg < ctx->no_out_regs; reg++) {
    if (result->values[reg] != ctx->out_regs[reg][run]) {
      return 0;
    }
  }
  return 1;
}

static int ix_from_values(test_ctx_t* ctx, int run) {
  uint64_t val = 0;
  for (int reg = 0; reg < ctx->no_out_regs; reg++) {
    uint64_t v = ctx->out_regs[reg][run];
    if (v < 4) {
      val *= 4;
      val += (int)v; /* must be less than 4 so fine ... */
    } else {
      return -1;
    }
  }
  return val;
}

static test_result_t* lookup(test_hist_t* res, test_ctx_t* ctx, int run)  {
}

static void add_results(test_hist_t* res, test_ctx_t* ctx, int run) {
  /* fast case: check lut */
  test_result_t** lut = res->lut;
  int ix = ix_from_values(ctx, run);
  if (ix != -1 && lut[ix] != NULL) {
    lut[ix]->counter++;
    return;
  }

  /* otherwise, slow case: walk table for entry */
  /* if already allocated */
  int missing = 1;
  for (int i = 0; i < res->allocated; i++) {
    /* found a matching entry */
    if (matches(res->results[i], ctx, run)) {
      /* just increment its count */
      missing = 0;
      res->results[i]->counter++;
      break;
    }
  }
  /* if not found, insert it */
  if (missing) {
    if (res->allocated >= res->limit) {
      printf("! fatal:  overallocated results\n");
      printf("this probably means the test had too many outcomes\n");
      abort();
    }
    test_result_t* new_res = res->results[res->allocated];
    for (int reg = 0; reg < ctx->no_out_regs; reg++) {
      new_res->values[reg]=ctx->out_regs[reg][run];
    }
    new_res->counter = 1;
    res->allocated++;

    /* update LUT to point if future accesses should be fast */
    if (ix != -1) {
      lut[ix] = new_res;
    }
  }
}

static void prefetch(test_ctx_t* ctx, int i) {
  for (int v = 0; v < ctx->no_heap_vars; v++) {
    if (randn() % 2 && ctx->heap_vars[v][i] != 0) {
      printf("%s\n", "Fail!  initial state wasn't 0");
    }
  }
}

void start_of_run(test_ctx_t* ctx, int i) {
  prefetch(ctx, i);
}

void end_of_run(test_ctx_t* ctx, int i) {
  test_hist_t* res = ctx->hist;
  add_results(res, ctx, i);
}

void print_results(test_hist_t* res, test_ctx_t* ctx, char** out_reg_names, int* interesting_results) {
  printf("Results:\n");
  int marked = 0;
  for (int r = 0; r < res->allocated; r++) {
    int was_interesting = 1;
    for (int reg = 0; reg < ctx->no_out_regs; reg++) {
      printf(" %s=%d ", out_reg_names[reg], res->results[r]->values[reg]);

      if (res->results[r]->values[reg] != interesting_results[reg])
        was_interesting = 0;
    }

    if (was_interesting) {
      marked = res->results[r]->counter;
      printf(" : %d *\n", res->results[r]->counter);
    } else {
      printf(" : %d\n", res->results[r]->counter);
    }
  }
  printf("Observation %s: %d (of %d)\n", ctx->test_name, marked, ctx->no_runs);
}

/* Random */

void rand_seed(uint64_t seed) {
  SEED = seed;
}

uint64_t read_clk(void) {
  return 42;  /* TODO: read from some actual randomish source */
}

uint64_t randn(void) {
  uint64_t st = SEED;
  for (int i = 1; i < 64; i++) {
    int x = (st >> (i-1)) & 0x1;
    int y = (st >> (i+1)) & 0x1;
    int z = x ^ y;
    int mask = z << i;
    SEED = SEED | mask;
  }
  return SEED;
}

void shuffle(uint64_t* arr, int n) {
  for (int i = 0; i < n; i++) {
    int x = randn() % n;
    int y = randn() % n;
    uint64_t temp = arr[x];
    arr[x] = arr[y];
    arr[y] = temp;
  }
}



/* Exception Vectors */

void* default_handler(uint64_t vec, uint64_t esr) {
    uint64_t ec = esr >> 26;
    uint64_t far;
    asm volatile ("mrs %[far], FAR_EL1\n" : [far] "=r" (far));
    uint64_t elr;
    asm volatile ("mrs %[elr], ELR_EL1\n" : [elr] "=r" (elr));
    printf("%s\n", "Unhandled Exception:");
    printf("  Vector: 0x%x (%s)\n", vec, vec_names[vec]);
    printf("  EC: 0x%x (%s)\n", ec, ec_names[ec]);
    printf("ESR_EL1: 0x%06x\n", esr);
    printf("FAR_EL1: 0x%016lx\n", far);
    printf("ELR_EL1: 0x%016lx\n", elr);
    abort();

/* unreachable */
    return NULL;
}

void set_handler(uint64_t vec, uint64_t ec,  exception_vector_fn* fn) {
  int cpu = smp_processor_id();
  table[cpu][vec][ec] = fn;
}

void reset_handler(uint64_t vec, uint64_t ec) {
  int cpu = smp_processor_id();
  table[cpu][vec][ec] = NULL;
}

void* handle_exception(uint64_t vec, uint64_t esr, regvals_t* regs) {
  uint64_t ec = esr >> 26;
  int cpu = smp_processor_id();
  exception_vector_fn* fn = table[cpu][vec][ec];
  if (fn) {
    return fn(esr, regs);
  } else {
    return default_handler(vec, esr);
  }
}

/* Synchronisation */

void bwait(int cpu, int i, uint64_t volatile* barrier) {
  if (i == cpu) {
    *barrier = 1;
    dmb();
  } else {
    while (*barrier == 0);
  }
}
