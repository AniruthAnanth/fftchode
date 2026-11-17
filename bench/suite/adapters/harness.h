// Shared harness for the comparator adapters (FFTE-6).  It reproduces bench/suite/fftsuite.cpp exactly (same case
// parser, geometry, Minstd inputs seeded from the case name, restore/loop/timing logic, output lines) but drives a
// library through a tiny adapter interface instead of the FFTW API.  fftsuite.cpp itself is left untouched; adapters/fftw.cpp
// is an FFTW-API adapter used to prove (--mode hash) that this file and fftsuite.cpp agree bit for bit.
//
//   <bin> --suite FILE --threads N [--mode time|mem|hash|err] [--batches K] [--tmin S] [--only NAME] [--plan estimate|measure|patient]
//
// Lines: time  "name threads plan_s iters t_1..t_K"  (seconds per transform, unnormalized, forward = exp(-i..))
//        mem   "name threads heap_live_after_plan heap_peak_plan_exec"
//        hash  "name threads fnv64(output)"
//        err   "name rel_rms rel_max"   (needs -DFFTSUITE_REF and libfftw3l.a: error against long-double upstream FFTW)
//        unsupported case:  "name threads FAIL <reason>"   (err mode: "name FAIL <reason>")
//
// An adapter provides (see adapters/*.cpp): adapter_name(), adapter_init(threads), adapter_plan() for double and float,
// adapter_cleanup().  adapter_plan() receives the buffers the transform will run on; it must not rely on their contents
// (they are zero while planning) and returns nullptr with a reason when the case is unsupported.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#if defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
#ifdef FFTSUITE_REF
#include <fftw3.h>
#endif

extern "C" { int mt_enabled(void); long mt_live(void); long mt_peak(void); void mt_reset_peak(void); }

struct Case { std::string name, kind, layout, place; char prec; std::vector<int> n; int howmany; };
// One transform dimension: length n, input stride is, output stride os (units: elements of the respective side;
// complex arrays count complex elements, real arrays real elements), exactly the FFTW guru iodim convention.
struct Dim { long long n, is, os; };
struct Geo { int rank; std::vector<Dim> d; Dim v; long long in_len, out_len; };  // lengths in T units (reals)

// Adapter-facing plan.  exec() runs one transform in -> out as planned.  load() is called before the first exec of every
// timed batch and before the single exec of the err/hash modes; adapters that only work in place use it to copy in -> out.
struct Plan {
    virtual ~Plan() {}
    virtual void exec() = 0;
    virtual void load() {}
    double plan_s = -1;  // >= 0: adapter-measured plan time (seconds) replaces the wall time around adapter_plan()
};
const char *adapter_name();
void adapter_init(int threads);
void adapter_cleanup();
Plan *adapter_plan(const Case &, const Geo &, double *in, double *out, std::string &why);
Plan *adapter_plan(const Case &, const Geo &, float *in, float *out, std::string &why);

// For libraries without an explicit plan object (stateless calls with an internal plan cache): the plan is built by the first
// call, so plan_s = (time of first call) - (time of second call), clipped at 0.  Called by the adapter while the buffers are still zero.
#ifdef HARNESS_TYPES_ONLY  // a translation unit that only defines adapter plans (kissfft.cpp is compiled once per precision)
static inline double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
#else
static inline double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
static uint64_t fnv(const void *p, size_t n) {
    const unsigned char *c = (const unsigned char *)p; uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
    return h;
}
struct Minstd {  // values in [-0.5,0.5), exactly representable in float
    uint64_t s; explicit Minstd(uint64_t seed) : s(1 + (seed * 7919) % 2147483646ull) {}
    double next() { s = (48271 * s) % 2147483647ull; return (double)(s >> 7) / 16777216.0 - 0.5; }
};

static Geo geometry(const Case &c) {
    Geo g; g.rank = (int)c.n.size(); g.d.resize(g.rank);
    bool real = c.kind == "rf" || c.kind == "rb";
    int last = c.n.back(), lastc = real ? last / 2 + 1 : last;
    if (c.layout == "col") {  // rank 1, stride howmany, dist 1 (complex units)
        long long h = c.howmany;
        g.d[0] = {c.n[0], h, h};
        g.v = {h, 1, 1};
        long long nr = (long long)c.n[0] * h, nc = (long long)lastc * h;
        if (c.kind == "rf") { g.in_len = nr; g.out_len = 2 * nc; }
        else if (c.kind == "rb") { g.in_len = 2 * nc; g.out_len = nr; }
        else { g.in_len = g.out_len = 2 * nr; }
        return g;
    }
    long long sr = 1, sc = 1;  // real-side and complex-side strides
    for (int k = g.rank - 1; k >= 0; k--) {
        int nr = c.n[k], nc = (k == g.rank - 1) ? lastc : c.n[k];
        if (c.kind == "rf") g.d[k] = {c.n[k], sr, sc};
        else if (c.kind == "rb") g.d[k] = {c.n[k], sc, sr};
        else g.d[k] = {c.n[k], sc, sc};
        sr *= nr; sc *= nc;
    }
    if (c.kind == "rf") { g.v = {c.howmany, sr, sc}; g.in_len = sr * c.howmany; g.out_len = 2 * sc * c.howmany; }
    else if (c.kind == "rb") { g.v = {c.howmany, sc, sr}; g.in_len = 2 * sc * c.howmany; g.out_len = sr * c.howmany; }
    else { g.v = {c.howmany, sc, sc}; g.in_len = g.out_len = 2 * sc * c.howmany; }
    return g;
}

