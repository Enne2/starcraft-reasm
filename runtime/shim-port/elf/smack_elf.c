#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
/* P9 — filmati SMACK nativi sopra libsmacker (ex task T7).
 *
 * PERIMETRO: si implementano SOLO le 13 funzioni che storm risolve da "smackw32.dll" a runtime.
 * Nessuna modifica al .text di storm né al gioco. La libreria (LGPL) si carica con dlopen a
 * runtime: sostituibile (variabile SC_SMACK_SO) e assente => tutto tace, nessun falso successo.
 *
 * ATTIVAZIONE: **ON di default** dal 2026-10-27 (P9 e P11 chiusi e verificati); `SC_SMACK=0`
 * disattiva. Il vecchio default 0 nasceva dal primo passo sperimentale ("attivo solo con
 * SC_SMACK=1") e lasciava il gioco senza alcuna animazione per chi non esportava la variabile:
 * l'utente lo percepiva come una regressione. Degrado dichiarato: se il .so manca, tutto tace
 * (nessun falso successo) e ogni chiamata resta nel percorso `filmato assente, saltato`.
 *
 * CONTRATTO MISURATO (IDA su storm/storm.dll.orig.i64, non ipotizzato):
 *  - sub_1502E430 (SVid.cpp): carica "smackw32.dll" con LoadLibraryA e risolve i 13 nomi decorati;
 *    se UNA sola GetProcAddress torna NULL l'intera tabella è inutilizzabile.
 *    Ordine della tabella in dword_1505EAA0: +0 BufferClose, +4 BufferNewPalette, +8 BufferOpen,
 *    +12 Close, +16 DoFrame, +20 Goto, +24 NextFrame, +28 Open, +32 SoundUseDirectSound,
 *    +36 ToBuffer, +40 ToBufferRect, +44 VolumePan, +48 Wait.
 *  - storm_454 (#454, apre): SmackOpen(arg, flags, -1) dove arg è il NOME passato dal gioco
 *    (se flags & 0x10000) oppure il risultato di storm_268(0,nome,5,&h) — e con mode 5
 *    storm_268 restituisce un HANDLE file posizionato all'inizio dei dati dell'entry MPQ.
 *    Poi legge dal SMACK: +4 larghezza, +8 altezza.
 *    Chiama SmackBufferOpen(0, 4, w*(d+1), h, w*(d+1), h) e SmackToBuffer(smk, 0, 0, w*(d+1), h,
 *    *(DWORD*)(smackbuf + 1096), 0).
 *  - storm_457 (#457, avanza): SmackWait(smk)!=0 => nessun avanzamento; SmackDoFrame(smk)
 *    (0 = frame presentabile, !=0 = non presentare: storm 1502F8B5); legge +104 (flag palette presente),
 *    +108 (palette RGB: 256 voci da 3 byte; storm converte a PALETTEENTRY da 4 byte),
 *    +12 (frame totali), +884 (frame corrente); poi SmackNextFrame(smk).
 *  - sub_1502EFA0 (blit): [video+0x58] -> +0 = SMACKBUF -> +1096 = buffer di destinazione.
 *
 * La struct SMACK è nostra: si allocano 4096 byte azzerati e si scrivono SOLO i campi che storm
 * legge (offset sopra), più i nostri dati privati a +2048 (che storm non tocca).
 */
#include "smack_elf.h"
#include "thread_affinity.h"
#include <pthread.h>
#include <stdint.h>

typedef unsigned int u32;
typedef unsigned long ulong;
typedef unsigned char u8;

#define SMK_STDCALL __attribute__((stdcall))
#define SMK_FMT "%s"

extern void reasm_elf_log(const char *);
extern void reasm_elf_log_hex(unsigned);

/* Win32 dello shim (stesse firme; stdcall) */
extern void *SMK_STDCALL CreateFileA(const char *, u32, u32, void *, u32, u32, void *);
extern int   SMK_STDCALL ReadFile(void *, void *, u32, u32 *, void *);
extern u32   SMK_STDCALL SetFilePointer(void *, long, long *, u32);
extern int   SMK_STDCALL CloseHandle(void *);
extern u32   SMK_STDCALL GetTickCount(void);

/* libc per dlopen (il nostro ELF è dinamico: libc.so.6) */
extern void *dlopen(const char *, int);
extern int dlclose(void *);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
#define RTLD_NOW 2
#define RTLD_LOCAL 0

static int active(void);

static void log2(const char *a, const char *b) { reasm_elf_log(a); reasm_elf_log(b ? b : "?"); reasm_elf_log("\n"); }
static void log1h(const char *a, unsigned v) { reasm_elf_log(a); reasm_elf_log_hex(v); reasm_elf_log("\n"); }

/* ---------------------------------------------------------------- libsmacker via dlopen */
typedef void *smk_t;
static void *g_lib;
static int g_tried;
static smk_t (*p_open_memory)(const u8 *, ulong);
static void   (*p_close)(smk_t);
static char   (*p_info_all)(const smk_t, ulong *, ulong *, double *);
static char   (*p_info_video)(const smk_t, ulong *, ulong *, u8 *);
static char   (*p_info_audio)(const smk_t, u8 *, u8[7], u8[7], ulong[7]);
static char   (*p_enable_video)(smk_t, u8);
static char   (*p_first)(smk_t);
static char   (*p_next)(smk_t);
static char   (*p_seek)(smk_t, ulong);
static const u8 *(*p_get_palette)(const smk_t);
static const u8 *(*p_get_video)(const smk_t);
static const u8 *(*p_get_audio)(const smk_t, u8);
static ulong   (*p_get_audio_size)(const smk_t, u8);

