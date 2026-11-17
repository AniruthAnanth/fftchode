/*
 * Benchmark driver on top of benchFFT's libbench.
 *
 * libbench supplies the problem description/allocation/copy routines, the benchee
 * interface (can_do/setup/doit/done) and the verification code; this driver replaces
 * libbench's bench-main/speed/timer/report so that every timing trial is recorded and
 * written as CSV, plan time is measured separately, each (kind, size) cell runs in its own
 * forked process (so a crash or hang only loses that cell), and memory can be measured.
 *
 * Speed methodology is benchFFT's: double the iteration count until one batch takes at
 * least time_min, then time `trials` batches; mflops = 5 N log2 N / t (complex) or
 * 2.5 N log2 N / t (real), where t is the time of one transform.
 */
#define _GNU_SOURCE
#include "bench.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int verbose = 0;
int paranoid = 0;

extern const char *na_reason __attribute__((weak));
extern const char *benchee_info(void) __attribute__((weak));
extern int mt_enabled(void);
extern long mt_live(void), mt_peak(void), mt_count(void);
extern void mt_reset_peak(void);

static const char *lib = "?";
static int direct;
static char **self_argv;
static int threads = 1;
static int trials = 5;
static double time_min_s = 0.1;
static int timeout_s = 600;
static int estimate = 1;

static const char *prec_name(void) { return DOUBLE_PRECISION ? "double" : "single"; }
static const char *kind_name(int real) { return real ? "r2c" : "c2c"; }

static double now(void)
{
     struct timespec ts;
     clock_gettime(CLOCK_MONOTONIC, &ts);
     return (double) ts.tv_sec + 1e-9 * (double) ts.tv_nsec;
}

static int cmpd(const void *a, const void *b)
{
     double x = *(const double *) a, y = *(const double *) b;
     return (x > y) - (x < y);
}

static double median(const double *v, int n)
{
     double *c = malloc(n * sizeof(double)), m;
     memcpy(c, v, n * sizeof(double));
     qsort(c, n, sizeof(double), cmpd);
     m = (n & 1) ? c[n / 2] : 0.5 * (c[n / 2 - 1] + c[n / 2]);
     free(c);
     return m;
}

/* Out-of-place is tried first; libraries that only do in-place (FFTE) get in-place. */
static struct problem *make_problem(int real, unsigned n, const char **placement)
{
     char buf[64];
     struct problem *p;

     snprintf(buf, sizeof buf, "of%c%u", real ? 'r' : 'c', n);
     p = problem_parse(buf);
     *placement = "out-of-place";
     if (can_do(p)) return p;
     problem_destroy(p);
     snprintf(buf, sizeof buf, "if%c%u", real ? 'r' : 'c', n);
     p = problem_parse(buf);
     *placement = "in-place";
     if (can_do(p)) return p;
     problem_destroy(p);
     return 0;
}

static void fill_random(struct problem *p)
{
     unsigned i;
     problem_zero(p);
     if (p->kind == PROBLEM_COMPLEX) {
          bench_complex *a = p->in;
          for (i = 0; i < p->phys_size; ++i) { c_re(a[i]) = bench_drand(); c_im(a[i]) = bench_drand(); }
     } else {
          bench_real *a = p->in;
          for (i = 0; i < p->phys_size; ++i) a[i] = bench_drand();
     }
}

static void configure_benchee(void)
{
     char buf[32];
     snprintf(buf, sizeof buf, "threads=%d", threads);
     useropt(buf);
     if (estimate) useropt("estimate");
}

static void print_prefix(int real, unsigned n)
{
     printf("%s,%s,%s,%d,%u,", lib, prec_name(), kind_name(real), threads, n);
}

static int nempty;

static void status_row(int real, unsigned n, const char *placement, const char *status, const char *note)
{
     int i;
     print_prefix(real, n);
     printf("%s,%s", placement, status);
     for (i = 0; i < nempty; ++i) putchar(',');
     printf(",%s\n", note);
}

static void na_row(int real, unsigned n, const char *why)
{
     status_row(real, n, "-", "n/a", why);
}

static const char *why_na(void)
{
     return (&na_reason && na_reason && *na_reason) ? na_reason : "not supported by this library";
}

