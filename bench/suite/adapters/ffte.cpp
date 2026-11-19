// FFTE 7.0 adapter (Fortran 77, zfft1d).  FFTE provides only 1-D complex double-precision transforms in place, N = 2^a 3^b 5^c;
// everything else prints FAIL.  Batches are a loop.  Plan = zfft1d(a, n, 0, b) (coefficient table in the work array b, 2N complex).
// Out-of-place cases run in place on the output buffer (load() copies in -> out before every timed batch; FFTE has no out-of-place entry,
// and repeated in-place transforms inside a batch make the values grow, which does not slow x86 down: same as bench/README.md).
// FFTE's inverse (iopt = +1) divides by N; the adapter multiplies by N afterwards to make it unnormalized, which adds one O(N) pass to cb cases.
#include "harness.h"
extern "C" void zfft1d_(void *a, int *n, int *iopt, void *b);

const char *adapter_name() { return "ffte"; }
void adapter_init(int) {}
void adapter_cleanup() {}

struct FftePlan : Plan {
    double *in, *out, *work; int n; long long batch, dist; bool bwd;
    ~FftePlan() { free(work); }
    void load() override {
        int zero = 0, nn = n; zfft1d_(out, &nn, &zero, work);
        if (in != out) memcpy(out, in, sizeof(double) * 2 * (size_t)n * batch);
    }
    void exec() override {
        int nn = n, op = bwd ? 1 : -1;
        for (long long b = 0; b < batch; b++) {
            double *a = out + b * dist; zfft1d_(a, &nn, &op, work);
            if (bwd) for (long long i = 0; i < 2LL * n; i++) a[i] *= (double)n;
        }
    }
};
static bool smooth(long long n) { for (int f : {2, 3, 5}) while (n % f == 0) n /= f; return n == 1; }
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) {
    if (c.kind != "cf" && c.kind != "cb") { why = "FFTE 7.0 has no 1-D real transform"; return nullptr; }
    if (g.rank != 1 || c.layout == "col") { why = "FFTE adapter: 1-D contiguous only"; return nullptr; }
    if (!smooth(g.d[0].n)) { why = "FFTE: N is not 2^a 3^b 5^c"; return nullptr; }
    std::unique_ptr<FftePlan> p(new FftePlan());
    p->in = in; p->out = out; p->n = (int)g.d[0].n; p->batch = g.v.n; p->dist = 2 * g.v.os; p->bwd = c.kind == "cb";
    p->work = (double *)abuf(sizeof(double) * 4 * (size_t)p->n);
    memset(p->work, 0, sizeof(double) * 4 * (size_t)p->n);
    int zero = 0, nn = p->n; zfft1d_(out, &nn, &zero, p->work);
    return p.release();
}
Plan *adapter_plan(const Case &, const Geo &, float *, float *, std::string &why) { why = "FFTE is double precision only"; return nullptr; }
