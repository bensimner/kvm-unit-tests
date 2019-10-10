/****************************************************************************/
/*                           the diy toolsuite                              */
/*                                                                          */
/* Jade Alglave, University College London, UK.                             */
/* Luc Maranget, INRIA Paris-Rocquencourt, France.                          */
/*                                                                          */
/* This C source is a product of litmus7 and includes source that is        */
/* governed by the CeCILL-B license.                                        */
/****************************************************************************/

/* Parameters */
#define SIZE_OF_TEST 10000
/* #define NUMBER_OF_RUN 100 */
#define AVAIL 2
#define STRIDE 1
#define MAX_LOOP 0
#define N 2
#define AFF_INCR (0)
/* Includes */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include "utils.h"
#include "outs.h"
#include "affinity.h"

#include <libcflat.h>

static int randn(void) {
    static int X[2048];
    static int i = 0;
    i = (X[i] + 1) % 2048;
    return X[i];
}


/* params */
typedef struct {
  int verbose;
  int size_of_test,max_run;
  int stride;
  aff_mode_t aff_mode;
  int ncpus, ncpus_used;
  int do_change;
  cpus_t *cm;
} param_t;


/* Full memory barrier */
inline static void mbar(void) {
  asm __volatile__ ("dsb sy" ::: "memory");
}

/* Barriers macros */
inline static void barrier_wait(unsigned int id, unsigned int k, int volatile *b) {
  if ((k % N) == id) {
    *b = 1 ;
    mbar();
  } else {
    while (*b == 0) ;
  }
}

/*
 Topology: {{{0}, {1}}}
*/

static int cpu_scan[] = {
// [[0],[1]]
1, 0,
};

static char *group[] = {
"[[0],[1]]",
};

#define SCANSZ 1
#define SCANLINE 2

static count_t ngroups[SCANSZ];

/**********************/
/* Context definition */
/**********************/


typedef struct {
/* Shared variables */
  int *y;
  int *x;
/* Final content of observed  registers */
  int *out_1_x0;
  int *out_1_x2;
/* Check data */
  pb_t *fst_barrier;
/* Barrier for litmus loop */
  int volatile *barrier;
/* Instance seed */
  st_t seed;
/* Parameters */
  param_t *_p;
} ctx_t;

inline static int final_cond(int _out_1_x0,int _out_1_x2) {
  switch (_out_1_x0) {
  case 1:
    switch (_out_1_x2) {
    case 0:
      return 1;
    default:
      return 0;
    }
  default:
    return 0;
  }
}

inline static int final_ok(int cond) {
  return cond;
}

/**********************/
/* Outcome collection */
/**********************/
#define NOUTS 2
typedef intmax_t outcome_t[NOUTS];

static const int out_1_x0_f = 0 ;
static const int out_1_x2_f = 1 ;


typedef struct hist_t {
  outs_t *outcomes ;
  count_t n_pos,n_neg ;
} hist_t ;

static hist_t *alloc_hist(void) {
  hist_t *p = malloc_check(sizeof(*p)) ;
  p->outcomes = NULL ;
  p->n_pos = p->n_neg = 0 ;
  return p ;
}

static void free_hist(hist_t *h) {
  free_outs(h->outcomes) ;
  free(h) ;
}

static void add_outcome(hist_t *h, count_t v, outcome_t o, int show) {
  h->outcomes = add_outcome_outs(h->outcomes,o,NOUTS,v,show) ;
}

static void merge_hists(hist_t *h0, hist_t *h1) {
  h0->n_pos += h1->n_pos ;
  h0->n_neg += h1->n_neg ;
  h0->outcomes = merge_outs(h0->outcomes,h1->outcomes,NOUTS) ;
}


static void do_dump_outcome(intmax_t *o, count_t c, int show) {
  printf("%-6"PCTR"%c>1:X0=%d; 1:X2=%d;\n",c,show ? '*' : ':',(int)o[out_1_x0_f],(int)o[out_1_x2_f]);
}

