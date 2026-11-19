// oneMKL DFTI adapter (FFTE-6 comparators).  Native DFTI descriptors, no FFTW wrapper:
//   rank, lengths, input/output strides, DFTI_NUMBER_OF_TRANSFORMS with input/output distances, in/out-of-place,
//   real transforms in CCE storage (DFTI_CONJUGATE_EVEN_STORAGE = DFTI_COMPLEX_COMPLEX, DFTI_PACKED_FORMAT = DFTI_CCE_FORMAT).
// Plan time = DftiCreateDescriptor + DftiSetValue* + DftiCommitDescriptor.  Threads: mkl_set_num_threads(N).
// Both directions are unnormalized (DFTI forward/backward scale stay 1.0).  Forward = exp(-i...).
// Environment knobs of MKL itself (MKL_ENABLE_INSTRUCTIONS, MKL_VERBOSE, MKL_DYNAMIC, ...) apply unchanged;
// FFTSUITE_INFO=1 prints the MKL version/instruction-set information to stderr at start-up.
#include "harness.h"
#include "mkl_dfti.h"
#include "mkl_service.h"

const char *adapter_name() { return "mkl"; }
void adapter_init(int threads) {
    mkl_set_num_threads(threads);
    if (getenv("FFTSUITE_INFO")) {
        char v[512]; mkl_get_version_string(v, sizeof v);
        fprintf(stderr, "%s\nmkl_get_max_threads=%d mkl_get_dynamic=%d\n", v, mkl_get_max_threads(), mkl_get_dynamic());
    }
}
void adapter_cleanup() { mkl_free_buffers(); }

template <typename T> struct MklPlan : Plan {
    DFTI_DESCRIPTOR_HANDLE h = nullptr; T *in, *out; bool fwd, ip;
    ~MklPlan() { if (h) DftiFreeDescriptor(&h); }
    void exec() override {
        if (fwd) { if (ip) DftiComputeForward(h, in); else DftiComputeForward(h, in, out); }
        else { if (ip) DftiComputeBackward(h, in); else DftiComputeBackward(h, in, out); }
    }
};

template <typename T> static Plan *mk(const Case &c, const Geo &g, T *in, T *out, std::string &why) {
    bool real = c.kind == "rf" || c.kind == "rb", ip = in == out;
    std::unique_ptr<MklPlan<T>> p(new MklPlan<T>());
    p->in = in; p->out = out; p->fwd = c.kind == "cf" || c.kind == "rf"; p->ip = ip;
    int rank = g.rank;
    std::vector<MKL_LONG> len(rank), is(rank + 1, 0), os(rank + 1, 0);
    for (int k = 0; k < rank; k++) { len[k] = g.d[k].n; is[k + 1] = g.d[k].is; os[k + 1] = g.d[k].os; }
    MKL_LONG st;
    DFTI_CONFIG_VALUE prec = sizeof(T) == 8 ? DFTI_DOUBLE : DFTI_SINGLE;
    DFTI_CONFIG_VALUE dom = real ? DFTI_REAL : DFTI_COMPLEX;
    st = rank == 1 ? DftiCreateDescriptor(&p->h, prec, dom, 1, len[0]) : DftiCreateDescriptor(&p->h, prec, dom, rank, len.data());
    if (st) { why = std::string("DftiCreateDescriptor: ") + DftiErrorMessage(st); return nullptr; }
    auto set = [&](const char *what, DFTI_CONFIG_PARAM k, auto val) {
        st = DftiSetValue(p->h, k, val);
        if (st) { why = std::string(what) + ": " + DftiErrorMessage(st); return false; }
        return true;
    };
    if (!set("PLACEMENT", DFTI_PLACEMENT, ip ? DFTI_INPLACE : DFTI_NOT_INPLACE)) return nullptr;
    if (real) {
        if (!set("CONJUGATE_EVEN_STORAGE", DFTI_CONJUGATE_EVEN_STORAGE, DFTI_COMPLEX_COMPLEX)) return nullptr;
        if (!set("PACKED_FORMAT", DFTI_PACKED_FORMAT, DFTI_CCE_FORMAT)) return nullptr;
    }
    if (c.howmany > 1 && !set("NUMBER_OF_TRANSFORMS", DFTI_NUMBER_OF_TRANSFORMS, (MKL_LONG)g.v.n)) return nullptr;
    bool f = p->fwd;  // FWD_* describes the forward-domain array (real side of a real transform), BWD_* the other one
    if (!set("FWD_STRIDES", DFTI_FWD_STRIDES, (f ? is : os).data())) return nullptr;
    if (!set("BWD_STRIDES", DFTI_BWD_STRIDES, (f ? os : is).data())) return nullptr;
    if (!set("FWD_DISTANCE", DFTI_FWD_DISTANCE, (MKL_LONG)(f ? g.v.is : g.v.os))) return nullptr;
    if (!set("BWD_DISTANCE", DFTI_BWD_DISTANCE, (MKL_LONG)(f ? g.v.os : g.v.is))) return nullptr;
    st = DftiCommitDescriptor(p->h);
    if (st) { why = std::string("DftiCommitDescriptor: ") + DftiErrorMessage(st); return nullptr; }
    return p.release();
}
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return mk<double>(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return mk<float>(c, g, in, out, why); }

