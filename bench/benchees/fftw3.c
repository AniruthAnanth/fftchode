/*
 * FFTW3 benchee, adapted from benchFFT's benchees/fftw3/doit.c: FFTW_ESTIMATE planning by
 * default (MATLAB's mode), optional "threads=N", complex c2c and real r2c, 1-D only.
 * The same file is compiled against fft.cpp (src/fftw3.h) and against upstream FFTW.
 */
#include "bench-user.h"
#include <fftw3.h>
#include <stdio.h>
#include <string.h>

#define CONCAT(prefix, name) prefix##name
#if defined(BENCHFFT_SINGLE)
#define FFTW(x) CONCAT(fftwf_, x)
#else
#define FFTW(x) CONCAT(fftw_, x)
#endif

BEGIN_BENCH_DOC
BENCH_DOC("name", "fftw3")
END_BENCH_DOC

const char *na_reason = "";
static FFTW(plan) the_plan;
static unsigned the_flags;
static int nthreads = 1;

const char *benchee_info(void)
{
     static char buf[512];
     snprintf(buf, sizeof buf, "%s; cc=[%s]; codelet_optim=[%s]", FFTW(version), FFTW(cc), FFTW(codelet_optim));
     return buf;
}

void useropt(const char *arg)
{
     if (!strncmp(arg, "threads=", 8)) nthreads = atoi(arg + 8);
     else if (!strcmp(arg, "estimate")) the_flags |= FFTW_ESTIMATE;
     else if (!strcmp(arg, "patient")) the_flags |= FFTW_PATIENT;
     else if (!strcmp(arg, "measure")) the_flags |= FFTW_MEASURE;
     else fprintf(stderr, "unknown user option: %s.  Ignoring.\n", arg);
}

int can_do(struct problem *p) { return p->rank == 1 && p->vrank == 0; }

void copy_h2c(struct problem *p, bench_complex *out) { copy_h2c_unpacked(p, out, -1.0); }
void copy_c2h(struct problem *p, bench_complex *in) { copy_c2h_unpacked(p, in, -1.0); }
void copy_r2c(struct problem *p, bench_complex *out)
{
     if (problem_in_place(p)) copy_r2c_unpacked(p, out);
     else copy_r2c_packed(p, out);
}
void copy_c2r(struct problem *p, bench_complex *in)
{
     if (problem_in_place(p)) copy_c2r_unpacked(p, in);
     else copy_c2r_packed(p, in);
}

void setup(struct problem *p)
{
     static int threads_ready;
     BENCH_ASSERT(can_do(p));
     if (nthreads > 1) {
          if (!threads_ready) { BENCH_ASSERT(FFTW(init_threads)()); threads_ready = 1; }
          FFTW(plan_with_nthreads)(nthreads);
     }
     if (p->kind == PROBLEM_COMPLEX)
          the_plan = FFTW(plan_dft)(p->rank, (int *) p->n, p->in, p->out, p->sign, the_flags);
     else if (p->sign == -1)
          the_plan = FFTW(plan_dft_r2c)(p->rank, (int *) p->n, p->in, p->out, the_flags);
     else
          the_plan = FFTW(plan_dft_c2r)(p->rank, (int *) p->n, p->in, p->out, the_flags);
     BENCH_ASSERT(the_plan);
}

void doit(int iter, struct problem *p)
{
     int i;
     FFTW(plan) plan = the_plan;
     UNUSED(p);
     for (i = 0; i < iter; ++i) FFTW(execute)(plan);
}

void done(struct problem *p)
{
     UNUSED(p);
     FFTW(destroy_plan)(the_plan);
}
