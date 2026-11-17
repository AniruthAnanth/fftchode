/*
 * KISS FFT benchee, adapted from benchFFT's benchees/kissfft/doit.c (1-D only).
 * kiss_fftr needs an even length.  KISS FFT handles a prime factor p with an O(N p) generic
 * butterfly, so lengths with a prime factor above KISS_MAXPRIME are reported n/a rather than
 * run (N = 1000003 would take roughly 10^12 operations per transform).
 */
#include "bench-user.h"
#include "kiss_fft.h"
#include "kiss_fftr.h"

#define KISS_MAXPRIME 1024

BEGIN_BENCH_DOC
BENCH_DOC("name", "kissfft")
END_BENCH_DOC

const char *na_reason = "";
static void *cfg;

const char *benchee_info(void) { return "KISS FFT, kiss_fft_scalar=" 
#ifdef KISS_DOUBLE
"double"
#else
"float"
#endif
; }

void useropt(const char *arg) { UNUSED(arg); }

static unsigned max_prime_factor(unsigned n)
{
     unsigned f, m = 1;
     for (f = 2; f * f <= n; ++f)
          while (n % f == 0) { m = f; n /= f; }
     return n > 1 ? n : m;
}

int can_do(struct problem *p)
{
     na_reason = "";
     if (p->rank != 1 || p->vrank != 0) return 0;
     if (p->kind == PROBLEM_REAL && p->n[0] % 2) {
          na_reason = "kiss_fftr requires even N";
          return 0;
     }
     if (max_prime_factor(p->n[0]) > KISS_MAXPRIME) {
          na_reason = "prime factor > 1024: generic O(N*p) butterfly, not run";
          return 0;
     }
     return 1;
}

void copy_h2c(struct problem *p, bench_complex *out) { copy_h2c_unpacked(p, out, -1.0); }
void copy_c2h(struct problem *p, bench_complex *in) { copy_c2h_unpacked(p, in, -1.0); }
void copy_r2c(struct problem *p, bench_complex *out) { copy_r2c_packed(p, out); }
void copy_c2r(struct problem *p, bench_complex *in) { copy_c2r_packed(p, in); }

void setup(struct problem *p)
{
     size_t lenmem = 0;
     BENCH_ASSERT(can_do(p));
     if (p->kind == PROBLEM_COMPLEX) {
          kiss_fft_alloc(p->n[0], p->sign == 1, 0, &lenmem);
          cfg = bench_malloc(lenmem);
          kiss_fft_alloc(p->n[0], p->sign == 1, cfg, &lenmem);
     } else {
          kiss_fftr_alloc(p->n[0], p->sign == 1, 0, &lenmem);
          cfg = bench_malloc(lenmem);
          kiss_fftr_alloc(p->n[0], p->sign == 1, cfg, &lenmem);
     }
}

void doit(int iter, struct problem *p)
{
     int i;
     if (p->kind == PROBLEM_COMPLEX) {
          kiss_fft_cpx *in = (kiss_fft_cpx *) p->in, *out = (kiss_fft_cpx *) p->out;
          for (i = 0; i < iter; ++i) kiss_fft((kiss_fft_cfg) cfg, in, out);
     } else {
          kiss_fft_scalar *in = (kiss_fft_scalar *) p->in;
          kiss_fft_cpx *out = (kiss_fft_cpx *) p->out;
          for (i = 0; i < iter; ++i) kiss_fftr((kiss_fftr_cfg) cfg, in, out);
     }
}

void done(struct problem *p)
{
     UNUSED(p);
     bench_free(cfg);
}
