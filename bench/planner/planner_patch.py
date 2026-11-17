#!/usr/bin/env python3
"""Apply the opt/planner fast-mode ESTIMATE cost model to an amalgamated fft.cpp
(baseline-v0 or the opt/simd regeneration).
Usage: planner_patch.py fft.cpp ["w0, ..., w35"]   (weights default to the shipped calibration)
COLPACK_ESTIMATE=1 also lets fast ESTIMATE pick dft-colpack (off: it lost 0.4-0.7x on 2-D/3-D cases)."""
import sys, os
p = sys.argv[1]; s = open(p).read()
def rep(old, new, cnt=1):
    global s
    c = s.count(old)
    assert c == cnt, ('anchor count %d != %d' % (c, cnt), old[:400])
    s = s.replace(old, new)
FAST = '::fftchode_numerics_mode()'
O = '&pln->super.super.ops'

# ---------------------------------------------------------------- globals
GLOBAL = r'''/* ==== fftchode fast-mode ESTIMATE planner (opt/planner, FFTE-6) ====
 * In fast mode FFTW_ESTIMATE searches every applicable plan (no pruning) and
 * ranks them with a cost that adds a memory-traffic and codelet-efficiency
 * model to FFTW's operation count (see X(fc_mem), X(fc_loop), X(fc_arith)).
 * The weights were fitted offline on this CPU class (bench/planner/calibrate.sh)
 * on problem sizes outside the frozen suite.  Index: 0 arithmetic outside
 * codelets, 1 FFTW's "other" ops (copies), 2-5 cache lines fetched from
 * L1/L2/L3/DRAM, 6 child-plan calls in loops, 7-34 codelet arithmetic by
 * register width (scalar, 128, 256, 512 bits) x family (n1 n2 t1 t2 t3 q1
 * real), 35 extra for codelets of radix > 16.
 * A threaded plan (X(fc_thr)) costs its parallel part divided by the lanes
 * the thread pool will actually run it on (the CPUs in the process's affinity
 * mask, fewer when the work is too small to wake a thread), plus
 * FFTCHODE_FC_SPAWN cost units per parallel region (one unit is ~0.13 ns on
 * the calibration machine).
 * The unpruned search evaluates at most FFTCHODE_PLAN_BUDGET plans per
 * top-level plan; after that it prunes as stock ESTIMATE does.  The budget is
 * a count, not a time, so plans stay reproducible.
 * Building with -DFFTCHODE_PLANNER_CALIBRATE reads FFTCHODE_PLANW (36
 * comma-separated weights), FFTCHODE_PLANSPAWN (spawn cost),
 * FFTCHODE_PLANBUDGET (plan budget) and FFTCHODE_PLANRAND (sigma,seed: random
 * cost perturbation for sampling plans). */
#include <thread>
#define FFTCHODE_FC_SPAWN 3e4
#define FFTCHODE_PLAN_BUDGET 10000
static const double fftchode_fcw_default[36] = FFTCHODE_FCW_INIT;
#ifdef FFTCHODE_PLANNER_CALIBRATE
#include <cmath>
static double fftchode_fcw_[36];
static double fftchode_rand_[2];
static int fftchode_fcw_init_ = 0;
static void fftchode_fcw_load(void)
{
     const char *e = std::getenv("FFTCHODE_PLANW"), *r = std::getenv("FFTCHODE_PLANRAND");
     int i;
     for (i = 0; i < 36; ++i) fftchode_fcw_[i] = fftchode_fcw_default[i];
     for (i = 0; e && *e && i < 36; ++i) {
          char *q; double x = std::strtod(e, &q);
          if (q == e) break;
          fftchode_fcw_[i] = x; e = (*q == ',') ? q + 1 : q;
     }
     for (i = 0; r && *r && i < 2; ++i) {
          char *q; double x = std::strtod(r, &q);
          if (q == r) break;
          fftchode_rand_[i] = x; r = (*q == ',') ? q + 1 : q;
     }
     fftchode_fcw_init_ = 1;
}
static const double *fftchode_fcw(void)
{
     if (!fftchode_fcw_init_) fftchode_fcw_load();
     return fftchode_fcw_;
}
static double fftchode_fc_spawn(void)
{
     const char *e = std::getenv("FFTCHODE_PLANSPAWN");
     return e ? std::atof(e) : FFTCHODE_FC_SPAWN;
}
static int fftchode_plan_budget(void)
{
     const char *e = std::getenv("FFTCHODE_PLANBUDGET");
     return e ? std::atoi(e) : FFTCHODE_PLAN_BUDGET;
}
#else
static const double *fftchode_fcw(void) { return fftchode_fcw_default; }
static double fftchode_fc_spawn(void) { return FFTCHODE_FC_SPAWN; }
static int fftchode_plan_budget(void) { return FFTCHODE_PLAN_BUDGET; }
#endif

'''
WEIGHTS = "{ 1,1.397,1.802,2.308,4.549,0,39.11,0.9886,0,0.9091,1.066,0,0,0.8838,0.9278,0.9154,1.016,0.5812,0.7173,0.155,1.345,1.456,1.145,0.8925,0.5188,0.9154,0,1.36,8.137,9.919,0.9093,0.07543,1.262,8.516,11.2,0.0975 }"
if len(sys.argv) > 2: WEIGHTS = '{ ' + sys.argv[2] + ' }'
rep('''#define FFT_CPP_INNER 1
#ifndef FFT_NO_DOUBLE''', '#define FFTCHODE_FCW_INIT ' + WEIGHTS + '\n' + GLOBAL + '''#define FFT_CPP_INNER 1
#ifndef FFT_NO_DOUBLE''')

