/* malloc_count.c — LD_PRELOAD allocation counter for benchmarks/sqlite.
 *
 *   LD_PRELOAD=build/sqlite-bench/malloc_count.so MALLOC_TAG=full ./bench
 *   ... MALLOC_TAG=setup BENCH_ITERS=0 ./bench
 *
 * Prints one line at exit:
 *
 *   MALLOC tag=<tag> malloc=<n> calloc=<n> realloc=<n> free=<n> bytes=<n>
 *
 * run.sh subtracts the setup run from the full run to get the marginal cost
 * per measured operation. Only glibc's allocator is used by SQLite and the
 * HolyPHP runtime, so wrapping these four entry points is enough.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);

static unsigned long g_malloc, g_calloc, g_realloc, g_free, g_bytes;
static volatile int g_off;

void *malloc(size_t n) {
    void *p = __libc_malloc(n);
    if (p && !g_off) {
        __atomic_fetch_add(&g_malloc, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_bytes, (unsigned long)n, __ATOMIC_RELAXED);
    }
    return p;
}

void *calloc(size_t n, size_t sz) {
    void *p = __libc_calloc(n, sz);
    if (p && !g_off) {
        __atomic_fetch_add(&g_calloc, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_bytes, (unsigned long)(n * sz), __ATOMIC_RELAXED);
    }
    return p;
}

void *realloc(void *old, size_t n) {
    void *p = __libc_realloc(old, n);
    if (p && !g_off) __atomic_fetch_add(&g_realloc, 1, __ATOMIC_RELAXED);
    return p;
}

void free(void *p) {
    if (p && !g_off) __atomic_fetch_add(&g_free, 1, __ATOMIC_RELAXED);
    __libc_free(p);
}

__attribute__((destructor))
static void report(void) {
    const char *tag = getenv("MALLOC_TAG");
    char buf[256];
    int n;
    g_off = 1;
    if (!tag) tag = "-";
    n = snprintf(buf, sizeof buf,
                 "MALLOC tag=%s malloc=%lu calloc=%lu realloc=%lu free=%lu bytes=%lu\n",
                 tag, g_malloc, g_calloc, g_realloc, g_free, g_bytes);
    if (n > 0) (void)!write(2, buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf));
}
