#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include <MyCommon.h>
#include <MyVMM.h>

static void go_cpus(void* a);
static void run_thread(test_ctx_t* ctx, int cpu);

/* entry point */
void run_test(const char* name, int no_threads, th_f** funcs, int no_heap_vars, char** heap_var_names, int no_regs, char** reg_names, uint64_t* relaxed_result) {

  /* create test context obj */
  test_ctx_t ctx;
  start_of_test(&ctx, name, no_threads, funcs, no_heap_vars, no_regs, NUMBER_OF_RUNS);


  /* run it */
  trace("%s\n", "Running Tests ...");
  on_cpus(go_cpus, &ctx);

  /* clean up and display results */
  end_of_test(&ctx, reg_names, relaxed_result);
}

static void go_cpus(void* a) {
  test_ctx_t* ctx = (test_ctx_t* )a;
  int cpu = smp_processor_id();
  start_of_thread(ctx, cpu);

  if (cpu < ctx->no_threads) {
    run_thread(ctx, cpu);
  }

  end_of_thread(ctx, cpu);
}

static uint64_t PA(test_ctx_t* ctx, uint64_t va) {
  /* return the PA associated with the given va in a particular iteration */
  return translate4k(ctx->ptable, va);
}

static uint64_t PTE(test_ctx_t* ctx, uint64_t va) {
  /* return the VA at which the PTE lives for the given va in a particular
    * iteration */
  return ref_pte4k(ctx->ptable, va);
}

static void run_thread(test_ctx_t* ctx, int cpu) {
  th_f* func = ctx->thread_fns[cpu];
  uint64_t* start_bars = ctx->start_barriers;
  uint64_t* end_bars = ctx->end_barriers;

  for (int j = 0; j < ctx->no_runs; j++) {
    int i = ctx->shuffled_ixs[j];
    start_of_run(ctx, cpu, i);
    uint64_t heaps[ctx->no_heap_vars];
    uint64_t ptes[ctx->no_heap_vars];
    uint64_t pas[ctx->no_heap_vars];  /* TODO: BS: wire up PAs */
    uint64_t regs[ctx->no_out_regs];

    for (int v = 0; v < ctx->no_heap_vars; v++) {
      uint64_t* p = &ctx->heap_vars[v][i];
      heaps[v] = p;
      ptes[v] = PTE(ctx, p);
      pas[v] = PA(ctx, p);
    }
    for (int r = 0; r < ctx->no_out_regs; r++)
      regs[r] = &ctx->out_regs[r][i];

    func(ctx, i, heaps, ptes, regs);  /* TODO: BS: add PTE pointers */
    end_of_run(ctx, cpu, i);
  }
}


/* static allocator
 *
 * Statically allocate the 64MB region:
 *  0x42610000 ->
 *  0x46610000
 *    (8192 4k pages)
 *
 * Note:  kvm-unit-tests's vmalloc "malloc" implementation may allocate pages in this region, too.
 * So don't use both!
 *
 *
 * The test context (e.g. heap variables, output registers, test barriers and results histogram)
 * is stored in this region.
 *
 * At the end of each test a call to free_all() will begin re-allocating this space anew.
 */
static char* page_buf = 0x42610000;
static char* free_start = 0x42610000;
static char* free_end = 0x46610000;

static uint64_t alignup(uint64_t x, uint64_t to) {
  return (x + to - 1) & ~(to - 1);
}

static char* static_malloc(size_t sz) {
  if (free_start + sz > free_end) {
    printf("! static_malloc: no free pages\n");
    abort();
  }

  // if sz is large then we lose lots of free space here ...
  char* start = (char*)alignup(free_start, sz);
  free_start = start + sz;
  memset(start, 0, sz);
  return (char*)start;
}

static void free_all(void) {
  free_start = page_buf;
}

/* Test Data */


