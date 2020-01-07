/* Common types and declarations 
 */

#ifndef _MYCOMMON_H
#define _MYCOMMON_H

#define N_CPUS 4

#include <stdint.h>
#include <libcflat.h>

#define isb()  asm volatile("isb")
#define dsb()  asm volatile("dsb sy")
#define dmb()  asm volatile("dmb sy")
#define eret()  asm volatile("eret")


/* configuration */
uint64_t NUMBER_OF_RUNS;

/* test data */
typedef struct {
    uint64_t counter;
    uint64_t values[];
} test_result_t;

typedef struct {
    int allocated;
    int limit;
    test_result_t** lut;
    test_result_t* results[];
} test_hist_t;

/* Each thread is a functon that takes pointers to a slice of heap variables and output registers */
typedef struct test_ctx test_ctx_t;
typedef void th_f(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t** out_regs);

typedef struct test_ctx {
  uint64_t no_threads;
  th_f** thread_fns;             /* pointer to each thread function */
  uint64_t** heap_vars;         /* set of heap variables: x, y, z etc */
  size_t no_heap_vars;
  uint64_t** out_regs;          /* set of output register values: P1:x1,  P2:x3 etc */
  size_t no_out_regs;
  uint64_t volatile* start_barriers;
  uint64_t volatile* end_barriers;
  uint64_t volatile* final_barrier;
  uint64_t* shuffled_ixs;
  uint64_t no_runs;
  char* test_name;
  test_hist_t* hist;
  uint64_t* ptable;
  uint64_t n_run;
  uint64_t privileged_harness;  /* require harness to run at EL1 between runs ? */
};


void init_test_ctx(test_ctx_t* ctx, char* test_name, int no_threads, th_f** funcs, int no_heap_vars, int no_out_regs, int no_runs);
void free_test_ctx(test_ctx_t* ctx);

/* print the collected results out */
void print_results(test_hist_t* results, test_ctx_t* ctx, char** out_reg_names, int* interesting_results);

/* call at the start and end of each run  */
void start_of_run(test_ctx_t* ctx, int thread, int i);
void end_of_run(test_ctx_t* ctx, int thread, int i);

/* call at the start and end of each thread */
void start_of_thread(test_ctx_t* ctx, int cpu);
void end_of_thread(test_ctx_t* ctx, int cpu);

/* call at the beginning and end of each test */
void start_of_test(test_ctx_t* ctx, const char* name, int no_threads, th_f** funcs, int no_heap_vars, int no_regs, int no_runs);
void end_of_test(test_ctx_t* ctx, char** out_reg_names, int* interesting_result);

/* entry point for tests */
void run_test(const char* name, int no_threads, th_f** funcs, int no_heap_vars, char** heap_var_names, int no_regs, char** reg_names, uint64_t* interesting_result);

/* random numbers */
volatile uint64_t SEED;

uint64_t read_clk(void);
void rand_seed(uint64_t seed);
uint64_t randn(void);
void shuffle(uint64_t* arr, int n);

/* Exception Vectors */

/* defined in MyVectorTable.S */
extern uint64_t el1_exception_vector_table;

/* Enum of vector table entries
 * Stored in order, aligned at 0x20 boundries
 */
enum vec_entries {
  VEC_EL1T_SYNC,
  VEC_EL1T_IRQ,
  VEC_EL1T_FIQ,
  VEC_EL1T_ERROR,
  VEC_EL1H_SYNC,
  VEC_EL1H_IRQ,
  VEC_EL1H_FIQ,
  VEC_EL1H_ERROR,
  VEC_EL0_SYNC_64,
  VEC_EL0_IRQ_64,
  VEC_EL0_FIQ_64,
  VEC_EL0_ERROR_64,
  VEC_EL0_SYNC_32,
  VEC_EL0_IRQ_32,
  VEC_EL0_FIQ_32,
  VEC_EL0_ERROR_32,
};

static const char* vec_names[16] = {
  "VEC_EL1T_SYNC",
  "VEC_EL1T_IRQ",
  "VEC_EL1T_FIQ",
  "VEC_EL1T_ERROR",
  "VEC_EL1H_SYNC",
  "VEC_EL1H_IRQ",
  "VEC_EL1H_FIQ",
  "VEC_EL1H_ERROR",
  "VEC_EL0_SYNC_64",
  "VEC_EL0_IRQ_64",
  "VEC_EL0_FIQ_64",
  "VEC_EL0_ERROR_64",
  "VEC_EL0_SYNC_32",
  "VEC_EL0_IRQ_32",
  "VEC_EL0_FIQ_32",
  "VEC_EL0_ERROR_32",
};

/* useful ECs */
#define EC_SVC64     0x15  /* SVC from AArch64 */
#define EC_IABT_EL0  0x20  /* Instruction Abort */
#define EC_IABT_EL1  0x21  /* Instruction Abort */
#define EC_PC_ALIGN  0x22  /* PC Alignment Fault */
#define EC_DABT_EL0  0x24  /* Data Abort */
#define EC_DABT_EL1  0x25  /* Data Abort */

static const char* ec_names[0x26] = {
  [0x15] = "EC_SVC64",
  [0x18] = "EC_MRS_TRAP",
  [0x20] = "EC_IABT_EL0",
  [0x21] = "EC_IABT_EL1",
  [0x22] = "EC_PC_ALIGN",
  [0x24] = "EC_DABT_EL0",
  [0x25] = "EC_DABT_EL1",
};

typedef struct {
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
} regvals_t;

/* Exception Vectors
 *
 *  SVC has default handlers provided by the framework:
 *    SVC #10  will drop to EL0
 *    SVC #11  will raise to EL1
 *    SVC #12  will place CurrentEL in X0
 * 
 *  Other handlers can be installed with set_svc_handler(svc_no, ptr_to_func)
 */

void* default_handler(uint64_t vec, uint64_t esr);
typedef void* exception_vector_fn(uint64_t esr, regvals_t* regs);

exception_vector_fn* table[N_CPUS][4][64];
exception_vector_fn* table_svc[N_CPUS][64];  /* 64 SVC handlers */

void set_handler(uint64_t vec, uint64_t ec,  exception_vector_fn* fn);
void reset_handler(uint64_t vec, uint64_t ec);
void* handle_exception(uint64_t vec, uint64_t esr, regvals_t* regs);

void set_svc_handler(uint64_t svc_no, exception_vector_fn* fn);
void reset_svc_handler(uint64_t svc_no);

void drop_to_el0(void);
void raise_to_el1(void);

extern uint64_t set_vector_table(uint64_t);
extern void tnop(void);

/* Synchronisation */

void bwait(int cpu, int i, uint64_t* barrier, int sz);
void lock(volatile int* lock);
void unlock(volatile int* lock);

/* Tracing */
void trace(char* fmt, ...);
#endif