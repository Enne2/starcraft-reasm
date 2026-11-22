/* sc-reasm/tools/elf/com_dsound.c — IDirectSound + IDirectSoundBuffer su SDL (via host_elf).
 *
 * Modello di riferimento (read-only): ~/dev/lola-recomp/runtime/com/dsound_shim.c (vtable 11+21,
 * pool STATICO: lezione "una scrittura fuori posto del gioco nell'heap condiviso sovrascriveva la
 * vtable del buffer") e ~/dev/reasm-toolkit/port/dsound_shim.c (mixer software con volume/pan/
 * ricampionamento). Qui il mixer vive in host_elf (voci) e questo file e' la traduzione COM.
 *
 * D19: gli slot fuori dai metodi documentati (IDirectSound 11 = IDirectSound8::VerifyCertification,
 * IDirectSoundBuffer 21..23 = IDirectSoundBuffer8::SetFX/AcquireResources/GetObjectInPath) sono
 * ANNUNCIATI e fermano il programma: mai un `ret N` con arita' indovinata.
 */
#include "com_dsound.h"
#include "host_elf.h"
#include "reasm_elf.h"
#if defined(__arm__) || defined(__i386__)
extern void arm_com_wrap_vtable(void **, const unsigned char *, unsigned);
extern void *arm_com_thunk(void *, unsigned, unsigned);
#endif