void init_test_ctx(test_ctx_t* ctx, char* test_name, int no_threads, th_f** funcs, int no_heap_vars, int no_out_regs, int no_runs) {
  uint64_t** heap_vars = static_malloc(sizeof(uint64_t*)*no_heap_vars);
  uint64_t** out_regs = static_malloc(sizeof(uint64_t*)*no_out_regs);
  uint64_t* bars = static_malloc(sizeof(uint64_t)*no_runs);
  uint64_t* end_bars = static_malloc(sizeof(uint64_t)*no_runs);
  uint64_t* final_barrier = static_malloc(sizeof(uint64_t));
  uint64_t* shuffled = static_malloc(sizeof(uint64_t)*no_runs);

  for (int v = 0; v < no_heap_vars; v++) {
      // ensure each heap var allloc'd into its own page...
      uint64_t* heap_var = static_malloc(alignup(sizeof(uint64_t)*no_runs, 4096UL));
      heap_vars[v] = heap_var;
  }

  for (int r = 0; r < no_out_regs; r++) {
      uint64_t* out_reg = static_malloc(sizeof(uint64_t)*no_runs);
      out_regs[r] = out_reg;
  }

  for (int i = 0; i < no_runs; i++) {
    /* one time init so column major doesnt matter */
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

  test_hist_t* hist = static_malloc(sizeof(test_hist_t)+sizeof(test_result_t)*100);
  hist->allocated = 0;
  hist->limit = 100;
  test_result_t** lut = static_malloc(sizeof(test_result_t*)*100);
  hist->lut = lut;

  for (int t = 0; t < 100; t++) {
    test_result_t* new_res = static_malloc(sizeof(test_result_t)+sizeof(uint64_t)*no_out_regs);
    hist->results[t] = new_res;
    lut[t] = NULL;
  }

  ctx->no_threads = no_threads;
  ctx->heap_vars = heap_vars;
  ctx->thread_fns = funcs;
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
  ctx->ptable = NULL;
  ctx->n_run = 0;
  ctx->privileged_harness = 0;
}

void free_test_ctx(test_ctx_t* ctx) {
  free_all();
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
  int val = 0;
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
      raise_to_el1();  /* can only abort at EL1 */
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

void prefetch(test_ctx_t* ctx, int i) {
  for (int v = 0; v < ctx->no_heap_vars; v++) {
    if (randn() % 2 && ctx->heap_vars[v][i] != 0) {
      raise_to_el1();  /* can abort only at EL1 */
      printf("! fatal: initial state for heap var %d on run %d was %d not 0\n", v, i, ctx->heap_vars[v][i]);
      abort();
    }
  }
}

static void resetsp(void) {
  /* check and reset to SP_EL0 */
  uint64_t currentsp;
  asm volatile (
    "mrs %[currentsp], SPSel\n"
    : [currentsp] "=r" (currentsp)
  );

  if (currentsp == 0b1) {  /* if not already using SP_EL0 */
    asm volatile (
      "mov x18, sp\n"
      "msr spsel, x18\n"
      "dsb nsh\n"
      "isb\n"
      "mov sp, x18\n"
    :
    :
    : "x18"
    );
  }
}

void start_of_run(test_ctx_t* ctx, int thread, int i) {
  prefetch(ctx, i);
  bwait(thread, i % ctx->no_threads, &ctx->start_barriers[i], ctx->no_threads); 
  if (ctx->n_run == 0 || ctx->privileged_harness)
    drop_to_el0();
}

void end_of_run(test_ctx_t* ctx, int thread, int i) {
  if (ctx->n_run == ctx->no_runs - 1 || ctx->privileged_harness)
    raise_to_el1();

  bwait(thread, i % ctx->no_threads, &ctx->end_barriers[i], ctx->no_threads);

  /* only 1 thread should collect the results, else they will be duplicated */
  if (thread == 0) {
    uint64_t r = ctx->n_run++;

    test_hist_t* res = ctx->hist;
    add_results(res, ctx, i);

    /* progress indicator */
    uint64_t step = (ctx->no_runs/10);
    if (r % step == 0) {
      trace("[%d/%d]\n", r, ctx->no_runs);
    } else if (r == ctx->no_runs - 1) {
      trace("[%d/%d]\n", r+1, ctx->no_runs);
    }
  }
}

static uint64_t _ttbrs[N_CPUS];
static uint64_t _tcrs[N_CPUS];
static uint64_t _vbars[N_CPUS];
void start_of_thread(test_ctx_t* ctx, int cpu) {
  _ttbrs[cpu] = read_ttbr();
  _tcrs[cpu] = read_tcr();
  set_new_id_translation(ctx->ptable);
  _vbars[cpu] = set_vector_table(&el1_exception_vector_table);

  /* before can drop to EL0, ensure EL0 has a valid mapped stack space
  */
  resetsp();

  trace("CPU%d: on\n", cpu);
}

void end_of_thread(test_ctx_t* ctx, int cpu) {
  trace("CPU%d: wait to idle\n", cpu);
  set_vector_table(_vbars[cpu]);
  restore_old_id_translation(_ttbrs[cpu], _tcrs[cpu]);
  bwait(cpu, 0, ctx->final_barrier, N_CPUS);
  trace("CPU%d: ->idle\n", cpu);
}

void start_of_test(test_ctx_t* ctx, const char* name, int no_threads,  th_f** funcs, int no_heap_vars, int no_regs, int no_runs) {
  trace("====== %s ======\n", name);
  init_test_ctx(ctx, name, no_threads, funcs, no_heap_vars, no_regs, no_runs);
  ctx->ptable = alloc_new_idmap_4k();
}

void end_of_test(test_ctx_t* ctx, char** out_reg_names, int* interesting_result) {
  trace("%s\n", "Printing Results...");
  print_results(ctx->hist, ctx, out_reg_names, interesting_result);
  free_test_ctx(ctx);
  free_aligned_pages();
  trace("Finished test %s\n", ctx->test_name);
}

void print_results(test_hist_t* res, test_ctx_t* ctx, char** out_reg_names, int* interesting_results) {
  printf("Test %s:\n", ctx->test_name);
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
  uint64_t clk;
  asm volatile (
    "mrs %[clk], pmccntr_el0\n"
  : [clk] "=r" (clk)
  );
  return clk;
}

uint64_t randn(void) {
  uint64_t st = 1;

  for (int i = 0; i < 64; i++) {
    if (i % 7 == 0 || i % 13 == 0) {
      int x = (SEED >> i) & 0x1;
      st = st ^ x;
    }
  }
  SEED = (SEED << 1) + st;
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
static int _EXC_PRINT_LOCK = 0;
void* default_handler(uint64_t vec, uint64_t esr) {
    uint64_t ec = esr >> 26;
    uint64_t far;
    asm volatile ("mrs %[far], FAR_EL1\n" : [far] "=r" (far));
    uint64_t elr;
    asm volatile ("mrs %[elr], ELR_EL1\n" : [elr] "=r" (elr));
    uint64_t cpu = smp_processor_id();
    lock(&_EXC_PRINT_LOCK);
    printf("Unhandled Exception (CPU%d): \n", cpu);
    printf("  [Vector: 0x%x (%s)]\n", vec, vec_names[vec]);
    printf("  [EC: 0x%x (%s)]\n", ec, ec_names[ec]);
    printf("  ESR_EL1: 0x%06x\n", esr);
    printf("  FAR_EL1: 0x%016lx\n", far);
    printf("  ELR_EL1: 0x%016lx\n", elr);
    printf("  \n");
    unlock(&_EXC_PRINT_LOCK);
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

void drop_to_el0(void) {
  asm volatile (
    "svc #10\n\t"
  :
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );
}

void raise_to_el1(void) {
  asm volatile (
    "svc #11\n\t"
  :
  :
  : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"  /* dont touch parameter registers */
  );
}

static void* default_svc_drop_el0(uint64_t vec, uint64_t esr, regvals_t* regs) {
  asm volatile (
    "mrs x18, spsr_el1\n"

    /* zero SPSR_EL1[0:3] */
    "lsr x18, x18, #4\n"
    "lsl x18, x18, #4\n"

    /* write back to SPSR */
    "msr spsr_el1, x18\n"
    "dsb nsh\n"
    "isb\n"
  :
  :
  : "memory", "x18"
  );

  return NULL;
}

static void* default_svc_raise_el1(uint64_t vec, uint64_t esr, regvals_t* regs) {
  /* raise to EL1h */
  asm volatile (
    "mrs x18, spsr_el1\n"

    /* zero SPSR_EL1[0:3] */
    "lsr x18, x18, #4\n"
    "lsl x18, x18, #4\n"
    
    /* add targetEL and writeback */
    "add x18, x18, #4\n"  /* EL1 */
    "add x18, x18, #0\n"  /* h */
    "msr spsr_el1, x18\n"
    "dsb nsh\n"
    "isb\n"
  :
  :
  : "memory", "x18"
  );

  return NULL;
}

static void* default_svc_read_currentel(uint64_t vec, uint64_t esr, regvals_t* regs) {
  /* read CurrentEL via SPSPR */
  uint64_t cel;
  asm volatile (
    "mrs %[cel], SPSR_EL1\n"
    "and %[cel], %[cel], #12\n"
  : [cel] "=r" (cel)
  );
  return cel;
}

static void* default_svc_handler(uint64_t vec, uint64_t esr, regvals_t* regs) {
  uint64_t imm = esr & 0xffffff;
  int cpu = smp_processor_id();
  if (table_svc[cpu][imm] == NULL)
    if (imm == 10)
      return default_svc_drop_el0(vec, esr, regs);
    else if (imm == 11)
      return default_svc_raise_el1(vec, esr, regs);
    else if (imm == 12)
      return default_svc_read_currentel(vec, esr, regs);
    else
      return default_handler(vec, esr);
  else
    return table_svc[cpu][imm](esr, regs);
}

void* handle_exception(uint64_t vec, uint64_t esr, regvals_t* regs) {
  uint64_t ec = esr >> 26;
  int cpu = smp_processor_id();
  exception_vector_fn* fn = table[cpu][vec][ec];
  if (fn) {
    return fn(esr, regs);
  } else if (ec == 0x15) {
    return default_svc_handler(vec, esr, regs);
  } else {
    return default_handler(vec, esr);
  }
}

void set_svc_handler(uint64_t svc_no, exception_vector_fn* fn) {
  int cpu = smp_processor_id();
  table_svc[cpu][svc_no] = fn;
}

void reset_svc_handler(uint64_t svc_no) {
    int cpu = smp_processor_id();
  table_svc[cpu][svc_no] = NULL;
}

/* Synchronisation */

void bwait(int cpu, int i, uint64_t* barrier, int sz) {
  asm volatile (
    "0:\n"
    "ldxr x0, [%[bar]]\n"
    "add x0, x0, #1\n"
    "stxr w1, x0, [%[bar]]\n"
    "cbnz w1, 0b\n"
  :
  : [bar] "r" (barrier)
  : "x0", "x1", "memory"
  );

  dsb();

  if (i == cpu) {
    while (*barrier != sz) wfe();
    *barrier = 0;
    dmb();
  } else {
    while (*barrier != 0) wfe();
  }
}

void lock(volatile int* lock) {
  asm volatile (
    "0:\n"
    "ldxr x0, [%[lock]]\n"
    "cbnz x0, 0b\n"
    "mov x0, #1\n"
    "stxr w1, x0, [%[lock]]\n"
    "cbnz w1, 0b\n"
  :
  : [lock] "r" (lock)
  : "x0", "x1", "memory"
  );

  dsb();
}

void unlock(volatile int* lock) {
  *lock = 0;
}


/* Tracing */
uint64_t _TRACE_LOCK = 0;
void trace(char* fmt, ...) {
#ifdef TRACE
  char out[1000];
  va_list args;
  va_start(args, fmt);
  vsnprintf(out, 1000, fmt, args);
  va_end(args);
  lock(&_TRACE_LOCK);
  printf("[trace] %s", out);
  unlock(&_TRACE_LOCK);
#endif
}
