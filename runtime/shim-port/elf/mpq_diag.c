/* T2: MPQ completion events for the native ELF runtime.
 *
 * The shared shim has 16 event slots and returns handle 1 on exhaustion.
 * Its wait on 1 returns success without waiting. With MPQ archive flags=2,
 * this lets the caller free a request/buffer while Storm's worker is decoding
 * it (ERROR_HANDLE_EOF 0x26, then streaming-decompressor use-after-free).
 *
 * Only the unnamed-event APIs and CloseHandle are redirected, in BOTH image
 * IATs: the EXE creates hundreds of events that Storm's worker signals. Other
 * handle types and named events stay on the shared implementation. No changes
 * to Storm .text, MPQ crypto, shared runtime sources, or the build script.
 * Included by storm_ext.c because build.sh is owned by another task.
 */
#include "mpq_diag.h"
#include "win32_shim.h"
#include "reasm_elf.h"

#define MPQ_S __attribute__((stdcall))
/* glibc i386 objects, conservatively sized/aligned; zero is the documented
 * PTHREAD_MUTEX_INITIALIZER / PTHREAD_COND_INITIALIZER on this runtime. */
typedef union { unsigned char bytes[40]; long align; } MpqMutex;
typedef union { unsigned char bytes[64]; long long align; } MpqCond;
typedef struct MpqEvent {
    unsigned handle, refs;
    int manual, signaled, closed;
    MpqMutex mutex;
    MpqCond cond;
    struct MpqEvent *next;
} MpqEvent;

extern void *calloc(unsigned long, unsigned long);
extern void free(void *);
extern char *getenv(const char *);
extern int strcmp(const char *, const char *);
extern int pthread_mutex_lock(void *);
extern int pthread_mutex_unlock(void *);
extern int pthread_mutex_destroy(void *);
extern int pthread_cond_wait(void *, void *);
extern int pthread_cond_timedwait(void *, void *, const void *);
extern int pthread_cond_broadcast(void *);
extern int pthread_cond_destroy(void *);
extern int clock_gettime(int, void *);
extern void _exit(int);
/* Known Win32 stdcall signatures (D19). These are the unmodified named shims,
 * not IAT calls: delegation cannot recurse through this bridge. */
extern void *MPQ_S CreateEventA(void *, int, int, const char *);
extern int MPQ_S SetEvent(void *);
extern int MPQ_S ResetEvent(void *);
extern unsigned MPQ_S WaitForSingleObject(void *, unsigned);
extern int MPQ_S CloseHandle(void *);
extern void MPQ_S SetLastError(unsigned);

static MpqMutex mpq_registry_lock;
static MpqEvent *mpq_events;
static unsigned mpq_next_event = 0x5f000001u;
static unsigned mpq_event_created, mpq_event_live, mpq_event_peak;