#define S DSOUND_STDCALL
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void *malloc(unsigned long);
extern void free(void *);
extern char *getenv(const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern void *memset(void *, int, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern void _exit(int);

#ifndef NULL
#define NULL ((void *) 0)
#endif

/* ---- HRESULT ---- */
#define DS_OK 0
#define DSERR_GENERIC        0x8007000Eu
#define DSERR_INVALIDPARAM   0x80070057u
#define DSERR_ALLOCATED      0x8878000Au
#define DSERR_UNSUPPORTED    0x80004005u

/* ---- costanti DirectSound (DDK) ---- */
#define DSBCAPS_PRIMARYBUFFER   0x00000001u
#define DSBCAPS_STATIC          0x00000002u
#define DSBCAPS_CTRLFREQUENCY   0x00000020u
#define DSBCAPS_CTRLPAN         0x00000040u
#define DSBCAPS_CTRLVOLUME      0x00000080u
#define DSBCAPS_GLOBALFOCUS     0x00008000u
#define DSBCAPS_GETCURRENTPOSITION2 0x00010000u
#define DSBPLAY_LOOPING         0x00000001u
#define DSBSTATUS_PLAYING       0x00000001u
#define DSBSTATUS_BUFFERLOST    0x00000002u
#define DSBSTATUS_LOOPING       0x00000004u
#define DSSCL_WRITEPRIMARY      0x00000004u
#define DSCAPS_PRIMARY16BIT     0x00000008u
#define DSCAPS_CONTINUOUSRATE   0x00001000u
#define DSCAPS_EMULDRIVER       0x00000200u

/* DSBUFFERDESC: dwSize 0, dwFlags 4, dwBufferBytes 8, dwReserved 12, lpwfxFormat 16 */
#define DSBD_DWFLAGS       0x04
#define DSBD_DWBUFFERBYTES 0x08
#define DSBD_LPWXFFORMAT   0x10
/* WAVEFORMATEX */
#define WFX_FORMATTAG      0x00
#define WFX_CHANNELS       0x02
#define WFX_SAMPLESPERSEC  0x04
#define WFX_AVGBYTESPERSEC 0x08
#define WFX_BLOCKALIGN     0x0C
#define WFX_BITSPERSAMPLE  0x0E
/* DSBCAPS: dwSize 0, dwFlags 4, dwBufferBytes 8, dwUnlockTransferRate 12, dwPlayCpuOverhead 16 */
#define DSBCAPS_SIZE       36
/* DSCAPS (96 byte) */
#define DSCAPS_SIZE        96

#define DSOUND_MAX_DS      4
#define DSOUND_MAX_BUF     DSOUND_BUFFER_CAPACITY

/* ============================ oggetti (pool statico per gli oggetti) ============================
 * Le vtable e gli oggetti vivono FUORI dall'heap: una scrittura fuori posto del gioco non puo'
 * sovrascrivere il puntatore di vtable (la causa di crash documentata in LOLA). I dati PCM, che
 * il gioco scrive per definizione, stanno su heap e passano dai controlli di Lock/Unlock. */
typedef struct DSB {
    void **vt;
    long refs;
    int used;
    u8 *data;                 /* dati PCM (0 per la primaria) */
    unsigned long size;
    int is_primary;
    int rate, ch, bits;
    int volume;               /* semantica DirectSound: 0 = massimo, -10000 = silenzio */
    int pan;                  /* -10000 .. +10000 */
    int loop;
    int playing;
    int voice;                /* id della voce del mixer host (-1 = nessuna) */
    unsigned int id;
    unsigned long locked_max; /* byte massimi resi scrivibili (diagnostica) */
    unsigned int last_play;   /* tick dell'ultima Play: serve alla politica di furto delle voci */
    u32 cursor;              /* sopravvive a Stop, reaping e furto della voce host */
} DSB;

typedef struct DSIn {
    void **vt;
    long refs;
    int used;
    void *hwnd;
    u32 coop;
} DSIn;

static void *ds_vt[12];
static void *dsb_vt[24];
static DSIn g_ds[DSOUND_MAX_DS];
static DSB g_buf[DSOUND_MAX_BUF];
static DSIn *g_last_ds;
static DSB *g_first_buf;
static u32 g_bufseq;

static com_dsound_stats g_stats;

static void ds_ensure_out(void);   /* definita piu' sotto (uscita host, formato della primaria) */

/* ---- slot non documentati: annuncio + stop (D19) ---- */
static long S unk_ds(void) {
    reasm_elf_log("[dsound] NON IMPLEMENTATO: IDirectSound slot oltre i documentati -> stop (D19)\n");
    _exit(3);
    return 0;
}
static long S unk_buf(void) {
    reasm_elf_log("[dsound] NON IMPLEMENTATO: IDirectSoundBuffer slot oltre i documentati -> stop (D19)\n");
    _exit(3);
    return 0;
}

/* ============================ IDirectSoundBuffer (21) ============================ */
static void ds_event(const char *what, DSB *b, int voice);
static long S b_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DS_OK; }
static long S b_AddRef(void *t) { return ++((DSB *) t)->refs; }
static long S b_Release(void *t) {
    DSB *b = (DSB *) t;
    if (--b->refs > 0) return b->refs;
    ds_event("RELEASE", b, b->voice);
    if (b->voice >= 0) { host_elf_voice_close(b->voice); b->voice = -1; }
    if (b->data) { free(b->data); b->data = 0; }
    b->used = 0;
    --g_stats.buffers_live;
    return 0;
}
static long S b_GetCaps(void *t, void *caps) {
    DSB *b = (DSB *) t;
    if (caps) {
        u8 *c = (u8 *) caps;
        memset(c, 0, DSBCAPS_SIZE);
        *(u32 *) (c + 0) = DSBCAPS_SIZE;
        *(u32 *) (c + 4) = (b->is_primary ? DSBCAPS_PRIMARYBUFFER : 0) |
                           DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
        *(u32 *) (c + 8) = (u32) (b->is_primary ? 0 : b->size);
        *(u32 *) (c + 12) = 0;
        *(u32 *) (c + 16) = 0;
    }
    return DS_OK;
}
static long S b_GetCurrentPosition(void *t, u32 *play, u32 *write) {
    DSB *b = (DSB *) t;
    u32 p = (b->voice >= 0) ? (u32) host_elf_voice_pos(b->voice) : b->cursor;
    b->cursor = p;
    if (play) *play = p;
    if (write) *write = p;
    return DS_OK;
}
static long S b_GetFormat(void *t, void *wfx, u32 size, u32 *written) {
    DSB *b = (DSB *) t;
    u8 *w = (u8 *) wfx;
    (void) size;
    if (w) {
        memset(w, 0, 18);
        *(u16 *) (w + WFX_FORMATTAG) = 1;                       /* WAVE_FORMAT_PCM */
        *(u16 *) (w + WFX_CHANNELS) = (u16) b->ch;
        *(u32 *) (w + WFX_SAMPLESPERSEC) = (u32) b->rate;
        *(u32 *) (w + WFX_AVGBYTESPERSEC) = (u32) (b->rate * b->ch * (b->bits / 8));
        *(u16 *) (w + WFX_BLOCKALIGN) = (u16) (b->ch * (b->bits / 8));
        *(u16 *) (w + WFX_BITSPERSAMPLE) = (u16) b->bits;
        *(u16 *) (w + 16) = 0;                                  /* cbSize */
    }
    if (written) *written = 18;
    return DS_OK;
}
static long S b_GetVolume(void *t, long *v) { if (v) *v = ((DSB *) t)->volume; return DS_OK; }
static long S b_GetPan(void *t, long *v) { if (v) *v = ((DSB *) t)->pan; return DS_OK; }
static long S b_GetFrequency(void *t, u32 *v) { if (v) *v = (u32) ((DSB *) t)->rate; return DS_OK; }
static long S b_GetStatus(void *t, u32 *st) {
    DSB *b = (DSB *) t;
    unsigned int vs = 0;
    if (b->voice >= 0) {
        vs = host_elf_voice_status(b->voice);
        /* se il mixer ha finito (buffer non loopato) il buffer non e' piu' PLAYING: senza questo
         * il flag nostro resterebbe alto e il gioco crederebbe di suonare per sempre. */
        if (!(vs & HOST_VOICE_PLAYING)) b->playing = 0;
    }
    if (st) {
        *st = (b->playing ? DSBSTATUS_PLAYING : 0u) |
              ((b->playing && b->loop) ? DSBSTATUS_LOOPING : 0u);
    }
    return DS_OK;
}
static long S b_Initialize(void *t, void *ds, void *desc) { (void) t; (void) ds; (void) desc; return DS_OK; }
static long S b_Lock(void *t, u32 offset, u32 bytes, void **p1, u32 *n1, void **p2, u32 *n2, u32 flags) {
    DSB *b = (DSB *) t;
    unsigned long rest;
    (void) flags;
    if (!b->data || b->size == 0) return DSERR_INVALIDPARAM;      /* la primaria non si blocca */
    if (offset >= b->size) return DSERR_INVALIDPARAM;
    rest = b->size - offset;
    if (bytes == 0 || bytes > b->size) bytes = (u32) (rest ? rest : b->size);
    if (p1) *p1 = b->data + offset;
    if (n1) *n1 = (u32) (bytes <= rest ? bytes : rest);
    if (p2) *p2 = (bytes > rest) ? b->data : NULL;
    if (n2) *n2 = (bytes > rest) ? (u32) (bytes - rest) : 0;
    if (bytes > b->locked_max) b->locked_max = bytes;
    if (bytes > g_stats.bytes_locked) g_stats.bytes_locked = bytes;
    return DS_OK;
}

/* Log compatto degli eventi audio (SC_DSOUND_LOG=1): serve a distinguere "il gioco ha smesso di
 * chiedere un suono" da "il nostro strato ha smesso di darlo". Ogni 128 eventi stampa un riepilogo
 * con buffer vivi, voci occupate, furti e fallimenti. */
extern char *getenv(const char *);
static int ds_log_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("SC_DSOUND_LOG"); v = (e && e[0] == '1') ? 1 : 0; } return v; }
static unsigned int ds_live(void) { int i, n = 0; for (i = 0; i < DSOUND_MAX_BUF; ++i) if (g_buf[i].used && !g_buf[i].is_primary) ++n; return (unsigned int) n; }
static void ds_event(const char *what, DSB *b, int voice) {
    static unsigned int n;
    if (!ds_log_on()) return;
    { char m[160];
      snprintf(m, sizeof m, "[dsound] t=%u %s #%u voice=%d loop=%d live=%u voci=%d\n",
               host_elf_ticks(), what, (unsigned) b->id, voice, b->loop, ds_live(), host_elf_voice_used_count());
      reasm_elf_log(m); }
    if (++n % 128u == 0u) {
        char m[256];
        snprintf(m, sizeof m, "[dsound] RIEPILOGO t=%u creati=%u vivi=%u voci=%d rubate=%u fallite=%u plays=%u oggetti=%u picco=%u limite=%u buf_fail=%u\n",
                 host_elf_ticks(), g_stats.buffers, ds_live(), host_elf_voice_used_count(),
                 g_stats.voice_steals, g_stats.voice_fail, g_stats.plays,
                 g_stats.buffers_live, g_stats.buffers_peak, (unsigned) DSOUND_MAX_BUF, g_stats.buffer_fail);
        reasm_elf_log(m);
    }
}

