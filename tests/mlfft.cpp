// MATLAB-style fft/ifft/fft2/fftn/'symmetric' semantics on top of the FFTW API,
// reconstructed from MATLAB's observable outputs. Reads one job per line on stdin:
//   <d|s> <in_is_complex> <ndim> <shape...> <in.bin> <out.bin> <nthreads> <scale> <nops> <op...>
// op = <kind>:<dims>:<lengths>, dims 0-based and comma separated, length -1 = unchanged
//   kind F  forward            I  inverse, complex result     S  inverse, 'symmetric' (real result)
//   kind RF/RI/RS: same, as one rank-k FFTW plan over all listed dims (else one 1-D pass per dim)
//   kind H  real -> half spectrum along one dim (r2c, no Hermitian fill)
//   kind E  expand a half spectrum (first listed dim halved) to the full Hermitian spectrum;
//           length = full length of the first dim
//   kind C  half spectrum -> real along one dim (c2r); length = real output length
//   kind T  truncate one dim to the given length (no transform)
// scale = how 1/N is applied after a backward transform: mul (x * (1/N)), div (x / N)
// Writes the result (interleaved complex, or real when every imaginary part is zero, as MATLAB
// does) to out.bin and prints "<id-free> <isreal> <shape...>" on stdout.
#include "fftw3.h"
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

typedef std::vector<long long> Shape;

