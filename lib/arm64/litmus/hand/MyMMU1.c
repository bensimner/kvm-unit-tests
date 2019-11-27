#include <stdint.h>
#include <stdlib.h>

#include <asm/smp.h>
#include <libcflat.h>

#include "MyCommon.h"
//#include "MyVMM.h"

#define T 100              /* number of runs */
#define NAME "MMU1"        /* litmus test name */

#define X 0
#define Y 1

#define out_p1_x0 0
#define out_p1_x2 1

static uint64_t PA(uint64_t va, int i) {
    /* return the PA associated with the given va in a particular iteration */
}

static uint64_t PTE(uint64_t va, int i) {
    /* return the VA at which the PTE lives for the given va in a particular
     * iteration */
}

/* page table management */

#define BIT(x, i) ((x >> i) & 0x1)
#define BIT_SLICE(x, i, j) ((x >> i) & ((1 << (1 + j - i)) - 1))
#define IS_ALIGNED(v, bits) ((v & ((1UL << bits) - 1)) == 0)
#define ALIGN_TO(v, bits) (v & ~((1UL << bits) - 1))

extern unsigned long etext; /* end of .text section (see flat.lds) */
static uint64_t* alloc_new_idmap_4k(void) {
    uint64_t* root_ptable = alloc_page_aligned();
    uint64_t code_end = (uint64_t)&etext;

    /* set ranges according to kvm-unit-tests/lib/arm/mmu.c */
    uint64_t phys_end = (3UL << 30);

    ptable_set_idrange_4k_smart(root_ptable, 0x00000000UL, 0x40000000UL, 0x44);
    ptable_set_idrange_4k_smart(root_ptable, 0x4000000000UL, 0x4020000000UL,
                                0x44);
    ptable_set_idrange_4k_smart(root_ptable, 0x8000000000UL, 0x10000000000UL,
                                0x44);

    ptable_set_idrange_4k_smart(root_ptable, PHYS_OFFSET, code_end,
                                0xd0);  // code
    ptable_set_idrange_4k_smart(root_ptable, code_end, phys_end,
                                0x50);  // stack(?) = 0x50

    // vmalloc.c  will also alloc some pages for allocating thread stack space.
    // have to ensure the space between (3UL << 30) and (4UL << 30) are mapped.
    // .. this happens dynamically inside set_new_id_translation for each core.

    return root_ptable;
}

static void set_new_id_translation(uint64_t* pgtable) {
    /* Each thread has some dynamically allocated stack space inside of KVM unit
     * tests this space is non-identiy mapped and so we must read the SP and
     * translate it via the original 64K mappings to figure out where to map to
     * in the new mappings.
     */
    uint64_t old_ttbr, sp;
    asm volatile("mov %[sp], SP\nmrs %[ttbr], TTBR0_EL1"
                 : [sp] "=r"(sp), [ttbr] "=r"(old_ttbr));
    uint64_t pa = translate64k(old_ttbr, sp);
    sp = ALIGN_TO(sp, 16);
    pa = ALIGN_TO(pa, 16);
    ptable_set_range_4k_smart(ptroot, sp, sp + (1UL << 16), pa, 0x50);

    /* now set the new TTBR and TCR */
    uint64_t ttbr = (uint64_t)pgtable;
    uint64_t tcr =
        (1L << 39) | /* HA, hardware access flag */
        (1L << 37) | /* TBI, top byte ignored. */
        (5L << 32) | /* IPS, 48-bit (I)PA. */
        (0 << 14) |  /* TG0, granule size, 4K. */
        (3 << 12) |  /* SH0, inner shareable. */
        (1 << 10) |  /* ORGN0, normal mem, WB RA WA Cacheable. */
        (1 << 8) |   /* IRGN0, normal mem, WB RA WA Cacheable. */
        (16 << 0) |  /* T0SZ, input address is 48 bits => VA[47:12] are used for
                        translation starting at level 0. */
        0;

    asm volatile(
        /* turn off MMU */
        "mrs x18, SCTLR_EL1\n"
        "mov x19, #0\n"
        "bfi x18, x19, #0, #1\n"
        "bfi x18, x19, #19, #1\n"
        "msr SCTLR_EL1, x18\n"
        "dsb ish\n"
        "isb\n"

        /* set TCR/TTBR to new pagetable */
        "msr TCR_EL1, %[tcr]\n"
        "msr TTBR0_EL1, %[ttbr]\n"
        "dsb ish\n"
        "isb\n"

        /* flush any TLBs */
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"

        /* turn MMU back on */
        "mrs x18, SCTLR_EL1\n"
        "orr x18, x18, #1\n"
        "msr SCTLR_EL1, x18\n"
        "dsb ish\n"
        "isb\n"
        "mov x18, #0\n"
        "ldr x18,[x18]\n"
        :
        : [tcr] "r"(tcr), [ttbr] "r"(ttbr)
        : "x18", "x19", "memory");
}