/* Gestione delle VOCI del mixer host.
 * Il mixer ha HOST_VOICES voci (32) e prima una voce veniva liberata solo con Release: il gioco
 * tiene vivi decine di buffer (uno per frase, uno per unita'), quindi dopo ~32 suoni diverse il
 * mixer era pieno e OGNI Play successivo restava muto ("mixer pieno") — sintomo: cliccando piu'
 * volte un Marine, dopo la prima frase bonus lui e tutte le unita' dello stesso tipo tacciono.
 * Qui: (a) le voci dei suoni FINITI (non in loop) tornano disponibili; (b) se serve una voce e sono
 * tutte occupate, si ruba quella del buffer suonato meno di recente (il mixer, come su hardware
 * reale, ha un numero finito di canali simultanei). */
static void ds_voice_reap(void) {
    int i;
    for (i = 0; i < DSOUND_MAX_BUF; ++i) {
        DSB *b = &g_buf[i];
        if (!b->used || b->is_primary || b->voice < 0) continue;
        if (b->loop) continue;
        if (!(host_elf_voice_status(b->voice) & HOST_VOICE_PLAYING)) {
            b->cursor = (u32) host_elf_voice_pos(b->voice);
            host_elf_voice_close(b->voice);
            b->voice = -1;
            b->playing = 0; /* GetStatus non potra' piu' interrogare la voce chiusa. */
        }
    }
}

/* Apre una voce per b; se il mixer e' pieno ruba la voce del buffer non-loop piu' vecchio. */
static int ds_voice_acquire(DSB *b, host_audio_voice *v) {
    int i, victim = -1;
    unsigned int oldest = 0;
    ds_voice_reap();
    b->voice = host_elf_voice_open(v);
    if (b->voice >= 0) return b->voice;
    /* prima scelta: buffer non in loop; se sono TUTTI in loop si ruba comunque il piu' vecchio
     * (meglio perdere un suono in loop che lasciare il gioco muto). */
    for (i = 0; i < DSOUND_MAX_BUF; ++i) {
        DSB *c = &g_buf[i];
        if (c == b || !c->used || c->is_primary || c->voice < 0 || c->loop) continue;
        if (victim < 0 || c->last_play < oldest) { victim = i; oldest = c->last_play; }
    }
    if (victim < 0) {
        for (i = 0; i < DSOUND_MAX_BUF; ++i) {
            DSB *c = &g_buf[i];
            if (c == b || !c->used || c->is_primary || c->voice < 0) continue;
            if (victim < 0 || c->last_play < oldest) { victim = i; oldest = c->last_play; }
        }
    }
    if (getenv("REASM_TRACE")) {
        char m[160];
        snprintf(m, sizeof m, "[dsound] acquire: open fallita (voci usate=%d, pcm=%u byte=%u), vittime candidate=%d\n",
                 host_elf_voice_used_count(), (unsigned)(unsigned long) v->pcm, (unsigned) v->bytes, victim);
        reasm_elf_log(m);
    }
    if (victim >= 0) {
        DSB *c = &g_buf[victim];
        if (getenv("REASM_TRACE")) {
            char m[128];
            snprintf(m, sizeof m, "[dsound] mixer pieno: rubo la voce del buffer #%u (suonato a %u)\n",
                     (unsigned) c->id, c->last_play);
            reasm_elf_log(m);
        }
        c->cursor = (u32) host_elf_voice_pos(c->voice);
        host_elf_voice_close(c->voice);
        c->voice = -1;
        c->playing = 0;
        b->voice = host_elf_voice_open(v);
        if (b->voice >= 0) ++g_stats.voice_steals;
    }
    return b->voice;
}

