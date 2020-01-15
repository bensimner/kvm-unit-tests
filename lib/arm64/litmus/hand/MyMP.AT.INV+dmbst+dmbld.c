#include <stdint.h>

#include "MyLitmusTests.h"
#include "MyCommon.h"

static void P0(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t* pas, uint64_t** _out_regs) {
  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];
  uint64_t* z = heap_vars[2];

  uint64_t* xpte = ptes[0];
  uint64_t* zpte = ptes[2];

  asm volatile (
    "ldr x0, [%[zpte]]\n\t"
    "str x0, [%[x1]]\n\t"
    "dmb st\n\t"
    "mov x2, #1\n\t"
    "str x2, [%[x3]]\n\t"
  :
  : [x1] "r" (xpte), [x3] "r" (y), [zpte] "r" (zpte)
  : "cc", "memory", "x0", "x2"
  );
}

static void* pagefault_handler(uint64_t esr, regvals_t* regs) {
  uint64_t* flag = regs->x0;
  *flag = 1;

  // set return address back to test 
  asm volatile (
    "mrs x0, ELR_EL1\n"
    "add x0, x0, #4\n"
    "msr ELR_EL1, x0\n"
  :
  :
  : "x0", "memory"
  );
}


static void P1(test_ctx_t* ctx, int i, uint64_t** heap_vars, uint64_t** ptes, uint64_t* pas, uint64_t** out_regs) {
  uint64_t* x = heap_vars[0];
  uint64_t* y = heap_vars[1];
  uint64_t* flag = heap_vars[3];
  uint64_t* x0 = out_regs[0];
  uint64_t* x2 = out_regs[1];

  set_pgfault_handler(x, pagefault_handler);

  asm volatile (
    "mov x0, %[flag]\n\t"

    "ldr %[x0], [%[x1]]\n\t"
    "dmb ld\n\t"
    "ldr x2, [%[x3]]\n\t"
    "ldr %[x2], [%[flag]]\n\t"
  : [x0] "=&r" (*x0), [x2] "=&r" (*x2)
  : [x1] "r" (y), [x3] "r" (x), [flag] "r" (flag)
  : "x0", "x2", "cc", "memory"
  );

  reset_pgfault_handler(x);
}

void MyMP_AT_inv_dmbst_dmbld(void) {
  run_test(
    "MP.AT.INV+dmb.st+dmb.ld",
    2, (th_f*[]){P0,P1}, 
    4, (const char*[]){"x", "y", "z", "flag"}, 
    2, (const char*[]){"p1:x0", "p1:x2"}, 
    (test_config_t){
      .interesting_result =
        (uint64_t[]){
          /* p1:x0 =*/ 1,
          /* p1:x2 =*/ 1,
        },
      .no_init_states = 1,
      .init_states = 
        (init_varstate_t[]){
          (init_varstate_t){
            "x",
            TYPE_PTE,
            0,
          }
        }
    });
}
