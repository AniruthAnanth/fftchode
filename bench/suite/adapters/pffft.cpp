// PFFFT (marton78/pffft) adapter.  1-D, contiguous only; lengths N = 2^a 3^b 5^c that are multiples of 16 (complex) / 32 (real) with SIMD
// (pffft_is_valid_size); anything else prints FAIL.  Batches are a loop of single transforms.  SIMD used = what the compiler flags enable
// (x86: SSE for float, SSE2 or, with -mavx, AVX for double; no AVX-512 backend) - FFTSUITE_INFO=1 prints pffft_simd_arch().
// Complex: pffft_transform_ordered (interleaved).  Real: pffft packs the Nyquist term into Im(X_0); the adapter converts to/from the
// FFTW CCE layout with O(1) element moves per transform (out[n] = out[1], out[1] = 0 ...; the other N-2 values already coincide), counted in the time.
// Transforms are unnormalized, FORWARD = exp(-i...) (verified by --mode err).  c2r overwrites in[1] (= Im X_0, zero for a real signal) with Re X_{N/2}.
#include "harness.h"
extern "C" {
#include "pffft/pffft.h"
#include "pffft/pffft_double.h"
}

const char *adapter_name() { return "pffft"; }
void adapter_init(int) {
    if (getenv("FFTSUITE_INFO")) fprintf(stderr, "pffft float: %s (simd size %d); double: %s (simd size %d)\n", pffft_simd_arch(), pffft_simd_size(), pffftd_simd_arch(), pffftd_simd_size());
}
void adapter_cleanup() {}

template <typename T> struct Api;
template <> struct Api<float> {
    typedef PFFFT_Setup Setup;
    static Setup *setup(int n, pffft_transform_t t) { return pffft_new_setup(n, t); }
    static void destroy(Setup *s) { pffft_destroy_setup(s); }
    static int valid(int n, pffft_transform_t t) { return pffft_is_valid_size(n, t); }
    static void tr(const Setup *s, const float *i, float *o, float *w, pffft_direction_t d) { pffft_transform_ordered(s, i, o, w, d); }
    static void *amalloc(size_t b) { return pffft_aligned_malloc(b); }
    static void afree(void *p) { pffft_aligned_free(p); }
    static const int align = 16;
};
template <> struct Api<double> {
    typedef PFFFTD_Setup Setup;
    static Setup *setup(int n, pffft_transform_t t) { return pffftd_new_setup(n, t); }
    static void destroy(Setup *s) { pffftd_destroy_setup(s); }
    static int valid(int n, pffft_transform_t t) { return pffftd_is_valid_size(n, t); }
    static void tr(const Setup *s, const double *i, double *o, double *w, pffft_direction_t d) { pffftd_transform_ordered(s, i, o, w, d); }
    static void *amalloc(size_t b) { return pffftd_aligned_malloc(b); }
    static void afree(void *p) { pffftd_aligned_free(p); }
    static const int align = 32;  // v4sf is 4 doubles wide even with SSE2 (two 128-bit halves); VALIGNED asserts 32-byte alignment
};

template <typename T> struct PffftPlan : Plan {
    typename Api<T>::Setup *s = nullptr; T *work = nullptr, *in, *out; int n; long long batch, di, dout; int kind;  // 0 c2c fwd, 1 c2c bwd, 2 r2c, 3 c2r
    ~PffftPlan() { if (s) Api<T>::destroy(s); if (work) Api<T>::afree(work); }
    void exec() override {
        for (long long b = 0; b < batch; b++) {
            T *i = in + b * di, *o = out + b * dout;
            switch (kind) {
            case 0: Api<T>::tr(s, i, o, work, PFFFT_FORWARD); break;
            case 1: Api<T>::tr(s, i, o, work, PFFFT_BACKWARD); break;
            case 2: Api<T>::tr(s, i, o, work, PFFFT_FORWARD); o[n] = o[1]; o[n + 1] = 0; o[1] = 0; break;
            case 3: i[1] = i[n]; Api<T>::tr(s, i, o, work, PFFFT_BACKWARD); break;
            }
        }
    }
};
template <typename T> static Plan *mk(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    bool real = c.kind == "rf" || c.kind == "rb";
    if (g.rank != 1) { why = "pffft is 1-D only"; return nullptr; }
    int n = (int)g.d[0].n;
    if (g.d[0].is != 1 || g.d[0].os != 1) { why = "pffft needs contiguous (unit-stride) data"; return nullptr; }
    if (real && in == out) { why = "in-place real not supported"; return nullptr; }
    pffft_transform_t tt = real ? PFFFT_REAL : PFFFT_COMPLEX;
    if (!Api<T>::valid(n, tt)) { why = std::string("pffft size not supported (need 2^a3^b5^c, multiple of ") + (real ? "32" : "16") + ")"; return nullptr; }
    std::unique_ptr<PffftPlan<T>> p(new PffftPlan<T>());
    p->n = n; p->batch = g.v.n; p->di = g.v.is * (c.kind == "rf" ? 1 : 2); p->dout = g.v.os * (c.kind == "rb" ? 1 : 2);
    p->in = in; p->out = out;
    p->kind = c.kind == "cf" ? 0 : c.kind == "cb" ? 1 : c.kind == "rf" ? 2 : 3;
    if (p->batch > 1 && (((p->di * (long long)sizeof(T)) % Api<T>::align) || ((p->dout * (long long)sizeof(T)) % Api<T>::align))) { why = "batch slices not aligned to the SIMD vector size (16 B float, 32 B double)"; return nullptr; }
    p->s = Api<T>::setup(n, tt);
    if (!p->s) { why = "pffft_new_setup failed"; return nullptr; }
    p->work = (T *)Api<T>::amalloc(sizeof(T) * (real ? n : 2 * n));
    return p.release();
}
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return mk<double>(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return mk<float>(c, g, in, out, why); }
