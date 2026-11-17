// FFTW-API adapter on top of harness.h.  Only used to prove that harness.h reproduces fftsuite.cpp: built against the
// same FFTW-API library, --mode hash must print the same fnv64 lines as fftsuite (see bench/suite/comparators.sh verify step).
#include "harness.h"
#include <fftw3.h>

const char *adapter_name() { return "fftw-adapter"; }
void adapter_init(int threads) { fftw_init_threads(); fftwf_init_threads(); fftw_plan_with_nthreads(threads); fftwf_plan_with_nthreads(threads); }
void adapter_cleanup() {}

template <typename T> struct Api;
#define ADAPTER_API(T, P)                                                                                         \
template <> struct Api<T> {                                                                                       \
    typedef P##plan plan;                                                                                         \
    static plan make(const Case &c, const Geo &g, T *in, T *out) {                                                \
        std::vector<P##iodim64> d(g.rank); for (int k = 0; k < g.rank; k++) d[k] = {(ptrdiff_t)g.d[k].n, (ptrdiff_t)g.d[k].is, (ptrdiff_t)g.d[k].os}; \
        P##iodim64 v = {(ptrdiff_t)g.v.n, (ptrdiff_t)g.v.is, (ptrdiff_t)g.v.os};                                \
        if (c.kind == "cf" || c.kind == "cb") return P##plan_guru64_dft(g.rank, d.data(), 1, &v, (P##complex *)in, (P##complex *)out, c.kind == "cf" ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_ESTIMATE); \
        if (c.kind == "rf") return P##plan_guru64_dft_r2c(g.rank, d.data(), 1, &v, in, (P##complex *)out, FFTW_ESTIMATE); \
        return P##plan_guru64_dft_c2r(g.rank, d.data(), 1, &v, (P##complex *)in, out, FFTW_ESTIMATE);              \
    }                                                                                                             \
    static void exec(plan p) { P##execute(p); }                                                                   \
    static void destroy(plan p) { P##destroy_plan(p); }                                                           \
};
ADAPTER_API(double, fftw_)
ADAPTER_API(float, fftwf_)
template <typename T> struct FPlan : Plan {
    typename Api<T>::plan p;
    ~FPlan() { Api<T>::destroy(p); }
    void exec() override { Api<T>::exec(p); }
};
template <typename T> static Plan *mk(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    auto p = Api<T>::make(c, g, in, out);
    if (!p) { why = "no plan"; return nullptr; }
    FPlan<T> *r = new FPlan<T>(); r->p = p; return r;
}
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return mk<double>(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return mk<float>(c, g, in, out, why); }
