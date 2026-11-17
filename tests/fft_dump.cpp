// Deterministic battery of FFTW_ESTIMATE transforms. Prints one line per case:
//   <case> <fnv64 of output bytes> <fnv64 of exported wisdom>
// Linked once against src/fftchode.cpp and once against upstream libfftw3/libfftw3f;
// the two transcripts must be identical.
#if __has_include("fftchode.h")
#include "fftchode.h"
#else
#include <fftw3.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static uint64_t fnv(const void *p, size_t n, uint64_t h = 1469598103934665603ull) {
    const unsigned char *c = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
    return h;
}

// MINSTD draws in [-0.5, 0.5), exactly representable in float and double
struct Minstd {
    uint64_t s;
    explicit Minstd(uint64_t seed) : s(1 + (seed * 7919) % 2147483646ull) {}
    double next() { s = (48271 * s) % 2147483647ull; return (double)(s >> 7) / 16777216.0 - 0.5; }
};

template <typename T> struct Api;
template <> struct Api<double> {
    typedef fftw_plan plan; typedef fftw_complex cpx; typedef fftw_iodim64 dim;
    static void *alloc(size_t n) { return fftw_malloc(n); }
    static void release(void *p) { fftw_free(p); }
    static plan many_dft(int r, const int *n, int h, cpx *in, int is, int id, cpx *out, int os, int od, int sign) {
        return fftw_plan_many_dft(r, n, h, in, nullptr, is, id, out, nullptr, os, od, sign, FFTW_ESTIMATE);
    }
    static plan many_r2c(int r, const int *n, int h, double *in, int is, int id, cpx *out, int os, int od) {
        return fftw_plan_many_dft_r2c(r, n, h, in, nullptr, is, id, out, nullptr, os, od, FFTW_ESTIMATE);
    }
    static plan many_c2r(int r, const int *n, int h, cpx *in, int is, int id, double *out, int os, int od) {
        return fftw_plan_many_dft_c2r(r, n, h, in, nullptr, is, id, out, nullptr, os, od, FFTW_ESTIMATE);
    }
    static plan many_r2r(int r, const int *n, int h, double *in, double *out, const fftw_r2r_kind *k) {
        return fftw_plan_many_r2r(r, n, h, in, nullptr, 1, 0, out, nullptr, 1, 0, k, FFTW_ESTIMATE);
    }
    static plan guru_dft(int r, const dim *d, int hr, const dim *hd, cpx *in, cpx *out, int sign) {
        return fftw_plan_guru64_dft(r, d, hr, hd, in, out, sign, FFTW_ESTIMATE);
    }
    static void exec(plan p) { fftw_execute(p); }
    static void destroy(plan p) { fftw_destroy_plan(p); }
    static uint64_t wisdom() { char *w = fftw_export_wisdom_to_string(); uint64_t h = fnv(w, strlen(w)); free(w); fftw_forget_wisdom(); return h; }
    static void threads(int n) {
#ifndef FFT_DUMP_NO_THREADS
        fftw_plan_with_nthreads(n);
#else
        (void)n;
#endif
    }
    static const char *tag() { return "d"; }
};
template <> struct Api<float> {
    typedef fftwf_plan plan; typedef fftwf_complex cpx; typedef fftwf_iodim64 dim;
    static void *alloc(size_t n) { return fftwf_malloc(n); }
    static void release(void *p) { fftwf_free(p); }
    static plan many_dft(int r, const int *n, int h, cpx *in, int is, int id, cpx *out, int os, int od, int sign) {
        return fftwf_plan_many_dft(r, n, h, in, nullptr, is, id, out, nullptr, os, od, sign, FFTW_ESTIMATE);
    }
    static plan many_r2c(int r, const int *n, int h, float *in, int is, int id, cpx *out, int os, int od) {
        return fftwf_plan_many_dft_r2c(r, n, h, in, nullptr, is, id, out, nullptr, os, od, FFTW_ESTIMATE);
    }
    static plan many_c2r(int r, const int *n, int h, cpx *in, int is, int id, float *out, int os, int od) {
        return fftwf_plan_many_dft_c2r(r, n, h, in, nullptr, is, id, out, nullptr, os, od, FFTW_ESTIMATE);
    }
    static plan many_r2r(int r, const int *n, int h, float *in, float *out, const fftwf_r2r_kind *k) {
        return fftwf_plan_many_r2r(r, n, h, in, nullptr, 1, 0, out, nullptr, 1, 0, k, FFTW_ESTIMATE);
    }
    static plan guru_dft(int r, const dim *d, int hr, const dim *hd, cpx *in, cpx *out, int sign) {
        return fftwf_plan_guru64_dft(r, d, hr, hd, in, out, sign, FFTW_ESTIMATE);
    }
    static void exec(plan p) { fftwf_execute(p); }
    static void destroy(plan p) { fftwf_destroy_plan(p); }
    static uint64_t wisdom() { char *w = fftwf_export_wisdom_to_string(); uint64_t h = fnv(w, strlen(w)); free(w); fftwf_forget_wisdom(); return h; }
    static void threads(int n) {
#ifndef FFT_DUMP_NO_THREADS
        fftwf_plan_with_nthreads(n);
#else
        (void)n;
#endif
    }
    static const char *tag() { return "s"; }
};