static long S b_Play(void *t, u32 reserved1, u32 priority, u32 flags) {
    DSB *b = (DSB *) t;
    (void) reserved1; (void) priority;
    b->loop = (flags & DSBPLAY_LOOPING) ? 1 : 0;      /* i FLAG sono il TERZO argomento (DDK) */
    b->playing = 1;
    ++g_stats.plays;
    if (!b->is_primary) ds_ensure_out();     /* primo suono: apri l'uscita host (come il modello Wine) */
    if (!b->is_primary && b->data) {
        host_audio_voice v;
        v.pcm = b->data;
        v.bytes = b->size;
        v.rate = b->rate;
        v.channels = b->ch;
        v.bits = b->bits;
        v.loop = b->loop;
        v.volume = 256 + (b->volume < 0 ? (b->volume * 256) / 10000 : 0);
        if (v.volume < 0) v.volume = 0;
        v.pan = b->pan / 100;
        b->last_play = host_elf_ticks();
        if (b->voice >= 0) b->cursor = (u32) host_elf_voice_pos(b->voice);
        if (b->voice < 0) {
            if (ds_voice_acquire(b, &v) < 0) {
                ++g_stats.voice_fail;
                b->playing = 0;
                reasm_elf_log("[dsound] mixer pieno: nessuna voce disponibile (Play senza uscita, DSERR_ALLOCATED)\n");
                ds_event("PLAY_FAIL", b, -1);
                return DSERR_ALLOCATED;
            }
        } else {
            host_elf_voice_set(b->voice, &v);
        }
        if (b->voice >= 0) {
            /* Stop riprende dal cursore conservato; un suono terminato puo' ripartire. */
            if (b->cursor >= b->size) b->cursor = 0;
            host_elf_voice_set_pos(b->voice, b->cursor);
            host_elf_voice_play(b->voice);
        }
        ds_event("PLAY", b, b->voice); /* voce effettivamente acquisita, non il vecchio -1 */
    }
    return DS_OK;
}
static long S b_SetCurrentPosition(void *t, u32 pos) {
    DSB *b = (DSB *) t;
    if (pos >= b->size && !b->is_primary) return DSERR_INVALIDPARAM;
    b->cursor = pos;
    if (b->voice >= 0) host_elf_voice_set_pos(b->voice, pos);
    return DS_OK;
}
static long S b_SetFormat(void *t, void *wfx) {
    DSB *b = (DSB *) t;
    const u8 *w = (const u8 *) wfx;
    if (!w) return DSERR_INVALIDPARAM;
    {   u16 ch = *(const u16 *) (w + WFX_CHANNELS);
        u32 rate = *(const u32 *) (w + WFX_SAMPLESPERSEC);
        u16 bits = *(const u16 *) (w + WFX_BITSPERSAMPLE);
        if (ch == 1 || ch == 2) b->ch = ch;
        if (rate > 0) b->rate = (int) rate;
        if (bits == 8 || bits == 16) b->bits = bits;
    }
    if (b->voice >= 0) {
        host_audio_voice v;
        v.pcm = b->data; v.bytes = b->size; v.rate = b->rate; v.channels = b->ch; v.bits = b->bits;
        v.loop = b->loop;
        v.volume = 256 + (b->volume < 0 ? (b->volume * 256) / 10000 : 0);
        if (v.volume < 0) v.volume = 0;
        v.pan = b->pan / 100;
        host_elf_voice_set(b->voice, &v);
    }
    return DS_OK;
}
static long S b_SetVolume(void *t, long v) {
    DSB *b = (DSB *) t;
    b->volume = (v > 0) ? 0 : (v < -10000 ? -10000 : (int) v);
    if (b->voice >= 0) {
        host_audio_voice q;
        q.pcm = b->data; q.bytes = b->size; q.rate = b->rate; q.channels = b->ch; q.bits = b->bits;
        q.loop = b->loop;
        q.volume = 256 + (b->volume * 256) / 10000;
        if (q.volume < 0) q.volume = 0;
        q.pan = b->pan / 100;
        host_elf_voice_set(b->voice, &q);
    }
    return DS_OK;
}
static long S b_SetPan(void *t, long v) {
    DSB *b = (DSB *) t;
    b->pan = (int) ((v > 10000) ? 10000 : (v < -10000 ? -10000 : v));
    if (b->voice >= 0) host_elf_voice_set(b->voice, &(host_audio_voice) {
        b->data, b->size, b->rate, b->ch, b->bits, b->loop,
        256 + (b->volume < 0 ? (b->volume * 256) / 10000 : 0), b->pan / 100 });
    return DS_OK;
}
static long S b_SetFrequency(void *t, u32 v) {
    DSB *b = (DSB *) t;
    if (v > 0) b->rate = (int) v;
    if (b->voice >= 0) host_elf_voice_set(b->voice, &(host_audio_voice) {
        b->data, b->size, b->rate, b->ch, b->bits, b->loop,
        256 + (b->volume < 0 ? (b->volume * 256) / 10000 : 0), b->pan / 100 });
    return DS_OK;
}
static long S b_Stop(void *t) {
    DSB *b = (DSB *) t;
    b->playing = 0;
    ds_event("STOP", b, b->voice);
    if (b->voice >= 0) {
        host_elf_voice_stop(b->voice);
        b->cursor = (u32) host_elf_voice_pos(b->voice);
        host_elf_voice_close(b->voice);   /* la voce torna al mixer: senza questo si esaurisce */
        b->voice = -1;
    }
    return DS_OK;
}
static long S b_Unlock(void *t, void *p1, u32 n1, void *p2, u32 n2) {
    (void) t; (void) p1; (void) n1; (void) p2; (void) n2;
    ++g_stats.unlocks;
    return DS_OK;
}
static long S b_Restore(void *t) { (void) t; return DS_OK; }