static void *abuf(size_t bytes) { void *p = nullptr; if (posix_memalign(&p, 64, bytes ? bytes : 64)) abort(); return p; }

template <typename T> struct Prob {
    std::unique_ptr<Plan> p; T *in = nullptr, *out = nullptr; Geo g; double plan_s = 0; bool ip = false;
    std::vector<T> backup; std::string why;
};
template <typename T> static void fill(T *x, long long len, const Case &c) {
    Minstd r(fnv(c.name.data(), c.name.size()) & 0xffffffff);
    for (long long i = 0; i < len; i++) x[i] = (T)r.next();
}
template <typename T> static Plan *mkplan(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    return adapter_plan(c, g, in, out, why);
}

// Same contract as fftsuite's make(): allocate (zeroed) buffers, plan (timed), then fill the input.  For c2r the input is the
// r2c of the real signal, built with an r2c plan of the same problem from the library under test.
template <typename T> static bool make(const Case &c, Prob<T> &P, bool fill_input) {
    P.g = geometry(c); P.ip = c.place == "ip";
    if (P.ip && (c.kind == "rf" || c.kind == "rb")) { P.why = "ip real not supported"; return false; }
    long long ilen = P.g.in_len, olen = P.g.out_len;
    P.in = (T *)abuf(sizeof(T) * std::max(ilen, P.ip ? olen : ilen));
    P.out = P.ip ? P.in : (T *)abuf(sizeof(T) * olen);
    memset(P.in, 0, sizeof(T) * ilen); if (!P.ip) memset(P.out, 0, sizeof(T) * olen);
    double t0 = now();
    P.p.reset(mkplan<T>(c, P.g, P.in, P.out, P.why));
    P.plan_s = now() - t0;
    if (!P.p) { if (P.why.empty()) P.why = "no plan"; return false; }
    if (P.p->plan_s >= 0) P.plan_s = P.p->plan_s;
    if (fill_input) {
        if (c.kind == "rb") {
            std::vector<T> tmpin(P.g.out_len);
            fill(tmpin.data(), (long long)tmpin.size(), c);
            Case c2 = c; c2.kind = "rf"; Geo g2 = geometry(c2);
            T *rin = (T *)abuf(sizeof(T) * P.g.out_len);
            memset(rin, 0, sizeof(T) * P.g.out_len);
            std::string why2;
            std::unique_ptr<Plan> q(mkplan<T>(c2, g2, rin, P.in, why2));
            if (!q) { P.why = "r2c for c2r input: " + why2; return false; }
            memcpy(rin, tmpin.data(), sizeof(T) * P.g.out_len);
            q->load(); q->exec(); free(rin);
        } else fill(P.in, ilen, c);
        P.backup.assign(P.in, P.in + ilen);
    }
    return true;
}
template <typename T> static void restore(Prob<T> &P) { if (!P.backup.empty()) memcpy(P.in, P.backup.data(), sizeof(T) * P.backup.size()); }
template <typename T> static void destroy(Prob<T> &P) { P.p.reset(); if (!P.ip) free(P.out); free(P.in); }

static bool parse(const std::string &line, Case &c) {
    char name[128], prec[8], kind[8], shape[128], layout[16], place[8]; int h;
    if (line.empty() || line[0] == '#') return false;
    if (sscanf(line.c_str(), "%127s %7s %7s %127s %d %15s %7s", name, prec, kind, shape, &h, layout, place) != 7) return false;
    c.name = name; c.prec = prec[0]; c.kind = kind; c.layout = layout; c.place = place; c.howmany = h; c.n.clear();
    for (char *s = strtok(shape, "x"); s; s = strtok(nullptr, "x")) c.n.push_back(atoi(s));
    return true;
}

static int g_batches = 3, g_threads = 1; static double g_tmin = 0.02;

static void fail(const char *name, const std::string &why) { printf("%s %d FAIL %s\n", name, g_threads, why.c_str()); fflush(stdout); }