static long long prod(const std::vector<int> &n) { long long p = 1; for (int v : n) p *= v; return p; }
static std::string dims(const std::vector<int> &n) {
    std::string s; for (size_t i = 0; i < n.size(); i++) { if (i) s += "x"; s += std::to_string(n[i]); } return s;
}

template <typename T> static void fill(T *p, size_t count, uint64_t seed) {
    Minstd g(seed); for (size_t i = 0; i < count; i++) p[i] = (T)g.next();
}

template <typename T> static void report(const std::string &name, int nt, const void *out, size_t bytes, uint64_t w) {
    printf("%s %s nt%d %016llx %016llx\n", Api<T>::tag(), name.c_str(), nt, (unsigned long long)fnv(out, bytes), (unsigned long long)w);
    fflush(stdout);
}

// complex <-> complex; howmany transforms, either contiguous (dist = N) or interleaved (stride = howmany)
template <typename T> static void c2c(const std::vector<int> &n, int howmany, bool interleaved, bool inplace, int sign, int nt) {
    typedef Api<T> A; long long N = prod(n), tot = N * howmany;
    typename A::cpx *in = (typename A::cpx *)A::alloc(sizeof(typename A::cpx) * tot);
    typename A::cpx *out = inplace ? in : (typename A::cpx *)A::alloc(sizeof(typename A::cpx) * tot);
    int stride = interleaved ? howmany : 1, dist = interleaved ? 1 : (int)N;
    A::threads(nt);
    typename A::plan p = A::many_dft((int)n.size(), n.data(), howmany, in, stride, dist, out, stride, dist, sign);
    fill((T *)in, 2 * tot, N * 31 + howmany);
    A::exec(p);
    std::string name = std::string("c2c") + (sign < 0 ? "f " : "b ") + dims(n) + " h" + std::to_string(howmany) +
                       (interleaved ? " il" : " ct") + (inplace ? " ip" : " op");
    report<T>(name, nt, out, sizeof(typename A::cpx) * tot, A::wisdom());
    A::destroy(p); A::threads(1);
    if (!inplace) A::release(out);
    A::release(in);
}

