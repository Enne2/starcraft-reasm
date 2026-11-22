/* T4 — property storage + rectangle utility for the native gameplay path.
 * Prototypes/semantics: Microsoft Learn, links in docs/19-CODA-API.md.
 * No guessed APIs, fallback stubs, new HWND registry, or Windows DLLs.
 */
#include "win32_game.h"
extern void *malloc(unsigned long);
extern void free(void *);
extern unsigned long strlen(const char *);
extern int strcmp(const char *, const char *);
extern void *memcpy(void *, const void *, unsigned long);
extern int GAME_STDCALL IsWindow(game_u32);
extern void GAME_STDCALL SetLastError(game_u32);
/* glibc ELF32 mutex ABI, same freestanding convention as win32_shim.c. */
typedef union { long align; unsigned char bytes[24]; } GameMutex;
extern int pthread_mutex_lock(GameMutex *);
extern int pthread_mutex_unlock(GameMutex *);
static GameMutex properties_mutex;

typedef struct Property {
    struct Property *next;
    game_u32 hwnd, atom;
    char *key;
    void *value; /* application owned, never freed by RemoveProp */
} Property;
static Property *properties;

static void dispose(Property *p) { free(p->key); free(p); }
/* Handles are monotonically allocated by win32_ui.c. Reap abandoned properties
 * on the next property operation; normal SDlg removes them at WM_NCDESTROY.
 * String/atom cross-resolution needs a GlobalAtom registry (not provided yet).
 * Atom-to-atom and case-sensitive string-to-string keys are supported. */
static void reap(void) {
    Property **pp = &properties;
    while (*pp) {
        Property *p = *pp;
        if (!IsWindow(p->hwnd)) { *pp = p->next; dispose(p); }
        else pp = &p->next;
    }
}
static Property **find_property(game_u32 h, const char *key) {
    game_u32 atom = (game_u32)key < 65536u ? (game_u32)key : 0;
    Property **pp;
    for (pp = &properties; *pp; pp = &(*pp)->next) {
        Property *p = *pp;
        if (p->hwnd == h && (atom ? p->atom == atom :
            !p->atom && !strcmp(p->key, key))) break;
    }
    return pp;
}
int GAME_STDCALL SetPropA(game_u32 h, const char *key, void *value) {
    Property **pp, *p;
    if (!IsWindow(h)) { SetLastError(1400); return 0; }
    if (!key) { SetLastError(87); return 0; }
    pthread_mutex_lock(&properties_mutex);
    reap(); pp = find_property(h, key); p = *pp;
    if (!p) {
        p = malloc(sizeof *p);
        if (!p) goto oom;
        p->atom = (game_u32)key < 65536u ? (game_u32)key : 0;
        p->key = 0;
        if (!p->atom) {
            unsigned long n = strlen(key) + 1;
            p->key = malloc(n);
            if (!p->key) { free(p); goto oom; }
            memcpy(p->key, key, n);
        }
        p->hwnd = h; p->next = 0; *pp = p;
    }
    p->value = value;
    pthread_mutex_unlock(&properties_mutex);
    return 1;
oom:
    pthread_mutex_unlock(&properties_mutex);
    SetLastError(8); return 0;
}
void *GAME_STDCALL GetPropA(game_u32 h, const char *key) {
    Property **pp; void *value;
    if (!key) return 0;
    pthread_mutex_lock(&properties_mutex);
    reap(); pp = find_property(h, key); value = *pp ? (*pp)->value : 0;
    pthread_mutex_unlock(&properties_mutex);
    return value;
}
void *GAME_STDCALL RemovePropA(game_u32 h, const char *key) {
    Property **pp, *p; void *value = 0;
    if (!key) return 0;
    pthread_mutex_lock(&properties_mutex);
    reap(); pp = find_property(h, key); p = *pp;
    if (p) { value = p->value; *pp = p->next; dispose(p); }
    pthread_mutex_unlock(&properties_mutex);
    return value;
}
int GAME_STDCALL IntersectRect(GameRect *out, const GameRect *a, const GameRect *b) {
    GameRect r;
    if (!out || !a || !b) return 0; /* invalid input, no invented writes */
    r.left = a->left > b->left ? a->left : b->left;
    r.top = a->top > b->top ? a->top : b->top;
    r.right = a->right < b->right ? a->right : b->right;
    r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (r.left >= r.right || r.top >= r.bottom) {
        r.left = r.top = r.right = r.bottom = 0;
        *out = r; return 0;
    }
    *out = r; return 1; /* source/destination aliasing is safe */
}