# ---------------------------------------------------------------- flags
rep('''     map_flags(&flags, &flags, self_flagmap, NELEM(self_flagmap));

     l = u = 0;''', '''     map_flags(&flags, &flags, self_flagmap, NELEM(self_flagmap));

     /* fast mode: ESTIMATE ranks every applicable plan instead of stopping
        at the first one marked could_prune_now_p, within a plan budget */
     if ((flags & FFTW_ESTIMATE) && %s) {
          flags &= ~FFTW_ALLOW_PRUNING;
          plnr->fc_nplan0 = plnr->nplan;
     }

     l = u = 0;''' % FAST)

# ---------------------------------------------------------------- search budget
rep('''     int nprob;    /* number of problems evaluated */
};''', '''     int nprob;    /* number of problems evaluated */
     int fc_nplan0; /* fftchode fast mode: nplan when the top-level plan began */
};''')
rep('''     p->nplan = p->nprob = 0;''', '''     p->nplan = p->nprob = 0;
     p->fc_nplan0 = 0;''')
rep('''static plan *search0(planner *ego, const problem *p, unsigned *slvndx, ''', '''/* fftchode fast mode: once a top-level ESTIMATE plan has evaluated more than
   the plan budget, the rest of its search prunes as stock ESTIMATE does.
   Highly composite sizes otherwise multiply the unpruned search. */
static int fc_over_budget(const planner *ego)
{
     return ESTIMATEP(ego) && %s
\t  && ego->nplan - ego->fc_nplan0 > ::fftchode_plan_budget();
}

static plan *search0(planner *ego, const problem *p, unsigned *slvndx, ''' % FAST)
rep('''\t       if (ALLOW_PRUNINGP(ego) && could_prune_now_p) ''', '''\t       if ((ALLOW_PRUNINGP(ego) || fc_over_budget(ego)) && could_prune_now_p) ''')

# ---------------------------------------------------------------- opcnt
rep('''typedef struct {
     double add;
     double mul;
     double fma;
     double other;
} opcnt;
''', '''typedef struct {
     double add;
     double mul;
     double fma;
     double other;
     /* fftchode fast mode: [0-3] cache lines fetched from L1, L2, L3, DRAM;
        [4] child-plan calls in loops; [5-32] codelet arithmetic by register
        width (scalar, 128, 256, 512 bits) x family (n1 n2 t1 t2 t3 q1 real);
        [33] arithmetic of codelets with radix > 16; [34] cost-unit adjustment
        for threaded plans (X(fc_thr)) */
     double fcl[35];
} opcnt;
''')
i = s.index('} tensor;'); j = s.index('\n', i) + 1
s = s[:j] + '''extern "C" void X(fc_mem)(opcnt *o, double k, INT n, INT s, INT v, INT vs, INT esz, INT vv, INT vvs);
extern "C" void X(fc_loop)(opcnt *o, const tensor *sz, const tensor *vecsz, int vdim, INT esz);
extern "C" void X(fc_vops)(opcnt *o, const char *nam, INT r, INT v, INT cnt, INT gvl, const opcnt *c);
extern "C" double X(fc_cost)(const opcnt *o);
extern "C" void X(fc_thr)(opcnt *o, int nthr);
extern "C" int X(fc_lanes)(double work, int nthr);
''' + s[j:]

