// KissFFT plan builder, compiled once per precision (-Dkiss_fft_scalar=double [-DKISS_DOUBLE] -DKISS_PREC_D, or float with
// -include kissf_rename.h -DKISS_PREC_S) and linked into adapters/kissfft.cpp.  Reuses the pinned KissFFT build of bench/ (kiss_fft.c,
// kiss_fftr.c) plus kiss_fftnd.c / kiss_fftndr.c for multi-dimensional problems.
// Supported: contiguous (unit-stride) data, c2c any rank (kiss_fftnd), r2c / c2r with even last dimension (kiss_fftr / kiss_fftndr);
// howmany = loop over transforms.  Not run (FAIL with reason): strided "col" layouts, odd real length, and any dimension with a prime
// factor > 1024 (KissFFT's generic butterfly is O(N p); bench/README.md keeps the same rule).
#define HARNESS_TYPES_ONLY
#include "harness.h"
extern "C" {
#include "kiss_fft.h"
#include "kiss_fftr.h"
#include "kiss_fftnd.h"
#include "kiss_fftndr.h"
}
#ifdef KISS_PREC_D
#define KISS_MAKE kiss_plan_d
#else
#define KISS_MAKE kiss_plan_s
#endif
typedef kiss_fft_scalar S;

static unsigned max_prime_factor(unsigned n) {
    unsigned f, m = 1;
    for (f = 2; f * f <= n; ++f) while (n % f == 0) { m = f; n /= f; }
    return n > 1 ? n : m;
}
namespace {  // internal linkage: this file is compiled twice (double, float) into one executable
struct KissPlan : Plan {
    int kind, rank; long long batch, di, dout;  // distances in scalar units
    S *in, *out; void *cfg = nullptr; bool bwd;
    ~KissPlan() { free(cfg); }
    void exec() override {
        for (long long b = 0; b < batch; b++) {
            S *i = in + b * di, *o = out + b * dout;
            if (kind == 0) { if (rank == 1) kiss_fft((kiss_fft_cfg)cfg, (kiss_fft_cpx *)i, (kiss_fft_cpx *)o); else kiss_fftnd((kiss_fftnd_cfg)cfg, (kiss_fft_cpx *)i, (kiss_fft_cpx *)o); }
            else if (kind == 1) { if (rank == 1) kiss_fftr((kiss_fftr_cfg)cfg, i, (kiss_fft_cpx *)o); else kiss_fftndr((kiss_fftndr_cfg)cfg, i, (kiss_fft_cpx *)o); }
            else { if (rank == 1) kiss_fftri((kiss_fftr_cfg)cfg, (kiss_fft_cpx *)i, o); else kiss_fftndri((kiss_fftndr_cfg)cfg, (kiss_fft_cpx *)i, o); }
        }
    }
};
}  // namespace
Plan *KISS_MAKE(const Case &c, const Geo &g, S *in, S *out, std::string &why) {
    if (c.layout == "col") { why = "KissFFT: strided (col) layout not supported"; return nullptr; }
    bool real = c.kind == "rf" || c.kind == "rb";
    if (real && in == out) { why = "in-place real not supported"; return nullptr; }
    std::vector<int> dims;
    for (int k = 0; k < g.rank; k++) {
        dims.push_back((int)g.d[k].n);
        if (max_prime_factor((unsigned)g.d[k].n) > 1024) { why = "KissFFT: prime factor > 1024 (generic O(N p) butterfly), not run"; return nullptr; }
    }
    if (real && dims.back() % 2) { why = "KissFFT: real transforms need an even last dimension"; return nullptr; }
    std::unique_ptr<KissPlan> p(new KissPlan());
    p->rank = g.rank; p->batch = g.v.n; p->in = in; p->out = out;
    p->kind = real ? (c.kind == "rf" ? 1 : 2) : 0; p->bwd = c.kind == "cb" || c.kind == "rb";
    p->di = g.v.is * (c.kind == "rf" ? 1 : 2); p->dout = g.v.os * (c.kind == "rb" ? 1 : 2);
    if (p->kind == 0) p->cfg = g.rank == 1 ? (void *)kiss_fft_alloc(dims[0], p->bwd, nullptr, nullptr) : (void *)kiss_fftnd_alloc(dims.data(), g.rank, p->bwd, nullptr, nullptr);
    else p->cfg = g.rank == 1 ? (void *)kiss_fftr_alloc(dims[0], p->bwd, nullptr, nullptr) : (void *)kiss_fftndr_alloc(dims.data(), g.rank, p->bwd, nullptr, nullptr);
    if (!p->cfg) { why = p->kind != 0 && g.rank > 1 ? "KissFFT: kiss_fftndr_alloc rejects the problem (its integer-overflow guard fires for N-D real problems of this size)" : "KissFFT: alloc failed"; return nullptr; }
    return p.release();
}
