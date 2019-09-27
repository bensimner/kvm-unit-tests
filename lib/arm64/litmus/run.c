#include <libcflat.h>
#include <stdlib.h>

/* Declarations of tests entry points */
extern int MP(int argc,char **argv);

/* Date function */
#include <time.h>
static void my_date(void) {
  /* time_t t = time(NULL); */
  /* fprintf(out,"%s",ctime(&t)); */
}

/* Postlude */
static void end_report(int argc,char **argv) {
  printf("%s\n","Revision 6df01eae65da067104a82cfcdcc9d3629c927419, version 7.52+10(dev)");
  printf("%s\n","Command line: litmus7 -o build/ -force_affinity true -mach nexus9 -carch AArch64 MP.litmus");
  printf("%s\n","Parameters");
  printf("%s\n","#define SIZE_OF_TEST 10000");
  printf("%s\n","#define NUMBER_OF_RUN 100");
  printf("%s\n","#define AVAIL 2");
  printf("%s\n","#define STRIDE 1");
  printf("%s\n","#define MAX_LOOP 0");
  printf("%s\n","/* gcc options: -D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -O2 -pthread */");
  printf("%s\n","/* gcc link options: -static */");
  printf("%s\n","/* barrier: userfence */");
  printf("%s\n","/* launch: changing */");
  printf("%s\n","/* affinity: incr0 */");
  printf("%s\n","/* alloc: dynamic */");
  printf("%s\n","/* memory: direct */");
  printf("%s\n","/* stride: 1 */");
  printf("%s\n","/* safer: write */");
  printf("%s\n","/* preload: random */");
  printf("%s\n","/* speedcheck: no */");
  printf("%s\n","/* proc used: 2 */");
/* Command line options */
  printf("Command:");
  for ( ; *argv ; argv++) {
    printf(" %s",*argv);
  }
  printf("%s", "\n");
}

/* Run all tests */
static void run(int argc,char **argv) {
  my_date();
  MP(argc,argv);
  end_report(argc,argv);
  my_date();
}

int main(int argc,char **argv) {
  run(argc,argv);
  return 0;
}