/* ---------------- speed ---------------- */
static void cell_speed(int real, unsigned n)
{
     const char *placement;
     struct problem *p = make_problem(real, n, &placement);
     double *t, tt, plan_cold, planw[3], tmed, tbest, tworst;
     int iter, k;

     if (!p) { na_row(real, n, why_na()); return; }
     configure_benchee();
     problem_alloc(p);
     fill_random(p);

     tt = now(); setup(p); plan_cold = now() - tt;

     for (iter = 1;; iter *= 2) {
          tt = now(); doit(iter, p); tt = now() - tt;
          if (tt >= time_min_s || iter >= (1 << 28)) break;
     }
     t = malloc(trials * sizeof(double));
     for (k = 0; k < trials; ++k) {
          tt = now(); doit(iter, p); t[k] = (now() - tt) / iter;
     }
     done(p);
     for (k = 0; k < 3; ++k) {
          tt = now(); setup(p); planw[k] = now() - tt;
          done(p);
     }

     tmed = median(t, trials);
     tbest = tworst = t[0];
     for (k = 1; k < trials; ++k) {
          if (t[k] < tbest) tbest = t[k];
          if (t[k] > tworst) tworst = t[k];
     }
     print_prefix(real, n);
     printf("%s,ok,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,", placement, iter, trials,
            mflops(p, tmed), mflops(p, tbest), mflops(p, tworst), tmed, tbest, plan_cold, median(planw, 3));
     for (k = 0; k < trials; ++k) printf("%s%.6g", k ? ";" : "", t[k]);
     printf(",\n");
     free(t);
     problem_destroy(p);
}

/* ---------------- verify ---------------- */
static void cell_verify(int real, unsigned n)
{
     const char *placement;
     struct problem *p = make_problem(real, n, &placement);
     char buf[64];
     if (!p) { na_row(real, n, why_na()); return; }
     snprintf(buf, sizeof buf, "%cf%c%u", placement[0] == 'i' ? 'i' : 'o', real ? 'r' : 'c', n);
     problem_destroy(p);
     configure_benchee();
     verify(buf, 3, SINGLE_PRECISION ? 1.0e-3 : 1.0e-10);
     status_row(real, n, placement, "verified", "");
}

/* ---------------- memory ---------------- */
static long proc_status_kb(const char *key)
{
     FILE *f = fopen("/proc/self/status", "r");
     char line[256];
     long v = -1;
     size_t k = strlen(key);
     if (!f) return -1;
     while (fgets(line, sizeof line, f))
          if (!strncmp(line, key, k)) { v = atol(line + k); break; }
     fclose(f);
     return v;
}

static void cell_mem(int real, unsigned n)
{
     const char *placement;
     struct problem *p = make_problem(real, n, &placement);
     long live0, peak_plan, live_plan, peak_exec, rss0, count0, bufbytes;
     struct rusage ru;

     if (!p) { na_row(real, n, why_na()); return; }
     configure_benchee();
     problem_alloc(p);
     fill_random(p);
     bufbytes = (long) p->phys_size * (real ? sizeof(bench_real) : sizeof(bench_complex)) * (p->in_place ? 1 : 2);
     rss0 = proc_status_kb("VmRSS:");
     live0 = mt_live();
     count0 = mt_count();
     mt_reset_peak();
     setup(p);
     live_plan = mt_live();
     peak_plan = mt_peak();
     doit(1, p);
     peak_exec = mt_peak();
     getrusage(RUSAGE_SELF, &ru);
     print_prefix(real, n);
     printf("%s,ok,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,\n", placement, bufbytes, live0, live_plan - live0,
            peak_plan - live0, peak_exec - live0, mt_count() - count0, rss0, proc_status_kb("VmHWM:"),
            (long) ru.ru_maxrss);
     done(p);
     problem_destroy(p);
}

typedef void (*cell_fn)(int real, unsigned n);

static void run_cell(cell_fn fn, int real, unsigned n)
{
     pid_t pid;
     int st;
     if (direct) { fn(real, n); fflush(stdout); return; }
     fflush(stdout);
     pid = fork();
     if (pid < 0) { perror("fork"); exit(2); }
     if (pid == 0) {
          alarm(timeout_s);
          if (fn == cell_mem) {
               /* fresh exec so VmHWM / ru_maxrss describe only this cell, not the forking parent */
               char sz[32], th[32], tm[32], *av[16];
               int k = 0;
               snprintf(sz, sizeof sz, "%u", n);
               snprintf(th, sizeof th, "%d", threads);
               snprintf(tm, sizeof tm, "%d", timeout_s);
               av[k++] = self_argv[0]; av[k++] = "--direct"; av[k++] = "--mode"; av[k++] = "mem";
               av[k++] = "--lib"; av[k++] = (char *) lib; av[k++] = "--threads"; av[k++] = th;
               av[k++] = "--sizes"; av[k++] = sz; av[k++] = "--kinds"; av[k++] = real ? "r" : "c";
               if (!estimate) av[k++] = "--no-estimate";
               av[k] = 0;
               execv("/proc/self/exe", av);
               _exit(127);
          }
          fn(real, n);
          fflush(stdout);
          _exit(0);
     }
     while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
     if (WIFSIGNALED(st)) {
          int to = WTERMSIG(st) == SIGALRM;
          char why[64];
          snprintf(why, sizeof why, "signal %d", WTERMSIG(st));
          status_row(real, n, "-", to ? "timeout" : "crash", why);
     } else if (WEXITSTATUS(st)) {
          char why[64];
          snprintf(why, sizeof why, "exit status %d", WEXITSTATUS(st));
          status_row(real, n, "-", "fail", why);
     }
     fflush(stdout);
}

