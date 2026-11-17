// Frozen-suite FFT benchmark (FFTE-6). Reads a case list and, for each case, plans with
// FFTW_ESTIMATE through the FFTW3 API and measures plan time and execution time separately.
// Links against any FFTW3-API object: this repo's fft.o (any revision) or upstream libfftw3[f].
//
//   fftsuite --suite FILE --threads N [--mode time|mem|hash|err] [--batches K] [--tmin S] [--only NAME]
//            [--plan estimate|measure|patient]   (default estimate; MATLAB's mode)
//
// Case line:  name prec kind shape howmany layout place
//   prec  d|s        kind  cf (c2c fwd) cb (c2c bwd) rf (r2c) rb (c2r)
//   shape 1024 or 64x64x64 (row-major)   howmany  batch count
//   layout contig (batch dist = one array) | col (rank 1 only: transform along dim 2 of a
//          howmany x n column-major matrix, stride howmany, dist 1, as MATLAB fft(x,[],2))
//   place oop|ip     (ip only for c2c)
// time: name threads plan_s iters t_1..t_K   (seconds per transform)
// mem : name threads heap_live_after_plan heap_peak_plan_exec (bytes above the I/O buffers)
// hash: name threads fnv64(output)   err (needs -DFFTSUITE_REF and libfftw3l): name rel_rms rel_max
#include "fftw3.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#if defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

static unsigned g_flags = FFTW_ESTIMATE;
extern "C" { int mt_enabled(void); long mt_live(void); long mt_peak(void); void mt_reset_peak(void); }

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
static uint64_t fnv(const void *p, size_t n) {
    const unsigned char *c = (const unsigned char *)p; uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
    return h;
}
struct Minstd {  // values in [-0.5,0.5), exactly representable in float
    uint64_t s; explicit Minstd(uint64_t seed) : s(1 + (seed * 7919) % 2147483646ull) {}
    double next() { s = (48271 * s) % 2147483647ull; return (double)(s >> 7) / 16777216.0 - 0.5; }
};

struct Case {
    std::string name, kind, layout, place; char prec; std::vector<int> n; int howmany;
};