static void just_dump_outcomes(hist_t *h) {
  outcome_t buff ;
  dump_outs(do_dump_outcome,h->outcomes,buff,NOUTS) ;
}

/**************************************/
/* Prefetch (and check) global values */
/**************************************/

static void check_globals(ctx_t *_a) {
  int *y = _a->y;
  int *x = _a->x;
  for (int _i = _a->_p->size_of_test-1 ; _i >= 0 ; _i--) {
    if (rand_bit(&(_a->seed)) && y[_i] != 0) fatal("MP, check_globals failed");
    if (rand_bit(&(_a->seed)) && x[_i] != 0) fatal("MP, check_globals failed");
  }
  pb_wait(_a->fst_barrier);
}

/***************/
/* Litmus code */
/***************/

static void e1_sync_sp_el1(void) {
  ctx_t *_a;
  int i;
  asm volatile (
    "mov %[a], X30\n"
    "mov %[i], X29\n"
  : [a] "=r" (_a), [i] "=r" (i)
  :
  : "x29", "x30", "memory"
  );

  asm volatile (
    "ldr w2, [%[x3]]\n"
    :
    : [x3] "r" (&_a->x[i])
    : "x2", "memory"
  );
}

static void __vtable(void) {
  asm volatile (
    ".balign 0x800\n"
    ".balign 0x200\n"
    "eret\n\t"
    ".balign 0x200\n");
    e1_sync_sp_el1();
  asm volatile (
    "eret\n\t"
    ".balign 0x200\n\t"
    "eret\n\t"
    ".balign 0x200\n\t"
    "eret\n\t"
  );
}


typedef struct {
  int th_id; /* I am running on this thread */
  int *cpu; /* On this cpu */
  ctx_t *_a;   /* In this context */
} parg_t;

static void *P0(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  /* int _ecpu = _b->cpu[_b->th_id]; */
  /* force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"MP"); */
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
      uint64_t ctxp = (uint64_t)_a;
      int trashed_x0;
      int trashed_x2;
asm __volatile__ (
"\n"
"mov x30, %[ctx]\n\t"
"mov x29, %[i]\n\t"
"#START _litmus_P0\n"
"#_litmus_P0_0\n\t"
"mov %w[x0],#1\n"
"#_litmus_P0_1\n\t"
"str %w[x0],[%[x1]]\n"
"#_litmus_P0_2\n\t"
"mov %w[x2],#1\n"
"#_litmus_P0_3\n\t"
"str %w[x2],[%[x3]]\n"
"#END _litmus_P0\n\t"
:[x2] "=&r" (trashed_x2),[x0] "=&r" (trashed_x0)
:[x1] "r" (&_a->x[_i]),[x3] "r" (&_a->y[_i]),[ctx] "r" (ctxp),[i] "r" (_i)
:"cc","memory","x29","x30"
);
    }
  }
  mbar();
  return NULL;
}

static void *P1(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  /* int _ecpu = _b->cpu[_b->th_id]; */
  /* force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"MP"); */
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  int *out_1_x0 = _a->out_1_x0;
  int *out_1_x2 = _a->out_1_x2;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
      uint64_t ctxp = (uint64_t)_a;
asm __volatile__ (
"\n"
"mov x30, %[ctx]\n\t"
"mov x29, %[i]\n\t"
"#START _litmus_P1\n"
"#_litmus_P1_0\n\t"
"ldr %w[x0],[%[x1]]\n"
"#_litmus_P1_1\n\t"
/* "ldr %w[x2],[%[x3]]\n" */
"svc #0\n"
"mov %w[x2], w2\n"
"#END _litmus_P1\n\t"
:[x2] "=&r" (out_1_x2[_i]),[x0] "=&r" (out_1_x0[_i])
:[x1] "r" (&_a->y[_i]),[x3] "r" (&_a->x[_i]),[ctx] "r" (ctxp),[i] "r" (_i)
:"cc","memory","x29","x30","x0","x1","x2","x10"
);
    }
  }
  mbar();
  return NULL;
}

