#include <stdint.h>

#include <libcflat.h>
#include <asm/smp.h>

#include <MyCommon.h>

/* Exception Vectors */

void* default_handler(uint64_t vec, uint64_t esr) {
    uint64_t ec = esr >> 26;
    printf("%s\n", "Unhandled Exception:");
    printf("  Vector: %d (%s)\n", vec, vec_names[vec]);
    printf("  EC: %d (%s)\n", ec, ec_names[ec]);
    printf("ESR_EL1: %06x\n", esr);
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
    dsb();
  } else {
    while (*barrier == 0);
  }
}
