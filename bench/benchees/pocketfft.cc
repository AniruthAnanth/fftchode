/*
 * pocketfft (C++ header-only, branch "cpp") benchee.  Uses the public stateless API
 * pocketfft::c2c / pocketfft::r2c with the plan cache enabled (POCKETFFT_CACHE_SIZE, set on the
 * command line) so repeated calls reuse the plan like FFTW's execute.  setup() performs one
 * transform to build the plan, so plan_cold_s includes one execution.
 */
#include "bench-user.h"
#include "pocketfft_hdronly.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace pocketfft;

BEGIN_BENCH_DOC
BENCH_DOC("name", "pocketfft")
END_BENCH_DOC

extern "C" {
const char *na_reason = "";
static size_t nthreads = 1;

const char *benchee_info(void) { return "pocketfft C++ header-only (mreineck/pocketfft branch cpp), plan cache enabled"; }

void useropt(const char *arg)
{
     if (!strncmp(arg, "threads=", 8)) nthreads = (size_t) atoi(arg + 8);
}

int can_do(struct problem *p) { return p->rank == 1 && p->vrank == 0; }

void copy_h2c(struct problem *p, bench_complex *out) { copy_h2c_unpacked(p, out, -1.0); }
void copy_c2h(struct problem *p, bench_complex *in) { copy_c2h_unpacked(p, in, -1.0); }
void copy_r2c(struct problem *p, bench_complex *out) { copy_r2c_packed(p, out); }
void copy_c2r(struct problem *p, bench_complex *in) { copy_c2r_packed(p, in); }

static void once(struct problem *p)
{
     shape_t shape{p->n[0]};
     if (p->kind == PROBLEM_COMPLEX) {
          stride_t st{(ptrdiff_t) sizeof(std::complex<bench_real>)};
          c2c(shape, st, st, shape_t{0}, p->sign == -1, (const std::complex<bench_real> *) p->in,
              (std::complex<bench_real> *) p->out, bench_real(1), nthreads);
     } else {
          BENCH_ASSERT(p->sign == -1);
          stride_t sin{(ptrdiff_t) sizeof(bench_real)}, sout{(ptrdiff_t) sizeof(std::complex<bench_real>)};
          r2c(shape, sin, sout, 0, true, (const bench_real *) p->in, (std::complex<bench_real> *) p->out,
              bench_real(1), nthreads);
     }
}

void setup(struct problem *p)
{
     BENCH_ASSERT(can_do(p));
     once(p);
}

void doit(int iter, struct problem *p)
{
     for (int i = 0; i < iter; ++i) once(p);
}

void done(struct problem *p) { UNUSED(p); }
}
