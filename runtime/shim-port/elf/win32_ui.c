/* sc-reasm/tools/elf/win32_ui.c — RISORSE + DIALOGHI + GDI minimo del runtime ELF nativo.
 *
 * Le risorse servono da DUE posti diversi:
 *   (a) l'eseguibile: la sua sezione .rsrc e' gia' copiata ai VA originali (0x6DE000), quindi il
 *       Resource Directory si legge direttamente dal PE header copiato a 0x400000;
 *   (b) local.dll: NON e' un modulo caricato, e' un FILE con sole risorse. Si legge da disco,
 *       si individua la sua sezione .rsrc e si legge il suo Resource Directory.
 * Per questo "modulo" e' una cosa astratta: immagine in memoria oppure file su disco.
 *
 * Finche' la parte dialoghi/GDI non e' pronta, qui c'e' il sottosistema risorse completo.
 */
#include "win32_shim.h"
#include "reasm_elf.h"
#include "dlg_elf.h"
#ifdef SC_ELF_REAL_GDI
#include "gdi_elf.h"
#endif
#include "host_elf.h"
#include "input_elf.h"
#pragma weak input_elf_translate
#pragma weak host_elf_input_owner
#pragma weak host_elf_next_input
#pragma weak host_elf_key_state
#pragma weak host_elf_mouse_screen
#pragma weak host_elf_mouse_warp
#pragma weak host_elf_mouse_capture
#pragma weak host_elf_cursor_visible
#pragma weak host_elf_dispatch_state
#pragma weak host_elf_input_state
#pragma weak host_elf_pump
#pragma weak host_elf_ticks
#pragma weak host_elf_window_size
#include <stdarg.h>   /* header di gcc (-isystem .../include): stdarg NON e' libc */

#define S __attribute__((stdcall))

extern char *getenv(const char *);
extern int strcasecmp(const char *, const char *);
extern int strcmp(const char *, const char *);
extern unsigned long strlen(const char *);
extern void *memcpy(void *, const void *, unsigned long);
extern void *memmove(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);
extern int open(const char *, int, ...);
extern int close(int);
extern long read(int, void *, unsigned long);
extern long lseek(int, long, int);
extern void *malloc(unsigned long);
extern void free(void *);
extern int access(const char *, int);
extern int snprintf(char *, unsigned long, const char *, ...);
S int GetKeyState(int);

#define MAX_MOD 16
#define MAX_RES 512

typedef struct {
    int used;
    unsigned int handle;
    int is_file;
    char path[300];
    unsigned char *img;          /* buffer del file (is_file) */
    unsigned int img_size;
    unsigned int base;           /* image base */
    unsigned int rsrc_rva, rsrc_size;
    unsigned int sec_va, sec_size, sec_ro;   /* sezione .rsrc nel file */
} UiModule;

typedef struct { void *data; unsigned int size; unsigned int rtype, rname; int owned; } UiRes;

static UiModule g_mods[MAX_MOD];
static int g_mods_n;
static UiRes g_res[MAX_RES];
static int g_res_n;
static int g_ui_ready;
static int g_uitrace;
static void strcpy_(char *d, const char *s);

/* --- lettura minimalista di un PE (header + sezione .rsrc) --- */
typedef struct {
    unsigned int base, rsrc_rva, rsrc_size;
    unsigned int sec_va, sec_size, sec_ro;
} PeInfo;

static int pe_info(const unsigned char *d, unsigned int len, PeInfo *out) {
    unsigned int e, optsz;
    unsigned int nsec, i, opt;
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return 0;
    e = *(const unsigned int *) (d + 0x3C);
    if (e > len - 24 || len - e < 0x78 || d[e] != 'P' || d[e + 1] != 'E' || d[e+2] || d[e+3]) return 0;
    nsec = *(const unsigned short *) (d + e + 6);
    optsz = *(const unsigned short *) (d + e + 20);
    opt = e + 24;
    if (optsz < 120 || optsz > len - opt || *(const unsigned short *)(d+opt) != 0x10B) return 0;
    out->base = *(const unsigned int *) (d + opt + 28);
    if (opt + 96 + 8 * 2 + 8 > len) return 0;
    out->rsrc_rva = *(const unsigned int *) (d + opt + 96 + 8 * 2);
    out->rsrc_size = *(const unsigned int *) (d + opt + 96 + 8 * 2 + 4);
    out->sec_va = 0; out->sec_size = 0; out->sec_ro = 0;
    for (i = 0; i < nsec; ++i) {
        unsigned int o = opt + optsz + 40 * i;
        unsigned int vsz, va, rsz, ro;
        if (o + 40 > len) break;
        vsz = *(const unsigned int *) (d + o + 8);
        va = *(const unsigned int *) (d + o + 12);
        rsz = *(const unsigned int *) (d + o + 16);
        ro = *(const unsigned int *) (d + o + 20);
        if (va == out->rsrc_rva) {
            out->sec_va = va;
            out->sec_size = (vsz > rsz) ? vsz : rsz;
            out->sec_ro = ro;
            if (!out->rsrc_size) out->rsrc_size = vsz ? vsz : rsz;
            break;
        }
        if (out->rsrc_rva >= va && out->rsrc_rva < va + ((vsz > rsz) ? vsz : rsz)) {
            out->sec_va = va;                        /* directory dentro un'altra sezione */
            out->sec_size = (vsz > rsz) ? vsz : rsz;
            out->sec_ro = ro;
        }
    }
    return out->rsrc_rva != 0;
}

static unsigned char *rva_span(UiModule *m, unsigned int rva, unsigned int n) {
    unsigned int off;
    if (rva < m->rsrc_rva || rva - m->rsrc_rva > m->rsrc_size ||
        n > m->rsrc_size - (rva - m->rsrc_rva)) return 0;
    if (!m->is_file) return (unsigned char *)(unsigned long)(m->base + rva);
    if (rva < m->sec_va || rva - m->sec_va > m->sec_size) return 0;
    off = m->sec_ro + (rva - m->sec_va);
    if (off < m->sec_ro || off > m->img_size || n > m->img_size - off) return 0;
    return m->img + off;
}

static UiModule *mod_by_handle(unsigned int h) {
    int i;
    if (h == 0x400000u) {
        for (i = 0; i < g_mods_n; ++i)
            if (g_mods[i].used && !g_mods[i].is_file && g_mods[i].base == 0x400000u) return &g_mods[i];
        return 0;
    }
    for (i = 0; i < g_mods_n; ++i)
        if (g_mods[i].used && g_mods[i].handle == h) return &g_mods[i];
    return 0;
}

static const char *base_name(const char *p) {
    const char *b = p;
    const char *s;
    for (s = p; *s; ++s) if (*s == '\\' || *s == '/') b = s + 1;
    return b;
}

static UiModule *mod_by_name(const char *name) {
    int i;
    const char *bn = base_name(name);
    for (i = 0; i < g_mods_n; ++i)
        if (g_mods[i].used && strcasecmp(base_name(g_mods[i].path), bn) == 0) return &g_mods[i];
    return 0;
}

void win32_ui_init(void) {
    UiModule *m;
    unsigned char *hdr;
    PeInfo pi;
    if (g_ui_ready) return;
    g_ui_ready = 1;
    g_uitrace = getenv("REASM_TRACE") != 0;
    /* modulo (a): l'eseguibile, con le header PE gia' copiate a 0x400000 da loader_init() */
    hdr = (unsigned char *) (unsigned long) 0x400000u;
    if (pe_info(hdr, 0x1000, &pi)) {
        m = &g_mods[g_mods_n++];
        m->used = 1;
        m->is_file = 0;
        m->handle = 0x400000u;
        m->base = pi.base;
        m->rsrc_rva = pi.rsrc_rva;
        m->rsrc_size = pi.rsrc_size;
        strcpy_(m->path, "StarCraft.exe");
        reasm_elf_log("[risorse] eseguibile: .rsrc RVA=");
        reasm_elf_log_hex(pi.rsrc_rva);
        reasm_elf_log(" VA=");
        reasm_elf_log_hex(pi.base + pi.rsrc_rva);
        reasm_elf_log("\n");
    } else {
        reasm_elf_log("[risorse] header PE dell'eseguibile illeggibili a 0x400000\n");
    }
}

static void strcpy_(char *d, const char *s) {
    while ((*d++ = *s++) != 0) { }
}

/* --- MODULI EMULATI DAI NOSTRI SHIM (nessun file su disco) ---
 * Il gioco fa LoadLibraryA("ddraw.dll") + GetProcAddress("DirectDrawCreate") / ("DirectSoundCreate").
 * Gli shim video (com_ddraw.c, di Q2) e audio (in coda) sono LINKATI nel binario: basta esporli.
 * I simboli sono dichiarati DEBOLI: se il file dell'altro agente non e' nel link (SC_ELF_NO_VIDEO=1)
 * l'indirizzo e' 0 e si ricade sul trampolino UNK, senza rompere la build. */
extern long __attribute__((stdcall, weak)) DirectDrawCreate(const void *, void **, void *);
extern long __attribute__((stdcall, weak)) DirectDrawCreateClipper(unsigned long, void **, void *);

static const struct { const char *name; unsigned int handle; } g_fake_mod[] = {
    { "ddraw.dll", 0x7000DD00u },
};
#define FAKE_MOD_N ((int) (sizeof g_fake_mod / sizeof g_fake_mod[0]))

/* nome di base, in minuscolo ("C:\x\DDRAW.DLL" -> "ddraw.dll"); ritorna la lunghezza */
static int base_lower(const char *p, char *out, int n) {
    int i, st = 0, l = 0;
    for (i = 0; p[i]; ++i) { if (p[i] == '/' || p[i] == '\\') st = i + 1; }
    for (i = st; p[i] && l < n - 1; ++i) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        out[l++] = c;
    }
    out[l] = 0;
    return l;
}

unsigned int win32_ui_fake_module(const char *name) {
    char b[64];
    int i;
    if (!name) return 0;
    base_lower(name, b, sizeof b);
    for (i = 0; i < FAKE_MOD_N; ++i) if (strcmp(b, g_fake_mod[i].name) == 0) return g_fake_mod[i].handle;
    return 0;
}

/* GetProcAddress per i moduli NON caricati da file. Ritorna 0 se non e' roba nostra. */
void *win32_ui_get_proc(unsigned long mod, const char *name) {
    if (!name || name < (const char *) 0x10000UL) return 0;
    (void) mod;
    if (!strcmp(name, "DirectDrawCreate"))        return (void *) (unsigned long) (unsigned int) DirectDrawCreate;
    if (!strcmp(name, "DirectDrawCreateClipper")) return (void *) (unsigned long) (unsigned int) DirectDrawCreateClipper;
    return 0;
}

/* Carica un file PE come "modulo di risorse" (NON lo esegue). Ritorna l'handle o 0. */
unsigned int win32_ui_load_library(const char *name) {
    UiModule *m;
    unsigned char *buf;
    long sz;
    int fd;
    PeInfo pi;
    if (!name) return 0;
    win32_ui_init();
    m = mod_by_name(name);
    if (m) return m->handle;
    { unsigned int fh = win32_ui_fake_module(name);
      if (fh) { if (g_uitrace) { reasm_elf_log("[win32] modulo emulato dagli shim: ");
                                 reasm_elf_log(name); reasm_elf_log("\n"); } return fh; } }
    { char up[300], full[600];
      extern int reasm_open_ci(const char *, int, int);
      extern char *getcwd(char *, unsigned long);
      extern int snprintf(char *, unsigned long, const char *, ...);
      reasm_win_path(name, up, sizeof up);
      if (up[0] != '/') {
          char cwd[300];
          if (!getcwd(cwd, sizeof cwd)) return 0;
          snprintf(full, sizeof full, "%s/%s", cwd, up);
          fd = reasm_open_ci(full, 0, 0);
      } else fd = reasm_open_ci(up, 0, 0);
    }
    if (fd < 0) return 0;
    sz = lseek(fd, 0, 2);
    lseek(fd, 0, 0);
    if (sz <= 0 || sz > (8 << 20)) { close(fd); return 0; }
    buf = (unsigned char *) malloc((unsigned long) sz);
    if (!buf) { close(fd); return 0; }
    if (read(fd, buf, (unsigned long) sz) != sz) { close(fd); free(buf); return 0; }
    close(fd);
    if (!pe_info(buf, (unsigned int) sz, &pi) || !pi.rsrc_rva) {
        free(buf);                                   /* PE senza risorse: handle generico */
        return (unsigned int) (0x70000000u + (unsigned int) sz % 0x1000u);
    }
    if (g_mods_n >= MAX_MOD) { free(buf); return 0; }
    m = &g_mods[g_mods_n++];
    m->used = 1;
    m->is_file = 1;
    m->handle = 0x71000000u + (unsigned int) (g_mods_n - 1);
    m->img = buf;
    m->img_size = (unsigned int) sz;
    m->base = pi.base;
    m->rsrc_rva = pi.rsrc_rva;
    m->rsrc_size = pi.rsrc_size;
    m->sec_va = pi.sec_va;
    m->sec_size = pi.sec_size;
    m->sec_ro = pi.sec_ro;
    strcpy_(m->path, name);
    reasm_elf_log("[risorse] caricato come FILE (sola lettura risorse): ");
    reasm_elf_log(name);
    reasm_elf_log(" -> handle ");
    reasm_elf_log_hex(m->handle);
    reasm_elf_log(", .rsrc ");
    reasm_elf_log_hex(pi.sec_va);
    reasm_elf_log("+");
    reasm_elf_log_hex(pi.rsrc_size);
    reasm_elf_log("\n");
    return m->handle;
}

unsigned int win32_ui_module_handle(const char *name) {
    UiModule *m;
    if (!name) return 0;
    win32_ui_init();
    m = mod_by_name(name);
    return m ? m->handle : 0;
}

