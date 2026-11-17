/* Hand-written replacement for the autoconf config.h that benchFFT's libbench expects. */
#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_ALLOCA_H 1
#define HAVE_MALLOC_H 1
#define HAVE_MALLOC 1
#define HAVE_MEMALIGN 1
#define HAVE_DRAND48 1
#define HAVE_DECL_DRAND48 1
#define HAVE_ISNAN 1
#define HAVE_GETTIMEOFDAY 1
#define F77_FUNC(name, NAME) name##_
#define F77_FUNC_(name, NAME) name##_
#endif
