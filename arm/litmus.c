#include <libcflat.h>

extern int MyMP_pos(void);
extern int MyMP_dmbs(void);
extern int MyMP_dmb_svc(void);
extern int MyMP_dmb_eret0(void);
extern int MyMP_dmb_1svc1(void);
extern int MyMP_dmb_svc_eret(void);
extern int MyMMU1(void);

int main(int argc, char **argv)
{
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

