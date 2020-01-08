#include <libcflat.h>

#include "MyLitmusTests.h"
#include "MyCommon.h"

int main(int argc, char **argv)
{
  NUMBER_OF_RUNS = 10000;

  MyMP_pos();
  MyMP_dmbs();
  MyMP_dmb_svc();
  MyMP_dmb_1svc1();
  MyMP_dmb_eret0();
  MyMP_dmb_svc_eret();
  MyMMU1();

  printf("<end of litmus tests>\n");
  return report_summary();
}