template <typename T> static void run_time(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { fail(c.name.c_str(), P.why); return; }
    P.p->load(); P.p->exec(); restore(P);
    long iters = 1;
    for (;;) { P.p->load(); double t0 = now(); for (long i = 0; i < iters; i++) P.p->exec(); double t = now() - t0;
               if (t >= g_tmin || iters >= (1L << 30)) break; iters *= 2; }
    printf("%s %d %.9g %ld", c.name.c_str(), g_threads, P.plan_s, iters);
    for (int b = 0; b < g_batches; b++) {
        restore(P); P.p->load();
        double t0 = now(); for (long i = 0; i < iters; i++) P.p->exec(); double t = now() - t0;
        printf(" %.9g", t / iters);
    }
    printf("\n"); fflush(stdout);
    destroy(P);
}
template <typename T> static void run_mem(const Case &c) {
    Geo g = geometry(c);
    long long ilen = g.in_len, olen = g.out_len; bool ip = c.place == "ip";
    long before = mt_live(); mt_reset_peak();
    Prob<T> P; if (!make<T>(c, P, false)) { fail(c.name.c_str(), P.why); return; }
    long io = (long)(sizeof(T) * (std::max(ilen, ip ? olen : ilen) + (ip ? 0 : olen)));
    long live_plan = mt_live() - before - io;
    P.p->load(); P.p->exec();
    long peak = mt_peak() - before - io;
    printf("%s %d %ld %ld\n", c.name.c_str(), g_threads, live_plan, peak); fflush(stdout);
    destroy(P); adapter_cleanup();
}
template <typename T> static void run_hash(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { fail(c.name.c_str(), P.why); return; }
    P.p->load(); P.p->exec();
    printf("%s %d %016llx\n", c.name.c_str(), g_threads, (unsigned long long)fnv(P.out, sizeof(T) * P.g.out_len)); fflush(stdout);
    destroy(P);
}

#ifdef FFTSUITE_REF
template <typename T> struct RefApi;
template <> struct RefApi<long double> {
    static fftwl_plan plan(const Case &c, const Geo &g, long double *in, long double *out) {
        std::vector<fftwl_iodim64> d(g.rank); for (int k = 0; k < g.rank; k++) d[k] = {(ptrdiff_t)g.d[k].n, (ptrdiff_t)g.d[k].is, (ptrdiff_t)g.d[k].os};
        fftwl_iodim64 v = {(ptrdiff_t)g.v.n, (ptrdiff_t)g.v.is, (ptrdiff_t)g.v.os};
        if (c.kind == "cf" || c.kind == "cb")
            return fftwl_plan_guru64_dft(g.rank, d.data(), 1, &v, (fftwl_complex *)in, (fftwl_complex *)out, c.kind == "cf" ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_ESTIMATE);
        if (c.kind == "rf") return fftwl_plan_guru64_dft_r2c(g.rank, d.data(), 1, &v, in, (fftwl_complex *)out, FFTW_ESTIMATE);
        return fftwl_plan_guru64_dft_c2r(g.rank, d.data(), 1, &v, (fftwl_complex *)in, out, FFTW_ESTIMATE);
    }
};
template <typename T> static void run_err(const Case &c) {
    Prob<T> P; if (!make<T>(c, P, true)) { printf("%s FAIL %s\n", c.name.c_str(), P.why.c_str()); fflush(stdout); return; }
    Geo g = P.g; bool ip = P.ip;
    long double *rin = (long double *)abuf(sizeof(long double) * std::max(g.in_len, ip ? g.out_len : g.in_len));
    long double *rout = ip ? rin : (long double *)abuf(sizeof(long double) * g.out_len);
    memset(rin, 0, sizeof(long double) * std::max(g.in_len, ip ? g.out_len : g.in_len));
    if (!ip) memset(rout, 0, sizeof(long double) * g.out_len);
    fftwl_plan rp = RefApi<long double>::plan(c, g, rin, rout);
    if (!rp) { printf("%s FAIL no reference plan\n", c.name.c_str()); fflush(stdout); return; }
    for (long long i = 0; i < g.in_len; i++) rin[i] = (long double)P.in[i];
    P.p->load(); P.p->exec(); fftwl_execute(rp);
    long double se = 0, sr = 0, me = 0, mr = 0;
    for (long long i = 0; i < g.out_len; i++) {
        long double e = (long double)P.out[i] - rout[i]; se += e * e; sr += rout[i] * rout[i];
        me = std::max(me, fabsl(e)); mr = std::max(mr, fabsl(rout[i]));
    }
    printf("%s %.4Le %.4Le\n", c.name.c_str(), sqrtl(se / (sr > 0 ? sr : 1)), me / (mr > 0 ? mr : 1)); fflush(stdout);
    fftwl_destroy_plan(rp); if (!ip) free(rout); free(rin); destroy(P);
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
        else if (!strcmp(argv[i], "--plan")) ++i;  // planning level is an FFTW-API concept; accepted and ignored here
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (!suite) { fprintf(stderr, "usage: %s --suite FILE [--threads N] [--mode time|mem|hash|err]\n", adapter_name()); return 2; }
#if defined(__x86_64__)
    if (!strcmp(mode, "time")) { _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); }
#endif
    adapter_init(g_threads);
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
    adapter_cleanup();
    return 0;
}
#endif  // HARNESS_TYPES_ONLY