rep('''     dst->add = dst->mul = dst->fma = dst->other = 0;''', '''     int i;
     dst->add = dst->mul = dst->fma = dst->other = 0;
     for (i = 0; i < 35; ++i) dst->fcl[i] = 0;''')
rep('''     dst->other = m * a->other + b->other;
}''', '''     dst->other = m * a->other + b->other;
     {
          int i;
          for (i = 0; i < 35; ++i) dst->fcl[i] = m * a->fcl[i] + b->fcl[i];
     }
}

/* fftchode fast-mode cost model.  Cache levels: L1 32 KiB (8-way, 64 sets),
   L2 1 MiB, L3 32 MiB, then DRAM; 64-byte lines. */
static int fc_level(double span)
{
     return span <= 32768.0 ? 0 : span <= 1048576.0 ? 1 : span <= 33554432.0 ? 2 : 3;
}
static INT fc_gcd(INT a, INT b) { while (b) { INT t = a % b; a = b; b = t; } return a; }

/* Memory traffic of a kernel that loops vv times (stride vvs) over v times
   (stride vs) over n elements (stride s, innermost) of esz bytes; strides in
   units of R.  Adds k times the 64-byte lines it fetches to the bucket of the
   level that holds the kernel's address span.  A line serves several elements
   when the inner stride is below a line, or when the outer stride is and the n
   lines in flight survive in L1: a power-of-two stride maps them to few sets. */
extern "C" void X(fc_mem)(opcnt *o, double k, INT n, INT s, INT v, INT vs, INT esz, INT vv, INT vvs)
{
     const double L = 64.0;
     double b1, b2, b3, g, span;
     if (n <= 1) { n = v; s = vs; v = 1; vs = 0; }
     if (n <= 1) { n = vv; s = vvs; vv = 1; vvs = 0; }
     if (n < 1) n = 1;
     if (v < 1) v = 1;
     if (vv < 1) vv = 1;
     b1 = (double)X(iabs)(s) * sizeof(R);
     b2 = (double)X(iabs)(vs) * sizeof(R);
     b3 = (double)X(iabs)(vvs) * sizeof(R);
     if (b1 < L)
          g = b1 > esz ? b1 : esz;
     else {
          INT q = (INT)(b1 / L), sets = 64;
          if ((double)q * L == b1) sets = 64 / fc_gcd(q, 64);
          if (v > 1 && b2 < L && n <= 4 * sets)
               g = b2 > esz ? b2 : esz;
          else
               g = L;
     }
     span = (n - 1) * b1 + (v - 1) * b2 + (vv - 1) * b3 + esz;
     o->fcl[fc_level(span)] += k * (double)n * (double)v * (double)vv * g / L;
}

/* A loop of calls to a child plan over dimension vdim of vecsz.  The first
   touch of each iteration's data comes from the level that holds the whole
   loop's span, not the child's, so one dense pass over the child's input and
   output moves to that level.  Also counts the calls. */
static double fc_tspan(const tensor *t, int skip, int out, double *nel)
{
     double sp = 0;
     int k;
     for (k = 0; k < t->rnk; ++k) {
          if (k == skip) continue;
          *nel *= t->dims[k].n;
          sp += (double)(t->dims[k].n - 1) * (double)X(iabs)(out ? t->dims[k].os : t->dims[k].is);
     }
     return sp;
}
extern "C" void X(fc_loop)(opcnt *o, const tensor *sz, const tensor *vecsz, int vdim, INT esz)
{
     const iodim *d = vecsz->dims + vdim;
     int out;
     for (out = 0; out < 2; ++out) {
          double nel = 1, sp, f;
          int lc, lt;
          sp = (fc_tspan(sz, -1, out, &nel) + fc_tspan(vecsz, vdim, out, &nel)) * sizeof(R) + esz;
          lc = fc_level(sp);
          lt = fc_level(sp + (double)(d->n - 1) * (double)X(iabs)(out ? d->os : d->is) * sizeof(R));
          f = (double)d->n * nel * esz / 64.0;
          if (lt > lc) { o->fcl[lt] += f; o->fcl[lc] -= f; }
     }
     o->fcl[4] += d->n;
}

/* A leaf that calls a codelet (name nam, ops c, radix r, SIMD vector length
   gvl) on v * cnt transforms.  FFTW counts v * (cnt / gvl) calls, so a SIMD
   codelet run on fewer than gvl transforms looks free; charge the partial
   vector call too.  The codelet's arithmetic is bucketed by register width
   (scalar, 128, 256, 512 bits: an operation costs more on wider registers when
   the core splits them, as Zen 4 does for 512 bits) and by codelet family (n1,
   n2, t1, t2, t3, q1, real), and codelets of radix > 16 (which spill registers)
   get an extra term. */
static int fc_width(const char *nam)
{
     const char *u = strrchr(nam, '_');
     if (!u) return 0;
     ++u;
     if (!strcmp(u, "avx512") || !strcmp(u, "kcvi")) return 3;
     if (!strcmp(u, "avx") || !strcmp(u, "avx2")) return 2;
     if (!strcmp(u, "sse2") || !strcmp(u, "128") || !strcmp(u, "fma") || !strcmp(u, "neon")
         || !strcmp(u, "altivec") || !strcmp(u, "vsx")) return 1;
     return 0;
}
static int fc_family(const char *nam)
{
     if (nam[0] == 'n') return nam[1] == '2' ? 1 : 0;
     if (nam[0] == 't') return nam[1] == '2' ? 3 : nam[1] == '3' ? 4 : 2;
     if (nam[0] == 'q') return 5;
     return 6;
}
extern "C" void X(fc_vops)(opcnt *o, const char *nam, INT r, INT v, INT cnt, INT gvl, const opcnt *c)
{
     INT k = v * ((cnt + gvl - 1) / gvl);
     double a = k * (c->add + c->mul + 2 * c->fma);
     X(ops_madd2)(k - v * (cnt / gvl), c, o);
     o->fcl[5 + 7 * fc_width(nam) + fc_family(nam)] += a;
     if (r > 16) o->fcl[33] += a;
}

/* The fast-mode cost of an operation count, in the units of X(iestimate_cost). */
extern "C" double X(fc_cost)(const opcnt *o)
{
     const double *w = ::fftchode_fcw(), *l = o->fcl;
     double a = o->add + o->mul
#if HAVE_FMA
          + o->fma;
#else
          + 2 * o->fma;
#endif
     int i;
     for (i = 5; i < 33; ++i) a -= l[i];
     a = w[0] * a + w[1] * o->other;
     for (i = 0; i < 34; ++i) a += w[2 + i] * l[i];
     return a + l[34];
}

/* o holds the summed operation counts of the nthr blocks a threaded solver
   runs in one parallel region.  The region runs on X(fc_lanes) lanes, so its
   critical path is ceil(nthr / lanes) blocks: charge that share of the cost,
   plus the cost of handing the blocks to the other lanes and joining them.
   FFTW's own counts are left alone, so fftw_flops still reports the total. */
extern "C" void X(fc_thr)(opcnt *o, int nthr)
{
     int lanes;
     if (nthr <= 1) return;
     lanes = X(fc_lanes)(o->add + o->mul + 2 * o->fma + o->other, nthr);
     if (lanes <= 1) return;
     o->fcl[34] -= (1.0 - (double)((nthr + lanes - 1) / lanes) / nthr) * X(fc_cost)(o)
	  - ::fftchode_fc_spawn();
}''')