/* --- attraversamento del Resource Directory --- */
extern S void SetLastError(unsigned int);
static const char *res_key(const char *key) {
    if ((unsigned long)key >= 65536 && key[0]=='#') {
        unsigned int n=0; const char *p=key+1;
        if (!*p) return key;
        while (*p>='0' && *p<='9') { n=n*10+(*p++-'0'); if(n>65535) return key; }
        if (!*p) return (const char *)(unsigned long)n;
    }
    return key;
}
static int res_match(UiModule *m, unsigned int entry, const char *key) {
    unsigned int len, i; unsigned char *s;
    key=res_key(key);
    if (!(entry & 0x80000000u)) return (unsigned long)key < 65536 && entry == (unsigned int)(unsigned long)key;
    if ((unsigned long)key < 65536) return 0;
    s=rva_span(m,m->rsrc_rva+(entry&0x7FFFFFFFu),2); if(!s) return 0;
    len=*(unsigned short *)s;
    s=rva_span(m,m->rsrc_rva+(entry&0x7FFFFFFFu)+2,len*2); if(!s) return 0;
    for(i=0;i<len;++i) {
        unsigned int a=*(unsigned short *)(s+2*i), b=(unsigned char)key[i];
        if(!b) return 0;
        if(a>='a' && a<='z') a-=32;
        if(b>='a' && b<='z') b-=32;
        if(a!=b) return 0;
    }
    return !key[len];
}
static unsigned char *res_entries(UiModule *m, unsigned int off, unsigned int *count) {
    unsigned char *p;
    if(off > m->rsrc_size) return 0;
    p=rva_span(m,m->rsrc_rva+off,16); if(!p) return 0;
    *count=*(unsigned short *)(p+12)+*(unsigned short *)(p+14);
    return rva_span(m,m->rsrc_rva+off+16,*count*8);
}
static int res_select(UiModule *m, unsigned int dir, const char *key, unsigned int *out) {
    unsigned int n,i; unsigned char *e=res_entries(m,dir,&n);
    if(!e) return 0;
    for(i=0;i<n;++i,e+=8) if(res_match(m,*(unsigned int *)e,key)) {
        *out=*(unsigned int *)(e+4); return 1;
    }
    return 0;
}
static int res_location(UiModule *m, const char *type, const char *name, int lang,
                        unsigned int *rva, unsigned int *size) {
    unsigned int off,n,i,chosen=0; unsigned char *e,*de;
    if(!res_select(m,0,type,&off) || !(off&0x80000000u)) return 0;
    if(!res_select(m,off&0x7FFFFFFFu,name,&off) || !(off&0x80000000u)) return 0;
    e=res_entries(m,off&0x7FFFFFFFu,&n); if(!e) return 0;
    for(i=0;i<n;++i,e+=8) {
        unsigned int id=*(unsigned int *)e;
        if(id&0x80000000u) continue;
        if(!chosen && lang<0) chosen=*(unsigned int *)(e+4)+1;
        if(id==(unsigned int)(lang<0?0x409:lang)) { chosen=*(unsigned int *)(e+4)+1; break; }
    }
    if(!chosen || ((chosen-1)&0x80000000u)) return 0;
    off=chosen-1;
    if(off > m->rsrc_size) return 0;
    de=rva_span(m,m->rsrc_rva+off,16); if(!de) return 0;
    *rva=*(unsigned int *)de; *size=*(unsigned int *)(de+4); return 1;
}

static void *res_find_lang(unsigned int hmod, const char *name, const char *type, int lang) {
    UiModule *m;
    unsigned int want_name = 0, want_type = 0, rva = 0, size = 0;
    unsigned char *data;
    UiRes *r;
    win32_ui_init();
    m = mod_by_handle(hmod ? hmod : 0x400000u);
    if (!m || !m->rsrc_rva) return 0;
    name=res_key(name); type=res_key(type);
    if ((unsigned long)name < 65536) want_name=(unsigned int)(unsigned long)name;
    if ((unsigned long)type < 65536) want_type=(unsigned int)(unsigned long)type;
    if (!res_location(m,type,name,lang,&rva,&size)) { SetLastError(lang<0?1814:1815); return 0; }
    data = rva_span(m, rva, size);
    if (!data) return 0;
    { int i; for (i = 0; i < g_res_n; ++i)
        if (g_res[i].data == data && g_res[i].size == size) return &g_res[i]; }
    if (g_res_n >= MAX_RES) return 0;
    r = &g_res[g_res_n++];
    r->data = data;
    r->size = size;
    r->rtype = want_type;
    r->rname = want_name;
    return r;
}

static void *res_find(unsigned int hmod, const char *name, const char *type) { return res_find_lang(hmod,name,type,-1); }
static UiRes *res_handle(void *h) {
    int i; for(i=0;i<g_res_n;++i) if(h==&g_res[i]) return &g_res[i]; return 0;
}

/* --- API risorse (user32/kernel32) --- */
S void *FindResourceA(void *hmod, const char *name, const char *type) {
    return res_find((unsigned int) (unsigned long) hmod, name, type);
}
S void *FindResourceExA(void *hmod, const char *type, const char *name, unsigned short lang) {
    return res_find_lang((unsigned int)(unsigned long)hmod,name,type,lang?lang:-1);
}
S void *LoadResource(void *hmod, void *hres) { (void)hmod; return res_handle(hres); }
S void *LockResource(void *hres) { UiRes *r=res_handle(hres); return r?r->data:0; }
S unsigned int SizeofResource(void *hmod, void *hres) { UiRes *r=res_handle(hres); (void)hmod; return r?r->size:0; }

/* LoadStringA: le stringhe stanno nella risorsa RT_STRING (6), 16 per blocco, ognuna preceduta
 * dalla lunghezza in WORD. */
S int LoadStringA(void *hinst, unsigned int id, char *buf, int n) {
    void *hres;
    UiRes *r;
    unsigned char *p;
    unsigned int i, block = (id >> 4) + 1, idx = id & 0xF, len = 0;
    if (!buf || n <= 0) return 0;
    buf[0] = 0;
    hres = res_find((unsigned int) (unsigned long) hinst, (const char *) (unsigned long) block,
                    (const char *) (unsigned long) 6 /*RT_STRING*/);
    if (!hres) return 0;
    r = (UiRes *) hres;
    p = (unsigned char *) r->data;
    for (i = 0; i < idx; ++i) {
        if ((unsigned int)(p - (unsigned char *)r->data) + 2 > r->size) return 0;
        len = *(unsigned short *) p;
        if (len * 2 + 2 > r->size - (unsigned int)(p - (unsigned char *)r->data)) return 0;
        p += 2 + len * 2;
    }
    if ((unsigned int)(p - (unsigned char *)r->data) + 2 > r->size) return 0;
    len = *(unsigned short *) p;
    p += 2;
    if (len * 2 > r->size - (unsigned int)(p - (unsigned char *)r->data)) return 0;
    {
        unsigned int k;
        for (k = 0; k < len && (int) k < n - 1; ++k) buf[k] = (char) p[k * 2];
        buf[k] = 0;
        return (int) k;
    }
}

/* --- guest callbacks (ARM and i386 translated runtimes) ---
 * A guest procedure in the LLASM model is a translation that takes the emulated %_cpu and returns
 * by branching to the VA it popped: it can only be entered through the runtime call-out, which
 * pushes arguments on the guest stack and runs the guest until it returns. */
#if defined(__arm__) || defined(__i386__)
extern int arm_guest_call4(unsigned int proc, unsigned int a, unsigned int b,
                           unsigned int c, unsigned int d) __attribute__((weak));
#define GUEST_CALLABLE(p) (arm_guest_call4 != 0)
#else
#define GUEST_CALLABLE(p) 0
#endif

/* --- wsprintfA ---
 * ATTENZIONE: NON e' stdcall, e' __cdecl con varargs: il chiamante pulisce la pila. Dichiararla
 * stdcall sfaserebbe la pila a ogni chiamata. storm la usa moltissimo. */
static char *ui_num(char *o, unsigned int v, int base, int minus, int width, int zero, int left) {
    char tmp[36];
    int n = 0, i, pad = 0;
    if (base == 10 && minus) { /* segno */ }
    do { int d = (int) (v % (unsigned) base); tmp[n++] = (char) (d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned) base; } while (v);
    if (minus) tmp[n++] = '-';
    if (width > n && !left) pad = width - n;
    for (i = 0; i < pad; ++i) *o++ = zero ? '0' : ' ';
    while (n) *o++ = tmp[--n];
    (void) left;
    return o;
}

int wsprintfA(char *out, const char *fmt, ...) {
    va_list ap;
    char *o = out;
    va_start(ap, fmt);
    for (; *fmt; ++fmt) {
        int width = 0, zero = 0, left = 0, islong = 0;
        if (*fmt != '%') { *o++ = *fmt; continue; }
        ++fmt;
        for (;;) {
            if (*fmt == '-') { left = 1; ++fmt; continue; }
            if (*fmt == '0') { zero = 1; ++fmt; continue; }
            if (*fmt == '+' || *fmt == ' ') { ++fmt; continue; }
            if (*fmt == '#') { ++fmt; continue; }
            break;
        }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); ++fmt; }
        if (*fmt == 'l' || *fmt == 'h') { islong = (*fmt == 'l'); ++fmt; }
        (void) islong;
        switch (*fmt) {
        case 'd': case 'i': { int v = va_arg(ap, int); if (v < 0) { o = ui_num(o, (unsigned) (-v), 10, 1, width, zero, left); } else o = ui_num(o, (unsigned) v, 10, 0, width, zero, left); break; }
        case 'u': o = ui_num(o, va_arg(ap, unsigned int), 10, 0, width, zero, left); break;
        case 'x': o = ui_num(o, va_arg(ap, unsigned int), 16, 0, width, zero, left); break;
        case 'X': { char *s = o; o = ui_num(o, va_arg(ap, unsigned int), 16, 0, width, zero, left); while (s < o) { if (*s >= 'a' && *s <= 'f') *s = (char) (*s - 32); ++s; } break; }
        case 'c': { char c = (char) va_arg(ap, int); *o++ = c; break; }
        case 's': { const char *s = va_arg(ap, const char *); int n = 0; if (!s) s = "(null)";
                    while (s[n]) ++n;
                    if (width > n && !left) { int i; for (i = 0; i < width - n; ++i) *o++ = ' '; }
                    { int i; for (i = 0; i < n; ++i) *o++ = s[i]; }
                    break; }
        case '%': *o++ = '%'; break;
        case 0: --fmt; break;
        default: *o++ = '%'; if (*fmt) *o++ = *fmt; break;
        }
    }
    *o = 0;
    va_end(ap);
    return (int) (o - out);
}

/* --- VERSION.dll: VS_VERSIONINFO dalla risorsa RT_VERSION (16), nome 1 ---
 * Serve a leggere la versione di un file (l'eseguibile o una DLL dei dati). Se il file non e'
 * indicato si usa l'eseguibile (risorse gia' mappate a 0x400000). */
static UiRes *version_res(const char *file) {
    unsigned int h;
    win32_ui_init();
    if (!file || !*file) {
        h = 0x400000u;
    } else {
        h = win32_ui_load_library(file);
        if (!h) h = 0x400000u;
    }
    return (UiRes *) res_find(h, (const char *) (unsigned long) 1, (const char *) (unsigned long) 16);
}
S unsigned int GetFileVersionInfoSizeA(const char *file, unsigned int *handle) {
    UiRes *r = version_res(file);
    if (handle) *handle = 0;
    return r ? r->size : 0;
}
S int GetFileVersionInfoA(const char *file, unsigned int handle, unsigned int len, void *data) {
    UiRes *r = version_res(file);
    (void) handle;
    if (!r || !data) return 0;
    if (len > r->size) len = r->size;
    memcpy(data, r->data, len);
    return 1;
}
S int VerQueryValueA(const void *block, const char *sub, void **ptr, unsigned int *len) {
    const unsigned char *p = (const unsigned char *) block;
    if (!block || !sub || !ptr || !len) return 0;
    if (sub[0] == '\\' && sub[1] == 0) {
        /* VS_FIXEDFILEINFO segue la chiave "VS_VERSION_INFO" (UTF-16, NUL incluso) + padding */
        const unsigned short *key = (const unsigned short *) (p + 6);
        unsigned int off;
        while (*key) ++key;
        off = (unsigned int) ((const unsigned char *) key - p) + 2;
        off = (off + 3u) & ~3u;
        *ptr = (void *) (p + off);
        *len = 13u * 4u;                                  /* sizeof(VS_FIXEDFILEINFO) */
        return 1;
    }
    *ptr = 0;
    *len = 0;
    return 0;
}

/* ============================================================================
 * MODELLO FINESTRE MINIMO (W2) + coda messaggi.
 * Serve al percorso di avvio (GetForegroundWindow ecc.), ai dialoghi e al GDI.
 * Le finestre hanno hwnd sintetici, gerarchia, GWL_*, proprieta' stringa e un canvas 8 bit.
 * ==========================================================================*/
#define MAX_WIN 96

/* definiti piu' sotto (coda messaggi); servono a ShowWindow per consegnare l'attivazione */
__attribute__((stdcall)) int PostMessageA(unsigned int, unsigned int, unsigned int, unsigned int);
unsigned int GetCurrentThreadId(void);
S int DefWindowProcA(unsigned int, unsigned int, unsigned int, unsigned int);
static S int ui_control_proc(unsigned int, unsigned int, unsigned int, unsigned int);

