#include <libcflat.h>

extern int MP(int argc, char** argv);

int main(int argc, char **argv)
{
    MP(argc, argv);
    return report_summary();
}