# ---------------------------------------------------------------- estimator
rep('''\t  + pln->ops.other;
     if (ego->cost_hook)''', '''\t  + pln->ops.other;
     if (%s) {
          cost = X(fc_cost)(&pln->ops);
#ifdef FFTCHODE_PLANNER_CALIBRATE
          if (::fftchode_rand_[0] > 0) {
               md5 m;
               X(md5begin)(&m);
               X(md5unsigned)(&m, (unsigned)::fftchode_rand_[1]);
               p->adt->hash(p, &m);
               X(md5putb)(&m, &pln->ops, sizeof(pln->ops));
               X(md5putb)(&m, &pln->adt, sizeof(pln->adt));
               X(md5end)(&m);
               cost *= std::exp(::fftchode_rand_[0] * (2.0 * (double)(m.s[0] & 0xffffff) / 16777216.0 - 1.0));
          }
#endif
     }
     if (ego->cost_hook)''' % FAST)

rep('''extern "C" double X(estimate_cost)(const X(plan) p)
{
     return X(iestimate_cost)(X(the_planner)(), p->pln, p->prb);
}''', '''extern "C" double X(estimate_cost)(const X(plan) p)
{
     return X(iestimate_cost)(X(the_planner)(), p->pln, p->prb);
}
#ifdef FFTCHODE_PLANNER_CALIBRATE
extern "C" void X(fc_dev_ops)(const X(plan) p, double *o)
{
     const opcnt *c = &p->pln->ops;
     int i;
     o[0] = c->add; o[1] = c->mul; o[2] = c->fma; o[3] = c->other;
     for (i = 0; i < 34; ++i) o[4 + i] = c->fcl[i];
}
#endif''')