/* Native page state comes from the kernel, not a parallel list which would
 * go stale after VirtualFree/munmap. Linux needs /proc/self/smaps available.
 * mlock is NOT a successful no-op: RLIMIT_MEMLOCK failures propagate. */
extern void *fopen(const char *, const char *);
extern char *fgets(char *, int, void *);
extern int fclose(void *);
extern int sscanf(const char *, const char *, ...);
extern int strncmp(const char *, const char *, unsigned long);
extern char *strstr(const char *, const char *);
extern int mlock(const void *, unsigned long);
extern int munlock(const void *, unsigned long);
extern int getpagesize(void);
extern int *__errno_location(void);
static GameMutex memory_mutex;

static int page_range(void *address, game_u32 size, game_u32 *start, game_u32 *end) {
    game_u32 a = (game_u32)address, ps = (game_u32)getpagesize(), last;
    if (!size || !ps || (ps & (ps - 1)) || size - 1 > 0xFFFFFFFFu - a) {
        SetLastError(87); return 0;
    }
    last = a + size - 1;
    if (last > 0xFFFFFFFFu - ps) { SetLastError(87); return 0; }
    *start = a & ~(ps - 1); *end = (last & ~(ps - 1)) + ps;
    return 1;
}
/* 1=fully mapped/access-enabled, 0=gap/noaccess, -1=proc unavailable.
 * all_locked uses VmFlags lo (not the proportional Locked kB statistic).
 * Parsing waits for VmFlags before accepting each mapping. */
static int range_state(game_u32 start, game_u32 end, int require_access, int *all_locked) {
    char line[512], perms[5];
    unsigned long lo, hi;
    game_u32 cursor = start, next = start;
    int selected = 0;
    void *f = fopen("/proc/self/smaps", "r");
    *all_locked = 1;
    if (!f) return -1;
    while (cursor < end && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) == 3) {
            selected = 0;
            if (hi <= cursor) continue;
            if (lo > cursor || (require_access && perms[0] == '-' && perms[1] == '-' && perms[2] == '-')) {
                fclose(f); return 0;
            }
            next = hi < end ? hi : end; selected = 1;
        } else if (selected && !strncmp(line, "VmFlags:", 8)) {
            if (!strstr(line, " lo ") && !strstr(line, " lo\n")) *all_locked = 0;
            cursor = next; selected = 0;
        }
    }
    fclose(f);
    return cursor == end ? 1 : 0;
}
static game_u32 memory_error(void) {
    int e = *__errno_location();
    if (e == 22) return 87;       /* EINVAL -> INVALID_PARAMETER */
    if (e == 1 || e == 11 || e == 12) return 1453; /* privilege/resource/working set */
    return 487;                  /* INVALID_ADDRESS */
}
static int memory_operation(void *address, game_u32 size, int locking) {
    game_u32 start, end, error = 0;
    int state, locked, rc;
    if (!page_range(address, size, &start, &end)) return 0;
    pthread_mutex_lock(&memory_mutex);
    state = range_state(start, end, locking, &locked);
    if (state != 1) error = state < 0 ? 50 : 487;
    if (!error) {
        rc = locking ? mlock((const void *)start, end - start) :
                       munlock((const void *)start, end - start);
        if (rc) error = memory_error();
        else if (!locking && !locked) error = 158; /* ERROR_NOT_LOCKED */
    }
    pthread_mutex_unlock(&memory_mutex);
    if (error) { SetLastError(error); return 0; }
    return 1;
}
int GAME_STDCALL VirtualLock(void *address, game_u32 size) {
    return memory_operation(address, size, 1);
}
int GAME_STDCALL VirtualUnlock(void *address, game_u32 size) {
    return memory_operation(address, size, 0);
}
int GAME_STDCALL IsDebuggerPresent(void) {
    char line[256]; unsigned int tracer = 0;
    void *f = fopen("/proc/self/status", "r");
    if (!f) return 0; /* documented procfs prerequisite, no fabricated tracer */
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "TracerPid: %u", &tracer) == 1) break;
    fclose(f); return tracer != 0;
}