template <typename T> struct Api;
#define FFTSUITE_API(T, P, TAG)                                                                       \
template <> struct Api<T> {                                                                          \
    typedef P##plan plan; typedef P##iodim64 dim;                                                    \
    static void *alloc(size_t n) { return P##malloc(n); }                                            \
    static void release(void *p) { P##free(p); }                                                     \
    static plan dft(int r, const dim *d, int hr, const dim *hd, T *in, T *out, int sign) {           \
        return P##plan_guru64_dft(r, d, hr, hd, (P##complex *)in, (P##complex *)out, sign, g_flags); } \
    static plan r2c(int r, const dim *d, int hr, const dim *hd, T *in, T *out) {                     \
        return P##plan_guru64_dft_r2c(r, d, hr, hd, in, (P##complex *)out, g_flags); }          \
    static plan c2r(int r, const dim *d, int hr, const dim *hd, T *in, T *out) {                     \
        return P##plan_guru64_dft_c2r(r, d, hr, hd, (P##complex *)in, out, g_flags); }          \
    static void exec(plan p) { P##execute(p); }                                                      \
    static void destroy(plan p) { P##destroy_plan(p); }                                              \
    static void threads(int n) { P##plan_with_nthreads(n); }                                         \
    static void cleanup() { P##cleanup(); }                                                          \
};
FFTSUITE_API(double, fftw_, d)
FFTSUITE_API(float, fftwf_, s)
#ifdef FFTSUITE_REF
FFTSUITE_API(long double, fftwl_, l)
#endif

// Problem geometry, in real (T) units.
struct Geo {
    int rank; std::vector<fftw_iodim64> d; fftw_iodim64 v; long long in_len, out_len;
};
static Geo geometry(const Case &c) {
    Geo g; g.rank = (int)c.n.size(); g.d.resize(g.rank);
    bool real = c.kind == "rf" || c.kind == "rb";
    int last = c.n.back(), lastc = real ? last / 2 + 1 : last;
    if (c.layout == "col") {  // rank 1, stride howmany, dist 1 (complex units)
        long long h = c.howmany;
        g.d[0] = {c.n[0], (ptrdiff_t)h, (ptrdiff_t)h};
        g.v = {h, 1, 1};
        long long nr = (long long)c.n[0] * h, nc = (long long)lastc * h;
        if (c.kind == "rf") { g.in_len = nr; g.out_len = 2 * nc; }
        else if (c.kind == "rb") { g.in_len = 2 * nc; g.out_len = nr; }
        else { g.in_len = g.out_len = 2 * nr; }
        return g;
    }
    // contiguous row-major; strides in element units of each side
    long long sr = 1, sc = 1;  // real-side and complex-side strides
    for (int k = g.rank - 1; k >= 0; k--) {
        int nr = c.n[k], nc = (k == g.rank - 1) ? lastc : c.n[k];
        if (c.kind == "rf") g.d[k] = {c.n[k], (ptrdiff_t)sr, (ptrdiff_t)sc};
        else if (c.kind == "rb") g.d[k] = {c.n[k], (ptrdiff_t)sc, (ptrdiff_t)sr};
        else g.d[k] = {c.n[k], (ptrdiff_t)sc, (ptrdiff_t)sc};
        sr *= nr; sc *= nc;
    }
    if (c.kind == "rf") { g.v = {c.howmany, (ptrdiff_t)sr, (ptrdiff_t)sc}; g.in_len = sr * c.howmany; g.out_len = 2 * sc * c.howmany; }
    else if (c.kind == "rb") { g.v = {c.howmany, (ptrdiff_t)sc, (ptrdiff_t)sr}; g.in_len = 2 * sc * c.howmany; g.out_len = sr * c.howmany; }
    else { g.v = {c.howmany, (ptrdiff_t)sc, (ptrdiff_t)sc}; g.in_len = g.out_len = 2 * sc * c.howmany; }
    return g;
}

template <typename T> struct Prob {
    typename Api<T>::plan p = nullptr; T *in = nullptr, *out = nullptr; Geo g; double plan_s = 0; bool ip = false;
    std::vector<T> backup;
};

template <typename T> static void fill(T *x, long long len, const Case &c) {
    Minstd r(fnv(c.name.data(), c.name.size()) & 0xffffffff);
    for (long long i = 0; i < len; i++) x[i] = (T)r.next();
}

// Hermitian-consistent input is not required for timing; for c2r the err/hash input is the
// r2c of a real signal so that the result is meaningful.
template <typename T> static bool make(const Case &c, Prob<T> &P, bool fill_input) {
    P.g = geometry(c); P.ip = c.place == "ip";
    if (P.ip && (c.kind == "rf" || c.kind == "rb")) { fprintf(stderr, "%s: ip real not supported\n", c.name.c_str()); return false; }
    long long ilen = P.g.in_len, olen = P.g.out_len;
    P.in = (T *)Api<T>::alloc(sizeof(T) * std::max(ilen, P.ip ? olen : ilen));
    P.out = P.ip ? P.in : (T *)Api<T>::alloc(sizeof(T) * olen);
    memset(P.in, 0, sizeof(T) * ilen); if (!P.ip) memset(P.out, 0, sizeof(T) * olen);
    const fftw_iodim64 *d = P.g.d.data(); int hr = 1;
    typedef typename Api<T>::dim D;
    double t0 = now();
    if (c.kind == "cf" || c.kind == "cb") P.p = Api<T>::dft(P.g.rank, (const D *)d, hr, (const D *)&P.g.v, P.in, P.out, c.kind == "cf" ? FFTW_FORWARD : FFTW_BACKWARD);
    else if (c.kind == "rf") P.p = Api<T>::r2c(P.g.rank, (const D *)d, hr, (const D *)&P.g.v, P.in, P.out);
    else P.p = Api<T>::c2r(P.g.rank, (const D *)d, hr, (const D *)&P.g.v, P.in, P.out);
    P.plan_s = now() - t0;
    if (!P.p) { fprintf(stderr, "%s: no plan\n", c.name.c_str()); return false; }
    if (fill_input) {
        if (c.kind == "rb") {  // input = r2c(real signal): build it with an r2c plan of the same problem
            std::vector<T> tmpin(P.g.out_len);
            fill(tmpin.data(), (long long)tmpin.size(), c);
            fftw_iodim64 dd[8]; for (int k = 0; k < P.g.rank; k++) dd[k] = {P.g.d[k].n, P.g.d[k].os, P.g.d[k].is};
            fftw_iodim64 vv = {P.g.v.n, P.g.v.os, P.g.v.is};
            T *rin = (T *)Api<T>::alloc(sizeof(T) * P.g.out_len);
            auto q = Api<T>::r2c(P.g.rank, (const D *)dd, hr, (const D *)&vv, rin, P.in);
            memcpy(rin, tmpin.data(), sizeof(T) * P.g.out_len);
            Api<T>::exec(q); Api<T>::destroy(q); Api<T>::release(rin);
        } else fill(P.in, ilen, c);
        P.backup.assign(P.in, P.in + ilen);
    }
    return true;
}
template <typename T> static void restore(Prob<T> &P) { if (!P.backup.empty()) memcpy(P.in, P.backup.data(), sizeof(T) * P.backup.size()); }
template <typename T> static void destroy(Prob<T> &P) {
    Api<T>::destroy(P.p); if (!P.ip) Api<T>::release(P.out); Api<T>::release(P.in);
}

static bool parse(const std::string &line, Case &c) {
    char name[128], prec[8], kind[8], shape[128], layout[16], place[8]; int h;
    if (line.empty() || line[0] == '#') return false;
    if (sscanf(line.c_str(), "%127s %7s %7s %127s %d %15s %7s", name, prec, kind, shape, &h, layout, place) != 7) return false;
    c.name = name; c.prec = prec[0]; c.kind = kind; c.layout = layout; c.place = place; c.howmany = h; c.n.clear();
    for (char *s = strtok(shape, "x"); s; s = strtok(nullptr, "x")) c.n.push_back(atoi(s));
    return true;
}

static int g_batches = 3, g_threads = 1; static double g_tmin = 0.02;

template <typename T> static void run_time(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { printf("%s %d FAIL\n", c.name.c_str(), g_threads); return; }
    Api<T>::exec(P.p); restore(P);
    long iters = 1;
    for (;;) { double t0 = now(); for (long i = 0; i < iters; i++) Api<T>::exec(P.p); double t = now() - t0;
               if (t >= g_tmin || iters >= (1L << 30)) break; iters *= 2; }
    printf("%s %d %.9g %ld", c.name.c_str(), g_threads, P.plan_s, iters);
    for (int b = 0; b < g_batches; b++) {
        restore(P);
        double t0 = now(); for (long i = 0; i < iters; i++) Api<T>::exec(P.p); double t = now() - t0;
        printf(" %.9g", t / iters);
    }
    printf("\n"); fflush(stdout);
    destroy(P);
}
template <typename T> static void run_mem(const Case &c) {
    Geo g = geometry(c);
    long long ilen = g.in_len, olen = g.out_len; bool ip = c.place == "ip";
    // I/O buffers are allocated inside make(); account them out using their known sizes
    long before = mt_live(); mt_reset_peak();
    Prob<T> P; if (!make<T>(c, P, false)) { printf("%s %d FAIL\n", c.name.c_str(), g_threads); return; }
    long io = (long)(sizeof(T) * (std::max(ilen, ip ? olen : ilen) + (ip ? 0 : olen)));
    long live_plan = mt_live() - before - io;
    Api<T>::exec(P.p);
    long peak = mt_peak() - before - io;
    printf("%s %d %ld %ld\n", c.name.c_str(), g_threads, live_plan, peak); fflush(stdout);
    destroy(P); Api<T>::cleanup();
}
template <typename T> static void run_hash(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { printf("%s %d FAIL\n", c.name.c_str(), g_threads); return; }
    Api<T>::exec(P.p);
    printf("%s %d %016llx\n", c.name.c_str(), g_threads, (unsigned long long)fnv(P.out, sizeof(T) * P.g.out_len)); fflush(stdout);
    destroy(P);
}
#ifdef FFTSUITE_REF
template <typename T> static void run_err(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { printf("%s FAIL\n", c.name.c_str()); return; }
    Prob<long double> R; if (!make<long double>(c, R, false)) { printf("%s FAIL\n", c.name.c_str()); return; }
    for (long long i = 0; i < P.g.in_len; i++) R.in[i] = (long double)P.in[i];  // identical (rounded-to-T) input
    Api<T>::exec(P.p); Api<long double>::exec(R.p);
    long double se = 0, sr = 0, me = 0, mr = 0;
    for (long long i = 0; i < P.g.out_len; i++) {
        long double e = (long double)P.out[i] - R.out[i]; se += e * e; sr += R.out[i] * R.out[i];
        me = std::max(me, fabsl(e)); mr = std::max(mr, fabsl(R.out[i]));
    }
    printf("%s %.4Le %.4Le\n", c.name.c_str(), sqrtl(se / (sr > 0 ? sr : 1)), me / (mr > 0 ? mr : 1)); fflush(stdout);
    destroy(P); destroy(R);
}
#endif

int main(int argc, char **argv) {
    const char *suite = nullptr, *mode = "time", *only = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--suite")) suite = argv[++i];
        else if (!strcmp(argv[i], "--threads")) g_threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode")) mode = argv[++i];
        else if (!strcmp(argv[i], "--batches")) g_batches = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tmin")) g_tmin = atof(argv[++i]);
        else if (!strcmp(argv[i], "--only")) only = argv[++i];
        else if (!strcmp(argv[i], "--plan")) { const char *m = argv[++i];
            g_flags = !strcmp(m, "measure") ? FFTW_MEASURE : !strcmp(m, "patient") ? FFTW_PATIENT : FFTW_ESTIMATE; }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (!suite) { fprintf(stderr, "usage: fftsuite --suite FILE [--threads N] [--mode time|mem|hash|err]\n"); return 2; }
#if defined(__x86_64__)
    if (!strcmp(mode, "time")) { _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); }
#endif
    fftw_init_threads(); fftwf_init_threads();
    fftw_plan_with_nthreads(g_threads); fftwf_plan_with_nthreads(g_threads);
#ifdef FFTSUITE_REF
    fftwl_init_threads(); fftwl_plan_with_nthreads(1);
#endif
    FILE *f = fopen(suite, "r"); if (!f) { perror(suite); return 2; }
    char buf[512];
    while (fgets(buf, sizeof buf, f)) {
        std::string line(buf); while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        Case c; if (!parse(line, c)) continue;
        if (only && c.name != only) continue;
        bool d = c.prec == 'd';
        if (!strcmp(mode, "time")) d ? run_time<double>(c) : run_time<float>(c);
        else if (!strcmp(mode, "mem")) d ? run_mem<double>(c) : run_mem<float>(c);
        else if (!strcmp(mode, "hash")) d ? run_hash<double>(c) : run_hash<float>(c);
#ifdef FFTSUITE_REF
        else if (!strcmp(mode, "err")) d ? run_err<double>(c) : run_err<float>(c);
#endif
        else { fprintf(stderr, "mode %s not available\n", mode); return 2; }
    }
    return 0;
}
