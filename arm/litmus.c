#include <libcflat.h>

extern int MP(int argc, char** argv);
extern int MP_dmb_svc(int argc, char** argv);
extern int MP_dmb_svc_eret(int argc, char** argv);

int main(int argc, char **argv)
{
    MP(argc, argv);
    MP_dmb_svc(argc, argv);
    MP_dmb_svc_eret(argc, argv);
    return report_summary();
}