static int lib_load(void)
{
    const char *so, *envso;
    const char *candidates[4];
    char exe_path[4096], exe_so[4096];
    unsigned i, n;
    long path_len;
    if (g_lib) return 1;
    if (g_tried) return (g_lib != 0);
    g_tried = 1;
    envso = getenv("SC_SMACKER_SO");
    if (envso && !envso[0]) envso = 0;
    candidates[0] = envso;
    candidates[1] = "./libsmacker-1.2.0.so";
    candidates[2] = 0;
    candidates[3] = "/home/enne2/dev/starcraft-armv6/build/arm-libs/libsmacker-1.2.0.so";
    path_len = readlink("/proc/self/exe", exe_path, sizeof exe_path - 1);
    if (path_len > 0 && (unsigned long) path_len < sizeof exe_path) {
        exe_path[path_len] = 0;
        n = (unsigned) path_len;
        while (n && exe_path[n - 1] != '/') --n;
        if (n) {
            const char suffix[] = "libsmacker-1.2.0.so";
            unsigned j = 0;
            if (n < sizeof exe_so) {
                memcpy(exe_so, exe_path, n);
                while (n + j + 1 < sizeof exe_so && suffix[j]) { exe_so[n + j] = suffix[j]; ++j; }
                if (!suffix[j]) { exe_so[n + j] = 0; candidates[2] = exe_so; }
            }
        }
    }
#define SYM(v, n) (*(void **) &(v) = dlsym(g_lib, n), (v) == 0)
    for (i = 0; i < 4; ++i) {
        so = candidates[i];
        if (!so) continue;
        g_lib = dlopen(so, RTLD_NOW | RTLD_LOCAL);
        if (!g_lib) continue;
        if (!(SYM(p_open_memory, "smk_open_memory") || SYM(p_close, "smk_close") ||
              SYM(p_info_all, "smk_info_all") || SYM(p_info_video, "smk_info_video") ||
              SYM(p_info_audio, "smk_info_audio") || SYM(p_get_audio, "smk_get_audio") ||
              SYM(p_get_audio_size, "smk_get_audio_size") || SYM(p_enable_video, "smk_enable_video") || SYM(p_first, "smk_first") ||
              SYM(p_next, "smk_next") || SYM(p_seek, "smk_seek_keyframe") ||
              SYM(p_get_palette, "smk_get_palette") || SYM(p_get_video, "smk_get_video"))) {
            log2("[smack] libsmacker caricata: ", so);
            return 1;
        }
        log2("[smack] simboli incompleti, provo il candidato successivo: ", so);
        dlclose(g_lib);
        g_lib = 0;
        p_open_memory = 0; p_close = 0; p_info_all = 0; p_info_video = 0;
        p_info_audio = 0; p_enable_video = 0; p_first = 0; p_next = 0; p_seek = 0;
        p_get_palette = 0; p_get_video = 0; p_get_audio = 0; p_get_audio_size = 0;
    }
#undef SYM
    log2("[smack] nessun libsmacker utilizzabile; ultimo candidato: ", so ? so : "(nessuno)");
    log2("[smack] motivo: ", dlerror());
    return 0;
}

/* ---------------------------------------------------------------- oggetto SMACK (offset di storm) */
#define SMK_W      4      /* larghezza      (storm_454/457) */
#define SMK_H      8      /* altezza        */
#define SMK_FRAMES 12     /* frame totali   */
#define SMK_HASPAL 104    /* flag palette   */
#define SMK_PAL    108    /* 256 * 3 byte RGB; NON invadere FrameNum/LastRect */
#define SMK_CUR    884    /* frame corrente */
#define SMK_RECTX  896
#define SMK_RECTY  900
#define SMK_RECTW  904
#define SMK_RECTH  908
#define SMK_SIZE   4096
#define SMK_PRIV   2048   /* dati nostri (storm non li legge) */
#define SMK_PRIV_BUF   0  /* +0  : puntatore al buffer dei byte .smk */
#define SMK_PRIV_SIZE  4  /* +4  : dimensione dei byte */
#define SMK_PRIV_SMK   8  /* +8  : handle libsmacker */
#define SMK_PRIV_USF   12 /* +12 : microsecondi per frame */
#define SMK_PRIV_T0    16 /* +16 : tick dell'ultimo frame mostrato */
#define SMK_PRIV_DEC   20 /* +20 : indice del frame attualmente decodificato (= -1 nessuno) */
#define SMK_PRIV_RECT  32 /* +32 : rettangolo aggiornato da enumerare */
#define SMK_PRIV_DEST  24
#define SMK_PRIV_PITCH 28
#define SMK_PRIV_TOP   36
#define SMK_PRIV_LEFT  40
#define SMK_PRIV_DH    44
#define SMK_PRIV_BAD   48
#define SMK_PRIV_ASYNC 52

#define BUF_DEST  1096    /* SMACKBUF: puntatore al buffer di destinazione (storm_454/EFA0) */
#define BUF_SIZE  4096
#define BUF_PRIV  2048

static u32 *priv(void *o) { return (u32 *) ((u8 *) o + SMK_PRIV); }
static u32 *bpriv(void *o) { return (u32 *) ((u8 *) o + BUF_PRIV); }

typedef struct {
    u8 *video;
    u8 palette[768];
    u8 *audio[7];
    ulong audio_size[7], audio_cap[7];
    u32 frame_idx;
    int valid;
} smack_frame_slot;
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    smk_t decoder;
    u8 *data;
    ulong bytes, width, height, frames;
    u32 audio_mask;
    u32 head, count;
    int stop, ready, failed, goto_pending;
    u32 goto_frame;
    u32 *object;
    smack_frame_slot slots[3];
} smack_async;

static int async_enabled(void)
{
    const char *e = getenv("SC_SMACK_ASYNC");
    return !(e && e[0] == '0');
}
static smack_async *async_of(u32 *o)
{
    return (smack_async *)(uintptr_t)priv(o)[SMK_PRIV_ASYNC / 4];
}

/* P9: contatori di strumentazione (prime chiamate di ogni punto della catena). */
static unsigned c_doframe, c_next, c_tobuf, c_torect, c_wait, c_wait_yes;

static u32 le32(const u8 *p)
{
    return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24);
}

static int read_exact(void *h, u8 *p, u32 size)
{
    u32 got;
    while (size) {
        got = 0;
        if (!ReadFile(h, p, size, &got, 0) || !got || got > size) {
            log1h("[smack] lettura corta/errore, byte mancanti=", size); return 0;
        }
        p += got; size -= got;
    }
    return 1;
}

/* A positioned HANDLE points into an MPQ, not to a standalone file. Read exactly
 * one SMK: header(104) + size/type tables + trees + chunks (ring frame included).
 * Layout cross-checked against pinned libsmacker smk_open_generic and a real
 * INTRO.SMK: declared size == physical 158814768 bytes. Limit rejects, NEVER truncates. */
