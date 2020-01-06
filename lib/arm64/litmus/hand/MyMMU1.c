#include <stdint.h>
//#include <stdlib.h>

#include <libcflat.h>
#include <asm/smp.h>

#include "MyCommon.h"
#include "MyVMM.h"

#define T 10000            /* number of runs */
#define NAME "MMU1"        /* litmus test name */
#define N_THREADS 1        /* number of hardware threads in test */

#define X 0
#define Y 1

#define out_p0_x2 0

static uint64_t PA(test_ctx_t* ctx, uint64_t va) {
    /* return the PA associated with the given va in a particular iteration */
    return translate4k(ctx->ptable, va);
}

static uint64_t PTE(test_ctx_t* ctx, uint64_t va) {
    /* return the VA at which the PTE lives for the given va in a particular
     * iteration */
    return ref_pte4k(ctx->ptable, va);
}

/* test payload */

static void P0(void* a) {
    test_ctx_t* ctx = (test_ctx_t*)a;
    uint64_t* x = ctx->heap_vars[X];
    uint64_t* y = ctx->heap_vars[Y];

    uint64_t* x2 = ctx->out_regs[out_p0_x2];

    for (int j = 0; j < T; j++) {
        int i = j;//ctx->shuffled_ixs[j];
        start_of_run(ctx, 0, i);
        raise_to_el1(); // only edit pagetables at EL1

        uint64_t old_pte_x = *(uint64_t*)PTE(ctx, &x[i]);
        uint64_t old_pte_y = *(uint64_t*)PTE(ctx, &y[i]);

        asm volatile(
            "ldr x0, [%[x1]]\n"
            "str x0, [%[x2]]\n"
            "dsb sy\n"
            "tlbi vmalle1is\n"
            "dsb sy\n"
            "isb\n"
            "mov x3, #1\n\t"
            "str x3, [%[x4]]\n\t"
            "ldr %[x5], [%[x6]]\n\t"
            : [x5] "=&r" (x2[i])
            : [x1] "r" (PTE(ctx, &x[i])), [x2] "r" (PTE(ctx, &y[i])), [x4] "r" (&x[i]), [x6] "r" (&y[i])
            : "cc", "memory", "x0", "x2");
        
        end_of_run(ctx, 0, i);
        *(uint64_t*)PTE(ctx, &x[i]) = old_pte_x;
        *(uint64_t*)PTE(ctx, &y[i]) = old_pte_y;
        flush_tlb();
    }
}

static void go_cpus(void* a) {
    static int go_bar = 0;
    test_ctx_t* ctx = (test_ctx_t*)a;
    int cpu = smp_processor_id();

    start_of_thread(ctx, cpu);
    bwait(cpu, 0, &go_bar, N_CPUS);

    switch (cpu) {
        case 1:
            P0(a);
            break;
        default:
            break;
    }

    end_of_thread(ctx, cpu);
}

void MyMMU1(void) {
    test_ctx_t ctx;
    start_of_test(&ctx, NAME, N_THREADS, 2, 1, T);
    ctx.privileged_harness = 1;

    /* run test */
    trace("%s\n", "Running Tests ...");
    on_cpus(go_cpus, &ctx);

    /* collect results */
    const char* reg_names[] = {
        "p0:x2",
    };
    const int relaxed_result[] = {
        /* p0:x2 =*/0,
    };

    end_of_test(&ctx, reg_names, relaxed_result);
}
