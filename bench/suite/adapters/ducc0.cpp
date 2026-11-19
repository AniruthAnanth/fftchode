// ducc0 (mreineck/ducc, FFT component) adapter: ducc0::c2c / r2c / c2r on cfmav/vfmav views (strides in elements).
// Batches (howmany) become a leading array axis excluded from "axes".  ducc0 caches plans internally; plan time = first call
// minus second call is too noisy, so plan time = construction time of the 1-D plan objects (detail_fft::pocketfft_c/_r, one per distinct axis
// length, scalar and, for batched or multi-dimensional problems, vectorized variant) that the stateless calls build on a plan-cache miss.  Threads: the nthreads argument (ducc0's own thread pool).
#include "harness.h"
#include "ducc0/fft/fft.h"

using namespace ducc0;
static size_t g_nt = 1;
const char *adapter_name() { return "ducc0"; }
void adapter_init(int threads) { g_nt = (size_t)threads; }
void adapter_cleanup() {}

template <typename T> struct DuccPlan : Plan {
    fmav_info::shape_t shp, axes; fmav_info::stride_t sin, sout; const T *in; T *out; bool fwd; int kind;  // 0 c2c, 1 r2c, 2 c2r
    void exec() override {
        typedef std::complex<T> C;
        if (kind == 0) c2c(cfmav<C>((const C *)in, shp, sin), vfmav<C>((C *)out, shp, sout), axes, fwd, (T)1, g_nt);
        else if (kind == 1) {
            fmav_info::shape_t so = shp; so[axes.back()] = shp[axes.back()] / 2 + 1;
            r2c(cfmav<T>(in, shp, sin), vfmav<C>((C *)out, so, sout), axes, true, (T)1, g_nt);
        } else {
            fmav_info::shape_t si = shp; si[axes.back()] = shp[axes.back()] / 2 + 1;
            c2r(cfmav<C>((const C *)in, si, sin), vfmav<T>(out, shp, sout), axes, false, (T)1, g_nt);
        }
    }
};
template <typename T> static Plan *mk(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    std::unique_ptr<DuccPlan<T>> p(new DuccPlan<T>());
    p->kind = c.kind == "rf" ? 1 : c.kind == "rb" ? 2 : 0; p->fwd = c.kind == "cf"; p->in = in; p->out = out;
    // complex arrays are indexed in complex elements, real arrays in real elements: the g.d strides are already in those units
    size_t ax0 = 0;
    if (c.howmany > 1) { p->shp.push_back((size_t)g.v.n); p->sin.push_back((ptrdiff_t)g.v.is); p->sout.push_back((ptrdiff_t)g.v.os); ax0 = 1; }
    for (int k = 0; k < g.rank; k++) { p->shp.push_back((size_t)g.d[k].n); p->sin.push_back((ptrdiff_t)g.d[k].is); p->sout.push_back((ptrdiff_t)g.d[k].os); p->axes.push_back(ax0 + k); }
    try {
        double t0 = now();
        std::vector<std::pair<size_t, bool>> seen;
        for (int k = 0; k < g.rank; k++) {
            std::pair<size_t, bool> key((size_t)g.d[k].n, k == g.rank - 1 && p->kind != 0);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            for (int vec = 0; vec <= (c.howmany > 1 || g.rank > 1); vec++) {
                if (key.second) { detail_fft::pocketfft_r<T> r(key.first, vec); } else { detail_fft::pocketfft_c<T> r(key.first, vec); }
            }
        }
        p->plan_s = now() - t0;
        return p.release();
    } catch (std::exception &e) { why = e.what(); return nullptr; }
}
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return mk<double>(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return mk<float>(c, g, in, out, why); }