// real -> complex forward, then complex -> real backward on the same layout (out-of-place)
template <typename T> static void r2c(const std::vector<int> &n, int howmany, bool interleaved, int nt) {
    typedef Api<T> A; long long N = prod(n), Nc = N / n.back() * (n.back() / 2 + 1);
    T *rin = (T *)A::alloc(sizeof(T) * N * howmany);
    T *rout = (T *)A::alloc(sizeof(T) * N * howmany);
    typename A::cpx *c = (typename A::cpx *)A::alloc(sizeof(typename A::cpx) * Nc * howmany);
    int stride = interleaved ? howmany : 1;
    int rd = interleaved ? 1 : (int)N, cd = interleaved ? 1 : (int)Nc;
    A::threads(nt);
    typename A::plan pf = A::many_r2c((int)n.size(), n.data(), howmany, rin, stride, rd, c, stride, cd);
    uint64_t wf = A::wisdom();
    typename A::plan pb = A::many_c2r((int)n.size(), n.data(), howmany, c, stride, cd, rout, stride, rd);
    uint64_t wb = A::wisdom();
    fill(rin, N * howmany, N * 17 + howmany);
    A::exec(pf);
    std::string base = dims(n) + " h" + std::to_string(howmany) + (interleaved ? " il" : " ct");
    report<T>("r2c " + base, nt, c, sizeof(typename A::cpx) * Nc * howmany, wf);
    A::exec(pb);
    report<T>("c2r " + base, nt, rout, sizeof(T) * N * howmany, wb);
    A::destroy(pf); A::destroy(pb); A::threads(1);
    A::release(rin); A::release(rout); A::release(c);
}

template <typename T> static void r2r(int n, fftw_r2r_kind kind) {
    typedef Api<T> A;
    T *in = (T *)A::alloc(sizeof(T) * n), *out = (T *)A::alloc(sizeof(T) * n);
    typename A::plan p = A::many_r2r(1, &n, 1, in, out, (const decltype(FFTW_R2HC) *)&kind);
    if (!p) {  // solver family compiled out (FFT_NO_REODFT)
        printf("%s r2r k%d %d nt1 noplan\n", A::tag(), (int)kind, n);
        A::release(in); A::release(out); return;
    }
    fill(in, n, n * 13 + kind);
    A::exec(p);
    report<T>("r2r k" + std::to_string((int)kind) + " " + std::to_string(n), 1, out, sizeof(T) * n, A::wisdom());
    A::destroy(p); A::release(in); A::release(out);
}

// guru64: transform along dimension d of a column-major nd array (MATLAB-style fft(x,[],d))
template <typename T> static void guru_dim(const std::vector<int> &shape, int d) {
    typedef Api<T> A; long long tot = prod(shape);
    std::vector<typename A::dim> tr(1), vec;
    long long stride = 1;
    for (size_t k = 0; k < shape.size(); k++) {
        typename A::dim dd; dd.n = shape[k]; dd.is = dd.os = stride;
        if ((int)k == d) tr[0] = dd; else vec.push_back(dd);
        stride *= shape[k];
    }
    typename A::cpx *in = (typename A::cpx *)A::alloc(sizeof(typename A::cpx) * tot);
    typename A::cpx *out = (typename A::cpx *)A::alloc(sizeof(typename A::cpx) * tot);
    typename A::plan p = A::guru_dft(1, tr.data(), (int)vec.size(), vec.data(), in, out, FFTW_FORWARD);
    fill((T *)in, 2 * tot, tot + d);
    A::exec(p);
    report<T>("guru " + dims(shape) + " dim" + std::to_string(d), 1, out, sizeof(typename A::cpx) * tot, A::wisdom());
    A::destroy(p); A::release(in); A::release(out);
}

static std::vector<int> sizes(bool quick) {
    std::vector<int> s;
    for (int n = 1; n <= (quick ? 64 : 300); n++) s.push_back(n);
    for (int n = 512; n <= (quick ? (1 << 16) : (1 << 22)); n *= 2) s.push_back(n);
    int extra[] = {317, 331, 509, 521, 1009, 1021, 2039, 4099, 8191, 16381, 65537, 131071, 1000003, // primes
                   384, 640, 768, 1000, 1080, 1536, 1920, 2187, 3125, 3600, 6561, 10000, 15625, 16807, // smooth
                   46656, 78125, 100000, 117649, 177147, 390625, 3 << 20, 1953125,                       // smooth, large
                   202, 771, 1022, 2046, 4036, 7217, 10007 * 2, 29919, 69683, 131074, 262146,             // big prime factors
                   999, 1001, 3003, 9999, 15015, 45045, 255255, 1048575, 1048577};                       // odd
    for (int n : extra) if (!quick || n <= (1 << 16)) s.push_back(n);
    return s;
}