# ---------------------------------------------------------------- leaves
# dft/direct.c (n1 codelets)
rep('''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * pln->n * pln->vl;
''', '''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * pln->n * pln->vl;
     if (%s) {
\t  X(fc_mem)(%s, 1, pln->n, d[0].is, pln->vl, pln->ivs, 2 * sizeof(R), 1, 0);
\t  X(fc_mem)(%s, 1, pln->n, d[0].os, pln->vl, pln->ovs, 2 * sizeof(R), 1, 0);
\t  X(fc_vops)(%s, e->nam, pln->n, 1, pln->vl, e->genus->vl, &e->ops);
     }
''' % (FAST, O, O, O))
# dft/dftw-direct.c (t1/t2 codelets)
rep('''     if (ego->bufferedp) {
\t  /* 8 load/stores * N * V */
\t  pln->super.super.ops.other += 8 * r * mcount * v;
     }
''', '''     if (ego->bufferedp) {
\t  /* 8 load/stores * N * V */
\t  pln->super.super.ops.other += 8 * r * mcount * v;
     }
     if (%s) {
\t  X(fc_mem)(%s, 2, r, irs, mcount, ms, 2 * sizeof(R), v, ivs);
\t  X(fc_mem)(%s, v, X(twiddle_length)(r, e->tw) * mcount, 1, 1, 0, sizeof(R), 1, 0);
\t  X(fc_vops)(%s, e->nam, r, v, mcount, e->genus->vl, &e->ops);
     }
''' % (FAST, O, O, O))
# dft/dftw-directsq.c (q1 codelets)
rep('''     X(ops_zero)(&pln->super.super.ops);
     X(ops_madd2)(mcount/e->genus->vl, &e->ops, &pln->super.super.ops);

     return &(pln->super.super);''', '''     X(ops_zero)(&pln->super.super.ops);
     X(ops_madd2)(mcount/e->genus->vl, &e->ops, &pln->super.super.ops);
     if (%s) {
\t  X(fc_mem)(%s, 2, r, irs, mcount, ms, 2 * sizeof(R), v, ivs);
\t  X(fc_mem)(%s, 1, X(twiddle_length)(r, e->tw) * mcount, 1, 1, 0, sizeof(R), 1, 0);
\t  X(fc_vops)(%s, e->nam, r, 1, mcount, e->genus->vl, &e->ops);
     }

     return &(pln->super.super);''' % (FAST, O, O, O))