/*******************************************************/
/* Context allocation, freeing and reinitialization    */
/*******************************************************/

static void init(ctx_t *_a) {
  int size_of_test = _a->_p->size_of_test;

  _a->seed = randn();
  _a->out_1_x0 = malloc_check(size_of_test*sizeof(*(_a->out_1_x0)));
  _a->out_1_x2 = malloc_check(size_of_test*sizeof(*(_a->out_1_x2)));
  _a->y = malloc_check(size_of_test*sizeof(*(_a->y)));
  _a->x = malloc_check(size_of_test*sizeof(*(_a->x)));
  _a->fst_barrier = pb_create(N);
  _a->barrier = malloc_check(size_of_test*sizeof(*(_a->barrier)));
}

static void finalize(ctx_t *_a) {
  free((void *)_a->y);
  free((void *)_a->x);
  free((void *)_a->out_1_x0);
  free((void *)_a->out_1_x2);
  pb_free(_a->fst_barrier);
  free((void *)_a->barrier);
}

static void reinit(ctx_t *_a) {
  for (int _i = _a->_p->size_of_test-1 ; _i >= 0 ; _i--) {
    _a->y[_i] = 0;
    _a->x[_i] = 0;
    _a->out_1_x0[_i] = -239487;
    _a->out_1_x2[_i] = -239487;
    _a->barrier[_i] = 0;
  }
}

typedef struct {
  pm_t *p_mutex;
  pb_t *p_barrier;
  param_t *_p;
  int z_id;
  int *cpus;
} zyva_t;

#define NT N

#ifdef ASS
static void ass(void) { }
#else
#include "MP.h"
#endif