/* Returns 0 if benchFFT's verification passes (or the cell is n/a, which the cell reports itself),
 * nonzero otherwise.  Runs in its own process because libbench's verify() exits on failure. */
static int verify_gate(int real, unsigned n)
{
     pid_t pid;
     int st;
     fflush(stdout);
     pid = fork();
     if (pid < 0) { perror("fork"); exit(2); }
     if (pid == 0) {
          const char *placement;
          struct problem *p;
          char buf[64];
          if (!freopen("/dev/null", "w", stdout)) _exit(4);
          if (!freopen("/dev/null", "w", stderr)) _exit(4);
          alarm(timeout_s);
          p = make_problem(real, n, &placement);
          if (!p) _exit(0);
          snprintf(buf, sizeof buf, "%cf%c%u", placement[0] == 'i' ? 'i' : 'o', real ? 'r' : 'c', n);
          problem_destroy(p);
          configure_benchee();
          verify(buf, 3, SINGLE_PRECISION ? 1.0e-3 : 1.0e-10);
          _exit(0);
     }
     while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
     return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static void drv_usage(void)
{
     fprintf(stderr,
             "usage: driver --lib NAME --mode speed|mem|verify [--threads T] [--sizes \"n n ...\"]\n"
             "              [--kinds \"c r\"] [--trials N] [--time-min S] [--timeout S] [--header]\n"
             "              [--no-estimate] [--no-verify]\n");
     exit(2);
}

int main(int argc, char **argv)
{
     const char *mode = "speed", *sizes = "1024", *kinds = "c r";
     int i, header = 0, gate = 1;
     cell_fn fn;
     char *s, *tok, *save;

     bench_srand(1);
     self_argv = argv;
     for (i = 1; i < argc; ++i) {
          const char *a = argv[i];
          if (!strcmp(a, "--header")) header = 1;
          else if (!strcmp(a, "--no-estimate")) estimate = 0;
          else if (!strcmp(a, "--no-verify")) gate = 0;
          else if (!strcmp(a, "--direct")) direct = 1;
          else if (i + 1 >= argc) drv_usage();
          else if (!strcmp(a, "--lib")) lib = argv[++i];
          else if (!strcmp(a, "--mode")) mode = argv[++i];
          else if (!strcmp(a, "--threads")) threads = atoi(argv[++i]);
          else if (!strcmp(a, "--sizes")) sizes = argv[++i];
          else if (!strcmp(a, "--kinds")) kinds = argv[++i];
          else if (!strcmp(a, "--trials")) trials = atoi(argv[++i]);
          else if (!strcmp(a, "--time-min")) time_min_s = atof(argv[++i]);
          else if (!strcmp(a, "--timeout")) timeout_s = atoi(argv[++i]);
          else drv_usage();
     }
     if (trials < 1 || threads < 1) drv_usage();
     if (strcmp(mode, "speed")) gate = 0;
     if (!strcmp(mode, "info")) {
          printf("%s,%s,%s\n", lib, prec_name(), benchee_info ? benchee_info() : "");
          return 0;
     }

     if (!strcmp(mode, "speed")) { fn = cell_speed; nempty = 10; }
     else if (!strcmp(mode, "mem")) { fn = cell_mem; nempty = 9; }
     else if (!strcmp(mode, "verify")) { fn = cell_verify; nempty = 0; }
     else drv_usage();

     if (header) {
          if (fn == cell_speed)
               puts("lib,precision,kind,threads,n,placement,status,iters,trials,mflops_median,mflops_best,mflops_worst,"
                    "t_median_s,t_best_s,plan_cold_s,plan_warm_s,trial_times_s,note");
          else if (fn == cell_mem)
               puts("lib,precision,kind,threads,n,placement,status,data_buffer_bytes,heap_before_plan_bytes,"
                    "heap_plan_live_bytes,heap_peak_plan_bytes,heap_peak_plan_exec_bytes,malloc_calls,"
                    "rss_before_plan_kb,vmhwm_kb,ru_maxrss_kb,note");
          else
               puts("lib,precision,kind,threads,n,placement,status,note");
     }

     s = strdup(kinds);
     for (tok = strtok_r(s, " ,", &save); tok; tok = strtok_r(0, " ,", &save)) {
          char *sz = strdup(sizes), *t2, *save2;
          int real = (tok[0] == 'r');
          for (t2 = strtok_r(sz, " ,", &save2); t2; t2 = strtok_r(0, " ,", &save2)) {
               unsigned n = (unsigned) strtoul(t2, 0, 10);
               if (gate && verify_gate(real, n)) {
                    status_row(real, n, "-", "verify-fail", "fails benchFFT verification, not timed");
                    continue;
               }
               run_cell(fn, real, n);
          }
          free(sz);
     }
     free(s);
     return 0;
}