typedef struct {
    int used;
    unsigned int hwnd, parent;
    int is_child;
    unsigned int id;
    unsigned int style, exstyle;
    char cls[64];
    char title[4096];
    int x, y, w, h;
    int visible;
    void *wndproc;
    unsigned int userdata;
    unsigned char *canvas;          /* canvas 8 bit (w*h), NULL finche' non serve */
    unsigned int cw, ch;
    int enabled;
    unsigned int dwl[3], font;
    int check, limit, selection, item_count;
    int edit_anchor, edit_caret, pressed;
    char *items[128];
    int activated;            /* WM_ACTIVATEAPP gia' consegnato */
} UiWin;

static UiWin g_win[MAX_WIN];
static unsigned int g_next_hwnd = 0x10001u;
static unsigned int g_foreground, g_active, g_focus, g_capture, g_saved_focus;
static unsigned int g_game_hwnd, g_paint_tick;
static int g_paint_pending, g_input_pumping, g_cursor_count;

static UiWin *win_of(unsigned int h) {
    int i;
    for (i = 0; i < MAX_WIN; ++i) if (g_win[i].used && g_win[i].hwnd == h) return &g_win[i];
    return 0;
}
static UiWin *win_new(unsigned int parent, int is_child, const char *cls, const char *title,
                      unsigned int style, int x, int y, int w, int h) {
    int i;
    UiWin *wn = 0;
    for (i = 0; i < MAX_WIN; ++i) if (!g_win[i].used) { wn = &g_win[i]; break; }
    if (!wn) return 0;
    memset(wn, 0, sizeof *wn);
    wn->used = 1;
    wn->hwnd = g_next_hwnd++;
    wn->parent = parent;
    wn->is_child = is_child;
    wn->style = style;
    wn->x = x; wn->y = y; wn->w = w; wn->h = h;
    wn->visible = (style & 0x10000000u) != 0;
    wn->enabled = !(style & 0x08000000u);
    wn->limit = sizeof wn->title - 1;
    wn->selection = -1;
    if (cls) { unsigned i2 = 0; while (cls[i2] && i2 < 63) { wn->cls[i2] = cls[i2]; ++i2; } wn->cls[i2] = 0; }
    if (title) { unsigned i2 = 0; while (title[i2] && i2 < sizeof wn->title - 1) { wn->title[i2] = title[i2]; ++i2; } wn->title[i2] = 0; }
    wn->edit_anchor=wn->edit_caret=(int)strlen(wn->title);
    return wn;
}

S unsigned int GetForegroundWindow(void) { return g_foreground ? g_foreground : g_active; }
S unsigned int GetActiveWindow(void) { return g_active; }
S unsigned int SetActiveWindow(unsigned int h) { unsigned int old = g_active; g_active = h; return old; }
S unsigned int SetForegroundWindow(unsigned int h) { unsigned int old = g_foreground; g_foreground = h; g_active = h; return old ? 1 : 0; }
S unsigned int GetDesktopWindow(void) { return 0x10000u; }
S unsigned int GetFocus(void) { return g_focus; }
S int SendMessageA(unsigned int, unsigned int, unsigned int, unsigned int);
S unsigned int SetFocus(unsigned int h) {
    unsigned int old = g_focus;
    if (h && (!win_of(h) || !win_of(h)->enabled)) return 0;
    if (old == h) return old;
    g_focus = h;
    if(h) g_saved_focus=h;
    if(getenv("SC_INPUT_TRACE")) {
        char b[144]; UiWin *w=win_of(h);
        snprintf(b,sizeof b,"[input-focus] hwnd=0x%X id=%u cls=%s\n",h,w?w->id:0,w?w->cls:"none"); reasm_elf_log(b);
    }
    if (old) SendMessageA(old, 8, h, 0);
    if (h) SendMessageA(h, 7, old, 0);
    dlg_elf_dirty(old); dlg_elf_dirty(h);
    return old;
}
S unsigned int GetCapture(void) { return g_capture; }
S unsigned int SetCapture(unsigned int h) {
    unsigned int old=g_capture;
    if(h && !win_of(h)) return 0;
    g_capture=h; if(host_elf_mouse_capture) host_elf_mouse_capture(h!=0); return old;
}
S int ReleaseCapture(void) { g_capture=0; if(host_elf_mouse_capture) host_elf_mouse_capture(0); return 1; }
S unsigned int GetParent(unsigned int h) { UiWin *w = win_of(h); return w ? w->parent : 0; }
S unsigned int GetTopWindow(unsigned int h) {
    int i;
    for (i = 0; i < MAX_WIN; ++i)
        if (g_win[i].used && g_win[i].parent == h) return g_win[i].hwnd;
    return 0;
}
S unsigned int GetWindow(unsigned int h, unsigned int cmd) {
    UiWin *w = win_of(h); unsigned int best=0; int i;
    if (!w) return 0;
    if (cmd==5) { for(i=0;i<MAX_WIN;++i) if(g_win[i].used && g_win[i].is_child && g_win[i].parent==h && (!best || g_win[i].hwnd<best)) best=g_win[i].hwnd; return best; }
    if (cmd==4) return w->is_child ? 0 : w->parent;
    for(i=0;i<MAX_WIN;++i) {
        UiWin *p=&g_win[i];
        if(!p->used || p->parent!=w->parent || p->is_child!=w->is_child) continue;
        if ((cmd==0 || (cmd==2 && p->hwnd>h)) && (!best || p->hwnd<best)) best=p->hwnd;
        if ((cmd==1 || (cmd==3 && p->hwnd<h)) && (!best || p->hwnd>best)) best=p->hwnd;
    }
    return best;
}
S unsigned int GetWindowThreadProcessId(unsigned int h, unsigned int *pid) {
    (void) h; if (pid) *pid = 1; return 1;
}
S int IsIconic(unsigned int h) { (void) h; return 0; }
S int IsWindow(unsigned int h) { return win_of(h) != 0; }
S int IsWindowVisible(unsigned int h) { UiWin *w = win_of(h); return w ? w->visible : 0; }
S int IsWindowEnabled(unsigned int h) { UiWin *w = win_of(h); return w ? w->enabled : 0; }
S int EnableWindow(unsigned int h, int en) {
    UiWin *w = win_of(h); int was;
    if (!w) return 0;
    was = !w->enabled; en = en != 0;
    if (w->enabled != en) {
        w->enabled = en;
        if (en) w->style &= ~0x08000000u; else w->style |= 0x08000000u;
        SendMessageA(h, 0xA, en, 0);
        dlg_elf_dirty(h);
    }
    return was;
}
/* ShowWindow: oltre a rendere visibile, consegna l'attivazione come fa Windows.
 * Il WndProc del gioco (dispatch su WM_* a 0x4D1D80) usa WM_ACTIVATEAPP (0x1C) per il flag
 * "applicazione attiva" a 0x51BFA8; quel flag e' la condizione con cui il ciclo dei filmati
 * (sub_4D4470) chiama l'avanzamento fotogramma #457. Senza WM_ACTIVATEAPP il ciclo resta in
 * Sleep(0) per sempre (visto: hang dopo il primo filmato). In Windows il messaggio arriva alla
 * PRIMA attivazione della finestra: si consegna una volta sola, in coda (il gioco pompa i
 * messaggi dentro il proprio ciclo, sub_4D1BF0 -> PeekMessage/Translate/Dispatch). */
S int ShowWindow(unsigned int h, int cmd) {
    UiWin *w = win_of(h);
    if (w) {
        w->visible = (cmd != 0);
        if (!w->parent && cmd != 0 && !w->activated) {
            w->activated = 1;
            if (g_foreground == h || g_foreground == 0) { g_foreground = h; g_active = h; }
            if (g_uitrace) { reasm_elf_log("[win32] ShowWindow: WM_ACTIVATEAPP(TRUE) a ");
                             reasm_elf_log_hex(h); reasm_elf_log("\n"); }
            PostMessageA(h, 0x001Cu /*WM_ACTIVATEAPP*/, 1, (unsigned) GetCurrentThreadId());
            PostMessageA(h, 0x0006u /*WM_ACTIVATE*/, 1 /*WA_ACTIVE*/, 0);
            if(!g_focus) { g_focus=h; PostMessageA(h,7,0,0); }
        }
    }
    return 1;
}
S unsigned int GetDlgItem(unsigned int h, int id) {
    int i;
    for (i = 0; i < MAX_WIN; ++i)
        if (g_win[i].used && g_win[i].parent == h && (int) g_win[i].id == id) return g_win[i].hwnd;
    return 0;
}
S int GetDlgCtrlID(unsigned int h) { UiWin *w = win_of(h); return w ? (int) w->id : 0; }
S int SetDlgCtrlID(unsigned int h, int id) { UiWin *w = win_of(h); if (w) w->id = (unsigned) id; return 1; }
S unsigned int GetDlgItem_owner(unsigned int h) { return h; }

S unsigned long GetWindowLongA(unsigned int h, int idx) {
    UiWin *w = win_of(h);
    if (!w) return 0;
    if (idx == 0 || idx == 4 || idx == 8) return w->dwl[idx / 4];
    if (idx == -4 /*GWL_WNDPROC*/) return (unsigned long) (unsigned int) w->wndproc;
    if (idx == -21 /*GWL_USERDATA*/) return w->userdata;
    if (idx == -16 /*GWL_STYLE*/) return w->style;
    if (idx == -20 /*GWL_EXSTYLE*/) return w->exstyle;
    if (idx == -8 /*GWL_ID*/) return w->id;
    if (idx == -12 /*GWL_HWNDPARENT*/) return w->parent;
    return 0;
}
S unsigned long SetWindowLongA(unsigned int h, int idx, unsigned long v) {
    UiWin *w = win_of(h);
    unsigned long old = 0;
    if (!w) return 0;
    old = GetWindowLongA(h, idx);
    if (idx == 0 || idx == 4 || idx == 8) w->dwl[idx / 4] = v;
    else if (idx == -4) w->wndproc = (void *) v;
    else if (idx == -21) w->userdata = v;
    else if (idx == -16) w->style = v;
    else if (idx == -20) w->exstyle = v;
    else if (idx == -8) w->id = v;
    return old;
}
S int GetClassNameA(unsigned int h, char *buf, int n) {
    UiWin *w = win_of(h); int i = 0;
    if (!w || !buf || n <= 0) return 0;
    while (w->cls[i] && i < n - 1) { buf[i] = w->cls[i]; ++i; }
    buf[i] = 0;
    return i;
}
S int GetWindowTextA(unsigned int h, char *buf, int n) {
    UiWin *w = win_of(h); int i = 0;
    if (!buf || n <= 0) return 0;
    if (!w) { buf[0] = 0; return 0; }
    while (w->title[i] && i < n - 1) { buf[i] = w->title[i]; ++i; }
    buf[i] = 0;
    return i;
}
S int SetWindowTextA(unsigned int h, const char *s) {
    UiWin *w = win_of(h); int i = 0;
    if (!w) return 0;
    if (!s) s = "";
    while (s[i] && i < sizeof w->title - 1) { w->title[i] = s[i]; ++i; }
    w->title[i] = 0;
    w->edit_anchor=w->edit_caret=i;
    dlg_elf_dirty(h);
    return 1;
}
S int GetWindowTextLengthA(unsigned int h) { UiWin *w = win_of(h); return w ? (int)strlen(w->title) : 0; }
S int SetDlgItemTextA(unsigned int h, int id, const char *s) {
    return SetWindowTextA(GetDlgItem(h, id), s);
}
S int GetDlgItemTextA(unsigned int h, int id, char *buf, int n) {
    return GetWindowTextA(GetDlgItem(h, id), buf, n);
}
S int GetClientRect(unsigned int h, int *rc) {
    UiWin *w = win_of(h);
    if (!rc) return 0;
    rc[0] = 0; rc[1] = 0;
    rc[2] = w ? w->w : 0;
    rc[3] = w ? w->h : 0;
    return 1;
}
extern int host_elf_window_origin(int *, int *) __attribute__((weak));
S int GetWindowRect(unsigned int h, int *rc) {
    UiWin *w = win_of(h);
    if (!rc) return 0;
    if (!w) { memset(rc, 0, 16); return 1; }
    rc[0] = w->x; rc[1] = w->y;
    /* Finestra top-level: il rettangolo e' in coordinate SCHERMO. L'origine vera e' quella che il
     * window manager ha dato alla finestra host (SDL), NON le coordinate interne usate dal gioco
     * (tipicamente 0,0). Senza questa correzione GetCursorPos (screen) e ScreenToClient (basato su
     * un'origine 0,0) vivono in due spazi diversi: con la finestra spostata dal WM, ScreenToClient
     * restituisce y_screen > altezza client e il gioco crede il cursore sul bordo inferiore. */
    /* L'origine resta quella interna (0,0): e' lo spazio di coordinate del gioco (SetCursorPos
     * riceve coordinate client, vedi host_elf_mouse_warp). Gate diagnostico dichiarato per l'A/B:
     * SC_WINRECT_SCREEN_ORIGIN=1 somma l'origine reale della finestra (comportamento "schermo"). */
    if (!w->is_child && host_elf_window_origin) {
        extern char *getenv(const char *);
        const char *scr = getenv("SC_WINRECT_SCREEN_ORIGIN");
        int ox = 0, oy = 0;
        if (scr && scr[0] == '1' && !scr[1] && host_elf_window_origin(&ox, &oy) == 0) {
            rc[0] += ox; rc[1] += oy;
        }
    }
    { UiWin *p = w; int depth = 0; while (p->is_child && depth++ < MAX_WIN && (p = win_of(p->parent))) { rc[0] += p->x; rc[1] += p->y; } }
    rc[2] = rc[0] + w->w; rc[3] = rc[1] + w->h;
    return 1;
}
S int SetRect(int *rc, int l, int t, int r, int b) {
    if (!rc) return 0;
    rc[0] = l; rc[1] = t; rc[2] = r; rc[3] = b;
    return 1;
}
S int OffsetRect(int *rc, int dx, int dy) {
    if (!rc) return 0;
    rc[0] += dx; rc[1] += dy; rc[2] += dx; rc[3] += dy;
    return 1;
}
S int PtInRect(const int *rc, int x, int y) {
    return rc && x >= rc[0] && x < rc[2] && y >= rc[1] && y < rc[3];
}
/* ClientToScreen/ScreenToClient: se l'hwnd non e' nella nostra tabella si usa la finestra di gioco
 * (fallback) invece di lasciare le coordinate INVARIATE e restituire 0. Lasciare invariato un valore
 * in coordinate schermo e' esattamente il modo in cui un difetto di coordinate diventa invisibile:
 * il chiamante crede di avere coordinate client e usa y_screen contro l'altezza del client. */