/* Errori SEMPRE visibili, anche senza SC_DSOUND_LOG. Non lasciare un puntatore
 * di uscita vecchio e non fingere successo: il chiamante decide se ritentare. */
static long ds_buffer_error(const char *op, const char *why, u32 bytes, u32 hr) {
    char m[224];
    ++g_stats.buffer_fail;
    snprintf(m, sizeof m, "[dsound] %s FALLITA: %s byte=%u vivi=%u capacita=%u voci=%d hr=0x%08x\n",
             op, why, bytes, g_stats.buffers_live, (unsigned) DSOUND_MAX_BUF,
             host_elf_voice_used_count(), hr);
    reasm_elf_log(m);
    return (long) hr;
}
static void ds_buffer_created(void) {
    ++g_stats.buffers;
    if (++g_stats.buffers_live > g_stats.buffers_peak)
        g_stats.buffers_peak = g_stats.buffers_live;
}

/* ============================ IDirectSound (11) ============================ */
static long S ds_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DS_OK; }
static long S ds_AddRef(void *t) { return ++((DSIn *) t)->refs; }
static long S ds_Release(void *t) { return --((DSIn *) t)->refs; }
static long S ds_CreateSoundBuffer(void *t, void *desc, void **out, void *outer) {
    const u8 *d = (const u8 *) desc;
    u32 flags = d ? *(const u32 *) (d + DSBD_DWFLAGS) : 0;
    u32 bytes = d ? *(const u32 *) (d + DSBD_DWBUFFERBYTES) : 0;
    const u8 *wfx = (d && *(const void **) (d + DSBD_LPWXFFORMAT))
                    ? (const u8 *) *(const void **) (d + DSBD_LPWXFFORMAT) : NULL;
    DSB *b = NULL;
    int i;
    (void) t; (void) outer;
    if (out) *out = NULL;
    if (!out || !desc) return ds_buffer_error("CreateSoundBuffer", "parametri", bytes, DSERR_INVALIDPARAM);
    for (i = 0; i < DSOUND_MAX_BUF; ++i) if (!g_buf[i].used) { b = &g_buf[i]; break; }
    if (!b) return ds_buffer_error("CreateSoundBuffer", "slot esauriti", bytes, DSERR_GENERIC);
    memset(b, 0, sizeof *b);
    b->vt = dsb_vt; b->refs = 1; b->used = 1; b->voice = -1;
    b->volume = 0; b->pan = 0; b->loop = 0; b->playing = 0;
    b->rate = 22050; b->ch = 2; b->bits = 16;
    b->is_primary = (flags & DSBCAPS_PRIMARYBUFFER) ? 1 : 0;
    if (wfx) {
        u16 ch = *(const u16 *) (wfx + WFX_CHANNELS);
        u32 rate = *(const u32 *) (wfx + WFX_SAMPLESPERSEC);
        u16 bits = *(const u16 *) (wfx + WFX_BITSPERSAMPLE);
        if (ch == 1 || ch == 2) b->ch = ch;
        if (rate > 0) b->rate = (int) rate;
        if (bits == 8 || bits == 16) b->bits = bits;
    }
    if (!b->is_primary) {
        if (bytes == 0 || bytes > 32u * 1024u * 1024u) bytes = 65536;
        b->size = bytes;
        b->data = (u8 *) malloc(b->size ? b->size : 1);
        if (!b->data) { b->used = 0; return ds_buffer_error("CreateSoundBuffer", "memoria PCM", bytes, DSERR_GENERIC); }
        memset(b->data, 0, b->size);
    }
    b->id = ++g_bufseq;
    if (!g_first_buf) g_first_buf = b;
    ds_buffer_created();
    if (out) *out = b;
    reasm_elf_log("[dsound] CreateSoundBuffer #");
    reasm_elf_log_hex(b->id);
    reasm_elf_log(b->is_primary ? " PRIMARIA" : " secondaria");
    reasm_elf_log(", byte ");
    reasm_elf_log_hex((unsigned) b->size);
    reasm_elf_log(", formato ");
    reasm_elf_log_hex((unsigned) ((b->rate << 16) | (b->ch << 8) | b->bits));
    reasm_elf_log("\n");
    return DS_OK;
}
static long S ds_GetCaps(void *t, void *caps) {
    u8 *c = (u8 *) caps;
    (void) t;
    if (c) {
        memset(c, 0, DSCAPS_SIZE);
        *(u32 *) (c + 0) = DSCAPS_SIZE;
        *(u32 *) (c + 4) = DSCAPS_PRIMARY16BIT | DSCAPS_CONTINUOUSRATE | DSCAPS_EMULDRIVER;
        *(u32 *) (c + 8) = 100;                 /* dwMinSecondarySampleRate */
        *(u32 *) (c + 12) = 100000;             /* dwMaxSecondarySampleRate */
        *(u32 *) (c + 16) = 1;                  /* dwPrimaryBuffers */
        *(u32 *) (c + 20) = DSOUND_MAX_BUF;     /* dwMaxHwMixingAllBuffers */
        *(u32 *) (c + 24) = DSOUND_MAX_BUF;     /* dwFreeHwMixingAllBuffers */
    }
    return DS_OK;
}
/* DuplicateSoundBuffer(this, src, out): 3 argomenti -> ret 12. D19: NON si ritorna DS_OK con
 * *out = NULL (era il comportamento precedente e faceva crashare il gioco: il canale duplicato
 * finiva a 0 e la `mov ecx,[eax]` successiva in sub_4BBAF0 andava in SEGV a indirizzo 0,
 * misurato 2026-10-11). Il duplicato e' un oggetto INDIPENDENTE con i dati PCM copiati. */