#define SMK_INPUT_MAX (256u * 1024u * 1024u)
static u8 *read_input(const void *arg, ulong *out_size)
{
    void *h; u8 header[104], *sizes = 0, *buf = 0;
    u32 n, count, tree, total, prefix, i, chunk, w, height; int from_handle;
    from_handle = ((u32) (ulong) arg) < 0x10000u; /* shim HANDLE is fd+1 */
    if (from_handle) h = (void *) (ulong) arg;
    else {
        h = CreateFileA((const char *) arg, 0x80000000u, 1u, 0, 3u, 0x8000000u, 0);
        if (!h || (long) h == -1) { log2("[smack] CreateFileA fallita per ", (const char *) arg); return 0; }
    }
    if (!read_exact(h, header, sizeof header)) goto done;
    if (header[0] != 'S' || header[1] != 'M' || header[2] != 'K' ||
        (header[3] != '2' && header[3] != '4')) {
        log2("[smack] firma NON SMK2/SMK4: ", "apertura rifiutata"); goto done;
    }
    count = le32(header + 12); w = le32(header + 4); height = le32(header + 8);
    tree = le32(header + 52);
    if (!count || count > 1000000u || !w || !height || w > 4096u || height > 4096u || tree > 16u * 1024u * 1024u) {
        log2("[smack] header fuori limiti: ", "apertura rifiutata"); goto done;
    }
    n = count + (le32(header + 20) & 1u);
    prefix = 104u + 4u * n;
    total = prefix + n + tree;
    sizes = (u8 *) calloc(1, 4u * n);
    if (!sizes || !read_exact(h, sizes, 4u * n)) goto done;
    for (i = 0; i < n; ++i) {
        chunk = le32(sizes + 4u * i) & ~3u;
        if (chunk > SMK_INPUT_MAX - total) {
            log2("[smack] SMK oltre limite256MiB: ", "rifiutato, non troncato"); goto done;
        }
        total += chunk;
    }
    buf = (u8 *) calloc(1, total);
    if (!buf) { log1h("[smack] allocazione SMK fallita, byte=", total); goto done; }
    memcpy(buf, header, 104); memcpy(buf + 104, sizes, 4u * n);
    if (!read_exact(h, buf + prefix, total - prefix)) { free(buf); buf = 0; goto done; }
    *out_size = total;
    reasm_elf_log("[smack] filmato letto: "); reasm_elf_log_hex(total);
    reasm_elf_log(from_handle ? " byte (da handle, una entry)" : " byte (da path)"); reasm_elf_log("\n");
done:
    free(sizes);
    if (!from_handle) CloseHandle(h);
    return buf;
}

/* Worker is the sole owner of smk_t after handoff: all mutating decoder calls and
 * all frame/audio reads happen here. Slots are preallocated for video/palette. */
static int async_publish(smack_async *a)
{
    ulong frame = 0; const u8 *video, *palette; smack_frame_slot *slot; unsigned t;
    if (p_info_all(a->decoder, &frame, 0, 0) != 0) return 0;
    video = p_get_video(a->decoder); palette = p_get_palette(a->decoder);
    if (!video || !palette) return 0;
    pthread_mutex_lock(&a->mutex);
    while (a->count == 3 && !a->stop && !a->goto_pending) pthread_cond_wait(&a->cond, &a->mutex);
    if (a->stop) { pthread_mutex_unlock(&a->mutex); return 0; }
    if (a->goto_pending) { pthread_mutex_unlock(&a->mutex); return 2; }
    slot = &a->slots[(a->head + a->count) % 3];
    memcpy(slot->video, video, a->width * a->height);
    memcpy(slot->palette, palette, 768);
    slot->frame_idx = (u32)frame;
    slot->valid = 0;
    for (t = 0; t < 7; ++t) {
        ulong n = (a->audio_mask & (1u << t)) ? p_get_audio_size(a->decoder, (u8)t) : 0;
        const u8 *pcm = n ? p_get_audio(a->decoder, (u8)t) : 0;
        if (n && !pcm) { pthread_mutex_unlock(&a->mutex); return 0; }
        if (n > slot->audio_cap[t]) {
            u8 *grown = (u8 *)realloc(slot->audio[t], n);
            if (!grown) { pthread_mutex_unlock(&a->mutex); return 0; }
            slot->audio[t] = grown; slot->audio_cap[t] = n;
        }
        if (n) memcpy(slot->audio[t], pcm, n);
        slot->audio_size[t] = n;
    }
    slot->valid = 1;
    ++a->count;
    a->ready = 1;
    pthread_cond_broadcast(&a->cond);
    pthread_mutex_unlock(&a->mutex);
    return 1;
}

static void *smack_async_worker(void *arg)
{
    smack_async *a = (smack_async *)arg;
    (void) sc_affinity_apply(1);
    int failed = 0;
    if (p_enable_video(a->decoder, 1) == (char)-1) failed = 1;
    { extern void smack_audio_hook_open(void *) __attribute__((weak));
      if (!failed && smack_audio_hook_open) smack_audio_hook_open(a->decoder); }
    if (!failed) {
        u8 mask = 0, ch[7], bd[7]; ulong rates[7];
        if (p_info_audio(a->decoder, &mask, ch, bd, rates) != 0) failed = 1;
        else a->audio_mask = mask;
    }
    if (!failed && p_first(a->decoder) == (char)-1) failed = 1;
    if (!failed && async_publish(a) == 0) failed = 1;
    pthread_mutex_lock(&a->mutex);
    a->failed = failed; a->ready = 1; pthread_cond_broadcast(&a->cond);
    pthread_mutex_unlock(&a->mutex);
    while (!failed) {
        int do_goto = 0; u32 target = 0;
        pthread_mutex_lock(&a->mutex);
        while (!a->stop && !a->goto_pending && a->count == 3) pthread_cond_wait(&a->cond, &a->mutex);
        if (a->stop) { pthread_mutex_unlock(&a->mutex); break; }
        if (a->goto_pending) {
            do_goto = 1; target = a->goto_frame; a->goto_pending = 0;
            a->head = 0; a->count = 0;
        }
        pthread_mutex_unlock(&a->mutex);
        if (do_goto) {
            ulong cur = 0;
            if (p_seek(a->decoder, target) == (char)-1 || p_info_all(a->decoder, &cur, 0, 0) != 0) failed = 1;
            while (!failed && cur < target) {
                char r = p_next(a->decoder);
                if (r == (char)-1 || r == 0 || p_info_all(a->decoder, &cur, 0, 0) != 0) failed = 1;
            }
            if (!failed && cur != target) failed = 1;
            if (!failed && async_publish(a) == 0) failed = 1;
            pthread_mutex_lock(&a->mutex); a->failed = failed; pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex);
            continue;
        }
        {
            char r = p_next(a->decoder);
            if (r == 0) r = p_first(a->decoder); /* preserve legacy non-ring rewind */
            if (r == (char)-1) failed = 1;
            else if (async_publish(a) == 0) failed = 1;
        }
        if (failed) { pthread_mutex_lock(&a->mutex); a->failed = 1; a->ready = 1; pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex); }
    }
    { extern void smack_audio_hook_close(void *) __attribute__((weak));
      if (smack_audio_hook_close) smack_audio_hook_close(a->decoder); }
    p_close(a->decoder);
    a->decoder = 0;
    return 0;
}