template <typename T> static void battery(bool quick) {
    for (int n : sizes(quick)) {
        c2c<T>({n}, 1, false, false, FFTW_FORWARD, 1);
        c2c<T>({n}, 1, false, false, FFTW_BACKWARD, 1);
        if (n % 7 == 0 || n < 40) c2c<T>({n}, 1, false, true, FFTW_FORWARD, 1);
        r2c<T>({n}, 1, false, 1);
    }
    // batched (MATLAB column batches are contiguous; row transforms are interleaved)
    int shapes[][2] = {{256, 64}, {1000, 37}, {17, 1000}, {1024, 128}, {33, 33}, {4096, 16}, {1, 1024}, {3, 5}, {64, 3000}};
    for (auto &sh : shapes) for (int il = 0; il < 2; il++) {
        c2c<T>({sh[0]}, sh[1], il, false, FFTW_FORWARD, 1);
        c2c<T>({sh[0]}, sh[1], il, true, FFTW_BACKWARD, 1);
        r2c<T>({sh[0]}, sh[1], il, 1);
    }
    // multi-dimensional (row-major n[]; MATLAB's column-major dims reversed)
    std::vector<std::vector<int>> nd = {{64, 64}, {37, 100}, {128, 127}, {512, 512}, {64, 1}, {1, 64}, {640, 480},
                                        {16, 16, 16}, {11, 9, 7}, {2, 8, 32, 32}, {64, 64, 64}, {3, 1, 5},
                                        {6, 5, 4, 3, 2}, {1000, 1000}, {17, 1, 1}};
    for (auto &n : nd) {
        if (quick && prod(n) > 300000) continue;
        c2c<T>(n, 1, false, false, FFTW_FORWARD, 1);
        c2c<T>(n, 1, false, false, FFTW_BACKWARD, 1);
        r2c<T>(n, 1, false, 1);
        c2c<T>(n, 4, false, false, FFTW_FORWARD, 1);
    }
    // guru64 along every dimension of column-major arrays
    std::vector<std::vector<int>> gs = {{16, 8, 12}, {7, 9, 11}, {32, 4, 64}, {2, 3, 4, 5}, {256, 64}, {17, 1000}};
    for (auto &g : gs) for (int d = 0; d < (int)g.size(); d++) guru_dim<T>(g, d);
    // r2r kinds (MATLAB dct/dst use these)
    for (int k = FFTW_R2HC; k <= FFTW_RODFT11; k++)
        for (int n : {1, 2, 3, 4, 5, 8, 15, 16, 17, 31, 64, 100, 127, 1000, 1024, 4096})
            if (!((k == FFTW_REDFT00) && n == 1)) r2r<T>(n, (fftw_r2r_kind)k);
#ifndef FFT_DUMP_NO_THREADS
    // threads
    for (int nt : {2, 3, 4, 8, 10}) {
        for (int n : {4096, 65536, 1 << 19, 1 << 20, 1000003, 3 << 20})
            if (!quick || n <= 65536) { c2c<T>({n}, 1, false, false, FFTW_FORWARD, nt); r2c<T>({n}, 1, false, nt); }
        c2c<T>({1024}, 128, false, false, FFTW_FORWARD, nt);
        c2c<T>({512, 512}, 1, false, false, FFTW_FORWARD, nt);
        r2c<T>({256, 256}, 4, false, nt);
    }
#endif
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "quick");
#ifndef FFT_DUMP_NO_THREADS
    fftw_init_threads();
#ifndef FFT_DUMP_DOUBLE_ONLY
    fftwf_init_threads();
#endif
#endif
    battery<double>(quick);
#ifndef FFT_DUMP_DOUBLE_ONLY
    battery<float>(quick);
#endif
    return 0;
}