# dft/dftw-generic.c
rep('''\t  double n0 = (r - 1) * (mcount - 1) * v;
\t  pln->super.super.ops = cld->ops;
\t  pln->super.super.ops.mul += 8 * n0;
\t  pln->super.super.ops.add += 4 * n0;
\t  pln->super.super.ops.other += 8 * n0;
''', '''\t  double n0 = (r - 1) * (mcount - 1) * v;
\t  pln->super.super.ops = cld->ops;
\t  pln->super.super.ops.mul += 8 * n0;
\t  pln->super.super.ops.add += 4 * n0;
\t  pln->super.super.ops.other += 8 * n0;
\t  if (%s) {
\t       X(fc_mem)(%s, 2, r, irs, mcount, ms, 2 * sizeof(R), v, ivs);
\t       X(fc_mem)(%s, v, 2 * r * mcount, 1, 1, 0, sizeof(R), 1, 0);
\t  }
''' % (FAST, O, O))
# dft/dftw-genericbuf.c
rep('''\t  double n0 = (r - 1) * (mcount - 1);
\t  pln->super.super.ops = cld->ops;
\t  pln->super.super.ops.mul += 8 * n0;
\t  pln->super.super.ops.add += 4 * n0;
\t  pln->super.super.ops.other += 8 * n0;
''', '''\t  double n0 = (r - 1) * (mcount - 1);
\t  pln->super.super.ops = cld->ops;
\t  pln->super.super.ops.mul += 8 * n0;
\t  pln->super.super.ops.add += 4 * n0;
\t  pln->super.super.ops.other += 8 * n0;
\t  if (%s) {
\t       X(fc_mem)(%s, 2, r, irs, mcount, ms, 2 * sizeof(R), 1, 0);
\t       X(fc_mem)(%s, 1, 2 * r * mcount, 1, 1, 0, sizeof(R), 1, 0);
\t  }
''' % (FAST, O, O))
# rdft/direct-r2c.c
rep('''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 2 * n * pln->vl;
''', '''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 2 * n * pln->vl;
     if (%s) {
\t  INT rvs = R2HC_KINDP(p->kind[0]) ? pln->ivs : pln->ovs;
\t  INT cvs = R2HC_KINDP(p->kind[0]) ? pln->ovs : pln->ivs;
\t  X(fc_mem)(%s, 1, n, rs, pln->vl, rvs, sizeof(R), 1, 0);
\t  X(fc_mem)(%s, 1, n, cs, pln->vl, cvs, sizeof(R), 1, 0);
\t  X(fc_vops)(%s, ego->desc->nam, n, 1, pln->vl, ego->desc->genus->vl, &ego->desc->ops);
     }
''' % (FAST, O, O, O))
# rdft/direct2.c
rep('''     if (p->kind == R2HC)
\t  pln->super.super.ops.other += 2 * pln->vl; /* + 2 stores */
''', '''     if (p->kind == R2HC)
\t  pln->super.super.ops.other += 2 * pln->vl; /* + 2 stores */
     if (%s) {
\t  INT rvs = r2hc_kindp ? pln->ivs : pln->ovs;
\t  INT cvs = r2hc_kindp ? pln->ovs : pln->ivs;
\t  X(fc_mem)(%s, 1, d->n, r2hc_kindp ? d->is : d->os, pln->vl, rvs, sizeof(R), 1, 0);
\t  X(fc_mem)(%s, 1, d->n / 2 + 1, r2hc_kindp ? d->os : d->is, pln->vl, cvs, 2 * sizeof(R), 1, 0);
\t  X(fc_vops)(%s, ego->desc->nam, d->n, 1, pln->vl, ego->desc->genus->vl, &ego->desc->ops);
     }
''' % (FAST, O, O, O))
# rdft/ct-hc2c-direct.c
rep('''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * r * m * v;
''', '''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * r * m * v;
     if (%s) {
\t  X(fc_mem)(%s, 2, r, rs, m, ms, 2 * sizeof(R), v, vs);
\t  X(fc_mem)(%s, v, X(twiddle_length)(r, e->tw) * ((m - 1) / 2), 1, 1, 0, sizeof(R), 1, 0);
\t  X(fc_vops)(%s, e->nam, r, v, (m - 1) / 2, e->genus->vl, &e->ops);
     }
''' % (FAST, O, O, O))
# rdft/hc2hc-direct.c
rep('''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * r * (pln->me - pln->mb) * v;
''', '''     if (ego->bufferedp) 
\t  pln->super.super.ops.other += 4 * r * (pln->me - pln->mb) * v;
     if (%s) {
\t  X(fc_mem)(%s, 2, r, rs, pln->me - pln->mb, ms, sizeof(R), v, vs);
\t  X(fc_mem)(%s, v, X(twiddle_length)(r, e->tw) * (pln->me - pln->mb), 1, 1, 0, sizeof(R), 1, 0);
\t  X(fc_vops)(%s, e->nam, r, v, pln->me - pln->mb, e->genus->vl, &e->ops);
     }
''' % (FAST, O, O, O))
# rdft/rank0.c copies and transposes
rep('''     /* X(tensor_sz)(p->vecsz) loads, X(tensor_sz)(p->vecsz) stores */
     X(ops_other)(2 * X(tensor_sz)(p->vecsz), &pln->super.super.ops);
     return &(pln->super.super);''', '''     /* X(tensor_sz)(p->vecsz) loads, X(tensor_sz)(p->vecsz) stores */
     X(ops_other)(2 * X(tensor_sz)(p->vecsz), &pln->super.super.ops);
     if (%s) {
\t  rdftapply a = ego->apply;
\t  opcnt *o = &pln->super.super.ops;
\t  INT esz = pln->vl * (INT)sizeof(R), nall = X(tensor_sz)(p->vecsz) / pln->vl;
\t  if (pln->rnk == 0)
\t       X(fc_mem)(o, 2, pln->vl, 1, 1, 0, sizeof(R), 1, 0);
\t  else if (a == apply_tiled || a == apply_tiledbuf
\t\t   || a == apply_ip_sq_tiled || a == apply_ip_sq_tiledbuf)
\t       X(fc_mem)(o, (a == apply_tiled || a == apply_tiledbuf) ? 2 : 4,
\t\t\t nall * pln->vl, 1, 1, 0, sizeof(R), 1, 0);
\t  else {
\t       /* innermost loop: smallest input stride (iter-ci, ip-sq), smallest
\t\t  output stride (iter-co), or the last dimension (memcpy-loop) */
\t       int side, k, ki = pln->rnk - 1, ko;
\t       for (k = 0; k < pln->rnk; ++k) {
\t\t    INT sk = (a == apply_cpy2dco) ? pln->d[k].os : pln->d[k].is;
\t\t    INT si = (a == apply_cpy2dco) ? pln->d[ki].os : pln->d[ki].is;
\t\t    if (a != apply_memcpy_loop && X(iabs)(sk) < X(iabs)(si)) ki = k;
\t       }
\t       for (side = 0; side < 2; ++side) {
\t\t    INT si = side ? pln->d[ki].os : pln->d[ki].is, so = 0;
\t\t    ko = -1;
\t\t    for (k = 0; k < pln->rnk; ++k) {
\t\t\t INT sk = side ? pln->d[k].os : pln->d[k].is;
\t\t\t if (k != ki && (ko < 0 || X(iabs)(sk) < X(iabs)(so))) { ko = k; so = sk; }
\t\t    }
\t\t    X(fc_mem)(o, a == apply_ip_sq ? 2 : 1, pln->d[ki].n, si,
\t\t\t      nall / pln->d[ki].n, so, esz, 1, 0);
\t       }
\t  }
     }
     return &(pln->super.super);''' % FAST)
