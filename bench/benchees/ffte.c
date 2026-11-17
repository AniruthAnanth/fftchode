/*
 * FFTE benchee: benchFFT's benchees/ffte/doit-ffte.c (taken from the benchFFT checkout; build.sh
 * copies it to build/doit-ffte.c with one patch: FFTE 7.0's work array B must hold 2N complex
 * values, benchFFT's FFTE 6.0 glue allocates N) plus can_do restrictions with reasons.  FFTE 7.0 provides only complex
 * double-precision 1-D transforms for N = 2^a 3^b 5^c, in place.
 */
#define NAME "ffte"
#define DOC_ITEMS
#define can_do ffte_can_do
#define useropt ffte_useropt_unused
#include "doit-ffte.c"
#undef can_do
#undef useropt

const char *na_reason = "";
const char *benchee_info(void) { return "FFTE 7.0 (Fortran 77), zfft1d, in-place"; }
void useropt(const char *arg) { UNUSED(arg); }

int can_do(struct problem *p)
{
     na_reason = "";
     if (!DOUBLE_PRECISION) { na_reason = "FFTE is double precision only"; return 0; }
     if (p->kind != PROBLEM_COMPLEX) { na_reason = "FFTE 7.0 has no 1-D real transform"; return 0; }
     if (p->rank != 1 || p->vrank != 0) return 0;
     if (!check_prime_factors(p->size, 5)) { na_reason = "N is not of the form 2^a 3^b 5^c"; return 0; }
     return ffte_can_do(p);
}

void copy_h2c(struct problem *p, bench_complex *out) { copy_h2c_unpacked(p, out, -1.0); }
void copy_c2h(struct problem *p, bench_complex *in) { copy_c2h_unpacked(p, in, -1.0); }
void copy_r2c(struct problem *p, bench_complex *out) { copy_r2c_packed(p, out); }
void copy_c2r(struct problem *p, bench_complex *in) { copy_c2r_packed(p, in); }