static void mpq_check(int r) {
    if (r) { reasm_elf_log("[mpq] event synchronization failure\n"); _exit(10); }
}
static int mpq_own_range(void *h) {
    unsigned v = (unsigned long) h;
    return v >= 0x5f000001u && v < 0x60000000u;
}
static void mpq_event_free(MpqEvent *e) {
    mpq_check(pthread_cond_destroy(&e->cond));
    mpq_check(pthread_mutex_destroy(&e->mutex));
    free(e);
}
static MpqEvent *mpq_event_get(void *h) {
    MpqEvent *e;
    mpq_check(pthread_mutex_lock(&mpq_registry_lock));
    for (e = mpq_events; e; e = e->next)
        if (e->handle == (unsigned long) h) break;
    if (e) ++e->refs;
    mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
    return e;
}
static void mpq_event_put(MpqEvent *e) {
    mpq_check(pthread_mutex_lock(&mpq_registry_lock));
    if (!--e->refs && e->closed) mpq_event_free(e);
    mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
}
static void *MPQ_S mpq_event_create(void *sa, int manual, int init, const char *name) {
    MpqEvent *e;
    unsigned handle;
    /* This bridge is deliberately scoped to MPQ's unnamed completion events. */
    if (name && *name) return CreateEventA(sa, manual, init, name);
    e = calloc(1, sizeof *e);
    if (!e) { SetLastError(8 /*ERROR_NOT_ENOUGH_MEMORY*/); return 0; }
    e->manual = manual != 0; e->signaled = init != 0;
    mpq_check(pthread_mutex_lock(&mpq_registry_lock));
    if (mpq_next_event == 0x60000000u) {
        free(e); mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
        SetLastError(8); return 0;
    }
    handle = e->handle = mpq_next_event++;
    e->next = mpq_events; mpq_events = e;
    ++mpq_event_created; ++mpq_event_live;
    if (mpq_event_live > mpq_event_peak) mpq_event_peak = mpq_event_live;
    mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
    return (void *) (unsigned long) handle;
}
static int MPQ_S mpq_event_set(void *h) {
    MpqEvent *e = mpq_event_get(h);
    if (!e) {
        if (mpq_own_range(h)) { SetLastError(6); return 0; }
        return SetEvent(h);
    }
    mpq_check(pthread_mutex_lock(&e->mutex));
    e->signaled = 1;
    mpq_check(pthread_cond_broadcast(&e->cond));
    mpq_check(pthread_mutex_unlock(&e->mutex));
    mpq_event_put(e); return 1;
}
static int MPQ_S mpq_event_reset(void *h) {
    MpqEvent *e = mpq_event_get(h);
    if (!e) {
        if (mpq_own_range(h)) { SetLastError(6); return 0; }
        return ResetEvent(h);
    }
    mpq_check(pthread_mutex_lock(&e->mutex)); e->signaled = 0;
    mpq_check(pthread_mutex_unlock(&e->mutex));
    mpq_event_put(e); return 1;
}
static unsigned MPQ_S mpq_event_wait(void *h, unsigned ms) {
    MpqEvent *e = mpq_event_get(h);
    unsigned r;
    if (!e) {
        if (mpq_own_range(h)) { SetLastError(6); return 0xffffffffu; }
        return WaitForSingleObject(h, ms);
    }
    mpq_check(pthread_mutex_lock(&e->mutex));
    if (ms == 0xffffffffu) {
        while (!e->signaled && !e->closed)
            mpq_check(pthread_cond_wait(&e->cond, &e->mutex));
    } else if (ms && !e->signaled && !e->closed) {
        struct { long sec, nsec; } deadline;
        mpq_check(clock_gettime(0 /*CLOCK_REALTIME*/, &deadline));
        deadline.sec += ms / 1000;
        deadline.nsec += (ms % 1000) * 1000000L;
        if (deadline.nsec >= 1000000000L) {
            deadline.nsec -= 1000000000L; ++deadline.sec;
        }
        while (!e->signaled && !e->closed) {
            int err = pthread_cond_timedwait(&e->cond, &e->mutex, &deadline);
            if (err == 110 /*glibc ETIMEDOUT*/) break;
            mpq_check(err);
        }
    }
    r = e->closed ? 0xffffffffu : (e->signaled ? 0 : 258);
    if (e->signaled && !e->manual) e->signaled = 0;
    mpq_check(pthread_mutex_unlock(&e->mutex)); mpq_event_put(e);
    if (r == 0xffffffffu) SetLastError(6 /*ERROR_INVALID_HANDLE*/);
    return r;
}
static int MPQ_S mpq_handle_close(void *h) {
    MpqEvent **link, *e;
    int found;
    mpq_check(pthread_mutex_lock(&mpq_registry_lock));
    for (link = &mpq_events; *link && (*link)->handle != (unsigned long) h;
         link = &(*link)->next) { }
    e = *link; found = e != 0;
    if (e) {
        *link = e->next; --mpq_event_live;
        /* Defer destruction while Set/Wait still holds a reference. Win32
         * leaves close-while-wait undefined; here it wakes with WAIT_FAILED. */
        mpq_check(pthread_mutex_lock(&e->mutex)); e->closed = 1;
        mpq_check(pthread_cond_broadcast(&e->cond));
        mpq_check(pthread_mutex_unlock(&e->mutex));
        if (!e->refs) mpq_event_free(e);
    }
    mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
    if (found) return 1;
    if (mpq_own_range(h)) { SetLastError(6); return 0; }
    return CloseHandle(h);
}

void mpq_diag_get_stats(MpqDiagStats *out) {
    if (!out) return;
    mpq_check(pthread_mutex_lock(&mpq_registry_lock));
    out->created = mpq_event_created;
    out->live = mpq_event_live; out->peak = mpq_event_peak;
    mpq_check(pthread_mutex_unlock(&mpq_registry_lock));
}
static void *mpq_event_target(const char *name) {
    if (!strcmp(name, "CreateEventA")) return (void *) mpq_event_create;
    if (!strcmp(name, "SetEvent")) return (void *) mpq_event_set;
    if (!strcmp(name, "ResetEvent")) return (void *) mpq_event_reset;
    if (!strcmp(name, "WaitForSingleObject")) return (void *) mpq_event_wait;
    if (!strcmp(name, "CloseHandle")) return (void *) mpq_handle_close;
    return 0;
}
void mpq_diag_install(void) {
    typedef struct { const char *dll, *name; unsigned slot; } MpqImport;
    extern const MpqImport g_imports[];
    extern const int g_imports_n;
    static int installed;
    const char *v;
    unsigned count = 0;
    int i;
    if (installed) return;
    v = getenv("SC_MPQ_EVENT_FIX");
    if (v && v[0] == '0') { installed = 1; return; }
    /* Ordinal lookup is queried again by iat_summary before DllMain/threads.
     * Require ALL matching slots to contain the resolved named shim. This
     * prevents installing into the partially resolved PE/ELF IAT. */
    for (i = 0; i < g_imports_n; ++i) {
        const MpqImport *p = &g_imports[i];
        if (!mpq_event_target(p->name)) continue;
        if (*(void **) (unsigned long) p->slot != win32_shim_lookup(p->name)) return;
        ++count;
    }
    if (!count) return;
    for (i = 0; i < g_imports_n; ++i) {
        const MpqImport *p = &g_imports[i];
        void *target = mpq_event_target(p->name);
        if (target) *(void **) (unsigned long) p->slot = target;
    }
    installed = 1;
    reasm_elf_log("[mpq] EXE/Storm IAT: dynamic completion events installed\n");
}
