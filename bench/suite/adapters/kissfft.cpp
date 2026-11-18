// KissFFT adapter (main translation unit): dispatches to the per-precision plan builders in kissfft_plan.cpp.
#include "harness.h"
const char *adapter_name() { return "kissfft"; }
void adapter_init(int) {}
void adapter_cleanup() {}
Plan *kiss_plan_d(const Case &, const Geo &, double *, double *, std::string &);
Plan *kiss_plan_s(const Case &, const Geo &, float *, float *, std::string &);
Plan *adapter_plan(const Case &c, const Geo &g, double *in, double *out, std::string &why) { return kiss_plan_d(c, g, in, out, why); }
Plan *adapter_plan(const Case &c, const Geo &g, float *in, float *out, std::string &why) { return kiss_plan_s(c, g, in, out, why); }
