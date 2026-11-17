/* Heap accounting by link-time interposition (ld --wrap).  Linked only into the "-mem"
 * binaries.  Every malloc-family call made by the benchmark code, the FFT library, and
 * libstdc++ (operator new) is routed here; the live byte count is the sum of
 * malloc_usable_size() of the blocks currently allocated.  Memory obtained by other means
 * (thread stacks, static/bss data, direct mmap) is not counted; use ru_maxrss for those. */
#define _GNU_SOURCE
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
int __real_posix_memalign(void **, size_t, size_t);
void *__real_aligned_alloc(size_t, size_t);
void *__real_memalign(size_t, size_t);

static long live_bytes, peak_bytes, n_allocs;

static void add(void *p)
{
     long l, pk;
     if (!p) return;
     l = __atomic_add_fetch(&live_bytes, (long) malloc_usable_size(p), __ATOMIC_RELAXED);
     __atomic_add_fetch(&n_allocs, 1, __ATOMIC_RELAXED);
     pk = __atomic_load_n(&peak_bytes, __ATOMIC_RELAXED);
     while (l > pk && !__atomic_compare_exchange_n(&peak_bytes, &pk, l, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
          ;
}
static void sub(void *p)
{
     if (p) __atomic_sub_fetch(&live_bytes, (long) malloc_usable_size(p), __ATOMIC_RELAXED);
}

void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); add(p); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); add(p); return p; }
void *__wrap_realloc(void *q, size_t n)
{
     void *p;
     sub(q);
     p = __real_realloc(q, n);
     if (p) add(p); else add(q);
     return p;
}
void __wrap_free(void *p) { sub(p); __real_free(p); }
int __wrap_posix_memalign(void **pp, size_t al, size_t n)
{
     int r = __real_posix_memalign(pp, al, n);
     if (r == 0) add(*pp);
     return r;
}
void *__wrap_aligned_alloc(size_t al, size_t n) { void *p = __real_aligned_alloc(al, n); add(p); return p; }
void *__wrap_memalign(size_t al, size_t n) { void *p = __real_memalign(al, n); add(p); return p; }

int mt_enabled(void) { return 1; }
long mt_live(void) { return __atomic_load_n(&live_bytes, __ATOMIC_RELAXED); }
long mt_peak(void) { return __atomic_load_n(&peak_bytes, __ATOMIC_RELAXED); }
long mt_count(void) { return __atomic_load_n(&n_allocs, __ATOMIC_RELAXED); }
void mt_reset_peak(void) { __atomic_store_n(&peak_bytes, __atomic_load_n(&live_bytes, __ATOMIC_RELAXED), __ATOMIC_RELAXED); }
