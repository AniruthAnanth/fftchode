/* Do-nothing benchee: measures the fixed cost of driver + libbench + libc in the size table. */
#include "bench-user.h"
BEGIN_BENCH_DOC
BENCH_DOC("name", "null")
END_BENCH_DOC
const char *na_reason = "";
void useropt(const char *arg) { UNUSED(arg); }
int can_do(struct problem *p) { UNUSED(p); return 1; }
void copy_h2c(struct problem *p, bench_complex *out) { copy_h2c_unpacked(p, out, -1.0); }
void copy_c2h(struct problem *p, bench_complex *in) { copy_c2h_unpacked(p, in, -1.0); }
void copy_r2c(struct problem *p, bench_complex *out) { copy_r2c_packed(p, out); }
void copy_c2r(struct problem *p, bench_complex *in) { copy_c2r_packed(p, in); }
void setup(struct problem *p) { UNUSED(p); }
void doit(int iter, struct problem *p) { UNUSED(iter); UNUSED(p); }
void done(struct problem *p) { UNUSED(p); }