static void async_destroy(smack_async *a, int join)
{
    unsigned i, t;
    if (!a) return;
    if (join) {
        pthread_mutex_lock(&a->mutex); a->stop = 1; pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex);
        pthread_join(a->thread, 0);
    }
    for (i = 0; i < 3; ++i) {
        free(a->slots[i].video);
        for (t = 0; t < 7; ++t) free(a->slots[i].audio[t]);
    }
    pthread_cond_destroy(&a->cond); pthread_mutex_destroy(&a->mutex);
    free(a->data); free(a);
}

static int async_wait_frame(smack_async *a, u32 frame, smack_frame_slot **out)
{
    unsigned i;
    pthread_mutex_lock(&a->mutex);
    for (;;) {
        for (i = 0; i < a->count; ++i) {
            smack_frame_slot *s = &a->slots[(a->head + i) % 3];
            if (s->valid && s->frame_idx == frame) { *out = s; pthread_mutex_unlock(&a->mutex); return 1; }
        }
        if (a->failed || a->stop) { pthread_mutex_unlock(&a->mutex); return 0; }
        pthread_cond_wait(&a->cond, &a->mutex);
    }
}

/* ---------------------------------------------------------------- 13 funzioni SMACK */
static void *SMK_STDCALL SmackOpen(const void *arg, u32 flags, u32 extra)
{
    u8 *data; ulong size = 0; u32 *o; smk_t s; ulong cur0 = 0, frames = 0; ulong w = 0, h = 0; double usf = 0; u8 yscale = 0;
    if (!active()) return 0;
    if (!lib_load()) return 0;
    (void) extra;
    reasm_elf_log("[smack] SmackOpen(arg="); reasm_elf_log_hex((unsigned) (ulong) arg);
    reasm_elf_log(", flags="); reasm_elf_log_hex(flags); reasm_elf_log(") tipo=");
    reasm_elf_log(((u32) (ulong) arg) < 0x10000u ? "HANDLE" : "nome");
    reasm_elf_log("\n");
    data = read_input(arg, &size);
    if (!data) return 0;
    reasm_elf_log("[smack] header: sig=");
    { int i; for (i = 0; i < 4; ++i) { char c = (char) data[i]; reasm_elf_log((c >= 32 && c < 127) ? (char[2]){c, 0} : "?"); }
      reasm_elf_log(" w="); reasm_elf_log_hex((unsigned) (data[4] | (data[5] << 8)));
      reasm_elf_log(" h="); reasm_elf_log_hex((unsigned) (data[8] | (data[9] << 8)));
      reasm_elf_log(" frames="); reasm_elf_log_hex((unsigned) (data[12] | (data[13] << 8))); }
    reasm_elf_log("\n");
    s = p_open_memory(data, size);
    if (!s) { log2("[smack] smk_open_memory ha rifiutato i dati, byte=", ""); free(data); return 0; }
    /* ATTENZIONE: le smk_info_* ritornano 0 = SUCCESSO, -1 = errore (verificato in smacker.c). */
    if (p_info_all(s, &cur0, &frames, &usf) != 0) { p_close(s); free(data); log2("[smack] smk_info_all errore", ""); return 0; }
    if (p_info_video(s, &w, &h, &yscale) != 0) {
        p_close(s); free(data);
        log2("[smack] smk_info_video errore su dati con firma SMK: ", "file non decodificabile da libsmacker");
        return 0;
    }
    {
        int use_async = async_enabled();
        if (!use_async) {
            p_enable_video(s, 1);
            /* Audio must be enabled before first-frame decode (P11 contract). */
            { extern void smack_audio_hook_open(void *) __attribute__((weak));
              if (smack_audio_hook_open) smack_audio_hook_open(s); }
            if (p_first(s) == (char)-1) { p_close(s); free(data); log2("[smack] smk_first errore", ""); return 0; }
        }
        o = (u32 *)calloc(1, SMK_SIZE);
        if (!o) { p_close(s); free(data); return 0; }
        o[SMK_W / 4] = (u32)w; o[SMK_H / 4] = (u32)h; o[SMK_FRAMES / 4] = (u32)frames; o[SMK_CUR / 4] = 0;
        priv(o)[SMK_PRIV_BUF / 4] = (u32)(ulong)data; priv(o)[SMK_PRIV_SIZE / 4] = (u32)size;
        priv(o)[SMK_PRIV_SMK / 4] = (u32)(ulong)s; priv(o)[SMK_PRIV_USF / 4] = (u32)usf;
        priv(o)[SMK_PRIV_T0 / 4] = GetTickCount(); priv(o)[SMK_PRIV_DEC / 4] = 0;
        priv(o)[SMK_PRIV_RECT / 4] = 1;
        if (use_async) {
            smack_async *a = (smack_async *)calloc(1, sizeof *a); unsigned i;
            if (!a) { free(o); p_close(s); free(data); return 0; }
            a->decoder = s; a->data = data; a->bytes = size; a->width = w; a->height = h; a->frames = frames; a->object = o;
            if (pthread_mutex_init(&a->mutex, 0) || pthread_cond_init(&a->cond, 0)) {
                free(a); free(o); p_close(s); free(data); return 0;
            }
            for (i = 0; i < 3; ++i) {
                a->slots[i].video = (u8 *)calloc(1, w * h);
                if (!a->slots[i].video) { async_destroy(a, 0); free(o); p_close(s); return 0; }
            }
            priv(o)[SMK_PRIV_ASYNC / 4] = (u32)(uintptr_t)a;
            if (pthread_create(&a->thread, 0, smack_async_worker, a)) {
                async_destroy(a, 0); free(o); p_close(s); return 0;
            }
            pthread_mutex_lock(&a->mutex);
            while (!a->ready) pthread_cond_wait(&a->cond, &a->mutex);
            i = (unsigned)a->failed;
            pthread_mutex_unlock(&a->mutex);
            if (i) { async_destroy(a, 1); free(o); return 0; }
            priv(o)[SMK_PRIV_SMK / 4] = (u32)(ulong)s;
            priv(o)[SMK_PRIV_BUF / 4] = 0; /* owned and freed by async_destroy */
            priv(o)[SMK_PRIV_DEC / 4] = 0;
            o[SMK_HASPAL / 4] = 1;
            memcpy((u8 *)o + SMK_PAL, a->slots[a->head].palette, 768);
        } else if (p_get_palette && p_get_palette(s)) {
            o[SMK_HASPAL / 4] = 1;
            memcpy((u8 *)o + SMK_PAL, p_get_palette(s), 768);
        }
    }
    reasm_elf_log("[smack] filmato pronto: "); reasm_elf_log_hex((unsigned) w); reasm_elf_log("x");
    reasm_elf_log_hex((unsigned) h); reasm_elf_log(" frame="); reasm_elf_log_hex((unsigned) frames); reasm_elf_log("\n");
    return o;
}

