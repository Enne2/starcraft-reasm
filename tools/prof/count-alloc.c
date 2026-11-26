/* count-alloc.c — LD_PRELOAD interposer to count allocation churn of the game process.
 *
 * Build (the game is i686, so the preload must be i686):
 *   clang --target=i686-unknown-linux-gnu -m32 -fPIC -shared -O1 -o count-alloc.so count-alloc.c -ldl
 *
 * Usage: SC_ALLOC_LOG=/tmp/alloc.txt LD_PRELOAD=./count-alloc.so ./starcraft-x86
 *
 * Every second it appends one line: t=<sec> malloc=<n> free=<n> calloc=<n> realloc=<n>
 * mmap=<n> munmap=<n> bytes=<total bytes requested> peak_live=<bytes in flight>.
 * Overhead is one atomic increment per call plus a write once per second.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>

static void *(*real_malloc)(size_t);
static void  (*real_free)(void *);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static int   (*real_munmap)(void *, size_t);

static unsigned long c_malloc, c_free, c_calloc, c_realloc, c_mmap, c_munmap;
static unsigned long long bytes_total, live_bytes, peak_live;
static unsigned long sz_bucket[9];   /* <=64,256,1K,4K,16K,64K,256K,1M,>1M */
/* attribuzione: chi chiama malloc (return address immediato), tabella con sonde lineari */
#define CSZ 512
static struct { void *caller; unsigned long n; unsigned long bytes; } callers[CSZ];

static void caller_add(void *ra, size_t n) {
    unsigned i = ((unsigned long)ra >> 3) % CSZ, k;
    for (k = 0; k < 16; ++k, i = (i + 1) % CSZ) {
        if (callers[i].caller == ra || callers[i].caller == 0) {
            callers[i].caller = ra;
            __atomic_add_fetch(&callers[i].n, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&callers[i].bytes, (unsigned long)n, __ATOMIC_RELAXED);
            return;
        }
    }
}

static void dump_callers(void) {
    int fd, i, j;
    const char *path = getenv("SC_ALLOC_CALLERS");
    char buf[512];
    int len;
    if (!path || !*path) return;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    for (i = 0; i < CSZ; ++i) {
        if (!callers[i].caller || !callers[i].n) continue;
        len = 0;
        {   char t[24]; int k = 0, m; unsigned long v = (unsigned long)callers[i].caller;
            const char *hx = "0123456789abcdef";
            do { t[k++] = hx[v & 15u]; v >>= 4; } while (v);
            buf[len++] = '0'; buf[len++] = 'x';
            for (m = k - 1; m >= 0; --m) buf[len++] = t[m];
        }
        buf[len++] = ' '; buf[len++] = 'n'; buf[len++] = '=';
        {   unsigned long v = callers[i].n; char t[24]; int k = 0, m;
            do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
            for (m = k - 1; m >= 0; --m) buf[len++] = t[m];
        }
        buf[len++] = ' '; buf[len++] = 'b'; buf[len++] = '=';
        {   unsigned long v = callers[i].bytes; char t[24]; int k = 0, m;
            do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
            for (m = k - 1; m >= 0; --m) buf[len++] = t[m];
        }
        buf[len++] = '\n';
        (void)write(fd, buf, (unsigned long)len);
    }
    (void)close(fd);
}
static int log_fd = -2;      /* -2 = not initialised, -1 = disabled */
static unsigned long last_sec;

/* tiny allocation-size map: store the size just before the returned block */
#define HDR 16u

static void __attribute__((destructor)) fini(void) { dump_callers(); }

static void init(void __attribute__((unused)) *p) {
    real_malloc  = dlsym(RTLD_NEXT, "malloc");
    real_free    = dlsym(RTLD_NEXT, "free");
    real_calloc  = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_mmap    = dlsym(RTLD_NEXT, "mmap");
    real_munmap  = dlsym(RTLD_NEXT, "munmap");
}

static void bucket_add(size_t n) {
    unsigned b = 0;
    if (n > 64) b = 1; if (n > 256) b = 2; if (n > 1024) b = 3; if (n > 4096) b = 4;
    if (n > 16384) b = 5; if (n > 65536) b = 6; if (n > 262144) b = 7; if (n > 1048576) b = 8;
    __atomic_add_fetch(&sz_bucket[b], 1, __ATOMIC_RELAXED);
}