static void prelude(void) {
  printf("%s\n","%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
  printf("%s\n","% Results for MP+dmb+svc.litmus %");
  printf("%s\n","%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
  printf("%s\n","AArch64 MP+dmb+svc");
  printf("%s\n","\"PodWW Rfe PodRR Fre\"");
  printf("%s\n","{0:X1=x; 0:X3=y; 1:X1=y; 1:X3=x;}");
  printf("%s\n"," P0          | P1          ;");
  printf("%s\n"," MOV W0,#1   | LDR W0,[X1] ;");
  printf("%s\n"," STR W0,[X1] | LDR W2,[X3] ;");
  printf("%s\n"," MOV W2,#1   |             ;");
  printf("%s\n"," STR W2,[X3] |             ;");
  printf("%s\n","");
  printf("%s\n","exists (1:X0=1 /\\ 1:X2=0)");
  printf("Generated assembler\n");
  ass();
}

#define ENOUGH 10

static void postlude(cmd_t *cmd,hist_t *hist,count_t p_true,count_t p_false,tsc_t total) {
  printf("Test MP Allowed\n");
  printf("Histogram (%i states)\n",finals_outs(hist->outcomes));
  just_dump_outcomes(hist);
  int cond = p_true > 0;
  printf("%s\n",cond?"Ok":"No");
  printf("\nWitnesses\n");
  printf("Positive: %" PCTR ", Negative: %" PCTR "\n",p_true,p_false);
  printf("Condition exists (1:X0=1 /\\ 1:X2=0) is %svalidated\n",cond ? "" : "NOT ");
  printf("Hash=211d5b298572012a0869d4ded6a40b7f\n");
  count_t cond_true = p_true;
  count_t cond_false = p_false;
  printf("Observation MP %s %" PCTR " %" PCTR "\n",!cond_true ? "Never" : !cond_false ? "Always" : "Sometimes",cond_true,cond_false);
  if (p_true > 0) {
    if (cmd->aff_mode == aff_scan) {
      for (int k = 0 ; k < SCANSZ ; k++) {
        count_t c = ngroups[k];
        if ((c*100)/p_true > ENOUGH) { printf("Topology %-6" PCTR":> %s\n",c,group[k]); }
      }
    } else if (cmd->aff_mode == aff_topo) {
      printf("Topology %-6" PCTR ":> %s\n",ngroups[0],cmd->aff_topo);
    }
  }
  printf("Time MP %.2f\n",total / 1000000.0);
}

/* static void *zyva(void *_va) { */
/*   printf("************* zyva start\n"); */
/*   zyva_t *_a = (zyva_t *) _va; */
/*   param_t *_b = _a->_p; */
/*   printf("************* pb_wait \n"); */
/*   pb_wait(_a->p_barrier); */
/*   printf("************* pb_wait ed \n"); */
/*   /1* pthread_t thread[NT]; *1/ */
/*   parg_t parg[N]; */
/*   f_t *fun[] = {&P0,&P1}; */
/*   printf("************* allocing hist \n"); */
/*   hist_t *hist = alloc_hist(); */
/*   printf("************* alloced \n"); */
/*   ctx_t ctx; */
/*   ctx._p = _b; */

/*   printf("************* to init \n"); */
/*   init(&ctx); */
/*   printf("************* init \n"); */
/*   for (int _p = N-1 ; _p >= 0 ; _p--) { */
/*     parg[_p].th_id = _p; parg[_p]._a = &ctx; */
/*     parg[_p].cpu = &(_a->cpus[0]); */
/*   } */

/*   for (int n_run = 0 ; n_run < 1 ; n_run++) { */
/*     printf("************* to run %d \n", n_run); */
/*     if (_b->aff_mode == aff_random) { */
/*       pb_wait(_a->p_barrier); */
/*       if (_a->z_id == 0) perm_prefix_ints(&ctx.seed,_a->cpus,_b->ncpus_used,_b->ncpus); */
/*       pb_wait(_a->p_barrier); */
/*     } else if (_b->aff_mode == aff_scan) { */
/*       pb_wait(_a->p_barrier); */
/*       int idx_scan = n_run % SCANSZ; */
/*       int *from =  &cpu_scan[SCANLINE*idx_scan]; */
/*       from += N*_a->z_id; */
/*       for (int k = 0 ; k < N ; k++) _a->cpus[k] = from[k]; */
/*       pb_wait(_a->p_barrier); */
/*     } else { */
/*     } */
/*     if (_b->verbose>1) printf("Run %i of %i\r", n_run, _b->max_run); */
/*     printf("************* to re-init %d \n", n_run); */
/*     reinit(&ctx); */
/*     printf("************* re-init %d \n", n_run); */
/*     if (_b->do_change) perm_funs(&ctx.seed,fun,N); */
/*     printf("************* permed %d \n", n_run); */
/*     for (int _p = NT-1 ; _p >= 0 ; _p--) { */
/*       printf("************* to launch %d at %d on %d \n", n_run, _p); */
/*       launch(_p,fun[_p],&parg[_p]); */
/*     } */
/*     go(); */
/*     /1* if (_b->do_change) perm_threads(&ctx.seed,thread,NT); *1/ */
/*     for (int _p = NT-1 ; _p >= 0 ; _p--) { */
/*       join(_p); */
/*     } */
/*     /1* Log final states *1/ */
/*     for (int _i = _b->size_of_test-1 ; _i >= 0 ; _i--) { */
/*       int _out_1_x0_i = ctx.out_1_x0[_i]; */
/*       int _out_1_x2_i = ctx.out_1_x2[_i]; */
/*       outcome_t o; */
/*       int cond; */

/*       cond = final_ok(final_cond(_out_1_x0_i,_out_1_x2_i)); */
/*       o[out_1_x0_f] = _out_1_x0_i; */
/*       o[out_1_x2_f] = _out_1_x2_i; */
/*       add_outcome(hist,1,o,cond); */
/*       if (_b->aff_mode == aff_scan && _a->cpus[0] >= 0 && cond) { */
/*         pm_lock(_a->p_mutex); */
/*         ngroups[n_run % SCANSZ]++; */
/*         pm_unlock(_a->p_mutex); */
/*       } else if (_b->aff_mode == aff_topo && _a->cpus[0] >= 0 && cond) { */
/*         pm_lock(_a->p_mutex); */
/*         ngroups[0]++; */
/*         pm_unlock(_a->p_mutex); */
/*       } */
/*       if (cond) { hist->n_pos++; } else { hist->n_neg++; } */
/*     } */
/*   } */

/*   finalize(&ctx); */
/*   return hist; */
/* } */

/* static void run(cmd_t *cmd,cpus_t *def_all_cpus) { */
/*   printf("!!!!!!!!!!!!!! start running .\n"); */
/*   if (cmd->prelude) prelude(); */
/*   tsc_t start = timeofday(); */
/*   printf("!!!!!!!!!!!!!! running ... \n"); */
/*   param_t prm ; */
/* /1* Set some parameters *1/ */
/*   prm.verbose = cmd->verbose; */
/*   prm.size_of_test = cmd->size_of_test; */
/*   prm.max_run = cmd->max_run; */
/*   prm.stride = cmd->stride > 0 ? cmd->stride : N ; */
/*   int ntopo = -1; */
/*   if (cmd->aff_mode == aff_topo) { */
/*     ntopo = find_string(group,SCANSZ,cmd->aff_topo); */
/*     if (ntopo < 0) { */
/*       log_error("Bad topology %s, reverting to scan affinity\n",cmd->aff_topo); */
/*       cmd->aff_mode = aff_scan; cmd->aff_topo = NULL; */
/*     } */
/*   } */
/*   prm.do_change = cmd->aff_mode != aff_custom && cmd->aff_mode != aff_scan && cmd->aff_mode != aff_topo; */
/*   if (cmd->fix) prm.do_change = 0; */
/*   prm.cm = coremap_seq(def_all_cpus->sz,1); */
/* /1* Computes number of test concurrent instances *1/ */
/*   int n_avail = cmd->avail > 0 ? cmd->avail : cmd->aff_cpus->sz; */
/*   if (n_avail >  cmd->aff_cpus->sz) log_error("Warning: avail=%i, available=%i\n",n_avail, cmd->aff_cpus->sz); */
/*   int n_exe; */
/*   if (cmd->n_exe > 0) { */
/*     n_exe = cmd->n_exe; */
/*   } else { */
/*     n_exe = n_avail < N ? 1 : n_avail / N; */
/*   } */
/* /1* Set affinity parameters *1/ */
/*   cpus_t *all_cpus = cmd->aff_cpus; */
/*   int aff_cpus_sz = cmd->aff_mode == aff_random ? max(all_cpus->sz,N*n_exe) : N*n_exe; */
/*   int aff_cpus[aff_cpus_sz]; */
/*   prm.aff_mode = cmd->aff_mode; */
/*   prm.ncpus = aff_cpus_sz; */
/*   prm.ncpus_used = N*n_exe; */
/* /1* Show parameters to user *1/ */
/*   if (prm.verbose) { */
/*     log_error( "MP: n=%i, r=%i, s=%i",n_exe,prm.max_run,prm.size_of_test); */
/*     log_error(", st=%i",prm.stride); */
/*     if (cmd->aff_mode == aff_incr) { */
/*       log_error( ", i=%i",cmd->aff_incr); */
/*     } else if (cmd->aff_mode == aff_random) { */
/*       log_error(", +ra"); */
/*     } else if (cmd->aff_mode == aff_custom) { */
/*       log_error(", +ca"); */
/*     } else if (cmd->aff_mode == aff_scan) { */
/*       log_error(", +sa"); */
/*     } */
/*     log_error(", p='"); */
/*     cpus_dump(cmd->aff_cpus); */
/*     log_error("'"); */
/*     log_error("\n"); */
/*   } */
/*   if (cmd->aff_mode == aff_random) { */
/*     for (int k = 0 ; k < aff_cpus_sz ; k++) { */
/*       aff_cpus[k] = all_cpus->cpu[k % all_cpus->sz]; */
/*     } */
/*   } else if (cmd->aff_mode == aff_topo) { */
/*     int *from = &cpu_scan[ntopo * SCANLINE]; */
/*     for (int k = 0 ; k < aff_cpus_sz ; k++) { */
/*       aff_cpus[k] = *from++; */
/*     } */
/*   } */
/*   printf("!!!!!!!!!!!!!! preparing running ... \n"); */
/*   hist_t *hist = NULL; */
/*   int n_th = n_exe-1; */
/*   /1* pthread_t th[n_th]; *1/ */
/*   zyva_t zarg[n_exe]; */
/*   pm_t *p_mutex = pm_create(); */
/*   pb_t *p_barrier = pb_create(n_exe); */
/*   printf("!!!!!!!!!!!!!! created mutex/barrier ... \n"); */
/*   int next_cpu = 0; */
/*   int delta = cmd->aff_incr; */
/*   if (delta <= 0) { */
/*     for (int k=0 ; k < all_cpus->sz ; k++) all_cpus->cpu[k] = -1; */
/*     delta = 1; */
/*   } else { */
/*     delta %= all_cpus->sz; */
/*   } */
/*   printf("!!!!!!!!!!!!!! got delta ... \n"); */
/*   int start_scan=0, max_start=gcd(delta,all_cpus->sz); */
/*   int *aff_p = aff_cpus; */
/*   for (int k=0 ; k < 1 ; k++) { */
/*     printf("!!!!!!!!!!!!!! for k=%d... \n", k); */
/*     zyva_t *p = &zarg[k]; */
/*     printf("!!!!!!!!!!!!!! 1 for k=%d... \n", k); */
/*     p->_p = &prm; */
/*     printf("!!!!!!!!!!!!!! 2 for k=%d... \n", k); */
/*     p->p_mutex = p_mutex; p->p_barrier = p_barrier; */ 
/*     printf("!!!!!!!!!!!!!! 3 for k=%d... \n", k); */
/*     p->z_id = k; */
/*     printf("!!!!!!!!!!!!!! 4 for k=%d... \n", k); */
/*     p->cpus = aff_p; */
/*     printf("!!!!!!!!!!!!!! 5 for k=%d... \n", k); */
/*     if (cmd->aff_mode != aff_incr) { */
/*       aff_p += N; */
/*     } else { */
/*       for (int i=0 ; i < N ; i++) { */
/*         *aff_p = all_cpus->cpu[next_cpu]; aff_p++; */
/*         next_cpu += delta; next_cpu %= all_cpus->sz; */
/*         if (next_cpu == start_scan) { */
/*           start_scan++ ; start_scan %= max_start; */
/*           next_cpu = start_scan; */
/*         } */
/*       } */
/*     } */
/*     /1* if (k < n_th) { *1/ */
/*     /1*   printf("!!!!!!!!!!!!!! launching %d ... \n", k); *1/ */
/*     /1*   launch(k,zyva,p); *1/ */
/*     /1* } else { *1/ */
/*     /1*   hist = (hist_t *)zyva(p); *1/ */
/*     /1* } *1/ */
/*     printf("!!!!!!!!!!!!!! 6 for k=%d... \n", k); */
/*       hist = (hist_t *)zyva(p); */
/*   } */

/*     printf("!!!!!!!!!!!!!! 7 ... \n"); */
/*   count_t n_outs = prm.size_of_test; n_outs *= prm.max_run; */
/*   for (int k=0 ; k < n_th ; k++) { */
/*     printf("!!!!!!!!!!!!!! 8 for k=%d... \n", k); */
/*     hist_t *hk = (hist_t *)join(k); */
/*     if (sum_outs(hk->outcomes) != n_outs || hk->n_pos + hk->n_neg != n_outs) { */
/*       fatal("MP, sum_hist"); */
/*     } */
/*     merge_hists(hist,hk); */
/*     free_hist(hk); */
/*   } */
/*   cpus_free(all_cpus); */
/*   tsc_t total = timeofday() - start; */
/*   pm_free(p_mutex); */
/*   pb_free(p_barrier); */

/*   n_outs *= n_exe ; */
/*   if (sum_outs(hist->outcomes) != n_outs || hist->n_pos + hist->n_neg != n_outs) { */
/*     fatal("MP, sum_hist") ; */
/*   } */
/*   count_t p_true = hist->n_pos, p_false = hist->n_neg; */
/*   postlude(cmd,hist,p_true,p_false,total); */
/*   free_hist(hist); */
/*   cpus_free(prm.cm); */
/* } */

/* a static version of the above dynamic run () */
static void run(cmd_t *cmd,cpus_t *def_all_cpus) {
  if (cmd->prelude) prelude();

  zyva_t z;
  pm_t *p_mutex = pm_create();
  pb_t *p_barrier = pb_create(2);

  param_t param;
  param.size_of_test = cmd->size_of_test;
  param.stride = cmd->stride > 0 ? cmd->stride : N ;
  param.max_run = cmd->max_run;

  f_t *funs[] = {&P0, &P1};
  parg_t parg[2];

  ctx_t ctx;
  ctx._p = &param;
  init(&ctx);

  for (int _p = 0; _p < 2; _p++) {
    parg[_p]._a = &ctx;
    parg[_p].cpu = _p+1;
    parg[_p].th_id = _p;
  }

  tsc_t start = timeofday();
  schedule(1, funs[0], &parg[0]);
  schedule(2, funs[1], &parg[1]);

  uint64_t new_table = (uint64_t)((void*)__vtable);
  new_table += 0x400;
  new_table &= ~(0x800 - 1);
  go(new_table);

  join(1);
  join(2);

  hist_t *hist = alloc_hist();
  for (int _i = param.size_of_test-1 ; _i >= 0 ; _i--) {
    int _out_1_x0_i = ctx.out_1_x0[_i];
    int _out_1_x2_i = ctx.out_1_x2[_i];
    outcome_t o;
    int cond;

    cond = final_ok(final_cond(_out_1_x0_i,_out_1_x2_i));
    o[out_1_x0_f] = _out_1_x0_i;
    o[out_1_x2_f] = _out_1_x2_i;
    add_outcome(hist,1,o,cond);
    if (cond) { hist->n_pos++; } else { hist->n_neg++; }
  }

  finalize(&ctx);

  tsc_t total = timeofday() - start;
  count_t p_true = hist->n_pos, p_false = hist->n_neg;
  postlude(cmd,hist,p_true,p_false,total);
}


int MP_dmb_svc(int argc, char** argv);
int MP_dmb_svc(int argc, char **argv) {
  cpus_t *def_all_cpus = read_force_affinity(AVAIL,0);
  if (def_all_cpus->sz < N) {
    cpus_free(def_all_cpus);
    return EXIT_SUCCESS;
  }
  cmd_t def = { 0, 1, SIZE_OF_TEST, STRIDE, AVAIL, 0, 0, aff_incr, 0, 1, AFF_INCR, def_all_cpus, NULL, -1, MAX_LOOP, NULL, NULL, -1, -1, -1, 0, 1};
  cmd_t cmd = def;
  /* parse_cmd(argc,argv,&def,&cmd); */
  run(&cmd,def_all_cpus);
  if (def_all_cpus != cmd.aff_cpus) cpus_free(def_all_cpus);
  return EXIT_SUCCESS;
}