template <typename T> struct F;
#define FFTW_TRAITS(T, P)                                                                              \
    template <> struct F<T> {                                                                          \
        typedef P##_plan plan; typedef P##_complex cpx; typedef P##_iodim64 dim;                       \
        static plan dft(int r, const dim *d, int h, const dim *hd, cpx *i, cpx *o, int s) {            \
            return P##_plan_guru64_dft(r, d, h, hd, i, o, s, FFTW_ESTIMATE); }                         \
        static plan r2c(int r, const dim *d, int h, const dim *hd, T *i, cpx *o) {                     \
            return P##_plan_guru64_dft_r2c(r, d, h, hd, i, o, FFTW_ESTIMATE); }                        \
        static plan c2r(int r, const dim *d, int h, const dim *hd, cpx *i, T *o) {                     \
            return P##_plan_guru64_dft_c2r(r, d, h, hd, i, o, FFTW_ESTIMATE); }                        \
        static void run(plan p) { P##_execute(p); P##_destroy_plan(p); }                               \
        static void threads(int n) { P##_init_threads(); P##_plan_with_nthreads(n); }                  \
        static std::string wisdom() { char *w = P##_export_wisdom_to_string(); std::string s(w); free(w); P##_forget_wisdom(); return s; } \
    };
FFTW_TRAITS(double, fftw)
FFTW_TRAITS(float, fftwf)

static long long prod(const Shape &s) { long long p = 1; for (long long v : s) p *= v; return p; }

template <typename T> struct Arr {
    Shape shape;
    bool real = false;
    std::vector<std::complex<T>> c;   // used when !real
    std::vector<T> r;                 // used when real
    long long stride(int d) const { long long s = 1; for (int k = 0; k < d; k++) s *= shape[k]; return s; }
};

static std::string scale_mode = "mul";
static bool inplace_later = false;   // c2c passes after the first one run in place (option "+ip")
static int pass_no = 0;
static bool fullbuf = false;         // r2c writes / c2r reads the half spectrum inside a full-size array ("+full")

template <typename T> static void scale(std::vector<std::complex<T>> &v, long long n) {
    if (scale_mode == "div") { for (auto &x : v) x = std::complex<T>(x.real() / (T)n, x.imag() / (T)n); return; }
    T s = (T)1 / (T)n;
    for (auto &x : v) x = std::complex<T>(x.real() * s, x.imag() * s);
}
template <typename T> static void scale(std::vector<T> &v, long long n) {
    if (scale_mode == "div") { for (auto &x : v) x /= (T)n; return; }
    T s = (T)1 / (T)n;
    for (auto &x : v) x *= s;
}

// resize dimension d to length n (zero-pad or truncate), MATLAB fft(x,n,dim)
template <typename T> static void resize(Arr<T> &a, int d, long long n) {
    while ((int)a.shape.size() <= d) a.shape.push_back(1);
    if (n < 0 || n == a.shape[d]) return;
    Shape ns = a.shape; ns[d] = n;
    long long inner = a.stride(d), outer = prod(a.shape) / (inner * a.shape[d]), m = std::min(n, a.shape[d]);
    if (a.real) {
        std::vector<T> o(prod(ns), 0);
        for (long long j = 0; j < outer; j++) for (long long k = 0; k < m; k++) for (long long i = 0; i < inner; i++)
            o[(j * n + k) * inner + i] = a.r[(j * a.shape[d] + k) * inner + i];
        a.r.swap(o);
    } else {
        std::vector<std::complex<T>> o(prod(ns), 0);
        for (long long j = 0; j < outer; j++) for (long long k = 0; k < m; k++) for (long long i = 0; i < inner; i++)
            o[(j * n + k) * inner + i] = a.c[(j * a.shape[d] + k) * inner + i];
        a.c.swap(o);
    }
    a.shape = ns;
}

template <typename T> static void to_complex(Arr<T> &a) {
    if (!a.real) return;
    a.c.assign(a.r.begin(), a.r.end()); a.r.clear(); a.real = false;
}

// FFTW tensors for transforming dims 'td' of a column-major array whose logical shape is 'sh'
// with per-dim strides 'is'/'os'; FFTW wants the stride-1 (MATLAB-first) dim last.
template <typename T> static void tensors(const Shape &sh, const Shape &is, const Shape &os, const std::vector<int> &td,
                                          std::vector<typename F<T>::dim> &tr, std::vector<typename F<T>::dim> &vec) {
    tr.clear(); vec.clear();
    for (int k = (int)td.size() - 1; k >= 0; k--) { typename F<T>::dim x; x.n = sh[td[k]]; x.is = is[td[k]]; x.os = os[td[k]]; tr.push_back(x); }
    for (int k = (int)sh.size() - 1; k >= 0; k--) {
        bool t = false; for (int d : td) t |= d == k;
        if (t) continue;
        typename F<T>::dim x; x.n = sh[k]; x.is = is[k]; x.os = os[k]; vec.push_back(x);
    }
}

static Shape strides(const Shape &sh) { Shape s(sh.size()); long long p = 1; for (size_t k = 0; k < sh.size(); k++) { s[k] = p; p *= sh[k]; } return s; }

// fill the full spectrum from an r2c half spectrum over transform dims td (td[0] is the halved dim)
template <typename T> static std::vector<std::complex<T>> hermitian_fill(const std::vector<std::complex<T>> &h, const Shape &hs,
                                                                         const Shape &fs, const std::vector<int> &td) {
    std::vector<std::complex<T>> o(prod(fs));
    Shape hst = strides(hs), idx(fs.size(), 0);
    int d0 = td[0];
    for (long long lin = 0; lin < (long long)o.size(); lin++) {
        long long rem = lin;
        for (size_t k = 0; k < fs.size(); k++) { idx[k] = rem % fs[k]; rem /= fs[k]; }
        bool conj = idx[d0] >= hs[d0];
        long long off = 0;
        for (size_t k = 0; k < fs.size(); k++) {
            long long v = idx[k];
            bool t = false; for (int d : td) t |= d == (int)k;
            if (conj && t) v = (fs[k] - v) % fs[k];
            off += v * hst[k];
        }
        o[lin] = conj ? std::conj(h[off]) : h[off];
    }
    return o;
}

template <typename T> static void transform(Arr<T> &a, char kind, const std::vector<int> &td, long long len = -1) {
    typedef F<T> A;
    std::vector<typename A::dim> tr, vec;
    Shape st = strides(a.shape);
    if (kind == 'H') {
        Shape hs = a.shape; hs[td[0]] = a.shape[td[0]] / 2 + 1;
        std::vector<std::complex<T>> h(prod(hs));
        tensors<T>(a.shape, st, strides(hs), td, tr, vec);
        std::vector<T> in = a.r;
        A::run(A::r2c((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), in.data(), (typename A::cpx *)h.data()));
        a.c.swap(h); a.r.clear(); a.real = false; a.shape = hs;
        return;
    }
    if (kind == 'E') {
        Shape fs = a.shape; fs[td[0]] = len;
        a.c = hermitian_fill(a.c, a.shape, fs, td); a.shape = fs;
        return;
    }
    if (kind == 'C') {
        to_complex(a);
        Shape os = a.shape; os[td[0]] = len;
        std::vector<std::complex<T>> in = a.c;
        std::vector<T> o(prod(os));
        tensors<T>(os, st, strides(os), td, tr, vec);
        A::run(A::c2r((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), (typename A::cpx *)in.data(), o.data()));
        scale(o, len);
        a.r.swap(o); a.c.clear(); a.real = true; a.shape = os;
        return;
    }
    if (kind == 'F' && a.real) {
        Shape hs = a.shape; hs[td[0]] = a.shape[td[0]] / 2 + 1;
        std::vector<std::complex<T>> h(prod(hs));
        std::vector<T> in = a.r;
        if (fullbuf) {
            std::vector<std::complex<T>> full(prod(a.shape));
            tensors<T>(hs, st, st, td, tr, vec);
            A::run(A::r2c((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), in.data(), (typename A::cpx *)full.data()));
            Shape hst = strides(hs);
            for (long long lin = 0; lin < (long long)h.size(); lin++) {
                long long rem = lin, off = 0;
                for (size_t k = 0; k < hs.size(); k++) { off += (rem % hs[k]) * st[k]; rem /= hs[k]; }
                h[lin] = full[off];
            }
        } else {
            tensors<T>(a.shape, st, strides(hs), td, tr, vec);
            A::run(A::r2c((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), in.data(), (typename A::cpx *)h.data()));
        }
        a.c = hermitian_fill(h, hs, a.shape, td); a.r.clear(); a.real = false;
        return;
    }
    if (kind == 'S') {
        to_complex(a);
        Shape hs = a.shape; hs[td[0]] = a.shape[td[0]] / 2 + 1;
        std::vector<std::complex<T>> h(prod(hs));
        Shape hst = strides(hs), idx(hs.size());
        for (long long lin = 0; lin < (long long)h.size(); lin++) {
            long long rem = lin, off = 0;
            for (size_t k = 0; k < hs.size(); k++) { off += (rem % hs[k]) * st[k]; rem /= hs[k]; }
            h[lin] = a.c[off];
        }
        std::vector<T> o(prod(a.shape));
        if (fullbuf) {
            std::vector<std::complex<T>> in = a.c;
            tensors<T>(a.shape, st, st, td, tr, vec);
            A::run(A::c2r((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), (typename A::cpx *)in.data(), o.data()));
        } else {
            tensors<T>(a.shape, hst, st, td, tr, vec);
            A::run(A::c2r((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), (typename A::cpx *)h.data(), o.data()));
        }
        long long N = 1; for (int d : td) N *= a.shape[d];
        scale(o, N);
        a.r.swap(o); a.c.clear(); a.real = true;
        return;
    }
    to_complex(a);
    bool ip = inplace_later && pass_no++ > 0;
    std::vector<std::complex<T>> in = a.c, out(ip ? 0 : a.c.size());
    if (ip) out = in;
    tensors<T>(a.shape, st, st, td, tr, vec);
    typename A::plan p = A::dft((int)tr.size(), tr.data(), (int)vec.size(), vec.data(), (typename A::cpx *)(ip ? out.data() : in.data()),
                                (typename A::cpx *)out.data(), kind == 'F' ? FFTW_FORWARD : FFTW_BACKWARD);
    A::run(p);
    if (kind == 'I') { long long N = 1; for (int d : td) N *= a.shape[d]; scale(out, N); }
    a.c.swap(out);
}

template <typename T> static void job(std::istringstream &ss, int ndim, Shape shape, bool cplx) {
    std::string inp, outp; int nt, nops; ss >> inp >> outp >> nt >> scale_mode >> nops;
    inplace_later = scale_mode.find("+ip") != std::string::npos;
    fullbuf = scale_mode.find("+full") != std::string::npos;
    scale_mode = scale_mode.substr(0, scale_mode.find('+'));
    pass_no = 0;
    F<T>::threads(nt);
    Arr<T> a; a.shape = shape; a.real = !cplx;
    long long N = prod(shape);
    FILE *f = fopen(inp.c_str(), "rb");
    if (cplx) { a.c.resize(N); if (N && fread(a.c.data(), sizeof(T) * 2, N, f) != (size_t)N) exit(3); }
    else { a.r.resize(N); if (N && fread(a.r.data(), sizeof(T), N, f) != (size_t)N) exit(3); }
    fclose(f);
    for (int k = 0; k < nops; k++) {
        std::string op; ss >> op;
        std::string kind = op.substr(0, op.find(':'));
        std::string rest = op.substr(op.find(':') + 1);
        std::string ds = rest.substr(0, rest.find(':')), ns = rest.substr(rest.find(':') + 1);
        std::vector<int> dims; std::vector<long long> lens;
        { std::istringstream x(ds); std::string t; while (std::getline(x, t, ',')) dims.push_back(std::stoi(t)); }
        { std::istringstream x(ns); std::string t; while (std::getline(x, t, ',')) lens.push_back(std::stoll(t)); }
        char kd = kind.back();
        if (kd == 'T') { resize(a, dims[0], lens[0]); continue; }
        if (kd == 'E' || kd == 'C') { if (prod(a.shape)) transform(a, kd, dims, lens[0]); continue; }
        for (size_t i = 0; i < dims.size(); i++) resize(a, dims[i], lens[i]);
        if (prod(a.shape) == 0) continue;
        bool rank = kind[0] == 'R';
        if (rank) transform(a, kd, dims);
        else for (int d : dims) transform(a, kd, std::vector<int>{d});
    }
    if (!a.real) {
        bool allz = true; for (auto &x : a.c) allz &= x.imag() == 0;
        if (allz) { a.r.resize(a.c.size()); for (size_t i = 0; i < a.c.size(); i++) a.r[i] = a.c[i].real(); a.c.clear(); a.real = true; }
    }
    if (getenv("MLFFT_WISDOM")) { FILE *w = fopen(getenv("MLFFT_WISDOM"), "w"); fputs(F<T>::wisdom().c_str(), w); fclose(w); }
    F<T>::wisdom();
    f = fopen(outp.c_str(), "wb");
    if (a.real) fwrite(a.r.data(), sizeof(T), a.r.size(), f); else fwrite(a.c.data(), sizeof(T) * 2, a.c.size(), f);
    fclose(f);
    printf("%d", (int)a.real);
    for (long long v : a.shape) printf(" %lld", v);
    printf("\n");
    fflush(stdout);
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cls; int cplx, ndim; ss >> cls >> cplx >> ndim;
        Shape sh(ndim); for (auto &v : sh) ss >> v;
        if (cls == "d") job<double>(ss, ndim, sh, cplx); else job<float>(ss, ndim, sh, cplx);
    }
}