S int ClientToScreen(unsigned int h, int *pt) {
    int r[4];
    if (!pt) return 0;
    if (!win_of(h)) h = g_game_hwnd;
    if (!win_of(h)) return 0;
    GetWindowRect(h, r); pt[0] += r[0]; pt[1] += r[1]; return 1;
}
S int ScreenToClient(unsigned int h, int *pt) {
    int r[4];
    if (!pt) return 0;
    if (!win_of(h)) h = g_game_hwnd;
    if (!win_of(h)) return 0;
    GetWindowRect(h, r); pt[0] -= r[0]; pt[1] -= r[1]; return 1;
}

S int SetWindowPos(unsigned int h, unsigned int after, int x, int y, int cx, int cy, unsigned int flags) {
    UiWin *w = win_of(h); (void)after;
    if (!w) return 0;
    if (g_uitrace) { char b[96]; snprintf(b,sizeof b,"[win32] SetWindowPos hwnd=0x%X %d,%d %dx%d flags=0x%X\n",h,x,y,cx,cy,flags); reasm_elf_log(b); }
    if (!(flags & 2)) { w->x=x; w->y=y; }
    if (!(flags & 1)) { w->w=cx; w->h=cy; }
    if (flags & 0x40) w->visible=1;
    if (flags & 0x80) w->visible=0;
    dlg_elf_dirty(h); return 1;
}
S int GetCursorPos(int *pt) {
    if(!pt) return 0;
    if(host_elf_mouse_screen) {
        int rc=host_elf_mouse_screen(pt,pt+1)==0;
        if(getenv("REASM_TRACE")) {
            char b[200]; int wr[4]={0,0,0,0}; GetWindowRect(g_game_hwnd,wr);
            /* Stato del cursore SECONDO IL GIOCO (globali 0x6CDDC0 stato, 0x6CDDC4/8 x,y): se diverge
             * da GetCursorPos, il gioco sta leggendo la posizione da un'altra fonte (WM_MOUSEMOVE). */
            unsigned int gs=*(volatile unsigned int*)0x6CDDC0u;
            unsigned int gx=*(volatile unsigned int*)0x6CDDC4u;
            unsigned int gy=*(volatile unsigned int*)0x6CDDC8u;
            snprintf(b,sizeof b,"[cursor] GetCursorPos -> %d,%d rect=%d,%d,%d,%d game_mouse=0x%X xy=%u,%u\n",
                pt[0],pt[1],wr[0],wr[1],wr[2],wr[3],gs,gx,gy);
            reasm_elf_log(b);
        }
        return rc;
    }
    pt[0]=pt[1]=0; return 1;
}
S int SetCursorPos(int x, int y) {
    return host_elf_mouse_warp ? host_elf_mouse_warp(x,y)==0 : 1;
}
extern int host_elf_mouse_clip(int, int, int, int, int) __attribute__((weak));
S int ClipCursor(const void *r0) {
    if (!host_elf_mouse_clip) return 1;
    if (!r0) return host_elf_mouse_clip(0, 0, 0, 0, 0) == 0;
    { const int *r = (const int *) r0; return host_elf_mouse_clip(1, r[0], r[1], r[2], r[3]) == 0; }
}
S int InvalidateRect(unsigned int h, const void *rc, int erase) {
    (void)rc; (void)erase; dlg_elf_dirty(h);
    if(h==g_game_hwnd && h) g_paint_pending=1;
    return 1;
}
S int UpdateWindow(unsigned int h) { if(h==g_game_hwnd && h) g_paint_pending=1; return 1; }
S int GetUpdateRgn(unsigned int h, void *rgn, int erase) { (void) h; (void) rgn; (void) erase; return 1; }
S int SystemParametersInfoA(unsigned int act, unsigned int p, void *v, unsigned int f) {
    if (act == 0x30 /*SPI_GETWORKAREA*/ && v) { ((int *) v)[0] = 0; ((int *) v)[1] = 0;
        ((int *) v)[2] = 640; ((int *) v)[3] = 480; return 1; }
    (void) p; (void) f;
    return 1;
}

/* --- CODA MESSAGGI (MSG a 32 bit: 28 byte) --- */
#define MAX_Q 256
/* Public MSG remains 28 bytes; trailing input snapshot is INTERNAL only. */
typedef struct {
    unsigned int hwnd, message, wparam, lparam, time; int px, py;
    int is_input; struct host_input_event input;
} QMsg;
static QMsg g_q[MAX_Q];
static int g_qh, g_qt, g_quit;

