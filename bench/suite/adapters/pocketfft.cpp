// pocketfft (C++ header-only, branch cpp) adapter.  Stateless API pocketfft::c2c / r2c / c2r with the library's plan cache
// (POCKETFFT_CACHE_SIZE, set on the compile line: without it every call would re-plan).  Strides are in BYTES in this API.
// Batches (howmany) become a leading array axis excluded from "axes", so pocketfft can vectorize across transforms.
// Plan time = construction time of the 1-D plan objects (pocketfft::detail::pocketfft_c/_r, one per distinct axis length) that the
// stateless calls build on a cache miss; the plan cache is warmed by the harness's first (untimed) execution.  Threads: the nthreads argument (std::thread per call).
#include "harness.h"
#include "pocketfft_hdronly.h"

static size_t g_nt = 1;
const char *adapter_name() { return "pocketfft"; }
void adapter_init(int threads) { g_nt = (size_t)threads; }
void adapter_cleanup() {}

template <typename T> struct PfPlan : Plan {
    pocketfft::shape_t shp, axes; pocketfft::stride_t sin, sout; const T *in; T *out; bool fwd; int kind;  // 0 c2c, 1 r2c, 2 c2r
    void exec() override {
        typedef std::complex<T> C;
        if (kind == 0) pocketfft::c2c(shp, sin, sout, axes, fwd, (const C *)in, (C *)out, (T)1, g_nt);
        else if (kind == 1) pocketfft::r2c(shp, sin, sout, axes, true, in, (C *)out, (T)1, g_nt);
        else pocketfft::c2r(shp, sin, sout, axes, false, (const C *)in, out, (T)1, g_nt);
    }
};
template <typename T> static Plan *mk(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    std::unique_ptr<PfPlan<T>> p(new PfPlan<T>());
    p->kind = c.kind == "rf" ? 1 : c.kind == "rb" ? 2 : 0; p->fwd = c.kind == "cf"; p->in = in; p->out = out;
    size_t si = (c.kind == "rf") ? sizeof(T) : 2 * sizeof(T), so = (c.kind == "rb") ? sizeof(T) : 2 * sizeof(T);
    size_t ax0 = 0;
    if (c.howmany > 1) { p->shp.push_back((size_t)g.v.n); p->sin.push_back((ptrdiff_t)(g.v.is * si)); p->sout.push_back((ptrdiff_t)(g.v.os * so)); ax0 = 1; }
    for (int k = 0; k < g.rank; k++) { p->shp.push_back((size_t)g.d[k].n); p->sin.push_back((ptrdiff_t)(g.d[k].is * si)); p->sout.push_back((ptrdiff_t)(g.d[k].os * so)); p->axes.push_back(ax0 + k); }
    try {
        double t0 = now();
        std::vector<std::pair<size_t, bool>> seen;
        for (int k = 0; k < g.rank; k++) {
            std::pair<size_t, bool> key((size_t)g.d[k].n, k == g.rank - 1 && p->kind != 0);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            if (key.second) { pocketfft::detail::pocketfft_r<T> r(key.first); } else { pocketfft::detail::pocketfft_c<T> r(key.first); }
        }
        p->plan_s = now() - t0;
        return p.release();
    } catch (std::exception &e) { why = e.what(); return nullptr; }
}
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return mk<double>(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return mk<float>(c, g, in, out, why); }