/* T4-bis: directory/profile backends. Registered legacy shim APIs must
 * explicitly delegate here; the extra table deliberately cannot override them.
 * Native statx uses the fixed Linux UAPI, not guessed glibc stat offsets. */
extern void reasm_win_path(const char *, char *, int);
extern int reasm_open_ci(const char *, int, int);
extern char *getcwd(char *, unsigned long);
extern char *strrchr(const char *, int);
extern void *memset(void *, int, unsigned long);
extern int close(int);
extern int mkdirat(int, const char *, unsigned int);
extern void *fdopendir(int);
extern void *readdir64(void *);
extern int closedir(void *);
extern int dirfd(void *);
typedef struct { long long sec; game_u32 nsec; int reserved; } GameStatTime;
typedef struct {
    game_u32 mask, blksize;
    unsigned long long attributes;
    game_u32 nlink, uid, gid;
    unsigned short mode, spare;
    unsigned long long ino, size, blocks, attributes_mask;
    GameStatTime atime, btime, ctime, mtime;
    unsigned char remaining[128];
} GameStatX;
extern int statx(int, const char *, int, unsigned int, GameStatX *);
_Static_assert(sizeof(GameStatX) == 256, "Linux statx UAPI size");
_Static_assert(__builtin_offsetof(GameStatX, mtime) == 112, "statx timestamp offset");
_Static_assert(sizeof(GameFindData) == 320, "WIN32_FIND_DATAA size");
_Static_assert(__builtin_offsetof(GameFindData, filename) == 44, "find filename offset");
_Static_assert(__builtin_offsetof(GameFindData, alternate) == 304, "find alternate offset");
static game_u32 file_error(int e) {
    switch (e) {
    case 1: case 13: case 30: return 5; /* permission/read-only filesystem */
    case 2: case 20: return 3; /* missing parent/not directory */
    case 9: return 6;
    case 12: return 8;
    case 17: return 183;
    case 22: return 123;
    case 23: case 24: return 4;
    case 28: case 122: return 112;
    case 36: return 206;
    case 38: case 95: return 50;
    default: return 1117;
    }
}
/* Reuse runtime translation/CI parent resolution. Reject unsupported UNC,
 * non-C drive and long input instead of silently truncating the target. */