void restore_old_id_translation(uint64_t* root) {
    // TODO
}

/* test payload */

static void P0(void* a) {
    test_ctx_t* ctx = (test_ctx_t*)a;
    uint64_t* x = ctx->heap_vars[X];
    uint64_t* y = ctx->heap_vars[Y];
    uint64_t* start_bars = ctx->start_barriers;
    uint64_t* end_bars = ctx->end_barriers;

    for (int j = 0; j < T; j++) {
        int i = ctx->shuffled_ixs[j];

        start_of_run(ctx, i);
        bwait(0, i % 2, &start_bars[i], 2);

        asm volatile(
            "mov x0, #1\n\t"
            "str x0, [%[x1]]\n\t"
            "dmb sy\n\t"
            "mov x2, #1\n\t"
            "str x2, [%[x3]]\n\t"
            :
            : [x1] "r"(&x[i]), [x3] "r"(&y[i])
            : "cc", "memory", "x0", "x2");

        bwait(0, i % 2, &end_bars[i], 2);
        end_of_run(ctx, i);
    }
}

static void P1(void* a) {
    test_ctx_t* ctx = (test_ctx_t*)a;

    uint64_t* start_bars = ctx->start_barriers;
    uint64_t* end_bars = ctx->end_barriers;

    uint64_t* y = ctx->heap_vars[Y];
    uint64_t* x = ctx->heap_vars[X];
    uint64_t* x0 = ctx->out_regs[out_p1_x0];
    uint64_t* x2 = ctx->out_regs[out_p1_x2];

    for (int j = 0; j < T; j++) {
        int i = ctx->shuffled_ixs[j];
        start_of_run(ctx, i);
        bwait(1, i % 2, &start_bars[i], 2);

        asm volatile(
            "ldr %[x0], [%[x1]]\n\t"
            "dmb sy\n\t"
            "ldr %[x2], [%[x3]]\n"
            : [x0] "=&r"(x0[i]), [x2] "=&r"(x2[i])
            : [x1] "r"(&y[i]), [x3] "r"(&x[i])
            : "cc", "memory", "x0", "x1", "x2", "x3", "x4", "x5", "x6",
              "x7" /* dont touch parameter registers */
        );

        bwait(1, i % 2, &end_bars[i], 2);
        if (i % T / 10 == 0) {
            trace("%s", ".\n");
        }
    }
}

static void go_cpus(void* a) {
    test_ctx_t* ctx = (test_ctx_t*)a;

    uint64_t old_ttbr;
    asm volatile("mrs %[ttbr], TTBR0_EL1\n" : [ttbr] "=r"(old_ttbr));

    int cpu = smp_processor_id();
    trace("CPU%d: on\n", cpu);

    /* setup new ttbr */
    set_new_id_translation(ptroot);

    switch (cpu) {
        case 1:
            P0(a);
            break;
        case 2:
            P1(a);
            break;
    }

    restore_old_id_translation(old_ttbr);

    printf("CPU%d: restored and waiting\n", cpu);
    printf("[%d] &ctx->final_bar? %p\n", cpu, ctx->final_barrier);
    bwait(cpu, 0, ctx->final_barrier, N_CPUS);
}

void MyMMU1(void) {
    test_ctx_t ctx;
    init_test_ctx(&ctx, NAME, 2, 2, T);

    trace("====== %s ======\n", NAME);
    ptroot = alloc_new_idmap_4k();

    trace("New EL1 Exception Vector @ %p\n", &el1_exception_vector_table);

    /* run test */
    trace("%s\n", "Running Tests ...");
    on_cpus(go_cpus, &ctx);

    /* collect results */
    trace("%s\n", "Ran Tests.");

    /* collect results */
    const char* reg_names[] = {
        "p1:x0",
        "p1:x2",
    };
    const int relaxed_result[] = {
        /* p1:x0 =*/1,
        /* p1:x2 =*/0,
    };

    trace("%s\n", "Printing Results...");
    print_results(ctx.hist, &ctx, reg_names, relaxed_result);
    free_test_ctx(&ctx);
}
