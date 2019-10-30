#include <libcflat.h>

/* extern int MP(int argc, char** argv); */
/* extern int MP_dmb_svc(int argc, char** argv); */
/* extern int MP_dmb_svc_eret(int argc, char** argv); */
/* extern int MP_dmbs(int argc, char** argv); */

extern int MyMP_pos(void);
extern int MyMP_dmbs(void);
extern int MyMP_dmb_svc(void);
extern int MyMP_dmb_eret0(void);
extern int MyMP_dmb_svc0(void);
extern int MyMP_dmb_svc_eret(void);

int main(int argc, char **argv)
{
  /* MP(argc, argv); */
  /* MP_dmbs(argc, argv); */
  /* MP_dmb_svc(argc, argv); */
  /* MP_dmb_svc_eret(argc, argv); */
  MyMP_pos();
  MyMP_dmbs();
  MyMP_dmb_svc();
  MyMP_dmb_svc0();
  MyMP_dmb_eret0();
  MyMP_dmb_svc_eret();
  return report_summary();
}