static void SMK_STDCALL SmackClose(void *smk)
{
    u32 *o = (u32 *) smk;
    if (!active() || !o) return;
    if (priv(o)[SMK_PRIV_ASYNC / 4]) {
        smack_async *a = async_of(o);
        async_destroy(a, 1); /* worker closes audio hook and smk_t before pthread_join returns */
    } else {
        /* P11: ferma la voce audio prima di distruggere il decoder sincrono. */
        { extern void smack_audio_hook_close(void *) __attribute__((weak));
          if (smack_audio_hook_close) smack_audio_hook_close((void *)(ulong)priv(o)[SMK_PRIV_SMK / 4]); }
        if (priv(o)[SMK_PRIV_SMK / 4]) p_close((smk_t)(ulong)priv(o)[SMK_PRIV_SMK / 4]);
        if (priv(o)[SMK_PRIV_BUF / 4]) free((void *)(ulong)priv(o)[SMK_PRIV_BUF / 4]);
    }
    free(o);
}

/* Porta il decoder al frame n (sequenziale: libsmacker mantiene lo stato). */
static int decode_to(u32 *o, u32 n)
{
    u32 dec = priv(o)[SMK_PRIV_DEC / 4], frames = o[SMK_FRAMES / 4];
    smk_t s = (smk_t) (ulong) priv(o)[SMK_PRIV_SMK / 4];
    if (n >= frames) return 0;
    if (dec == n) return 1;
    if (n < dec) {                                  /* riavvolge al keyframe e avanza */
        ulong real = 0;
        if (p_seek(s, n) == (char) -1) return 0;    /* -1 = SMK_ERROR */
        if (p_info_all(s, &real, 0, 0) == 0) dec = real;   /* smk_seek_keyframe atterra su un keyframe */
        else dec = n;
    }
    while (dec < n) { if (p_next(s) == (char) -1) return 0; ++dec; }
    priv(o)[SMK_PRIV_DEC / 4] = n;
    return 1;
}

static void refresh_palette(u32 *o)
{
    const u8 *pal;
    if (!p_get_palette) return;
    pal = p_get_palette((smk_t) (ulong) priv(o)[SMK_PRIV_SMK / 4]);
    if (!pal) { o[SMK_HASPAL / 4] = 0; return; }
    o[SMK_HASPAL / 4] = 1;
    memcpy((u8 *) o + SMK_PAL, pal, 768);
}

/* SmackToBuffer registers a target; DoFrame writes every decoded frame there.
 * Covered format: indexed 8-bit, flags=0. Bounds are checked before any copy. */
static int copy_frame_data(u32 *o, const u8 *px)
{
    u32 *p = priv(o), w = o[SMK_W / 4], h = o[SMK_H / 4], y, rows;
    u32 pitch = p[SMK_PRIV_PITCH / 4], left = p[SMK_PRIV_LEFT / 4];
    u32 top = p[SMK_PRIV_TOP / 4], dh = p[SMK_PRIV_DH / 4];
    u8 *dest = (u8 *) (ulong) p[SMK_PRIV_DEST / 4];
    if (p[SMK_PRIV_BAD / 4]) return 0;
    if (!dest) return 1; /* decode without a registered destination is allowed */
    if (!pitch || left > pitch || w > pitch - left || top >= dh || dh > 0xffffffffu / pitch) {
        log2("[smack] destinazione fuori limiti: ", "frame non copiato"); return 0;
    }
    if (!px) return 0;
    rows = h < dh - top ? h : dh - top;
    for (y = 0; y < rows; ++y)
        memcpy(dest + (ulong) (top + y) * pitch + left, px + (ulong) y * w, w);
    return 1;
}
static int copy_frame(u32 *o)
{
    u32 *p = priv(o);
    return copy_frame_data(o, p_get_video((smk_t)(ulong)p[SMK_PRIV_SMK / 4]));
}