static long S ds_DuplicateSoundBuffer(void *t, void *b, void **o) {
    DSB *src = (DSB *) b;
    DSB *n = NULL;
    int i;
    (void) t;
    if (o) *o = NULL;
    if (!o || !src || !src->used || src->vt != dsb_vt)
        return ds_buffer_error("DuplicateSoundBuffer", "parametri", 0, DSERR_INVALIDPARAM);
    for (i = 0; i < DSOUND_MAX_BUF; ++i) if (!g_buf[i].used) { n = &g_buf[i]; break; }
    if (!n) return ds_buffer_error("DuplicateSoundBuffer", "slot esauriti", (u32) src->size, DSERR_GENERIC);
    memcpy(n, src, sizeof *n);
    n->vt = dsb_vt; n->refs = 1; n->used = 1; n->voice = -1;
    n->playing = 0;                               /* il duplicato nasce FERMO, come nel DDK */
    n->cursor = 0;
    n->id = ++g_bufseq;
    n->locked_max = 0;
    n->data = NULL;
    if (src->data && src->size) {
        n->data = (u8 *) malloc(src->size);
        if (!n->data) { n->used = 0; return ds_buffer_error("DuplicateSoundBuffer", "memoria PCM", (u32) src->size, DSERR_GENERIC); }
        memcpy(n->data, src->data, src->size);    /* copia indipendente: l'originale puo' cambiare */
    }
    *o = n;
    ds_buffer_created();
    reasm_elf_log("[dsound] DuplicateSoundBuffer #");
    reasm_elf_log_hex(n->id);
    reasm_elf_log(" da #");
    reasm_elf_log_hex(src->id);
    reasm_elf_log(", byte ");
    reasm_elf_log_hex((unsigned) n->size);
    reasm_elf_log("\n");
    return DS_OK;
}
/* --- uscita host: aperta UNA volta, con il formato della PRIMARIA -------------------------------
 * MISURA 2026-10-11: nei run di gioco non compariva mai la riga "[host] audio aperto" —
 * host_elf_audio_open() era chiamata solo dal gate dsound_test. Risultato: Play/Lock/Unlock
 * funzionavano, i buffer si riempivano, ma nessun campione raggiungeva il device (silenzio totale).
 * Il modello Wine apre l'uscita al primo Play col formato del buffer che suona; qui si usa il
 * formato della PRIMARIA quando c'e' (e' l'uscita vera del gioco), altrimenti 22050/2/16. */
static void ds_ensure_out(void) {
    int rate = 22050, ch = 2, bits = 16, i;
    if (host_elf_audio_is_open()) return;
    for (i = 0; i < DSOUND_MAX_BUF; ++i)
        if (g_buf[i].used && g_buf[i].is_primary) {
            if (g_buf[i].rate > 0) rate = g_buf[i].rate;
            if (g_buf[i].ch == 1 || g_buf[i].ch == 2) ch = g_buf[i].ch;
            if (g_buf[i].bits == 8 || g_buf[i].bits == 16) bits = g_buf[i].bits;
            break;
        }
    if (host_elf_audio_open(rate, ch, bits) != 0)
        reasm_elf_log("[dsound] uscita host NON aperta: il mixer resta muto (D19, nessun finto OK)\n");
}