static int file_path(const char *in, char out[512]) {
    char translated[260], cwd[512]; unsigned long n, m;
    if (!in || !*in) { SetLastError(123); return 0; }
    n = strlen(in);
    if (n >= sizeof translated) { SetLastError(206); return 0; }
    if (in[0] == '\\' && in[1] == '\\') { SetLastError(50); return 0; }
    if (n > 1 && in[1] == ':' && in[0] != 'C' && in[0] != 'c') { SetLastError(15); return 0; }
    if (n > 1 && in[1] == ':' && in[2] != '/' && in[2] != '\\') { SetLastError(50); return 0; }
    reasm_win_path(in, translated, sizeof translated);
    {
        const char *p = translated; unsigned int component = 0;
        for (; *p; ++p) {
            if (*p == '/') component = 0;
            else if (++component > 255) { SetLastError(206); return 0; }
        }
    }
    if (translated[0] == '/') { memcpy(out, translated, strlen(translated)+1); return 1; }
    if (!getcwd(cwd, sizeof cwd)) { SetLastError(file_error(*__errno_location())); return 0; }
    n = strlen(cwd); m = strlen(translated);
    if (n + m + 2 >= 512) { SetLastError(206); return 0; }
    memcpy(out, cwd, n); out[n++] = '/'; memcpy(out+n, translated, m+1); return 1;
}
int GAME_STDCALL game_CreateDirectoryA(const char *path, void *security) {
    char native[512], *leaf, *p; int fd, rc, e;
    if (security) {
        game_u32 *sa = security;
        if (sa[0] != 12) { SetLastError(87); return 0; }
        if (sa[1]) { SetLastError(50); return 0; } /* no invented Windows ACL */
    }
    if (!file_path(path, native)) return 0;
    for (p = native; *p; ++p)
        if (*p == '*' || *p == '?' || *p == ':') { SetLastError(123); return 0; }
    /* O_PATH probes existence without requiring read access. A case variant of
     * either a file OR a directory must report ERROR_ALREADY_EXISTS. */
    fd = reasm_open_ci(native, 0x200000, 0);
    if (fd >= 0) { close(fd); SetLastError(183); return 0; }
    p = native + strlen(native);
    while (p > native+1 && p[-1] == '/') *--p = 0;
    leaf = strrchr(native, '/');
    if (!leaf || !leaf[1]) { SetLastError(123); return 0; }
    *leaf++ = 0;
    fd = reasm_open_ci(*native ? native : "/", 0x10000, 0);
    if (fd < 0) { SetLastError(file_error(*__errno_location())); return 0; }
    rc = mkdirat(fd, leaf, 0777); e = *__errno_location(); close(fd);
    if (rc) { SetLastError(file_error(e)); return 0; }
    return 1;
}
typedef struct Search {
    struct Search *next;
    void *dir;
    game_u32 handle;
    char pattern[260];
} Search;
static Search *searches;
static game_u32 next_search = 0x76000000u;
static GameMutex searches_mutex;
static int folded(int c) { return c >= 'A' && c <= 'Z' ? c+32 : c; }
/* Exact caller (*.spc) and ordinary '*'/'?' supported. Not a claim of full
 * DOS wildcard/8.3 alias equivalence. *.* includes extensionless names. */
