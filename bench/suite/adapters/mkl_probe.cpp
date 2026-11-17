// MKL code-path probe: which ISA branch does MKL pick on this CPU, and which instruction sets can be forced?  No timing.
// Build static or dynamic (comparators.sh does both); with the dynamic build, "strace -e openat" shows which libmkl_<isa>.so.3 is loaded.
#include <cstdio>
#include <cstdlib>
#include "mkl.h"
#include "mkl_service.h"
// Exported but undocumented dispatch queries of libmkl_core (present in 2026.2; they only report, never change, the code path).
extern "C" { int mkl_serv_is_avx2_enabled(void); int mkl_serv_is_avx512_enabled(void); int mkl_serv_is_avx10_enabled(void); int mkl_serv_is_sse4_2_enabled(void);
             int mkl_serv_cpuiszen(void); int mkl_serv_cpuhas256bit_vector(void); int mkl_serv_cpuhas512bit_vector(void); }
static const char *branch(int b) {
    switch (b) {
    case MKL_CBWR_OFF: return "OFF"; case MKL_CBWR_BRANCH_OFF: return "BRANCH_OFF"; case MKL_CBWR_AUTO: return "AUTO"; case MKL_CBWR_COMPATIBLE: return "COMPATIBLE";
    case MKL_CBWR_SSE2: return "SSE2"; case MKL_CBWR_SSE4_2: return "SSE4_2"; case MKL_CBWR_AVX2: return "AVX2"; case MKL_CBWR_AVX512: return "AVX512";
    case MKL_CBWR_AVX512_E1: return "AVX512_E1"; case MKL_CBWR_AVX10: return "AVX10"; default: return "?";
    }
}
static const char *envv(const char *n) { const char *v = getenv(n); return v ? v : "(unset)"; }
int main() {
    char v[512]; mkl_get_version_string(v, sizeof v);
    MKLVersion ver; mkl_get_version(&ver);
    printf("version: %s\n", v);
    printf("MKLVersion: %d.%d.%d build %s processor \"%s\" platform %s\n", ver.MajorVersion, ver.MinorVersion, ver.UpdateVersion, ver.Build, ver.Processor, ver.Platform);
    printf("env MKL_ENABLE_INSTRUCTIONS=%s MKL_DEBUG_CPU_TYPE=%s MKL_CBWR=%s\n", envv("MKL_ENABLE_INSTRUCTIONS"), envv("MKL_DEBUG_CPU_TYPE"), envv("MKL_CBWR"));
    printf("mkl_cbwr_get_auto_branch() = %s\n", branch(mkl_cbwr_get_auto_branch()));
    printf("mkl_cbwr_get(MKL_CBWR_BRANCH) = %s\n", branch(mkl_cbwr_get(MKL_CBWR_BRANCH)));
    printf("dispatch queries: is_sse4_2_enabled=%d is_avx2_enabled=%d is_avx512_enabled=%d is_avx10_enabled=%d cpuiszen=%d has256bit_vector=%d has512bit_vector=%d\n",
           mkl_serv_is_sse4_2_enabled(), mkl_serv_is_avx2_enabled(), mkl_serv_is_avx512_enabled(), mkl_serv_is_avx10_enabled(), mkl_serv_cpuiszen(), mkl_serv_cpuhas256bit_vector(), mkl_serv_cpuhas512bit_vector());
    const struct { const char *n; int id; } isa[] = {{"SSE4_2", MKL_ENABLE_SSE4_2}, {"AVX2", MKL_ENABLE_AVX2}, {"AVX512", MKL_ENABLE_AVX512}, {"AVX512_E1", MKL_ENABLE_AVX512_E1},
        {"AVX512_E2", MKL_ENABLE_AVX512_E2}, {"AVX512_E3", MKL_ENABLE_AVX512_E3}, {"AVX512_E4", MKL_ENABLE_AVX512_E4}, {"AVX2_E1", MKL_ENABLE_AVX2_E1}, {"AVX512_E5", MKL_ENABLE_AVX512_E5}, {"AVX10", MKL_ENABLE_AVX10}};
    for (auto &i : isa) printf("mkl_enable_instructions(%s) -> %d\n", i.n, mkl_enable_instructions(i.id));
    printf("mkl_cbwr_get(MKL_CBWR_BRANCH) after enable attempts = %s\n", branch(mkl_cbwr_get(MKL_CBWR_BRANCH)));
    printf("mkl_get_max_threads=%d\n", mkl_get_max_threads());
    return 0;
}