/* P11 (audio dei filmati SMACK, modulo tools/elf/smack_audio.c): apre/verifica l'uscita host con la
 * STESSA politica di ds_ensure_out() (formato della primaria del gioco). Ritorna 1 se l'uscita e'
 * aperta, 0 altrimenti: chi chiamera' NON deve fingere che l'audio esca (D19). Additiva: nessun
 * percorso esistente la usa. */
int com_dsound_out_ready(void)
{
    ds_ensure_out();
    return host_elf_audio_is_open() ? 1 : 0;
}

static long S ds_SetCooperativeLevel(void *t, void *hwnd, u32 flags) {
    DSIn *ds = (DSIn *) t;
    ds->hwnd = hwnd; ds->coop = flags;
    reasm_elf_log("[dsound] SetCooperativeLevel flags=");
    reasm_elf_log_hex(flags);
    reasm_elf_log("\n");
    ds_ensure_out();                     /* il vero DirectSound acquisisce qui il device */
    return DS_OK;
}
static long S ds_Compact(void *t) { (void) t; return DS_OK; }
static long S ds_GetSpeakerConfig(void *t, u32 *c) { (void) t; if (c) *c = 0; return DS_OK; }
static long S ds_SetSpeakerConfig(void *t, u32 c) { (void) t; (void) c; return DS_OK; }
static long S ds_Initialize(void *t, const void *g) { (void) t; (void) g; return DS_OK; }

/* ============================ init + export ============================ */
static void init_vt(void) {
    int i;
    if (ds_vt[0]) return;
    ds_vt[0] = ds_QueryInterface; ds_vt[1] = ds_AddRef; ds_vt[2] = ds_Release;
    ds_vt[3] = ds_CreateSoundBuffer; ds_vt[4] = ds_GetCaps; ds_vt[5] = ds_DuplicateSoundBuffer;
    ds_vt[6] = ds_SetCooperativeLevel; ds_vt[7] = ds_Compact; ds_vt[8] = ds_GetSpeakerConfig;
    ds_vt[9] = ds_SetSpeakerConfig; ds_vt[10] = ds_Initialize;
    for (i = 11; i < 12; ++i) ds_vt[i] = (void *) unk_ds;      /* IDirectSound8::VerifyCertification */
    dsb_vt[0] = b_QueryInterface; dsb_vt[1] = b_AddRef; dsb_vt[2] = b_Release;
    dsb_vt[3] = b_GetCaps; dsb_vt[4] = b_GetCurrentPosition; dsb_vt[5] = b_GetFormat;
    dsb_vt[6] = b_GetVolume; dsb_vt[7] = b_GetPan; dsb_vt[8] = b_GetFrequency;
    dsb_vt[9] = b_GetStatus; dsb_vt[10] = b_Initialize; dsb_vt[11] = b_Lock;
    dsb_vt[12] = b_Play; dsb_vt[13] = b_SetCurrentPosition; dsb_vt[14] = b_SetFormat;
    dsb_vt[15] = b_SetVolume; dsb_vt[16] = b_SetPan; dsb_vt[17] = b_SetFrequency;
    dsb_vt[18] = b_Stop; dsb_vt[19] = b_Unlock; dsb_vt[20] = b_Restore;
    for (i = 21; i < 24; ++i) dsb_vt[i] = (void *) unk_buf;    /* IDirectSoundBuffer8 in piu' */
#if defined(__arm__) || defined(__i386__)
    {
        static const unsigned char da[12] = {3,1,1,4,2,3,3,1,2,2,2,2};
        static const unsigned char ba[24] = {3,1,1,2,3,4,2,2,2,2,3,8,4,2,2,2,2,2,1,5,1,3,3,5};
        arm_com_wrap_vtable(ds_vt, da, 12);
        arm_com_wrap_vtable(dsb_vt, ba, 24);
    }
#endif
}

static long dsound_create(const void *guid, void **out, void *outer) {
    DSIn *ds = NULL;
    int i;
    (void) guid; (void) outer;
    init_vt();
    for (i = 0; i < DSOUND_MAX_DS; ++i) if (!g_ds[i].used) { ds = &g_ds[i]; break; }
    if (!ds) return DSERR_GENERIC;
    memset(ds, 0, sizeof *ds);
    ds->vt = ds_vt; ds->refs = 1; ds->used = 1;
    g_last_ds = ds;
    ++g_stats.creates;
    if (g_stats.creates == 1) {
        /* IL DATO CHE SERVE AL SUPERVISORE (docs/05 §5): il gioco risolve DirectSoundCreate ma
         * finora non l'ha mai chiamata. Questo e' il return address della prima chiamata reale. */
        g_stats.first_caller = (unsigned int) (unsigned long) __builtin_return_address(0);
        g_stats.first_tick = host_elf_ticks();
        reasm_elf_log("[dsound] *** PRIMA CHIAMATA REALE a DirectSoundCreate (chiamante ");
        reasm_elf_log_hex(g_stats.first_caller);
        reasm_elf_log(") ***\n");
    }
    if (out) *out = ds;
    reasm_elf_log("[dsound] DirectSoundCreate -> IDirectSound (mixer host)\n");
    return DS_OK;
}

long S DirectSoundCreate(const void *guid, void **out, void *outer) { return dsound_create(guid, out, outer); }
long S DirectSoundCreate8(const void *guid, void **out, void *outer) { return dsound_create(guid, out, outer); }