static int match_name(const char *pat, const char *s) {
    const char *star = 0, *retry = 0;
    if (!strcmp(pat, "*.*")) pat = "*";
    while (*s) {
        if (*pat == '?' || (*pat && folded((unsigned char)*pat) == folded((unsigned char)*s))) {
            ++pat; ++s;
        } else if (*pat == '*') { star = ++pat; retry = s; }
        else if (star) { pat = star; s = ++retry; }
        else return 0;
    }
    while (*pat == '*') ++pat;
    return !*pat;
}
static GameFileTime file_time(GameStatTime t) {
    unsigned long long v; GameFileTime result = {0, 0};
    if (t.sec < -11644473600LL || t.sec > 1833029933770LL) return result;
    v = (unsigned long long)(t.sec + 11644473600LL)*10000000ULL;
    if (v > ~0ULL - t.nsec/100) return result;
    v += t.nsec/100;
    result.lo = (game_u32)v; result.hi = (game_u32)(v >> 32); return result;
}
static int search_next(Search *s, GameFindData *data) {
    unsigned char *entry; GameStatX st; const char *name;
    for (;;) {
        *__errno_location() = 0; entry = readdir64(s->dir);
        if (!entry) {
            int e = *__errno_location(); SetLastError(e ? file_error(e) : 18); return 0;
        }
        name = (const char *)entry+19; /* Linux/glibc dirent64 d_name */
        if (!strcmp(name, ".") || !strcmp(name, "..") || !match_name(s->pattern, name)) continue;
        if (statx(dirfd(s->dir), name, 0, 0xFFF, &st)) {
            int e = *__errno_location();
            if (e == 2) continue; /* entry concurrently removed */
            SetLastError(file_error(e)); return 0;
        }
        memset(data, 0, sizeof *data);
        data->attributes = (st.mode & 0170000) == 0040000 ? 0x10 : 0x20;
        if (!(st.mode & 0222)) data->attributes |= 1;
        if (name[0] == '.') data->attributes |= 2;
        if (st.mask & 0x800) data->creation = file_time(st.btime);
        if (st.mask & 0x20) data->access = file_time(st.atime);
        if (st.mask & 0x40) data->write = file_time(st.mtime);
        if (!(data->attributes & 0x10)) {
            data->size_lo = (game_u32)st.size; data->size_hi = (game_u32)(st.size >> 32);
        }
        memcpy(data->filename, name, strlen(name)+1);
        return 1;
    }
}
void *GAME_STDCALL game_FindFirstFileA(const char *pattern, GameFindData *data) {
    char native[512], *leaf; int fd; void *dir; Search *s; game_u32 e;
    if (!data) { SetLastError(87); return (void *)-1; }
    if (!file_path(pattern, native)) return (void *)-1;
    leaf = strrchr(native, '/');
    if (!leaf || !leaf[1]) { SetLastError(123); return (void *)-1; }
    *leaf++ = 0;
    fd = reasm_open_ci(*native ? native : "/", 0x10000, 0);
    if (fd < 0) { SetLastError(file_error(*__errno_location())); return (void *)-1; }
    dir = fdopendir(fd);
    if (!dir) { e = file_error(*__errno_location()); close(fd); SetLastError(e); return (void *)-1; }
    s = malloc(sizeof *s);
    if (!s) { closedir(dir); SetLastError(8); return (void *)-1; }
    s->dir = dir; memcpy(s->pattern, leaf, strlen(leaf)+1);
    if (!search_next(s, data)) {
        extern game_u32 GAME_STDCALL GetLastError(void);
        e = GetLastError(); closedir(dir); free(s); SetLastError(e == 18 ? 2 : e); return (void *)-1;
    }
    pthread_mutex_lock(&searches_mutex);
    if (next_search == 0x76FFFFFFu) {
        pthread_mutex_unlock(&searches_mutex); closedir(dir); free(s); SetLastError(8); return (void *)-1;
    }
    s->handle = ++next_search; s->next = searches; searches = s;
    pthread_mutex_unlock(&searches_mutex); return (void *)s->handle;
}
int GAME_STDCALL game_FindNextFileA(void *handle, GameFindData *data) {
    Search *s; int rc;
    if (!data) { SetLastError(87); return 0; }
    pthread_mutex_lock(&searches_mutex);
    for (s = searches; s && s->handle != (game_u32)handle; s = s->next) {}
    if (!s) { pthread_mutex_unlock(&searches_mutex); SetLastError(6); return 0; }
    rc = search_next(s, data); pthread_mutex_unlock(&searches_mutex); return rc;
}
int GAME_STDCALL game_FindClose(void *handle) {
    Search **pp, *s; int rc, e;
    pthread_mutex_lock(&searches_mutex);
    for (pp = &searches; *pp && (*pp)->handle != (game_u32)handle; pp = &(*pp)->next) {}
    s = *pp;
    if (!s) { pthread_mutex_unlock(&searches_mutex); SetLastError(6); return 0; }
    *pp = s->next; rc = closedir(s->dir); e = *__errno_location(); free(s);
    pthread_mutex_unlock(&searches_mutex);
    if (rc) { SetLastError(file_error(e)); return 0; } return 1;
}

#define API(dll, n) {dll, #n, (void *)n}
const Win32GameApi win32_extra_apis[] = {
    API("USER32.dll", SetPropA), API("USER32.dll", GetPropA),
    API("USER32.dll", RemovePropA), API("USER32.dll", IntersectRect),
    API("KERNEL32.dll", VirtualLock), API("KERNEL32.dll", VirtualUnlock),
    API("KERNEL32.dll", IsDebuggerPresent),
    {0, 0, 0}
};
