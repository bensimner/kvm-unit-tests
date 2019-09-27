#include <stdint.h>

#include <libcflat.h>

void el1_sync_spel1(void) {
    uint64_t esr;
    asm volatile ("mrs %[esr], ESR_EL1\n" : [esr] "=r" (esr) : :);
    uint8_t ec = esr >> 26;
    if (ec == 0x15) /* SVC */
        asm volatile ("mov x16, #123\n\teret");
    else
        asm volatile ("eret");
}

void vector_table_new(void);

/* the new vector table */
__attribute__((align(11)))
inline void vector_table_new(void) {
    asm volatile (
        ".global vector_table_new\n\t"
        ".balign 0x800\n\t"
        ".balign 0x200\n\t"
        "eret\n\t"
        ".balign 0x200\n\t"
        "b el1_sync_spel1\n\t"  /* this is the handler SVC will call with SP_EL1 */
        ".balign 0x200\n\t"
        "eret\n\t"
        ".balign 0x200\n\t"
        "eret\n\t"
    );
}

void change_vbar_el1(char* p) {
    asm volatile (
        "msr vbar_el1, %[ptr]\n\t"
        "isb\n\t"
    :
    : [ptr] "r" (p)
    : "memory"
    );
}

uint64_t get_vbar_el1(void) {
    uint64_t vbar;
    asm volatile ("mrs %[vbar], vbar_el1\n" : [vbar] "=r" (vbar) : : "memory" );
    return vbar;
}

int main(int argc, char **argv)
{
    /* get current vtable ptr so we can restore later */
    uint64_t current_vtable = get_vbar_el1();

    /* change to use our vtable */
    uint64_t new_vtable = (uint64_t)vector_table_new;
    change_vbar_el1(new_vtable);

    /* run test */
    asm volatile ("svc #0\n\t");

    /* collect results */
    uint64_t x16;
    asm volatile (
                  "mov %[x16], x16\n\t"
    : [x16] "=r" (x16)
    :
    :
    );

    /* restore old vtable */
    asm volatile (
                  "msr VBAR_EL1, %[vbar]\n\t"
    :
    : [vbar] "r" (current_vtable)
    :
    );

    /* print results */
    printf("x16 = %d\n", x16);
    return report_summary();
}