S int PostMessageA(unsigned int hwnd, unsigned int msg, unsigned int wp, unsigned int lp) {
    int n = (g_qt + 1) % MAX_Q;
    if (n == g_qh) return 0;
    g_q[g_qt].hwnd = hwnd; g_q[g_qt].message = msg;
    g_q[g_qt].wparam = wp; g_q[g_qt].lparam = lp;
    g_q[g_qt].time = host_elf_ticks ? host_elf_ticks() : 0;
    g_q[g_qt].px = 0; g_q[g_qt].py = 0; g_q[g_qt].is_input=0;
    g_qt = n;
    if(g_uitrace && (msg==0x201 || msg==0x202)) {
        UiWin *w=win_of(hwnd); char b[200];
        snprintf(b,sizeof b,"[input-route] post hwnd=0x%X cls=%s parent=0x%X msg=0x%X wp=0x%X lp=0x%X focus=0x%X capture=0x%X\n",
            hwnd,w?w->cls:"none",w?w->parent:0,msg,wp,lp,g_focus,g_capture); reasm_elf_log(b);
    }
    /* Traccia dei MOVIMENTI del mouse: attiva solo con SC_MOUSE_TRACE=1 (volume alto).
     * Serve a riprodurre un difetto di interazione: senza i movimenti non si sa dove si trovava
     * il cursore quando il gioco ha deciso di reagire (o di crashare). */
    if(msg==0x200u && getenv("SC_MOUSE_TRACE")) {
        char b[128];
        snprintf(b,sizeof b,"[mouse] t=%u move hwnd=0x%X x=%d y=%d focus=0x%X capture=0x%X\n",
            (unsigned)(host_elf_ticks ? host_elf_ticks() : 0),
            hwnd,(int)(short)(lp & 0xffff),(int)(short)((lp>>16)&0xffff),g_focus,g_capture);
        reasm_elf_log(b);
    }
    return 1;
}
S void PostQuitMessage(int code) { g_quit = code; PostMessageA(0, 0x12u, (unsigned) code, 0); }
S int SendMessageA(unsigned int hwnd, unsigned int msg, unsigned int wp, unsigned int lp) {
    UiWin *w = win_of(hwnd);
    if(g_uitrace && (msg==0x201 || msg==0x202 || msg==0x111)) {
        char b[200];
        snprintf(b,sizeof b,"[input-route] wndproc hwnd=0x%X cls=%s parent=0x%X msg=0x%X wp=0x%X lp=0x%X proc=0x%X focus=0x%X capture=0x%X\n",
            hwnd,w?w->cls:"none",w?w->parent:0,msg,wp,lp,w?(unsigned int)(unsigned long)w->wndproc:0,g_focus,g_capture); reasm_elf_log(b);
    }
    if (w && w->wndproc) {
        int result;
#if defined(__arm__) || defined(__i386__)
        if (GUEST_CALLABLE(w->wndproc)) {
            result = arm_guest_call4((unsigned int)(unsigned long) w->wndproc, hwnd, msg, wp, lp);
        } else
#endif
        {
        int (__attribute__((stdcall)) *p)(unsigned int, unsigned int, unsigned int, unsigned int) =
            (int (__attribute__((stdcall)) *)(unsigned int, unsigned int, unsigned int, unsigned int)) w->wndproc;
        result=p(hwnd, msg, wp, lp);
        }
        if(g_uitrace && (msg==0x201 || msg==0x202)) {
            int cursor[2]={-1,-1},wr[4]={0,0,0,0},cr[4]={0,0,0,0}; char b[240];
            unsigned int ms=*(volatile unsigned short *)0x6CDDC0u;
            unsigned int mx=*(volatile unsigned int *)0x6CDDC4u, my=*(volatile unsigned int *)0x6CDDC8u;
            unsigned int downcb=*(volatile unsigned int *)0x5968B0u, upcb=*(volatile unsigned int *)0x5968B4u;
            GetCursorPos(cursor); GetWindowRect(hwnd,wr); GetClientRect(hwnd,cr);
            snprintf(b,sizeof b,"[input-route] wndproc-return hwnd=0x%X msg=0x%X result=%d cursor=%d,%d winrect=%d,%d,%d,%d client=%d,%d focus=0x%X capture=0x%X storm_mouse=0x%X xy=%u,%u cb=0x%X/0x%X\n",
                hwnd,msg,result,cursor[0],cursor[1],wr[0],wr[1],wr[2],wr[3],cr[2],cr[3],g_focus,g_capture,ms,mx,my,downcb,upcb); reasm_elf_log(b);
        }
        return result;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
S int CallWindowProcA(void *proc, unsigned int h, unsigned int m, unsigned int w, unsigned int l) {
#if defined(__arm__) || defined(__i386__)
    if (proc && GUEST_CALLABLE(proc))
        return arm_guest_call4((unsigned int)(unsigned long) proc, h, m, w, l);
#endif
    return proc ? ((DlgProc)proc)(h,m,w,l) : 0;
}
static int input_within(unsigned int top,unsigned int h) {
    int depth=0; while(h && depth++<MAX_WIN) { if(h==top) return 1; h=GetParent(h); }
    return 0;
}
/* Non-blocking bridge. Rendering calls this to POST only; WndProc execution
 * remains exclusively in the existing game's/modal Peek/Dispatch loop. */
void win32_input_pump(void) {
    struct host_input_event e; input_elf_message out[32];
    unsigned int top,now; UiWin *w; int i,n,wid,hei,freeq;
    if(g_input_pumping || !host_elf_input_owner || !host_elf_input_owner() || !input_elf_translate) return;
    g_input_pumping=1;
    host_elf_pump();
    top=g_active?g_active:(g_foreground?g_foreground:g_game_hwnd);
    w=win_of(top);
    /* A modal owner is disabled; after SDL focus loss its foreground HWND is
     * not a valid fallback. Keep draining events for the visible modal window. */
    if(!w || !w->enabled) {
        top=0;
        for(i=0;i<MAX_WIN;++i) if(g_win[i].used && !g_win[i].is_child &&
            g_win[i].visible && g_win[i].enabled && g_win[i].hwnd>top) top=g_win[i].hwnd;
        w=win_of(top);
    }
    if(w && w->enabled) {
        while((freeq=(g_qh-g_qt-1+MAX_Q)%MAX_Q)>=32 && host_elf_next_input(&e)) {
            unsigned int target=top;
            int rawx=e.x,rawy=e.y; unsigned int focus_before=g_focus,capture_before=g_capture;
            int mouse=e.type==HOST_INPUT_MOVE || e.type==HOST_INPUT_DOWN || e.type==HOST_INPUT_UP;
            if(mouse) {
                host_elf_window_size(&wid,&hei);
                if(e.x<0 || e.y<0 || e.x>=wid || e.y>=hei) {
                    if(g_uitrace && (e.type==HOST_INPUT_DOWN || e.type==HOST_INPUT_UP)) {
                        char b[160]; snprintf(b,sizeof b,"[input-route] drop outside host=%s xy=%d,%d size=%dx%d top=0x%X focus=0x%X capture=0x%X\n",
                            e.type==HOST_INPUT_DOWN?"DOWN":"UP",rawx,rawy,wid,hei,top,focus_before,capture_before); reasm_elf_log(b);
                    }
                    continue;
                }
                if(g_capture && IsWindow(g_capture)) {
                    int r[4]; target=g_capture;
                    if(win_of(target)->is_child) { GetWindowRect(target,r); e.x-=r[0]; e.y-=r[1]; }
                }
                else {
                    int best=-1;
                    for(i=0;i<MAX_WIN;++i) {
                        UiWin *c=&g_win[i]; int r[4];
                        if(!c->used || !c->is_child || c->parent!=top || !c->visible || !c->enabled) continue;
                        GetWindowRect(c->hwnd,r);
                        if(e.x>=r[0] && e.x<r[2] && e.y>=r[1] && e.y<r[3]) best=i;
                    }
                    if(best>=0) {
                        int r[4]; target=g_win[best].hwnd; GetWindowRect(target,r); e.x-=r[0]; e.y-=r[1];
                    }
                }
            } else if(e.type==HOST_INPUT_KEYDOWN || e.type==HOST_INPUT_KEYUP || e.type==HOST_INPUT_TEXT) {
                if(g_focus && IsWindowEnabled(g_focus)) target=g_focus;
            }
            if(e.type==HOST_INPUT_EXPOSE) { if(g_game_hwnd) g_paint_pending=1; continue; }
            n=input_elf_translate(&e,out);
            for(i=0;i<n;++i) {
                if(g_uitrace && (out[i].message==0x201 || out[i].message==0x202)) {
                    char b[220]; snprintf(b,sizeof b,"[input-route] route host=%s raw=%d,%d client=%d,%d msg=0x%X target=0x%X top=0x%X focus0=0x%X capture0=0x%X focus=0x%X capture=0x%X\n",
                        e.type==HOST_INPUT_DOWN?"DOWN":"UP",rawx,rawy,e.x,e.y,out[i].message,target,top,focus_before,capture_before,g_focus,g_capture); reasm_elf_log(b);
                }
                int slot=g_qt;
                unsigned int recipient=target;
                if(e.type==HOST_INPUT_FOCUS && i==2) {
                    unsigned int focus=g_focus?g_focus:g_saved_focus;
                    if(IsWindowEnabled(focus) && input_within(top,focus)) recipient=focus;
                }
                if(PostMessageA(recipient,out[i].message,out[i].wparam,out[i].lparam)) {
                    g_q[slot].is_input=1; g_q[slot].input=e;
                    g_q[slot].px=e.x; g_q[slot].py=e.y;
                }
            }
        }
    }
    /* Periodic safety paint: DEFERRED, only when no descriptor/compositor lock.
     * The game VA is read only if SWarClass was actually registered/created. */
    now=host_elf_ticks();
    if(g_game_hwnd && (unsigned int)(now-g_paint_tick)>=17) g_paint_pending=1;
    if(g_game_hwnd && g_paint_pending && !*(volatile unsigned int *)0x6CF4A8u) {
        int queued=0;
        for(i=g_qh;i!=g_qt;i=(i+1)%MAX_Q)
            if(g_q[i].hwnd==g_game_hwnd && g_q[i].message==0xF) queued=1;
        if(queued || PostMessageA(g_game_hwnd,0xF,0,0)) { g_paint_pending=0; g_paint_tick=now; }
    }
    g_input_pumping=0;
}
S int DispatchMessageA(const void *msg) {
    const unsigned int *m = (const unsigned int *) msg;
    if(!msg) return 0;
    if(m[0]==g_game_hwnd && m[0] && m[1]==0xF && *(volatile unsigned int *)0x6CF4A8u) {
        g_paint_pending=1; return 0;
    }
    if(host_elf_dispatch_state) host_elf_dispatch_state(m[1],m[2]);
    if(m[1]==6) { g_active=(m[2]&0xFFFF)?m[0]:0; if(g_active) g_foreground=m[0]; }
    if(m[1]==7) g_focus=m[0];
    if(m[1]==8 && g_focus==m[0]) { g_saved_focus=g_focus; g_focus=0; }
    if(g_uitrace && (m[1]==0x201 || m[1]==0x202)) {
        char b[240]; UiWin *w=win_of(m[0]);
        snprintf(b,sizeof b,"[input-route] dispatch hwnd=0x%X cls=%s msg=0x%X wp=0x%X lp=0x%X caller=0x%X focus=0x%X capture=0x%X storm_mouse=0x%X xy=%u,%u\n",
            m[0],w?w->cls:"none",m[1],m[2],m[3],(unsigned int)(unsigned long)__builtin_return_address(0),g_focus,g_capture,
            *(volatile unsigned int *)0x6CDDC0u,*(volatile unsigned int *)0x6CDDC4u,*(volatile unsigned int *)0x6CDDC8u); reasm_elf_log(b);
    }
    if(getenv("SC_INPUT_TRACE") &&
        (m[1]==0x200 || m[1]==0x201 || m[1]==0x202 || m[1]==0x100 || m[1]==0x101 || m[1]==0x102)) {
        static unsigned int moves; char b[144];
        if(m[1]!=0x200 || moves++<20) {
            snprintf(b,sizeof b,"[input] hwnd=0x%X dispatch msg=0x%X wp=0x%X lp=0x%X key-a=0x%X mouse-left=0x%X\n",
                m[0],m[1],m[2],m[3],GetKeyState('a'),GetKeyState(1)); reasm_elf_log(b);
        }
    }
    return SendMessageA(m[0], m[1], m[2], m[3]);
}
S int TranslateMessage(const void *msg) { (void) msg; return 0; }
S int PeekMessageA(void *msg, unsigned int hwnd, unsigned int mn, unsigned int mx, unsigned int remove) {
    int i;
    if(!msg) return 0;
    win32_input_pump();
    for(i=g_qh;i!=g_qt;i=(i+1)%MAX_Q) {
        if(g_q[i].hwnd==g_game_hwnd && g_game_hwnd && g_q[i].message==0xF && *(volatile unsigned int *)0x6CF4A8u) continue;
        QMsg *q=&g_q[i];
        /* Logical child focus changes after Tab/mouse dispatch, not while SDL
         * was polled. Resolve host keyboard recipients at dequeue time. */
        if(q->is_input && (q->input.type==HOST_INPUT_KEYDOWN ||
            q->input.type==HOST_INPUT_KEYUP || q->input.type==HOST_INPUT_TEXT) && IsWindowEnabled(g_focus)) q->hwnd=g_focus;
        unsigned int p=q->hwnd; int match=!hwnd || hwnd==p;
        if(hwnd==0xFFFFFFFFu) match=!p;
        else { int depth=0; while(!match && p && depth++<MAX_WIN) { p=GetParent(p); match=p==hwnd; } }
        if(q->message!=0x12 && (!match || ((mn || mx) && (q->message<mn || q->message>mx)))) continue;
        memcpy(msg,q,28); /* NEVER copy internal state beyond the Win32 MSG ABI */
        if(remove&1) {
            if(g_uitrace && q->is_input && (q->message==0x201 || q->message==0x202)) {
                char b[200]; snprintf(b,sizeof b,"[input-route] dequeue hwnd=0x%X msg=0x%X wp=0x%X lp=0x%X focus=0x%X capture=0x%X\n",
                    q->hwnd,q->message,q->wparam,q->lparam,g_focus,g_capture); reasm_elf_log(b);
            }
            if(q->is_input && host_elf_input_state) host_elf_input_state(&q->input);
            int j=i, k=(j+1)%MAX_Q;
            while(k!=g_qt) { g_q[j]=g_q[k]; j=k; k=(k+1)%MAX_Q; }
            g_qt=j;
        }
        return 1;
    }
    return 0;
}
S int GetMessageA(void *msg, unsigned int hwnd, unsigned int mn, unsigned int mx) {
    extern int usleep(unsigned int);
    if(!msg || (hwnd && hwnd!=0xFFFFFFFFu && !IsWindow(hwnd))) { SetLastError(1400); return -1; }
    while(!PeekMessageA(msg,hwnd,mn,mx,1)) usleep(10000);
    return ((unsigned int *)msg)[1]!=0x12;
}

/* --- ACCELERATORI ---
 * RT_ACCELERATOR = 9. La tabella e' un array di ACCEL { WORD fVirt; WORD key; WORD cmd; WORD pad }.
 * LoadAcceleratorsA restituisce un handle opaco (la nostra voce di risorsa). */
S void *LoadAcceleratorsA(void *hinst, const char *name) {
    return res_find((unsigned int) (unsigned long) hinst, name, (const char *) (unsigned long) 9);
}
S void *CreateAcceleratorTableA(const void *accel,int n) {
    const unsigned char *src=accel; unsigned char *tab; UiRes *r=0; int i;
    if(!src || n<=0 || n>65535) return 0;
    for(i=0;i<g_res_n;++i) if(g_res[i].owned && !g_res[i].data) { r=&g_res[i]; break; }
    if(!r && g_res_n<MAX_RES) r=&g_res[g_res_n++];
    if(!r || !(tab=malloc((unsigned long)n*8))) return 0;
    memset(tab,0,(unsigned long)n*8);
    for(i=0;i<n;++i) {
        *(unsigned short *)(tab+i*8)=src[i*6]&0x7F;
        *(unsigned short *)(tab+i*8+2)=*(const unsigned short *)(src+i*6+2);
        *(unsigned short *)(tab+i*8+4)=*(const unsigned short *)(src+i*6+4);
    }
    r->data=tab; r->size=(unsigned int)n*8; r->rtype=9; r->rname=0; r->owned=1; return r;
}
S int CopyAcceleratorTableA(void *hacc,void *out,int n) {
    UiRes *r=res_handle(hacc); unsigned char *dst=out; int i,count;
    if(!r || !r->data || r->rtype!=9) return 0;
    count=(int)(r->size/8); if(!out) return count;
    if(n<=0) return 0;
    if(count>n) count=n;
    for(i=0;i<count;++i) {
        dst[i*6]=*((unsigned char *)r->data+i*8)&0x7F; dst[i*6+1]=0;
        memcpy(dst+i*6+2,(unsigned char *)r->data+i*8+2,4);
    }
    return count;
}
S int DestroyAcceleratorTable(void *hacc) {
    UiRes *r=res_handle(hacc); if(!r || !r->data || r->rtype!=9) return 0;
    if(r->owned) { free(r->data); r->data=0; r->size=0; }
    return 1; /* resource-backed tables have module lifetime */
}
/* host_api uses 0x80; Win32 SHORT uses bit 15 (including signed < 0 tests).
 * Keep the backend contract unchanged, translate only at the Win32 boundary. */
S int GetKeyState(int vk) { return host_elf_key_state && (host_elf_key_state(vk)&0x80) ? -32768 : 0; }
S int TranslateAcceleratorA(unsigned int hwnd,void *hacc,const void *msg) {
    const unsigned int *m=msg; UiRes *r=res_handle(hacc); unsigned int i,mods;
    if(!m || !r || !r->data || r->rtype!=9 || !IsWindowEnabled(hwnd)) return 0;
    if(m[1]!=0x100 && m[1]!=0x104 && m[1]!=0x102 && m[1]!=0x106) return 0;
    mods=((GetKeyState(16)&0x8000)?4:0)|((GetKeyState(17)&0x8000)?8:0)|((GetKeyState(18)&0x8000)?16:0);
    for(i=0;i<r->size/8;++i) {
        const unsigned short *a=(const unsigned short *)((const unsigned char *)r->data+i*8);
        int virtual_key=a[0]&1,down=m[1]==0x100 || m[1]==0x104;
        if(virtual_key!=down || (a[0]&0x1C)!=mods || a[1]!=m[2]) continue;
        SendMessageA(hwnd,0x111,(unsigned int)a[2]|0x10000u,0); return 1;
    }
    return 0;
}

/* --- REGISTRO CLASSI + CREAZIONE FINESTRE ---
 * WNDCLASSA a 32 bit: style@0, lpfnWndProc@4, hInstance@16, hbrBackground@28, lpszClassName@36.
 * WNDCLASSEX: cbSize@0, style@4, lpfnWndProc@8, hInstance@20, hbrBackground@32, lpszClassName@40. */
typedef struct { int used; char name[64]; void *wndproc; } UiClass;
static UiClass g_cls[24];
static int g_cls_n;
static unsigned int g_next_atom = 0xC000;

static int edit_replace(UiWin *w,const char *text) {
    char copy[4096]; int a=w->edit_anchor,b=w->edit_caret,n=(int)strlen(w->title),len,room;
    unsigned int h=w->hwnd,parent=w->parent,id=w->id;
    if(w->style&0x800) return 0;
    if(a>b) { int t=a; a=b; b=t; }
    room=w->limit-(n-(b-a)); if(room<0) room=0;
    if(room>(int)sizeof copy-1-(n-(b-a))) room=(int)sizeof copy-1-(n-(b-a));
    for(len=0;text && text[len] && len<room;++len) copy[len]=text[len];
    if(!len && a==b) return 0;
    memmove(w->title+a+len,w->title+b,(unsigned long)(n-b+1));
    memcpy(w->title+a,copy,(unsigned long)len); w->edit_anchor=w->edit_caret=a+len;
    dlg_elf_dirty(h);
    SendMessageA(parent,0x111,id|(0x400u<<16),h); /* EN_UPDATE, before repaint */
    if(IsWindow(h)) SendMessageA(parent,0x111,id|(0x300u<<16),h); /* EN_CHANGE once */
    return 1;
}
static int edit_position(UiWin *w,int x) {
    int pos=0,n=(int)strlen(w->title); x-=4;
#ifdef SC_ELF_REAL_GDI
    void *dc=gdi_elf_window_dc(),*old=dc?SelectObject(dc,(void *)w->font):0; int size[2];
    if(dc) {
        while(pos<n && GetTextExtentPoint32A(dc,w->title,pos+1,size) && size[0]<=x) ++pos;
        SelectObject(dc,old); return pos;
    }
#endif
    pos=x>0?x/8:0; return pos<n?pos:n;
}
/* Built-in controls: text/font, edit limit/backspace, button check/click,
 * and string-backed combo/list selection. Custom registered classes retain
 * their own WndProc and can subclass this proc via CallWindowProcA. */
static S int ui_control_proc(unsigned int h, unsigned int msg, unsigned int wp, unsigned int lp) {
    UiWin *w=win_of(h); int combo, list, idx; unsigned int n;
    if(!w) return 0;
    if(msg==0xC) return SetWindowTextA(h,(const char *)lp);
    if(msg==0xD) return GetWindowTextA(h,(char *)lp,(int)wp);
    if(msg==0xE) return GetWindowTextLengthA(h);
    if(msg==0x30) { w->font=wp; dlg_elf_dirty(h); return 0; }
    if(msg==0x31) return (int)w->font;
    if(msg==0x87) {
        if(!strcasecmp(w->cls,"EDIT")) return 0x89 | ((wp==13 && (w->style&4) && (w->style&0x1000))?4:0); /* WANTCHARS|HASSETSEL|WANTARROWS; Tab stays dialog navigation */
        if(!strcasecmp(w->cls,"BUTTON")) { unsigned int type=w->style&15; return 0x2000 | (type==1?0x10:type==0?0x20:(type==4 || type==9)?0x40:0); }
        if(!strcasecmp(w->cls,"STATIC")) return 0x100;
        return 1;
    }
    if(!strcasecmp(w->cls,"BUTTON")) {
        if(msg==0xF0) return w->check;
        if(msg==0xF1) { w->check=(int)wp; dlg_elf_dirty(h); return 0; }
        if(msg==0x201 && w->enabled) { SetFocus(h); w->pressed=1; SetCapture(h); return 0; }
        if(msg==0x100 && wp==32 && w->enabled) { w->pressed=1; return 0; }
        if(msg==8 || msg==0x1F) { w->pressed=0; if(g_capture==h) ReleaseCapture(); return 0; }
        if((msg==0x202 || (msg==0x101 && wp==32)) && w->pressed) {
            int inside=(short)(lp&65535)>=0 && (short)(lp&65535)<w->w &&
                (short)(lp>>16)>=0 && (short)(lp>>16)<w->h;
            w->pressed=0; if(g_capture==h) ReleaseCapture();
            if(msg==0x202 && !inside) return 0;
            return SendMessageA(h,0xF5,0,0);
        }
        if(msg==0xF5) {
            unsigned int type=w->style&15;
            if(!w->enabled) return 0;
            if(type==3 || type==6) w->check=type==6?(w->check+1)%3:!w->check;
            if(type==9) w->check=1;
            dlg_elf_dirty(h);
            return SendMessageA(w->parent,0x111,w->id,h);
        }
    }
    if(!strcasecmp(w->cls,"EDIT")) {
        if(msg==0xC5) { w->limit=wp && wp<sizeof w->title?wp:sizeof w->title-1; return 0; }
        if(msg==0xD5) return w->limit;
        n=strlen(w->title);
        if(msg==0xB0) { /* EM_GETSEL: sorted bounds, optional DWORD outputs */
            unsigned int a=w->edit_anchor,b=w->edit_caret,t;
            if(a>b) { t=a; a=b; b=t; }
            if(wp) *(unsigned int *)wp=a;
            if(lp) *(unsigned int *)lp=b;
            return (int)(a|(b<<16));
        }
        if(msg==0xB1) { /* EM_SETSEL: -1 removes selection; endpoints clamp */
            int a=(int)wp,b=(int)lp;
            if(a==-1) { w->edit_anchor=w->edit_caret; return 0; }
            if(a<0) a=0;
            if(a>(int)n) a=n;
            if(b<0 || b>(int)n) b=n;
            w->edit_anchor=a; w->edit_caret=b; dlg_elf_dirty(h); return 0;
        }
        if(msg==0xC2) { edit_replace(w,(const char *)lp); return 0; }
        if(msg==0xCF) { if(wp) w->style|=0x800; else w->style&=~0x800; return 1; }
        if(msg==0x201) {
            int pos=edit_position(w,(short)(lp&65535)); SetFocus(h);
            if(!(GetKeyState(16)&0x8000)) w->edit_anchor=pos;
            w->edit_caret=pos; w->pressed=1; SetCapture(h); dlg_elf_dirty(h); return 0;
        }
        if(msg==0x200 && w->pressed) { w->edit_caret=edit_position(w,(short)(lp&65535)); dlg_elf_dirty(h); return 0; }
        if(msg==0x202 || msg==0x1F || msg==8) { w->pressed=0; if(g_capture==h) ReleaseCapture(); return 0; }
        if(msg==0x100) {
            int pos=w->edit_caret,shift=GetKeyState(16)&0x8000,ctrl=GetKeyState(17)&0x8000;
            if(ctrl && wp=='A') { w->edit_anchor=0; w->edit_caret=n; dlg_elf_dirty(h); return 0; }
            if(wp==46) { /* Delete belongs to KEYDOWN, backspace to CHAR only */
                if(w->edit_anchor==pos && pos<(int)n) ++w->edit_caret;
                edit_replace(w,""); return 0;
            }
            if(wp!=37 && wp!=39 && wp!=36 && wp!=35) return 0;
            if(wp==36) pos=0;
            else if(wp==35) pos=n;
            else if(!shift && w->edit_anchor!=pos) {
                if(wp==37 && w->edit_anchor<pos) pos=w->edit_anchor;
                if(wp==39 && w->edit_anchor>pos) pos=w->edit_anchor;
            } else if(wp==37 && pos>0) {
                --pos; if(ctrl) { while(pos>0 && w->title[pos]==' ') --pos; while(pos>0 && w->title[pos-1]!=' ') --pos; }
            } else if(wp==39 && pos<(int)n) {
                ++pos; if(ctrl) { while(pos<(int)n && w->title[pos-1]!=' ') ++pos; while(pos<(int)n && w->title[pos]==' ') ++pos; }
            }
            w->edit_caret=pos; if(!shift) w->edit_anchor=pos; dlg_elf_dirty(h); return 0;
        }
        if(msg==0x102) {
            char text[3]={0};
            if(GetKeyState(17)&0x8000) return 0; /* shortcuts must not insert letters */
            if(wp==8) {
                if(w->edit_anchor==w->edit_caret && w->edit_caret>0) --w->edit_anchor;
                edit_replace(w,""); return 0;
            }
            if(wp==13 && (w->style&4) && (w->style&0x1000)) { edit_replace(w,"\r\n"); return 0; }
            if(wp<32 || wp==127 || wp>255) return 0;
            if((w->style&0x2000) && (wp<'0' || wp>'9')) return 0;
            if((w->style&8) && wp>='a' && wp<='z') wp-=32;
            if((w->style&16) && wp>='A' && wp<='Z') wp+=32;
            text[0]=(char)wp; edit_replace(w,text); return 0;
        }
    }
    combo=!strcasecmp(w->cls,"COMBOBOX"); list=!strcasecmp(w->cls,"LISTBOX");
    if(combo || list) {
        unsigned int add=combo?0x143:0x180, reset=combo?0x14B:0x184;
        if(msg==add || msg==(combo?0x14A:0x181)) {
            const char *s=(const char *)lp; char *copy;
            if(!s || w->item_count>=128) return -1;
            copy=malloc(strlen(s)+1); if(!copy) return -2; strcpy_(copy,s);
            idx=(msg==add || (int)wp<0 || (int)wp>w->item_count)?w->item_count:(int)wp;
            { int j; for(j=w->item_count;j>idx;--j) w->items[j]=w->items[j-1]; }
            w->items[idx]=copy; ++w->item_count; dlg_elf_dirty(h); return idx;
        }
        if(msg==reset) { int j; for(j=0;j<w->item_count;++j) free(w->items[j]); w->item_count=0; w->selection=-1; return 0; }
        if(msg==(combo?0x146:0x18B)) return w->item_count;
        if(msg==(combo?0x147:0x188)) return w->selection;
        if(msg==(combo?0x14E:0x186)) {
            w->selection=(int)wp>=0 && (int)wp<w->item_count?(int)wp:-1;
            if(w->selection>=0) SetWindowTextA(h,w->items[w->selection]);
            return w->selection;
        }
        if(msg==(combo?0x149:0x18A) || msg==(combo?0x148:0x189)) {
            if((int)wp<0 || (int)wp>=w->item_count) return -1;
            n=strlen(w->items[wp]);
            if(msg==(combo?0x148:0x189) && lp) memcpy((void *)lp,w->items[wp],n+1);
            return (int)n;
        }
    }
    return DefWindowProcA(h,msg,wp,lp);
}

static UiClass *cls_find(const char *n) {
    int i;
    for (i = 0; i < g_cls_n; ++i)
        if (g_cls[i].used && strcasecmp(g_cls[i].name, n) == 0) return &g_cls[i];
    return 0;
}
static UiClass *cls_add(const char *n, void *wp) {
    UiClass *c = cls_find(n);
    if (c) { c->wndproc = wp; return c; }
    if (g_cls_n >= 24) return 0;
    c = &g_cls[g_cls_n++];
    c->used = 1; c->wndproc = wp;
    { int i = 0; while (n && n[i] && i < 63) { c->name[i] = n[i]; ++i; } c->name[i] = 0; }
    return c;
}

S unsigned short RegisterClassA(const void *wc) {
    const unsigned char *b = (const unsigned char *) wc;
    const char *name = *(const char *const *) (b + 36);
    void *wp = *(void *const *) (b + 4);
    if (!cls_add(name, wp)) return 0;
    if (g_uitrace) { reasm_elf_log("[win32] RegisterClassA "); reasm_elf_log(name ? name : "?"); reasm_elf_log("\n"); }
    return (unsigned short) (g_next_atom++ & 0xFFFF);
}
S unsigned short RegisterClassExA(const void *wc) {
    const unsigned char *b = (const unsigned char *) wc;
    const char *name = *(const char *const *) (b + 40);
    void *wp = *(void *const *) (b + 8);
    if (!cls_add(name, wp)) return 0;
    if (g_uitrace) { reasm_elf_log("[win32] RegisterClassExA "); reasm_elf_log(name ? name : "?"); reasm_elf_log("\n"); }
    return (unsigned short) (g_next_atom++ & 0xFFFF);
}
S int UnregisterClassA(const char *n, void *hinst) { (void) hinst; { UiClass *c = cls_find(n); if (c) c->used = 0; } return 1; }
S int GetClassInfoA(void *hinst, const char *n, void *out) { (void) hinst; (void) n; (void) out; return cls_find(n) != 0; }

S unsigned int CreateWindowExA(unsigned int exstyle, const char *cls, const char *title,
                               unsigned int style, int x, int y, int w, int h,
                               unsigned int parent, unsigned int menu, void *hinst, void *param) {
    UiClass *c = cls && (unsigned int)cls >= 65536 ? cls_find(cls) : 0;
    UiWin *wn;
    (void) hinst;
    if ((unsigned int)cls < 65536) cls = "#32770";
    win32_ui_init();
    wn = win_new(parent, (style & 0x40000000u) != 0, cls, title, style, x, y, w, h);
    if (!wn) return 0;
    wn->exstyle = exstyle;
    wn->id = wn->is_child ? menu : 0;
    wn->wndproc = c ? c->wndproc : (void *)ui_control_proc;
    if (!parent) { if (!g_foreground) { g_foreground = wn->hwnd; g_active = wn->hwnd; } }
    if (g_uitrace) { reasm_elf_log("[win32] CreateWindowExA cls="); reasm_elf_log(cls ? cls : "?");
                   reasm_elf_log(" title="); reasm_elf_log(title ? title : "");
                   reasm_elf_log(" hwnd="); reasm_elf_log_hex(wn->hwnd);
                   { char b[96]; snprintf(b,sizeof b," rect=%d,%d %dx%d style=0x%X parent=0x%X\n",x,y,w,h,style,parent);
                     reasm_elf_log(b); } }
    /* WM_CREATE (0x1): il gioco puo' inizializzare la finestra. */
    SendMessageA(wn->hwnd, 0x0001u, 0, (unsigned) (unsigned long) param);
    if(cls && !strcasecmp(cls,"SWarClass")) g_game_hwnd=wn->hwnd;
    return wn->hwnd;
}
S int DestroyWindow(unsigned int h) {
    UiWin *w = win_of(h); int i;
    if (!w || w->used == 2) return 0;
    w->used = 2; /* guard recursive destruction; still visible to WM_DESTROY */
    SendMessageA(h, 2, 0, 0);
    for (i=0;i<MAX_WIN;++i) if (g_win[i].used == 1 && g_win[i].is_child && g_win[i].parent == h) DestroyWindow(g_win[i].hwnd);
    SendMessageA(h, 0x82, 0, 0);
    dlg_elf_destroyed(h);
    for (i=0;i<w->item_count;++i) free(w->items[i]);
    if (g_focus==h) g_focus=0;
    if (g_saved_focus==h) g_saved_focus=0;
    if (g_active==h) g_active=0;
    if (g_foreground==h) g_foreground=0;
    if (g_game_hwnd==h) { g_game_hwnd=0; g_paint_pending=0; }
    if (g_capture==h) ReleaseCapture();
    w->used=0;
    return 1;
}
S int DefWindowProcA(unsigned int h, unsigned int msg, unsigned int wp, unsigned int lp) {
    (void) wp; (void) lp;
    if (msg == 0x0010u /*WM_CLOSE*/) { DestroyWindow(h); return 0; }
    return 0;
}
S void *LoadIconA(void *hinst, const char *n) { (void) hinst; (void) n; return (void *) 0x00020001u; }
S void *LoadCursorA(void *hinst, const char *n) { (void) hinst; (void) n; return (void *) 0x00020002u; }
S void *CreateCursor(void *hinst, int x, int y, int w, int h, const void *a, const void *b) {
    (void) hinst; (void) x; (void) y; (void) w; (void) h; (void) a; (void) b; return (void *) 0x00020003u;
}
S int DestroyCursor(void *c) { (void) c; return 1; }
S int DestroyIcon(void *i) { (void) i; return 1; }
S void *LoadImageA(void *hinst, const char *n, unsigned int type, int cx, int cy, unsigned int fu) {
    (void) hinst; (void) n; (void) type; (void) cx; (void) cy; (void) fu; return (void *) 0x00020004u;
}
S void *SetCursor(void *c) { (void) c; return (void *) 0x00020002u; }
S int ShowCursor(int show) {
    g_cursor_count += show?1:-1;
    if(host_elf_cursor_visible) host_elf_cursor_visible(g_cursor_count>=0);
    return g_cursor_count;
}
S unsigned long SetClassLongA(unsigned int h, int idx, unsigned long v) { (void) h; (void) idx; (void) v; return 0; }
S unsigned long GetClassLongA(unsigned int h, int idx) { (void) h; (void) idx; return 0; }

/* --- paint/DC minimi --- */
S unsigned int GetDC(unsigned int h) {
    (void)h;
#ifdef SC_ELF_REAL_GDI
    gdi_elf_init(); return (unsigned int)gdi_elf_window_dc();
#else
    return 0x30000001u;
#endif
}
S unsigned int GetWindowDC(unsigned int h) { return GetDC(h); }
S int ReleaseDC(unsigned int h, unsigned int dc) { (void) h; (void) dc; return 1; }
S unsigned int BeginPaint(unsigned int h, void *ps) {
    unsigned char *b = (unsigned char *) ps;
    if (b) memset(b, 0, 64);
    if (b) *(unsigned int *) (b + 0) = GetDC(h);
    return GetDC(h);
}
S int EndPaint(unsigned int h, const void *ps) { (void) h; (void) ps; return 1; }
S int ValidateRect(unsigned int h, const void *rc) { (void) h; (void) rc; return 1; }

/* --- GDI MINIMO ---
 * Un DC e' un descrittore con lo stato (colori, modalita', font, pennello); il disegno vero lo fa
 * il backend video (host_elf.c, altro agente): qui serve che lo stato sia COERENTE e che le API
 * non fermino il gioco. I getter di stato restituiscono i valori precedenti (il gioco li usa). */
#ifndef SC_ELF_REAL_GDI
typedef struct {
    int used; unsigned int hdc;
    unsigned int textcolor, bkcolor, bkmode, textalign;
    unsigned int font, pen, brush, pal;
    int used_stock;
} UiDc;
static UiDc g_dc[32];
static int g_dc_n;
static unsigned int g_next_obj = 0x40010000u;
static unsigned int g_stock[32];

static UiDc *dc_of(unsigned int h) {
    int i;
    for (i = 0; i < g_dc_n; ++i) if (g_dc[i].used && g_dc[i].hdc == h) return &g_dc[i];
    return 0;
}
static UiDc *dc_new(void) {
    UiDc *d;
    if (g_dc_n >= 32) return 0;
    d = &g_dc[g_dc_n++];
    memset(d, 0, sizeof *d);
    d->used = 1;
    d->hdc = g_next_obj++;
    d->textcolor = 0;
    d->bkcolor = 0x00FFFFFFu;
    d->bkmode = 2;                                   /* OPAQUE */
    d->textalign = 0;
    d->used_stock = 0;
    return d;
}

S unsigned int GetStockObject(int id) {
    if (id < 0 || id > 31) id = 0;
    if (!g_stock[id]) g_stock[id] = g_next_obj++;
    return g_stock[id];
}
S unsigned int CreateSolidBrush(unsigned int color) { (void) color; return g_next_obj++; }
S unsigned int CreatePen(int style, int w, unsigned int color) { (void) style; (void) w; (void) color; return g_next_obj++; }
S unsigned int CreateFontA(int h, int w, int esc, int orient, int weight, unsigned int ital,
                           unsigned int under, unsigned int strike, unsigned int charset,
                           unsigned int prec, unsigned int clip, unsigned int qual,
                           unsigned int pitch, const char *face) {
    (void) h; (void) w; (void) esc; (void) orient; (void) weight; (void) ital; (void) under;
    (void) strike; (void) charset; (void) prec; (void) clip; (void) qual; (void) pitch; (void) face;
    return g_next_obj++;
}
S unsigned int CreateFontIndirectA(const void *lf) { (void) lf; return g_next_obj++; }
S unsigned int CreateCompatibleDC(unsigned int hdc) {
    UiDc *d = dc_new();
    UiDc *s = dc_of(hdc);
    if (d && s) { d->textcolor = s->textcolor; d->bkcolor = s->bkcolor; d->bkmode = s->bkmode; }
    return d ? d->hdc : 0;
}
S int DeleteDC(unsigned int hdc) { UiDc *d = dc_of(hdc); if (d) d->used = 0; return 1; }
S unsigned int CreateCompatibleBitmap(unsigned int hdc, int w, int h) { (void) hdc; (void) w; (void) h; return g_next_obj++; }
S unsigned int CreateBitmap(int w, int h, unsigned int planes, unsigned int bpp, const void *bits) {
    (void) w; (void) h; (void) planes; (void) bpp; (void) bits; return g_next_obj++;
}
S unsigned int CreateDIBitmap(unsigned int hdc, const void *bi, unsigned int init,
                              const void *bits, const void *bi2, unsigned int usage) {
    (void) hdc; (void) bi; (void) init; (void) bits; (void) bi2; (void) usage; return g_next_obj++;
}
S unsigned int CreatePalette(const void *lp) { (void) lp; return g_next_obj++; }
S unsigned int SelectPalette(unsigned int hdc, unsigned int pal, int force) { (void) hdc; (void) force; return pal; }
S unsigned int RealizePalette(unsigned int hdc) { (void) hdc; return 1; }
S unsigned int SetPaletteEntries(unsigned int pal, unsigned int start, unsigned int n, const void *e) {
    (void) pal; (void) start; (void) n; (void) e; return n;
}
S unsigned int GetPaletteEntries(unsigned int pal, unsigned int start, unsigned int n, void *e) {
    (void) pal; (void) start; (void) e; return n;
}
S int GetSystemPaletteEntries(unsigned int hdc, unsigned int start, unsigned int n, void *e) {
    (void) hdc; (void) start; (void) n; (void) e; return (int) n;
}
S void *SelectObject(unsigned int hdc, void *obj) { (void) hdc; return obj; }
S int DeleteObject(void *obj) { (void) obj; return 1; }
S unsigned int GetCurrentObject(unsigned int hdc, unsigned int type) { (void) hdc; (void) type; return 0; }
S int GetObjectA(void *obj, int size, void *out) { (void) obj; (void) size; (void) out; return 0; }
S int GdiFlush(void) { return 1; }

S unsigned int SetBkColor(unsigned int hdc, unsigned int c) { UiDc *d = dc_of(hdc); unsigned int o = d ? d->bkcolor : 0; if (d) d->bkcolor = c; return o; }
S unsigned int SetTextColor(unsigned int hdc, unsigned int c) { UiDc *d = dc_of(hdc); unsigned int o = d ? d->textcolor : 0; if (d) d->textcolor = c; return o; }
S int SetBkMode(unsigned int hdc, int m) { UiDc *d = dc_of(hdc); int o = d ? (int) d->bkmode : 0; if (d) d->bkmode = (unsigned) m; return o; }
S unsigned int SetTextAlign(unsigned int hdc, unsigned int a) { UiDc *d = dc_of(hdc); unsigned int o = d ? d->textalign : 0; if (d) d->textalign = a; return o; }
S int SetMapMode(unsigned int hdc, int m) { (void) hdc; return 1; }

S int GetDeviceCaps(unsigned int hdc, int idx) {
    (void) hdc;
    switch (idx) {
    case 8: return 8;            /* BITSPIXEL */
    case 12: return 640;         /* HORZRES */
    case 10: return 480;         /* VERTRES */
    case 14: return 1;           /* PLANES */
    case 16: return 0;           /* NUMBRUSHES */
    case 18: return 0;           /* NUMPENS */
    case 20: return 0;           /* NUMMARKERS */
    case 22: return 0;           /* NUMFONTS */
    default: return 1;
    }
}
S int TextOutA(unsigned int hdc, int x, int y, const char *s, int n) { (void) hdc; (void) x; (void) y; (void) s; return n; }
S int ExtTextOutA(unsigned int hdc, int x, int y, unsigned int opts, const void *rc,
                  const char *s, unsigned int n, const int *dx) {
    (void) hdc; (void) x; (void) y; (void) opts; (void) rc; (void) s; (void) dx; return (int) n;
}
S int DrawTextA(unsigned int hdc, const char *s, int n, void *rc, unsigned int flags) {
    (void) hdc; (void) s; (void) n; (void) rc; (void) flags; return 14;
}
S int GetTextExtentPoint32A(unsigned int hdc, const char *s, int n, int *sz) {
    (void) hdc;
    if (sz) { sz[0] = (s && n > 0) ? n * 6 : 0; sz[1] = 13; }
    return 1;
}
S int GetTextExtentExPointA(unsigned int hdc, const char *s, int n, int maxw, int *fit,
                            int *dx, int *sz) {
    (void) hdc; (void) s; (void) maxw;
    if (fit) *fit = n;
    if (sz) { sz[0] = n * 6; sz[1] = 13; }
    if (dx) { int i; for (i = 0; i < n; ++i) dx[i] = (i + 1) * 6; }
    return 1;
}
S int GetCharABCWidthsA(unsigned int hdc, unsigned int first, unsigned int last, void *abc) {
    (void) hdc; (void) first; (void) last; (void) abc; return 1;
}
S int GetTextMetricsA(unsigned int hdc, void *tm) {
    (void) hdc;
    if (tm) {
        int *p = (int *) tm;
        memset(tm, 0, 56);
        p[0] = 13;   /* tmHeight */
        p[1] = 11;   /* tmAscent */
        p[2] = 2;    /* tmDescent */
        p[5] = 6;    /* tmAveCharWidth */
        p[6] = 6;    /* tmMaxCharWidth */
        ((unsigned char *) tm)[44] = 32;   /* tmFirstChar */
        ((unsigned char *) tm)[45] = 255;  /* tmLastChar */
        ((unsigned char *) tm)[52] = 0;    /* tmCharSet (ANSI_CHARSET) */
    }
    return 1;
}
S int Rectangle(unsigned int hdc, int l, int t, int r, int b) { (void) hdc; (void) l; (void) t; (void) r; (void) b; return 1; }
S int MoveToEx(unsigned int hdc, int x, int y, void *old) { (void) hdc; (void) x; (void) y; if (old) { ((int *) old)[0] = 0; ((int *) old)[1] = 0; } return 1; }
S int BitBlt(unsigned int dd, int x, int y, int w, int h, unsigned int sd, int sx, int sy, unsigned int rop) {
    (void) dd; (void) x; (void) y; (void) w; (void) h; (void) sd; (void) sx; (void) sy; (void) rop; return 1;
}
S int StretchBlt(unsigned int dd, int x, int y, int w, int h, unsigned int sd, int sx, int sy,
                 int sw, int sh, unsigned int rop) {
    (void) dd; (void) x; (void) y; (void) w; (void) h; (void) sd; (void) sx; (void) sy;
    (void) sw; (void) sh; (void) rop; return 1;
}
S int PatBlt(unsigned int hdc, int x, int y, int w, int h, unsigned int rop) {
    (void) hdc; (void) x; (void) y; (void) w; (void) h; (void) rop; return 1;
}
S int FillRect(unsigned int hdc, const void *rc, void *brush) { (void) hdc; (void) rc; (void) brush; return 1; }
S int InvertRect(unsigned int hdc, const void *rc) { (void) hdc; (void) rc; return 1; }
S int DrawEdge(unsigned int hdc, void *rc, unsigned int edge, unsigned int flags) {
    (void) hdc; (void) rc; (void) edge; (void) flags; return 1;
}
S int DrawFocusRect(unsigned int hdc, const void *rc) { (void) hdc; (void) rc; return 1; }
S int GetDIBits(unsigned int hdc, unsigned int bmp, unsigned int start, unsigned int lines,
                void *bits, void *bi, unsigned int usage) {
    (void) hdc; (void) bmp; (void) start; (void) usage; if (bi) ((unsigned int *) bi)[0] = 40;
    return (int) (bits ? lines : 0);
}
S int GetBitmapBits(unsigned int bmp, long n, void *out) { (void) bmp; (void) n; (void) out; return 0; }

/* --- regioni --- */
typedef struct { int used; unsigned int h; int l, t, r, b; } UiRgn;
static UiRgn g_rgn[32];
static int g_rgn_n;
S unsigned int CreateRectRgn(int l, int t, int r, int b) {
    UiRgn *g;
    if (g_rgn_n >= 32) return 0;
    g = &g_rgn[g_rgn_n++];
    g->used = 1; g->h = g_next_obj++; g->l = l; g->t = t; g->r = r; g->b = b;
    return g->h;
}
static UiRgn *rgn_of(unsigned int h) { int i; for (i = 0; i < g_rgn_n; ++i) if (g_rgn[i].used && g_rgn[i].h == h) return &g_rgn[i]; return 0; }
S int CombineRgn(unsigned int dst, unsigned int a, unsigned int b, int mode) {
    UiRgn *d = rgn_of(dst), *x = rgn_of(a), *y = rgn_of(b);
    (void) mode;
    if (d && x) { d->l = x->l; d->t = x->t; d->r = x->r; d->b = x->b; }
    else if (d && y) { d->l = y->l; d->t = y->t; d->r = y->r; d->b = y->b; }
    return 1;                                        /* SIMPLEREGION */
}
S int RectInRegion(unsigned int h, const int *rc) {
    UiRgn *g = rgn_of(h);
    if (!g || !rc) return 0;
    return rc[0] < g->r && rc[2] > g->l && rc[1] < g->b && rc[3] > g->t;
}
S unsigned int GetRegionData(unsigned int h, unsigned int n, void *buf) {
    (void) h; (void) n; (void) buf; return 32;
}
S int FillRgn(unsigned int hdc, unsigned int rgn, void *brush) { (void) hdc; (void) rgn; (void) brush; return 1; }
S int SelectClipRgn(unsigned int hdc, unsigned int rgn) { (void) hdc; (void) rgn; return 1; }
S int GetClipBox(unsigned int hdc, int *rc) {
    (void) hdc;
    if (rc) { rc[0] = 0; rc[1] = 0; rc[2] = 640; rc[3] = 480; }
    return 1;
}

#else
S int InvertRect(unsigned int dc, const int *r) { return r ? PatBlt((void *)dc,r[0],r[1],r[2]-r[0],r[3]-r[1],GDI_DSTINVERT) : 0; }
S int DrawEdge(unsigned int dc, int *r, unsigned int edge, unsigned int flags) { (void)edge; (void)flags; return r ? Rectangle((void *)dc,r[0],r[1],r[2],r[3]) : 0; }
S int DrawFocusRect(unsigned int dc, const int *r) { return InvertRect(dc,r); }
#endif

/* --- aggancio alla tabella shim --- */
void *win32_ui_lookup(const char *name) {
    void *dp;
    if (!name) return 0;
    dp = dlg_elf_lookup(name); if (dp) return dp;
    if (!strcasecmp(name, "CallWindowProcA")) return (void *)CallWindowProcA;
    if (!strcasecmp(name, "SetWindowPos")) return (void *)SetWindowPos;
    if (!strcasecmp(name, "GetWindowTextLengthA")) return (void *)GetWindowTextLengthA;
    if (!strcasecmp(name, "FindResourceA")) return (void *) FindResourceA;
    if (!strcasecmp(name, "FindResourceExA")) return (void *) FindResourceExA;
    if (!strcasecmp(name, "LoadResource")) return (void *) LoadResource;
    if (!strcasecmp(name, "LockResource")) return (void *) LockResource;
    if (!strcasecmp(name, "SizeofResource")) return (void *) SizeofResource;
    if (!strcasecmp(name, "LoadStringA")) return (void *) LoadStringA;
    if (!strcasecmp(name, "LoadAcceleratorsA")) return (void *) LoadAcceleratorsA;
    if (!strcasecmp(name, "CreateAcceleratorTableA")) return (void *) CreateAcceleratorTableA;
    if (!strcasecmp(name, "CopyAcceleratorTableA")) return (void *) CopyAcceleratorTableA;
    if (!strcasecmp(name, "DestroyAcceleratorTable")) return (void *) DestroyAcceleratorTable;
    if (!strcasecmp(name, "TranslateAcceleratorA")) return (void *) TranslateAcceleratorA;
    if (!strcasecmp(name, "GetKeyState")) return (void *) GetKeyState;
    if (!strcasecmp(name, "PostMessageA")) return (void *) PostMessageA;
    if (!strcasecmp(name, "PostQuitMessage")) return (void *) PostQuitMessage;
    if (!strcasecmp(name, "SendMessageA")) return (void *) SendMessageA;
    if (!strcasecmp(name, "DispatchMessageA")) return (void *) DispatchMessageA;
    if (!strcasecmp(name, "TranslateMessage")) return (void *) TranslateMessage;
    if (!strcasecmp(name, "GetMessageA")) return (void *) GetMessageA;
    if (!strcasecmp(name, "PeekMessageA")) return (void *) PeekMessageA;
    if (!strcasecmp(name, "RegisterClassA")) return (void *) RegisterClassA;
    if (!strcasecmp(name, "RegisterClassExA")) return (void *) RegisterClassExA;
    if (!strcasecmp(name, "UnregisterClassA")) return (void *) UnregisterClassA;
    if (!strcasecmp(name, "GetClassInfoA")) return (void *) GetClassInfoA;
    if (!strcasecmp(name, "CreateWindowExA")) return (void *) CreateWindowExA;
    if (!strcasecmp(name, "DestroyWindow")) return (void *) DestroyWindow;
    if (!strcasecmp(name, "DefWindowProcA")) return (void *) DefWindowProcA;
    if (!strcasecmp(name, "LoadIconA")) return (void *) LoadIconA;
    if (!strcasecmp(name, "LoadCursorA")) return (void *) LoadCursorA;
    if (!strcasecmp(name, "CreateCursor")) return (void *) CreateCursor;
    if (!strcasecmp(name, "DestroyCursor")) return (void *) DestroyCursor;
    if (!strcasecmp(name, "DestroyIcon")) return (void *) DestroyIcon;
    if (!strcasecmp(name, "LoadImageA")) return (void *) LoadImageA;
    if (!strcasecmp(name, "SetCursor")) return (void *) SetCursor;
    if (!strcasecmp(name, "ShowCursor")) return (void *) ShowCursor;
    if (!strcasecmp(name, "SetClassLongA")) return (void *) SetClassLongA;
    if (!strcasecmp(name, "GetClassLongA")) return (void *) GetClassLongA;
    if (!strcasecmp(name, "GetDC")) return (void *) GetDC;
    if (!strcasecmp(name, "GetWindowDC")) return (void *) GetWindowDC;
    if (!strcasecmp(name, "ReleaseDC")) return (void *) ReleaseDC;
    if (!strcasecmp(name, "BeginPaint")) return (void *) BeginPaint;
    if (!strcasecmp(name, "EndPaint")) return (void *) EndPaint;
    if (!strcasecmp(name, "ValidateRect")) return (void *) ValidateRect;
#ifndef SC_ELF_REAL_GDI
    if (!strcasecmp(name, "GetClipBox")) return (void *) GetClipBox;
#endif
    if (!strcasecmp(name, "SelectClipRgn")) return (void *) SelectClipRgn;
#ifndef SC_ELF_REAL_GDI
    if (!strcasecmp(name, "FillRgn")) return (void *) FillRgn;
#endif
    if (!strcasecmp(name, "GetRegionData")) return (void *) GetRegionData;
    if (!strcasecmp(name, "RectInRegion")) return (void *) RectInRegion;
    if (!strcasecmp(name, "CombineRgn")) return (void *) CombineRgn;
    if (!strcasecmp(name, "CreateRectRgn")) return (void *) CreateRectRgn;
    if (!strcasecmp(name, "GetBitmapBits")) return (void *) GetBitmapBits;
    if (!strcasecmp(name, "GetDIBits")) return (void *) GetDIBits;
    if (!strcasecmp(name, "DrawFocusRect")) return (void *) DrawFocusRect;
    if (!strcasecmp(name, "DrawEdge")) return (void *) DrawEdge;
    if (!strcasecmp(name, "InvertRect")) return (void *) InvertRect;
    if (!strcasecmp(name, "FillRect")) return (void *) FillRect;
    if (!strcasecmp(name, "PatBlt")) return (void *) PatBlt;
    if (!strcasecmp(name, "StretchBlt")) return (void *) StretchBlt;
    if (!strcasecmp(name, "BitBlt")) return (void *) BitBlt;
    if (!strcasecmp(name, "MoveToEx")) return (void *) MoveToEx;
    if (!strcasecmp(name, "Rectangle")) return (void *) Rectangle;
    if (!strcasecmp(name, "GetTextMetricsA")) return (void *) GetTextMetricsA;
    if (!strcasecmp(name, "GetCharABCWidthsA")) return (void *) GetCharABCWidthsA;
    if (!strcasecmp(name, "GetTextExtentExPointA")) return (void *) GetTextExtentExPointA;
    if (!strcasecmp(name, "GetTextExtentPoint32A")) return (void *) GetTextExtentPoint32A;
    if (!strcasecmp(name, "DrawTextA")) return (void *) DrawTextA;
    if (!strcasecmp(name, "ExtTextOutA")) return (void *) ExtTextOutA;
    if (!strcasecmp(name, "TextOutA")) return (void *) TextOutA;
    if (!strcasecmp(name, "GetDeviceCaps")) return (void *) GetDeviceCaps;
#ifndef SC_ELF_REAL_GDI
    if (!strcasecmp(name, "SetMapMode")) return (void *) SetMapMode;
#endif
    if (!strcasecmp(name, "SetTextAlign")) return (void *) SetTextAlign;
    if (!strcasecmp(name, "SetBkMode")) return (void *) SetBkMode;
    if (!strcasecmp(name, "SetTextColor")) return (void *) SetTextColor;
    if (!strcasecmp(name, "SetBkColor")) return (void *) SetBkColor;
    if (!strcasecmp(name, "GdiFlush")) return (void *) GdiFlush;
    if (!strcasecmp(name, "GetObjectA")) return (void *) GetObjectA;
    if (!strcasecmp(name, "GetCurrentObject")) return (void *) GetCurrentObject;
    if (!strcasecmp(name, "DeleteObject")) return (void *) DeleteObject;
    if (!strcasecmp(name, "SelectObject")) return (void *) SelectObject;
    if (!strcasecmp(name, "GetSystemPaletteEntries")) return (void *) GetSystemPaletteEntries;
    if (!strcasecmp(name, "GetPaletteEntries")) return (void *) GetPaletteEntries;
    if (!strcasecmp(name, "SetPaletteEntries")) return (void *) SetPaletteEntries;
    if (!strcasecmp(name, "RealizePalette")) return (void *) RealizePalette;
    if (!strcasecmp(name, "SelectPalette")) return (void *) SelectPalette;
    if (!strcasecmp(name, "CreatePalette")) return (void *) CreatePalette;
    if (!strcasecmp(name, "CreateDIBitmap")) return (void *) CreateDIBitmap;
#ifndef SC_ELF_REAL_GDI
    if (!strcasecmp(name, "CreateBitmap")) return (void *) CreateBitmap;
#endif
    if (!strcasecmp(name, "CreateCompatibleBitmap")) return (void *) CreateCompatibleBitmap;
    if (!strcasecmp(name, "DeleteDC")) return (void *) DeleteDC;
    if (!strcasecmp(name, "CreateCompatibleDC")) return (void *) CreateCompatibleDC;
    if (!strcasecmp(name, "CreateFontIndirectA")) return (void *) CreateFontIndirectA;
    if (!strcasecmp(name, "CreateFontA")) return (void *) CreateFontA;
    if (!strcasecmp(name, "CreatePen")) return (void *) CreatePen;
    if (!strcasecmp(name, "CreateSolidBrush")) return (void *) CreateSolidBrush;
    if (!strcasecmp(name, "GetStockObject")) return (void *) GetStockObject;
    if (!strcasecmp(name, "wsprintfA")) return (void *) wsprintfA;
    if (!strcasecmp(name, "GetFileVersionInfoSizeA")) return (void *) GetFileVersionInfoSizeA;
    if (!strcasecmp(name, "GetFileVersionInfoA")) return (void *) GetFileVersionInfoA;
    if (!strcasecmp(name, "VerQueryValueA")) return (void *) VerQueryValueA;
    if (!strcasecmp(name, "GetForegroundWindow")) return (void *) GetForegroundWindow;
    if (!strcasecmp(name, "GetActiveWindow")) return (void *) GetActiveWindow;
    if (!strcasecmp(name, "SetActiveWindow")) return (void *) SetActiveWindow;
    if (!strcasecmp(name, "SetForegroundWindow")) return (void *) SetForegroundWindow;
    if (!strcasecmp(name, "GetDesktopWindow")) return (void *) GetDesktopWindow;
    if (!strcasecmp(name, "GetFocus")) return (void *) GetFocus;
    if (!strcasecmp(name, "SetFocus")) return (void *) SetFocus;
    if (!strcasecmp(name, "GetCapture")) return (void *) GetCapture;
    if (!strcasecmp(name, "SetCapture")) return (void *) SetCapture;
    if (!strcasecmp(name, "ReleaseCapture")) return (void *) ReleaseCapture;
    if (!strcasecmp(name, "GetParent")) return (void *) GetParent;
    if (!strcasecmp(name, "GetTopWindow")) return (void *) GetTopWindow;
    if (!strcasecmp(name, "GetWindow")) return (void *) GetWindow;
    if (!strcasecmp(name, "GetWindowThreadProcessId")) return (void *) GetWindowThreadProcessId;
    if (!strcasecmp(name, "IsIconic")) return (void *) IsIconic;
    if (!strcasecmp(name, "IsWindow")) return (void *) IsWindow;
    if (!strcasecmp(name, "IsWindowVisible")) return (void *) IsWindowVisible;
    if (!strcasecmp(name, "IsWindowEnabled")) return (void *) IsWindowEnabled;
    if (!strcasecmp(name, "EnableWindow")) return (void *) EnableWindow;
    if (!strcasecmp(name, "ShowWindow")) return (void *) ShowWindow;
    if (!strcasecmp(name, "GetDlgItem")) return (void *) GetDlgItem;
    if (!strcasecmp(name, "GetDlgCtrlID")) return (void *) GetDlgCtrlID;
    if (!strcasecmp(name, "GetWindowLongA")) return (void *) GetWindowLongA;
    if (!strcasecmp(name, "SetWindowLongA")) return (void *) SetWindowLongA;
    if (!strcasecmp(name, "GetClassNameA")) return (void *) GetClassNameA;
    if (!strcasecmp(name, "GetWindowTextA")) return (void *) GetWindowTextA;
    if (!strcasecmp(name, "SetWindowTextA")) return (void *) SetWindowTextA;
    if (!strcasecmp(name, "SetDlgItemTextA")) return (void *) SetDlgItemTextA;
    if (!strcasecmp(name, "GetDlgItemTextA")) return (void *) GetDlgItemTextA;
    if (!strcasecmp(name, "GetClientRect")) return (void *) GetClientRect;
    if (!strcasecmp(name, "GetWindowRect")) return (void *) GetWindowRect;
    if (!strcasecmp(name, "SetRect")) return (void *) SetRect;
    if (!strcasecmp(name, "OffsetRect")) return (void *) OffsetRect;
    if (!strcasecmp(name, "PtInRect")) return (void *) PtInRect;
    if (!strcasecmp(name, "ClientToScreen")) return (void *) ClientToScreen;
    if (!strcasecmp(name, "ScreenToClient")) return (void *) ScreenToClient;
    if (!strcasecmp(name, "GetCursorPos")) return (void *) GetCursorPos;
    if (!strcasecmp(name, "SetCursorPos")) return (void *) SetCursorPos;
    if (!strcasecmp(name, "ClipCursor")) return (void *) ClipCursor;
    if (!strcasecmp(name, "InvalidateRect")) return (void *) InvalidateRect;
    if (!strcasecmp(name, "UpdateWindow")) return (void *) UpdateWindow;
    if (!strcasecmp(name, "GetUpdateRgn")) return (void *) GetUpdateRgn;
    if (!strcasecmp(name, "SystemParametersInfoA")) return (void *) SystemParametersInfoA;
    return 0;
}