static u32 SMK_STDCALL SmackDoFrame(void *smk)
{
    u32 *o = (u32 *)smk;
    u32 cur; const u8 *frame_px = 0; smack_frame_slot *slot = 0;
    if (!active() || !o || !priv(o)[SMK_PRIV_SMK / 4]) return 1;
    cur = o[SMK_CUR / 4];
    if (cur >= o[SMK_FRAMES / 4]) return 1;
    if (priv(o)[SMK_PRIV_ASYNC / 4]) {
        smack_async *a = async_of(o);
        const u8 *pcm[7]; ulong sizes[7]; unsigned t;
        if (!async_wait_frame(a, cur, &slot)) { log2("[smack] DoFrame async non disponibile: ", "non presentato"); return 1; }
        frame_px = slot->video;
        o[SMK_HASPAL / 4] = 1; memcpy((u8 *)o + SMK_PAL, slot->palette, 768);
        for (t = 0; t < 7; ++t) { pcm[t] = slot->audio[t]; sizes[t] = slot->audio_size[t]; }
        { extern void smack_audio_hook_frame_snapshot(void *, void *, ulong, const u8 *const[7], const ulong[7]) __attribute__((weak));
          if (smack_audio_hook_frame_snapshot) smack_audio_hook_frame_snapshot(o, a->decoder, cur, pcm, sizes); }
        if (!copy_frame_data(o, frame_px)) return 1;
    } else {
        if (!decode_to(o, cur)) { log2("[smack] DoFrame non decodificabile: ", "non presentato"); return 1; }
        refresh_palette(o);
        { extern void smack_audio_hook_frame(void *, void *) __attribute__((weak));
          if (smack_audio_hook_frame) smack_audio_hook_frame(o, (void *)(ulong)priv(o)[SMK_PRIV_SMK / 4]); }
        if (!copy_frame(o)) return 1;
    }
    priv(o)[SMK_PRIV_RECT / 4] = 1;
    ++c_doframe;
    if (getenv("SC_SMACK_TRACE") && getenv("SC_SMACK_TRACE")[0] == '1' &&
        (c_doframe <= 3 || (cur && cur <= 90 && cur % 30 == 0))) {
        const u8 *px = frame_px ? frame_px : p_get_video((smk_t)(ulong)priv(o)[SMK_PRIV_SMK / 4]);
        u32 i, nz = 0, hash = 2166136261u, bytes = o[SMK_W / 4] * o[SMK_H / 4];
        if (px) for (i = 0; i < bytes; ++i) { nz += px[i] != 0; hash = (hash ^ px[i]) * 16777619u; }
        reasm_elf_log("[smack] DoFrame #"); reasm_elf_log_hex(c_doframe);
        reasm_elf_log(" cur="); reasm_elf_log_hex(cur);
        reasm_elf_log(" px="); reasm_elf_log_hex((unsigned) (ulong) px);
        reasm_elf_log(" px[0,1,100,1000]=");
        if (px && bytes > 1000) { reasm_elf_log_hex(px[0]); reasm_elf_log_hex(px[1]); reasm_elf_log_hex(px[100]); reasm_elf_log_hex(px[1000]); }
        reasm_elf_log(" nonzero="); reasm_elf_log_hex(nz);
        reasm_elf_log(" hash="); reasm_elf_log_hex(hash);
        if (priv(o)[SMK_PRIV_DEST / 4]) {
            const u8 *dst = (const u8 *) (ulong) priv(o)[SMK_PRIV_DEST / 4];
            u32 y, x, dsthash = 2166136261u;
            u32 rows = o[SMK_H / 4], dh = priv(o)[SMK_PRIV_DH / 4] - priv(o)[SMK_PRIV_TOP / 4];
            if (rows > dh) rows = dh;
            for (y = 0; y < rows; ++y) for (x = 0; x < o[SMK_W / 4]; ++x)
                dsthash = (dsthash ^ dst[(y + priv(o)[SMK_PRIV_TOP / 4]) * priv(o)[SMK_PRIV_PITCH / 4] + x + priv(o)[SMK_PRIV_LEFT / 4]]) * 16777619u;
            reasm_elf_log(" dsthash="); reasm_elf_log_hex(dsthash);
        }
        reasm_elf_log(" pal[3..8]=");
        { const u8 *pp = (u8 *) o + SMK_PAL; int k; for (k = 3; k < 9; ++k) reasm_elf_log_hex(pp[k]); }
        reasm_elf_log("\n");
    }
    return 0; /* storm presents a decoded frame only with this return value */
}

static u32 SMK_STDCALL SmackNextFrame(void *smk)
{
    u32 *o = (u32 *)smk; smk_t s; ulong cur = 0; char r;
    if (!active() || !o || !priv(o)[SMK_PRIV_SMK / 4]) return 0;
    if (priv(o)[SMK_PRIV_ASYNC / 4]) {
        smack_async *a = async_of(o); unsigned i;
        pthread_mutex_lock(&a->mutex);
        for (;;) {
            for (i = 0; i < a->count; ++i) {
                smack_frame_slot *q = &a->slots[(a->head + i) % 3];
                if (q->valid && q->frame_idx == o[SMK_CUR / 4] && i + 1 < a->count) {
                    smack_frame_slot *n = &a->slots[(a->head + i + 1) % 3];
                    a->head = (a->head + i + 1) % 3; a->count -= i + 1;
                    o[SMK_CUR / 4] = n->frame_idx; priv(o)[SMK_PRIV_DEC / 4] = n->frame_idx;
                    o[SMK_HASPAL / 4] = 1; memcpy((u8 *)o + SMK_PAL, n->palette, 768);
                    pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex); return 1;
                }
            }
            if (a->failed || a->stop) { pthread_mutex_unlock(&a->mutex); return 0; }
            pthread_cond_wait(&a->cond, &a->mutex);
        }
    }
    s = (smk_t)(ulong)priv(o)[SMK_PRIV_SMK / 4];
    r = p_next(s);
    /* storm closes non-looping films before this call at their last frame.
     * Ring films: libsmacker keeps the delta/ring state. Non-ring loops: rewind.
     * Never increment beyond Frames: that froze menu animations after one pass. */
    if (r == 0) r = p_first(s); /* SMK_DONE, not SMK_ERROR */
    if (r == (char)-1 || p_info_all(s, &cur, 0, 0) != 0) {
        log2("[smack] NextFrame errore decoder: ", "avanzamento rifiutato"); return 0;
    }
    o[SMK_CUR / 4] = (u32) cur;
    priv(o)[SMK_PRIV_DEC / 4] = (u32) cur;
    refresh_palette(o); /* storm inspects palette before calling DoFrame */
    if (c_next < 3) {
        reasm_elf_log("[smack] NextFrame #"); reasm_elf_log_hex(++c_next);
        reasm_elf_log(" -> cur="); reasm_elf_log_hex(o[SMK_CUR / 4]); reasm_elf_log("\n");
    } else ++c_next;
    return 1;
}

