// Planner calibration helper for the fast-mode ESTIMATE cost model (bench/planner/calibrate.sh).
// Link against fft.cpp built with -DFFTCHODE_PLANNER_CALIBRATE and run with FFTCHODE_NUMERICS=fast.
//   retime --suite S --collect OUT [--flags estimate|measure|patient]
//          plan every case and append its wisdom to OUT
//          (FFTCHODE_PLANRAND=sigma,seed perturbs the estimated costs to sample other plans)
//   retime --suite S --retime R [--tmin T] F1 F2 ...
//          rebuild every distinct plan of each case from the wisdom files and time them
//          interleaved over R rounds; per plan it prints "name ... iters median ...",
//          "PLAN name <plan>" and "FEAT name <cost-model features>"
// The case format and the timing/fill code follow bench/suite/fftsuite.cpp.
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

static unsigned g_flags = 64U;
[[maybe_unused]] static int g_print = 0;
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
    static char *sprint(plan p) { return P##sprint_plan(p); }                                                          \
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
            memcpy(rin, tmpin.data(), sizeof(T) * P.g.out_len);
            auto q = Api<T>::r2c(P.g.rank, (const D *)dd, hr, (const D *)&vv, rin, P.in);
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

[[maybe_unused]] static int g_batches = 3, g_threads = 1; static double g_tmin = 0.02;

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
    printf("\n"); if (g_print) { char *s = Api<T>::sprint(P.p); printf("PLAN %s %s\n", c.name.c_str(), s); free(s); } fflush(stdout);
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

#include <map>
#include <set>
#include <fstream>
#include <sstream>
extern "C" void fftw_fc_dev_ops(const fftw_plan p, double *o);
extern "C" void fftwf_fc_dev_ops(const fftwf_plan p, double *o);
template <typename T> struct W;
#define WIS(T, P) template <> struct W<T> { \
    static void forget() { P##forget_wisdom(); } \
    static int imp(const char *s) { return P##import_wisdom_from_string(s); } \
    static char *exp() { return P##export_wisdom_to_string(); } \
    static void ops(Api<T>::plan p, double *o) { P##fc_dev_ops(p, o); } };
WIS(double, fftw_)
WIS(float, fftwf_)

struct Ent { std::string cas; unsigned flags; std::string wis; };
static std::vector<Ent> readents(const char *f) {
    std::vector<Ent> v; std::ifstream in(f); std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("CASE ", 0) != 0) continue;
        Ent e; e.cas = line.substr(5);
        std::getline(in, line); e.flags = (unsigned)strtoul(line.c_str() + 6, 0, 10);
        std::getline(in, line); size_t n = strtoul(line.c_str() + 4, 0, 10);
        e.wis.resize(n); in.read(&e.wis[0], n); v.push_back(e);
    }
    return v;
}
template <typename T> static typename Api<T>::plan mkp(const Case &c, Prob<T> &P) {
    typedef typename Api<T>::dim D; const fftw_iodim64 *d = P.g.d.data();
    if (c.kind == "cf" || c.kind == "cb") return Api<T>::dft(P.g.rank, (const D *)d, 1, (const D *)&P.g.v, P.in, P.out, c.kind == "cf" ? FFTW_FORWARD : FFTW_BACKWARD);
    if (c.kind == "rf") return Api<T>::r2c(P.g.rank, (const D *)d, 1, (const D *)&P.g.v, P.in, P.out);
    return Api<T>::c2r(P.g.rank, (const D *)d, 1, (const D *)&P.g.v, P.in, P.out);
}
template <typename T> static void collect(const Case &c, FILE *o) {
    W<T>::forget();
    Prob<T> P; if (!make<T>(c, P, false)) return;
    char *w = W<T>::exp();
    fprintf(o, "CASE %s\nFLAGS %u\nWIS %zu\n", c.name.c_str(), g_flags, strlen(w)); fwrite(w, 1, strlen(w), o); fprintf(o, "\n");
    free(w); destroy(P);
}
template <typename T> static void retime(const Case &c, const std::vector<Ent> &ents, int rounds) {
    unsigned f0 = g_flags; g_flags = FFTW_ESTIMATE;
    Prob<T> P; if (!make<T>(c, P, true)) return;
    Api<T>::destroy(P.p); P.p = nullptr;
    std::vector<typename Api<T>::plan> ps; std::vector<std::string> names; std::set<std::string> seen;
    for (auto &e : ents) {
        W<T>::forget(); W<T>::imp(e.wis.c_str()); g_flags = e.flags | FFTW_WISDOM_ONLY;
        typename Api<T>::plan p = mkp<T>(c, P);
        if (!p) { fprintf(stderr, "%s: replay failed\n", c.name.c_str()); continue; }
        char *s = Api<T>::sprint(p); std::string ss(s); free(s);
        if (seen.count(ss)) { Api<T>::destroy(p); continue; }
        seen.insert(ss); ps.push_back(p); names.push_back(ss);
    }
    g_flags = f0; restore(P);
    size_t np = ps.size(); std::vector<long> iters(np, 1); std::vector<std::vector<double>> ts(np);
    for (size_t k = 0; k < np; k++) {  // calibrate iteration counts (~tmin per sample)
        Api<T>::exec(ps[k]); restore(P);
        for (;;) { double t0 = now(); for (long i = 0; i < iters[k]; i++) Api<T>::exec(ps[k]); double t = now() - t0; restore(P);
                   if (t >= g_tmin || iters[k] >= (1L << 30)) break; iters[k] *= 2; }
    }
    for (int r = 0; r < rounds; r++)
        for (size_t k0 = 0; k0 < np; k0++) {
            size_t k = (k0 + r) % np;
            double t0 = now(); for (long i = 0; i < iters[k]; i++) Api<T>::exec(ps[k]); double t = now() - t0; restore(P);
            ts[k].push_back(t / iters[k]);
        }
    for (size_t k = 0; k < np; k++) {
        std::vector<double> v = ts[k]; std::sort(v.begin(), v.end());
        double o[38]; W<T>::ops(ps[k], o);
        printf("%s 1 0 %ld %.9g %.9g %.9g\nPLAN %s %s\nFEAT %s", c.name.c_str(), iters[k], v[v.size() / 2], v[0], v[v.size() / 4], c.name.c_str(), names[k].c_str(), c.name.c_str());
        for (int i = 0; i < 38; i++) printf(" %.6g", o[i]);
        printf("\n"); fflush(stdout);
        Api<T>::destroy(ps[k]);
    }
    P.p = nullptr; if (!P.ip) Api<T>::release(P.out); Api<T>::release(P.in);
}
int main(int argc, char **argv) {
    const char *suite = 0, *col = 0; int rounds = 0; std::vector<const char *> files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--suite")) suite = argv[++i];
        else if (!strcmp(argv[i], "--collect")) col = argv[++i];
        else if (!strcmp(argv[i], "--flags")) { const char *f = argv[++i]; g_flags = !strcmp(f, "measure") ? 0U : !strcmp(f, "patient") ? (1U << 5) : 64U; }
        else if (!strcmp(argv[i], "--tmin")) g_tmin = atof(argv[++i]);
        else if (!strcmp(argv[i], "--retime")) rounds = atoi(argv[++i]);
        else files.push_back(argv[i]);
    }
    std::vector<Case> cases; { std::ifstream in(suite); std::string l; while (std::getline(in, l)) { Case c; if (parse(l, c)) cases.push_back(c); } }
    if (col) {
        FILE *o = fopen(col, "w");
        for (auto &c : cases) c.prec == 'd' ? collect<double>(c, o) : collect<float>(c, o);
        fclose(o); return 0;
    }
    std::map<std::string, std::vector<Ent>> by;
    for (auto f : files) for (auto &e : readents(f)) by[e.cas].push_back(e);
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    for (auto &c : cases) if (by.count(c.name)) c.prec == 'd' ? retime<double>(c, by[c.name], rounds) : retime<float>(c, by[c.name], rounds);
}