# vrank-geq1 loops: dft (complex elements), rdft and rdft2 (real elements)
old = '''     pln->super.super.ops.other = 3.14159; /* magic to prefer codelet loops */
     X(ops_madd2)(pln->vl, &cld->ops, &pln->super.super.ops);'''
assert s.count(old) == 3
for esz in ('2 * sizeof(R)', 'sizeof(R)', 'sizeof(R)'):
    a = s.index(old)
    s = s[:a] + old.replace('magic', 'MAGIC') + '''
     if (%s)
\t  X(fc_loop)(%s, p->sz, p->vecsz, vdim, %s);''' % (FAST, O, esz) + s[a + len(old):]
s = s.replace('3.14159; /* MAGIC to prefer', '3.14159; /* magic to prefer')

# ---------------------------------------------------------------- fftchode SIMD solvers (opt/simd), if present
if 'XSIMD_STRING("dft-colpack")' in s and os.environ.get('COLPACK_ESTIMATE') == '1':
    rep('''     if (!(1
\t   && !ESTIMATEP(plnr)
\t   && !NO_SIMDP(plnr)
\t   && !NO_BUFFERINGP(plnr)
\t   && p->sz->rnk == 1
\t   && p->vecsz->rnk == 1
\t   && p->sz->dims[0].n >= 2
\t   && p->vecsz->dims[0].is == 2''', '''     if (!(1
\t   && !NO_SIMDP(plnr)
\t   && !NO_BUFFERINGP(plnr)
\t   && p->sz->rnk == 1
\t   && p->vecsz->rnk == 1
\t   && p->sz->dims[0].n >= 2
\t   && p->vecsz->dims[0].is == 2''')
if 'XSIMD_STRING("dft-colpack")' in s:
    rep('''     /* per VL columns and point: a load and a store in each direction */
     pln->super.super.ops.other += (double)(ncols / VL) * n * 4;''', '''     /* per VL columns and point: a load and a store in each direction */
     pln->super.super.ops.other += (double)(ncols / VL) * n * 4;
     /* fast-mode cost model: pack reads b adjacent columns per point, unpack writes them */
     X(fc_mem)(&pln->super.super.ops, 1, b, 2, n, is, 2 * sizeof(R), ncols / b, 2 * b);
     X(fc_mem)(&pln->super.super.ops, 1, b, 2, n, os, 2 * sizeof(R), ncols / b, 2 * b);''')
