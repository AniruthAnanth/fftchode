// CPU hog for the contended thread runs (FFTE-9): one thread per listed CPU spins for burst_us, then sleeps an
// exponential gap so that it is busy a fraction duty of the time (0.999 = always).  Without root it runs at normal priority.
//   hog <cpu or lo-hi> <burst_us> <duty 0..1> <seconds>
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
static double burst, duty, secs;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec;}
static void *run(void *a){
  int cpu=(int)(long)a; cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu,&s); pthread_setaffinity_np(pthread_self(),sizeof s,&s);
  struct sched_param p={.sched_priority=1}; if(pthread_setschedparam(pthread_self(),SCHED_FIFO,&p)) perror("fifo");
  unsigned seed=cpu*7919+1; double end=now()+secs, gap=burst*(1-duty)/duty;
  while(now()<end){
    double u=(rand_r(&seed)+1.0)/((double)RAND_MAX+2), sl=-log(u)*gap;
    struct timespec ts={(time_t)sl,(long)((sl-(time_t)sl)*1e9)}; nanosleep(&ts,0);
    double t0=now(); while(now()-t0<burst) ;
  }
  return 0;
}
int main(int c,char**v){
  int lo,hi; if(sscanf(v[1],"%d-%d",&lo,&hi)!=2) hi=lo=atoi(v[1]);
  burst=atof(v[2])*1e-6; duty=atof(v[3]); secs=atof(v[4]);
  pthread_t t[256]; int n=0; for(int i=lo;i<=hi;i++) pthread_create(&t[n++],0,run,(void*)(long)i);
  for(int i=0;i<n;i++) pthread_join(t[i],0); return 0;
}
