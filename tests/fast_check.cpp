// Fast-mode accuracy check (FFTE-6): every transform planned in fast numerics mode must
// agree with the same transform planned in baseline mode (scalar FFTW 3.3.10) to within
// a small multiple of the precision's roundoff.  Covers the solvers fast mode adds:
// SIMD codelets and the paired real-column solver (odd/even n, odd/even column counts,
// strided, batched, multi-dimensional, r2c and c2r).
//   fast_check [--verbose]     exit status 0 = all within tolerance
#if __has_include("fftchode.h")
#include "fftchode.h"
#else
#include <fftw3.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void fftchode_set_numerics(int mode);

template <class T> struct A;
#define FC_API(T, P)                                                                              \
    template <> struct A<T> {                                                                     \
        typedef P##plan plan; typedef P##iodim64 dim;                                             \
        static T *alloc(size_t n) { return (T *)P##malloc(sizeof(T) * n); }                        \
        static void release(void *p) { P##free(p); }                                              \
        static plan c2c(int r, const dim *d, const dim *v, T *i, T *o, int s) {                   \
            return P##plan_guru64_dft(r, d, 1, v, (P##complex *)i, (P##complex *)o, s, FFTW_ESTIMATE); } \
        static plan r2c(int r, const dim *d, const dim *v, T *i, T *o) {                         \
            return P##plan_guru64_dft_r2c(r, d, 1, v, i, (P##complex *)o, FFTW_ESTIMATE); }       \
        static plan c2r(int r, const dim *d, const dim *v, T *i, T *o) {                         \
            return P##plan_guru64_dft_c2r(r, d, 1, v, (P##complex *)i, o, FFTW_ESTIMATE); }       \
        static void exec(plan p) { P##execute(p); }                                              \
        static void destroy(plan p) { P##destroy_plan(p); }                                       \
        static double eps() { return sizeof(T) == 4 ? 6e-8 : 1.1e-16; }                          \
    };
FC_API(double, fftw_)
FC_API(float, fftwf_)

static bool verbose = false;
static int nfail = 0, ncase = 0;
static unsigned long long rng = 88172645463325252ull;
static double urand() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (double)(rng >> 11) / 9007199254740992.0 - 0.5; }

// kind: 'c' c2c forward, 'b' c2c backward, 'r' r2c, 'h' c2r; dims/vec in element units of each side
template <class T> static void run(const char *label, char kind, std::vector<fftw_iodim64> d, fftw_iodim64 v, long long ilen, long long olen) {
    typedef A<T> X; typedef typename X::dim D;
    T *in = X::alloc(ilen), *src = X::alloc(ilen), *o0 = X::alloc(olen), *o1 = X::alloc(olen);
    for (long long i = 0; i < ilen; i++) src[i] = (T)urand();
    T *outs[2] = {o0, o1};
    long double npts = 1; for (auto &x : d) npts *= x.n;
    for (int mode = 0; mode < 2; mode++) {
        fftchode_set_numerics(mode);
        memset(outs[mode], 0, sizeof(T) * olen);
        typename X::plan p;
        int r = (int)d.size();
        if (kind == 'c' || kind == 'b') p = X::c2c(r, (const D *)d.data(), (const D *)&v, in, outs[mode], kind == 'c' ? FFTW_FORWARD : FFTW_BACKWARD);
        else if (kind == 'r') p = X::r2c(r, (const D *)d.data(), (const D *)&v, in, outs[mode]);
        else p = X::c2r(r, (const D *)d.data(), (const D *)&v, in, outs[mode]);
        if (!p) { printf("FAIL %s: no plan (mode %d)\n", label, mode); nfail++; return; }
        memcpy(in, src, sizeof(T) * ilen);
        X::exec(p);
        X::destroy(p);
    }
    fftchode_set_numerics(0);
    double se = 0, sr = 0;
    for (long long i = 0; i < olen; i++) { double e = (double)o1[i] - (double)o0[i]; se += e * e; sr += (double)o0[i] * o0[i]; }
    double rel = sr > 0 ? std::sqrt(se / sr) : std::sqrt(se);
    double tol = 20 * X::eps() * std::log2((double)npts + 2);
    ncase++;
    bool bad = !(rel <= tol);
    if (bad) nfail++;
    if (bad || verbose) printf("%s %s %c rel %.3g tol %.3g\n", bad ? "FAIL" : "ok", label, kind, rel, tol);
    X::release(in); X::release(src); X::release(o0); X::release(o1);
}

// MATLAB fft(x,[],2)-like: transform along columns of an h x n column-major matrix
template <class T> static void col(char kind, int n, int h) {
    char label[64]; snprintf(label, sizeof label, "%s col n=%d h=%d", sizeof(T) == 4 ? "s" : "d", n, h);
    bool real = kind == 'r' || kind == 'h';
    long long nc = real ? n / 2 + 1 : n;
    std::vector<fftw_iodim64> d(1);
    long long rlen = (long long)n * h, clen = 2 * nc * h;
    if (kind == 'r') { d[0] = {n, h, h}; run<T>(label, kind, d, {h, 1, 1}, rlen, clen); }
    else if (kind == 'h') { d[0] = {n, h, h}; run<T>(label, kind, d, {h, 1, 1}, clen, rlen); }
    else { d[0] = {n, h, h}; run<T>(label, kind, d, {h, 1, 1}, 2LL * n * h, 2LL * n * h); }
}
// h contiguous transforms of size n (MATLAB fft(x) of an n x h matrix)
template <class T> static void batch(char kind, int n, int h) {
    char label[64]; snprintf(label, sizeof label, "%s batch n=%d h=%d", sizeof(T) == 4 ? "s" : "d", n, h);
    long long nc = n / 2 + 1;
    std::vector<fftw_iodim64> d(1);
    if (kind == 'r') { d[0] = {n, 1, 1}; run<T>(label, kind, d, {h, n, nc}, (long long)n * h, 2 * nc * h); }
    else if (kind == 'h') { d[0] = {n, 1, 1}; run<T>(label, kind, d, {h, nc, n}, 2 * nc * h, (long long)n * h); }
    else { d[0] = {n, 1, 1}; run<T>(label, kind, d, {h, n, n}, 2LL * n * h, 2LL * n * h); }
}
template <class T> static void two(char kind, int n0, int n1) {
    char label[64]; snprintf(label, sizeof label, "%s 2d %dx%d", sizeof(T) == 4 ? "s" : "d", n0, n1);
    long long c1 = (kind == 'r' || kind == 'h') ? n1 / 2 + 1 : n1;
    std::vector<fftw_iodim64> d(2);
    if (kind == 'r') { d[0] = {n0, n1, c1}; d[1] = {n1, 1, 1}; run<T>(label, kind, d, {1, 0, 0}, (long long)n0 * n1, 2 * c1 * n0); }
    else if (kind == 'h') { d[0] = {n0, c1, n1}; d[1] = {n1, 1, 1}; run<T>(label, kind, d, {1, 0, 0}, 2 * c1 * n0, (long long)n0 * n1); }
    else { d[0] = {n0, n1, n1}; d[1] = {n1, 1, 1}; run<T>(label, kind, d, {1, 0, 0}, 2LL * n0 * n1, 2LL * n0 * n1); }
}

template <class T> static void battery() {
    const int ns[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 20, 25, 30, 32, 60, 64, 100, 127, 128, 243, 256, 500, 512, 1000, 1024, 4096};
    const int hs[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 24, 31, 32, 33, 48, 64, 100, 129};
    for (char k : {'r', 'h', 'c', 'b'})
        for (int n : ns) {
            for (int h : hs) if ((long long)n * h <= (1 << 20)) col<T>(k, n, h);
            for (int h : {1, 2, 3, 8, 17}) batch<T>(k, n, h);
        }
    for (char k : {'r', 'h', 'c'})
        for (auto s : std::vector<std::pair<int, int>>{{2, 2}, {3, 5}, {8, 8}, {16, 31}, {64, 64}, {100, 37}, {128, 256}, {480, 640}})
            two<T>(k, s.first, s.second);
}

int main(int argc, char **argv) {
    verbose = argc > 1 && !strcmp(argv[1], "--verbose");
    battery<double>();
    battery<float>();
    printf("fast_check: %d cases, %d outside tolerance\n", ncase, nfail);
    return nfail != 0;
}