static u32 SMK_STDCALL SmackGoto(void *smk, u32 frame)
{
    u32 *o = (u32 *)smk;
    if (!active() || !o) return 0;
    if (priv(o)[SMK_PRIV_ASYNC / 4]) {
        smack_async *a = async_of(o); smack_frame_slot *slot; unsigned i;
        if (frame >= o[SMK_FRAMES / 4]) return 0;
        pthread_mutex_lock(&a->mutex);
        a->goto_frame = frame; a->goto_pending = 1; a->head = 0; a->count = 0;
        pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex);
        if (!async_wait_frame(a, frame, &slot)) return 0;
        pthread_mutex_lock(&a->mutex);
        for (i = 0; i < a->count; ++i) if (&a->slots[(a->head + i) % 3] == slot) {
            a->head = (a->head + i) % 3; a->count -= i; break;
        }
        o[SMK_CUR / 4] = frame; priv(o)[SMK_PRIV_DEC / 4] = frame;
        o[SMK_HASPAL / 4] = 1; memcpy((u8 *)o + SMK_PAL, slot->palette, 768);
        pthread_cond_broadcast(&a->cond); pthread_mutex_unlock(&a->mutex); return 1;
    }
    if (!decode_to(o, frame)) return 0;
    o[SMK_CUR / 4] = frame; refresh_palette(o); return 1;
}

/* Ritorna NON-zero finché il frame corrente deve ancora restare: storm salta l'avanzamento. */
static u32 SMK_STDCALL SmackWait(void *smk)
{
    u32 *o = (u32 *) smk;
    u32 usf, t0, now, r;
    if (!active() || !o) return 0;
    usf = priv(o)[SMK_PRIV_USF / 4];
    if (!usf) { if (c_wait < 3) { reasm_elf_log("[smack] Wait #"); reasm_elf_log_hex(++c_wait); reasm_elf_log(" usf=0 -> 0\n"); } else ++c_wait; return 0; }
    t0 = priv(o)[SMK_PRIV_T0 / 4]; now = GetTickCount();
    r = ((now - t0) * 1000u < usf) ? 1u : 0u;
    if (r) ++c_wait_yes; else priv(o)[SMK_PRIV_T0 / 4] = now;
    if (c_wait < 3) {
        reasm_elf_log("[smack] Wait #"); reasm_elf_log_hex(++c_wait);
        reasm_elf_log(" usf="); reasm_elf_log_hex(usf); reasm_elf_log(" dt="); reasm_elf_log_hex(now - t0);
        reasm_elf_log(" -> "); reasm_elf_log_hex(r); reasm_elf_log("\n");
    } else ++c_wait;
    return r;
}

static u32 SMK_STDCALL SmackBufferOpen(u32 hwnd, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
    u32 *b, bytes = a3 * a4;
    (void) hwnd; (void) a2; (void) a5; (void) a6;
    if (!active()) return 0;
    if (bytes == 0) bytes = 640u * 480u * 2u;
    b = (u32 *) calloc(1, BUF_SIZE);
    if (!b) return 0;
    *(void **) ((u8 *) b + BUF_DEST) = calloc(1, bytes);
    if (!*(void **) ((u8 *) b + BUF_DEST)) { free(b); return 0; }
    bpriv(b)[0] = bytes; bpriv(b)[4] = a4;      /* dimensione, altezza (destheight) */
    b[16 / 4] = a3;                          /* pitch lu da storm 1502EFB6 */
    reasm_elf_log("[smack] SmackBufferOpen: byte="); reasm_elf_log_hex(bytes); reasm_elf_log("\n");
    return (u32) (ulong) b;
}

static u32 SMK_STDCALL SmackBufferClose(void *buf)
{
    u32 *b = (u32 *) buf;
    if (!active() || !b) return 0;
    if (*(void **) ((u8 *) b + BUF_DEST)) free(*(void **) ((u8 *) b + BUF_DEST));
    free(b);
    return 1;
}

static u32 SMK_STDCALL SmackBufferNewPalette(void *buf, const void *pal, u32 a3)
{
    static int once;
    (void) buf; (void) pal; (void) a3;
    if (!active()) return 0;
    /* storm applies SMACK+108 itself; no claim that SmackBuffer's palette was updated. */
    if (!once) { once = 1; log2("[smack] BufferNewPalette non coperta: ", "palette applicata separatamente da storm"); }
    return 0;
}

static void SMK_STDCALL SmackToBuffer(void *smk, u32 left, u32 top, u32 pitch, u32 destheight,
                                     void *dest, u32 flags)
{
    u32 *o = (u32 *) smk;
    const u8 *px;
    u32 rows;
    if (!active() || !o) return;
    priv(o)[SMK_PRIV_DEST / 4] = (u32) (ulong) dest;
    priv(o)[SMK_PRIV_PITCH / 4] = pitch;
    priv(o)[SMK_PRIV_TOP / 4] = top; priv(o)[SMK_PRIV_LEFT / 4] = left;
    priv(o)[SMK_PRIV_DH / 4] = destheight;
    priv(o)[SMK_PRIV_BAD / 4] = flags != 0 || !dest;
    if (getenv("SC_SMACK_TRACE")) {
        reasm_elf_log("[smack-trace] ToBuffer smk="); reasm_elf_log_hex((u32)(ulong)smk);
        reasm_elf_log(" frame/w/h="); reasm_elf_log_hex(o[SMK_CUR/4]);
        reasm_elf_log_hex(o[SMK_W/4]); reasm_elf_log_hex(o[SMK_H/4]);
        reasm_elf_log(" dest/pitch/xy/dh/flags="); reasm_elf_log_hex((u32)(ulong)dest);
        reasm_elf_log_hex(pitch); reasm_elf_log_hex(left); reasm_elf_log_hex(top);
        reasm_elf_log_hex(destheight); reasm_elf_log_hex(flags); reasm_elf_log("\n");
    }
    if (flags) { log1h("[smack] ToBuffer flags non coperti: ", flags); return; }
    if (priv(o)[SMK_PRIV_ASYNC / 4]) {
        smack_frame_slot *slot;
        if (!async_wait_frame(async_of(o), o[SMK_CUR / 4], &slot)) return;
        px = slot->video;
        if (!copy_frame_data(o, px) || !dest) return;
    } else {
        if (!copy_frame(o) || !dest) return;
        px = p_get_video((smk_t)(ulong)priv(o)[SMK_PRIV_SMK / 4]);
    }
    rows = o[SMK_H / 4] < destheight - top ? o[SMK_H / 4] : destheight - top;
    priv(o)[SMK_PRIV_RECT / 4] = 1;
    if (c_tobuf < 3) {
        reasm_elf_log("[smack] ToBuffer #"); reasm_elf_log_hex(++c_tobuf);
        reasm_elf_log(" dest="); reasm_elf_log_hex((unsigned) (ulong) dest);
        reasm_elf_log(" pitch="); reasm_elf_log_hex(pitch); reasm_elf_log(" rows="); reasm_elf_log_hex(rows);
        reasm_elf_log(" px0="); reasm_elf_log_hex(px[0]); reasm_elf_log(" dst0=");
        reasm_elf_log_hex(((u8 *) dest)[0]); reasm_elf_log("\n");
    } else ++c_tobuf;
    /* Target remains registered until replaced; DoFrame refreshes it. */
}

