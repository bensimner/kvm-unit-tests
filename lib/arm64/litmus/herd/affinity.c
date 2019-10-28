/****************************************************************************/
/*                           the diy toolsuite                              */
/*                                                                          */
/* Jade Alglave, University College London, UK.                             */
/* Luc Maranget, INRIA Paris-Rocquencourt, France.                          */
/*                                                                          */
/* Copyright 2015-present Institut National de Recherche en Informatique et */
/* en Automatique and the authors. All rights reserved.                     */
/*                                                                          */
/* This software is governed by the CeCILL-B license under French law and   */
/* abiding by the rules of distribution of free software. You can use,      */
/* modify and/ or redistribute the software under the terms of the CeCILL-B */
/* license as circulated by CEA, CNRS and INRIA at the following URL        */
/* "http://www.cecill.info". We also give a copy in LICENSE.txt.            */
/****************************************************************************/
#include <stdio.h>
#include <sched.h>
#include <unistd.h>
#ifdef _FREEBSD_AFFINITY
#include <sys/cpuset.h>
/* #include <pthread_np.h> */
typedef cpuset_t cpu_set_t;
#endif
#include "utils.h"
#include "affinity.h"

#ifdef CPUS_DEFINED
cpus_t *read_affinity(void) {
  cpus_t *r = cpus_create(4) ;
  for (int i = 0; i < 4; i++) {
    r->cpu[i] = i;
  }
  return r ;
}

#endif
/* Attempt to force processors wake up, on devices where unused procs
   go to sleep... */


#ifdef FORCE_AFFINITY
static void* loop(void *p)  {
}


static void warm_up(int sz, tsc_t d) {
}

#ifdef CPUS_DEFINED
cpus_t *read_force_affinity(int n_avail, int verbose) {
  cpus_t *r = read_affinity() ;
  return r;
}
#endif
#endif

#ifdef CPUS_DEFINED

void write_affinity(cpus_t *p) {
}
#endif

void write_one_affinity(int a) {
}

#ifdef FORCE_AFFINITY
/* Get the number of present cpus, fragile */

static int get_present(void) {
  return 4 ;
}

void force_one_affinity(int a, int sz,int verbose, char *name) {
}
#endif