if 'XSIMD_STRING("rdft2-pair")' in s:
    rep('''     pln->super.super.ops.other += (double)(npairs / VL) * (2 * n * (pln->simd ? 1 : VL) + 4 * (n / 2 + 1));''',
        '''     pln->super.super.ops.other += (double)(npairs / VL) * (2 * n * (pln->simd ? 1 : VL) + 4 * (n / 2 + 1));
     /* fast-mode cost model: real side n points x 2 b2 columns, complex side n/2+1 x 2 b2 */
     X(fc_mem)(&pln->super.super.ops, 1, 2 * b2, rv, n, rs, sizeof(R), npairs / b2, 2 * b2 * rv);
     X(fc_mem)(&pln->super.super.ops, 1, 2 * b2, cv, n / 2 + 1, cs, 2 * sizeof(R), npairs / b2, 2 * b2 * cv);''')

# ---------------------------------------------------------------- threaded solvers
# the fftchode pool runs vrank blocks whose outputs start under a line apart on the caller
VGUARD = ' && X(iabs)(pln->ots) * (INT)sizeof(R) >= 64' if 'static int max_lanes = 1;' in s else ''
rep('''     for (i = 0; i < nthr; ++i) {
\t  X(ops_add2)(&cldrn[i]->ops, &pln->super.super.ops);
\t  pln->super.super.pcost += cldrn[i]->pcost;
     }
''', '''     for (i = 0; i < nthr; ++i) {
\t  X(ops_add2)(&cldrn[i]->ops, &pln->super.super.ops);
\t  pln->super.super.pcost += cldrn[i]->pcost;
     }
     if (%s%s)
\t  X(fc_thr)(&pln->super.super.ops, nthr);
''' % (FAST, VGUARD), 3)
rep('''\t  pln->super.super.could_prune_now_p |= cldws[i]->could_prune_now_p;
     }
     X(ops_add2)(&cld->ops, &pln->super.super.ops);
''', '''\t  pln->super.super.could_prune_now_p |= cldws[i]->could_prune_now_p;
     }
     if (%s)
\t  X(fc_thr)(&pln->super.super.ops, nthr);
     X(ops_add2)(&cld->ops, &pln->super.super.ops);
''' % FAST, 2)

# ---------------------------------------------------------------- lanes of a parallel region
if 'static int max_lanes = 1;' in s:
    rep('''extern "C" double X(spawn_work)(plan **plns, int n)
{''', '''/* fftchode fast-mode cost model: the lanes X(spawn_loop_work) would run nthr
   blocks of the given work on, with every spare lane free. */
extern "C" int X(fc_lanes)(double work, int nthr)
{
     int nlanes = nthr;
     if (work < min_work * nthr)
\t  nlanes = work < 2 * min_work ? 1 : (int)(work / min_work);
     return nlanes < max_lanes ? nlanes : max_lanes;
}

extern "C" double X(spawn_work)(plan **plns, int n)
{''')
else:
    rep('''extern "C" void X(fc_thr)(opcnt *o, int nthr)
{''', '''extern "C" int X(fc_lanes)(double work, int nthr)
{
     static int n = 0;
     (void)work;
     if (!n) {
#if defined(__linux__)
\t  cpu_set_t set;
\t  if (sched_getaffinity(0, sizeof(set), &set) == 0)
\t       n = CPU_COUNT(&set);
#endif
\t  if (n <= 0)
\t       n = (int)std::thread::hardware_concurrency();
\t  if (n <= 0)
\t       n = 1;
     }
     return nthr < n ? nthr : n;
}

extern "C" void X(fc_thr)(opcnt *o, int nthr)
{''')
    rep('''#include <thread>
#define FFTCHODE_FC_SPAWN''', '''#include <thread>
#if defined(__linux__)
#include <sched.h>
#endif
#define FFTCHODE_FC_SPAWN''')

open(p, 'w').write(s)
print('planner patch applied to', p)