void com_dsound_note_resolve(unsigned int caller) {
    ++g_stats.resolves;
    if (!g_stats.resolve_caller) {
        g_stats.resolve_caller = caller;
        reasm_elf_log("[dsound] DirectSoundCreate RISOLTA (GetProcAddress dal chiamante ");
        reasm_elf_log_hex(caller);
        reasm_elf_log(")\n");
    }
}

void com_dsound_get_stats(com_dsound_stats *out) {
    if (!out) return;
    *out = g_stats;
    out->buffer_capacity = DSOUND_MAX_BUF;
    out->bytes_mixed = (unsigned int) host_elf_audio_bytes();
}

void *com_dsound_last(void) { return (void *) g_last_ds; }
void *com_dsound_first_buffer(void) { return (void *) g_first_buf; }

void com_dsound_reset(void) {
    int i;
    for (i = 0; i < DSOUND_MAX_BUF; ++i) {
        if (g_buf[i].used) {
            if (g_buf[i].voice >= 0) host_elf_voice_close(g_buf[i].voice);
            if (g_buf[i].data) free(g_buf[i].data);
            g_buf[i].used = 0; g_buf[i].voice = -1; g_buf[i].data = 0;
        }
    }
    for (i = 0; i < DSOUND_MAX_DS; ++i) g_ds[i].used = 0;
    g_last_ds = NULL; g_first_buf = NULL;
    g_stats.buffers_live = 0;
}

void com_dsound_shutdown(void) {
    com_dsound_reset();
    memset(&g_stats, 0, sizeof g_stats);
}

/* ============ AGGANCIO ALLA RISOLUZIONE DINAMICA (hook deboli, win32_shim.c) ============
 * MISURA 2026-10-11 (gdb, build/elf-audio-m1/gdb-m3.log): il gioco chiama SEMPRE il suo SoundInit
 * all'avvio (_WinMain@16 -> sub_4E0820 -> sub_4DB060 -> sub_4BCB70). Quel codice fa
 * LoadLibraryA("dsound.dll") (0x4BCBBA, handle in dword_6D59F0) e poi
 * GetProcAddress(handle,"DirectSoundCreate") (site 0x4BB64C). Nel port nativo il secondo passo
 * ritornava 0: nessuno dei tre rami di win32_shim.c:GetProcAddress conosceva DirectSoundCreate
 * (win32_ui_get_proc gestisce solo DirectDraw*), quindi SoundInit abortiva, dword_6D59F4
 * (IDirectSound) restava 0 e la guardia di sub_4BBFD0 rendeva ogni suono un no-op.
 * Questi due hook (deboli: se com_dsound non e' nel link valgono NULL e il comportamento resta
 * quello di prima) rendono il modulo raggiungibile. Handle dedicato, DISTINTO da quello di ddraw
 * (0x7000DD00 in win32_ui.c): cosi' un GetProcAddress rivolto a un altro modulo non puo' ricevere
 * per sbaglio DirectSoundCreate. Nessuna API finta: qui si espone l'implementazione reale. */
#define DSOUND_FAKE_MODULE 0x7000DD01u

extern int strcmp(const char *, const char *);

static const char *ds_base_name(const char *p) {
    const char *r = p;
    while (*p) { if (*p == '/' || *p == '\\') r = p + 1; ++p; }
    return r;
}
static int ds_name_is(const char *a, const char *b) {   /* ASCII, case-insensitive */
    while (*a && *b) {
        int ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

/* Chiamata da win32_shim.c:LoadLibraryA PRIMA di win32_ui_load_library. */
void *win32_dsound_load_library(const char *name) {
    if (!name) return NULL;
    if (!ds_name_is(ds_base_name(name), "dsound.dll")) return NULL;
    reasm_elf_log("[dsound] LoadLibraryA(\"dsound.dll\") -> modulo nativo (handle ");
    reasm_elf_log_hex(DSOUND_FAKE_MODULE);
    reasm_elf_log(")\n");
    return (void *) (unsigned long) DSOUND_FAKE_MODULE;
}

/* Chiamata da win32_shim.c:GetProcAddress. NULL = non e' roba nostra (si prosegue nella catena). */
void *win32_dsound_get_proc(void *mod, const char *name) {
    if ((unsigned long) mod != (unsigned long) DSOUND_FAKE_MODULE) return NULL;
    if (!name || (unsigned long) name < 0x10000UL) return NULL;   /* ordinale: non noto */
    if (!strcmp(name, "DirectSoundCreate")) {
#if defined(__arm__) || defined(__i386__)
        return arm_com_thunk((void *)DirectSoundCreate, 3, 3);
#else
        return (void *) (unsigned long) (unsigned int) DirectSoundCreate;
#endif
    }
    if (!strcmp(name, "DirectSoundCreate8")) {
#if defined(__arm__) || defined(__i386__)
        return arm_com_thunk((void *)DirectSoundCreate8, 3, 3);
#else
        return (void *) (unsigned long) (unsigned int) DirectSoundCreate8;
#endif
    }
    reasm_elf_log("[dsound] GetProcAddress su dsound.dll non coperta: ");
    reasm_elf_log(name);
    reasm_elf_log("\n");
    return NULL;
}