static void tick(void) {
    char buf[256];
    unsigned long sec;
    int i = 0, k;
    char tmp[24];
    if (log_fd == -2) {
        const char *path = getenv("SC_ALLOC_LOG");
        if (!path || !*path) { log_fd = -1; return; }
        log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0) { log_fd = -1; return; }
        { struct timespec t0; if (clock_gettime(CLOCK_MONOTONIC, &t0) == 0) last_sec = (unsigned long)t0.tv_sec; }
    } else if (log_fd < 0) return;
    { struct timespec t1; if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) return; sec = (unsigned long)t1.tv_sec; }
    if (sec == last_sec) return;
    last_sec = sec;
#define APPEND(str) do { const char *s_ = (str); while (*s_ && i < (int)sizeof buf - 1) buf[i++] = *s_++; } while (0)
#define APPEND_NUM(v) do { unsigned long v_ = (unsigned long)(v); k = 0; do { tmp[k++] = (char)('0' + v_ % 10u); v_ /= 10u; } while (v_); while (k) buf[i++] = tmp[--k]; } while (0)
    APPEND("t="); APPEND_NUM(sec);
    APPEND(" malloc="); APPEND_NUM(c_malloc);
    APPEND(" free=");   APPEND_NUM(c_free);
    APPEND(" calloc="); APPEND_NUM(c_calloc);
    APPEND(" realloc=");APPEND_NUM(c_realloc);
    APPEND(" mmap=");   APPEND_NUM(c_mmap);
    APPEND(" munmap="); APPEND_NUM(c_munmap);
    APPEND(" bytes=");  APPEND_NUM(bytes_total);
    APPEND(" peak_live="); APPEND_NUM(peak_live);
    APPEND(" b=");
    { unsigned bb; for (bb = 0; bb < 9; ++bb) { APPEND_NUM(sz_bucket[bb]); if (bb < 8) APPEND(","); } }
    for (k = 0; k < 9; ++k) sz_bucket[k] = 0;
    buf[i++] = '\n';
    (void)write(log_fd, buf, (unsigned long)i);
    c_malloc = c_free = c_calloc = c_realloc = c_mmap = c_munmap = 0;
}

void *malloc(size_t n) {
    void *p;
    if (!real_malloc) init(0);
    __atomic_add_fetch(&c_malloc, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&bytes_total, (unsigned long long)n, __ATOMIC_RELAXED);
    bucket_add(n);
    caller_add(__builtin_return_address(0), n);
    p = real_malloc(n);
    if (p) {
        unsigned long long l = __atomic_add_fetch(&live_bytes, (unsigned long long)n, __ATOMIC_RELAXED);
        unsigned long long peak = __atomic_load_n(&peak_live, __ATOMIC_RELAXED);
        if (l > peak) __atomic_store_n(&peak_live, l, __ATOMIC_RELAXED);
    }
    tick();
    return p;
}

void free(void *p) {
    if (!real_free) init(0);
    __atomic_add_fetch(&c_free, 1, __ATOMIC_RELAXED);
    real_free(p);
    tick();
}

void *calloc(size_t a, size_t b) {
    void *p;
    if (!real_calloc) init(0);
    __atomic_add_fetch(&c_calloc, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&bytes_total, (unsigned long long)a * b, __ATOMIC_RELAXED);
    bucket_add(a * b);
    caller_add(__builtin_return_address(0), a * b);
    p = real_calloc(a, b);
    tick();
    return p;
}

void *realloc(void *p, size_t n) {
    void *q;
    if (!real_realloc) init(0);
    __atomic_add_fetch(&c_realloc, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&bytes_total, (unsigned long long)n, __ATOMIC_RELAXED);
    bucket_add(n);
    caller_add(__builtin_return_address(0), n);
    q = real_realloc(p, n);
    tick();
    return q;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    void *p;
    if (!real_mmap) init(0);
    __atomic_add_fetch(&c_mmap, 1, __ATOMIC_RELAXED);
    p = real_mmap(addr, len, prot, flags, fd, off);
    tick();
    return p;
}

int munmap(void *addr, size_t len) {
    int r;
    if (!real_munmap) init(0);
    __atomic_add_fetch(&c_munmap, 1, __ATOMIC_RELAXED);
    r = real_munmap(addr, len);
    tick();
    return r;
}