/* storm 1502F050/1502F0B3: (smk, surface=0), enumerate until return 0.
 * libsmacker has no dirty-rectangle API: one full-frame rectangle is conservative,
 * not a fabricated partial update. LastRect is OUTPUT in SMACK, not an input pointer. */
static u32 SMK_STDCALL SmackToBufferRect(void *smk, u32 surface)
{
    u32 *o = (u32 *) smk, result = 0;
    if (!active() || !o) return 0;
    if (surface != 0) {
        log1h("[smack] ToBufferRect surface non coperta: ", surface);
        return 0;
    }
    if (priv(o)[SMK_PRIV_RECT / 4]) {
        o[SMK_RECTX / 4] = 0; o[SMK_RECTY / 4] = 0;
        o[SMK_RECTW / 4] = o[SMK_W / 4]; o[SMK_RECTH / 4] = o[SMK_H / 4];
        priv(o)[SMK_PRIV_RECT / 4] = 0;
        result = 1;
    }
    if (c_torect++ < 6) {
        reasm_elf_log("[smack] ToBufferRect cur="); reasm_elf_log_hex(o[SMK_CUR / 4]);
        reasm_elf_log(" -> "); reasm_elf_log_hex(result); reasm_elf_log("\n");
    }
    return result;
}

static u32 SMK_STDCALL SmackSoundUseDirectSound(u32 a1)
{
    static int once;
    if (!active()) return 0;
    /* P11: se il modulo smack_audio è nel link, l'audio dei filmati è vero (assente => invariato). */
    {   extern int smack_audio_sound_use_dsound(void *) __attribute__((weak));
        if (smack_audio_sound_use_dsound) return (u32) smack_audio_sound_use_dsound((void *) (ulong) a1); }
    (void) a1;
    if (!once) { once = 1; log2("[smack] audio SMACK non implementato (video-only, dichiarato): ", "SmackSoundUseDirectSound"); }
    return 0;
}

static u32 SMK_STDCALL SmackVolumePan(void *smk, u32 track, u32 volume, u32 pan)
{
    static int once;
    if (!active()) return 0;
    /* P11: volume/pan reali quando il modulo smack_audio c'è (assente => invariato). */
    {   extern int smack_audio_volume_pan(void *, unsigned, unsigned, unsigned) __attribute__((weak));
        if (smack_audio_volume_pan) return (u32) smack_audio_volume_pan(smk, track, volume, pan); }
    (void) smk; (void) track; (void) volume; (void) pan;
    if (!once) { once = 1; log2("[smack] audio SMACK non implementato (video-only, dichiarato): ", "SmackVolumePan"); }
    return 0;
}

/* ---------------------------------------------------------------- aggancio allo shim */
#define SMACK_MODULE ((void *) (ulong) 0x73000001u)

static int active(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("SC_SMACK"); v = (e && e[0] == '0') ? 0 : 1; }
    return v;
}

typedef struct { const char *name; void *fn; unsigned char nargs; } SmackEntry;
#if defined(__arm__) || defined(__i386__)
extern void *arm_com_thunk(void *, unsigned, unsigned);
#endif
static const SmackEntry g_names[] = {
    { "_SmackBufferClose@4",          (void *) (ulong) SmackBufferClose, 1 },
    { "_SmackBufferNewPalette@12",    (void *) (ulong) SmackBufferNewPalette, 3 },
    { "_SmackBufferOpen@24",          (void *) (ulong) SmackBufferOpen, 6 },
    { "_SmackClose@4",                (void *) (ulong) SmackClose, 1 },
    { "_SmackDoFrame@4",              (void *) (ulong) SmackDoFrame, 1 },
    { "_SmackGoto@8",                 (void *) (ulong) SmackGoto, 2 },
    { "_SmackNextFrame@4",            (void *) (ulong) SmackNextFrame, 1 },
    { "_SmackOpen@12",                (void *) (ulong) SmackOpen, 3 },
    { "_SmackSoundUseDirectSound@4",  (void *) (ulong) SmackSoundUseDirectSound, 1 },
    { "_SmackToBuffer@28",            (void *) (ulong) SmackToBuffer, 7 },
    { "_SmackToBufferRect@8",         (void *) (ulong) SmackToBufferRect, 2 },
    { "_SmackVolumePan@16",           (void *) (ulong) SmackVolumePan, 4 },
    { "_SmackWait@4",                 (void *) (ulong) SmackWait, 1 },
    { 0, 0, 0 }
};

static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) { int x = *a++, y = *b++; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; }
    return *a == *b;
}

/* Chiamata dallo shim: LoadLibraryA("smackw32.dll") -> handle sintetico. */
void *win32_smack_load_library(const char *nativename)
{
    if (!nativename) return 0;
    if (!ci_equal(nativename, "smackw32.dll") && !ci_equal(nativename, "smackw32")) return 0;
    if (active()) {
        if (!lib_load()) return 0;
        log2("[smack] smackw32.dll fornita dal modulo nativo: ", nativename);
    }
    return SMACK_MODULE;
}

/* Chiamata dallo shim: GetProcAddress su quel modulo, nomi decorati. */
void *win32_smack_get_proc(void *module, const char *decorated)
{
    const SmackEntry *e;
    if (module != SMACK_MODULE || !decorated) return 0;
    for (e = g_names; e->name; ++e)
        if (ci_equal(decorated, e->name)) {
#if defined(__arm__) || defined(__i386__)
            /* Tutti i nomi esportati sono decorati _Name@N: N byte callee-pop = stdcall. */
            return arm_com_thunk(e->fn, e->nargs, e->nargs);
#else
            return e->fn;
#endif
        }
    log2("[smack] nome decorato non coperto: ", decorated);
    return 0;
}
