/* sc-reasm/tools/elf/smack_audio.c — P11: audio dei filmati SMACK.
 *
 * Cosa fa, in una riga: per ogni frame che storm PRESENTA, prende da libsmacker il chunk audio di
 * quel frame (`smk_get_audio`/`smk_get_audio_size`) e lo consegna al mixer host come voce in loop
 * su un anello (formato della traccia: nessuna conversione nostra), con l'uscita aperta dal
 * percorso di gioco (`com_dsound_out_ready`, formato della primaria).
 *
 * Perche' cosi': (a) i byte che arrivano al device sono esattamente quelli della libreria
 * (verificabile: sink di gate + hash/RMS); (b) il ricampionamento/mix e' quello gia' verificato dal
 * gate dsound_test.sh; (c) se l'uscita host non e' aperta NON si finge nulla: i byte non consegnati
 * sono contati e dichiarati (regola D19).
 *
 * NIENTE falso successo: libsmacker assente, header SMK senza tracce, Bink audio, >2 tracce,
 * formato non coperto, anello pieno, uscita host chiusa => righe di log esplicite e contatori.
 */
#include "smack_audio.h"
#include "host_elf.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long ulong;
typedef unsigned long long u64;

extern void reasm_elf_log(const char *);
extern void reasm_elf_log_hex(unsigned);
extern char *getenv(const char *);
extern void *calloc(ulong, ulong);
extern void free(void *);
extern void *memcpy(void *, const void *, ulong);
extern void *memset(void *, int, ulong);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern int dlclose(void *);
extern long readlink(const char *, char *, ulong);

/* com_dsound.c (modifica additiva P11). Debole: se il modulo non c'e' si ricade su
 * host_elf_audio_is_open() e la funzionalita' resta dichiarata, non finta. */
extern int com_dsound_out_ready(void) __attribute__((weak));

#define RTLD_NOW 2
#define RTLD_LOCAL 0

/* ---------------------------------------------------------------- libsmacker (dlopen nostro) */
typedef void *smk_t;
static void *g_lib;
static int g_tried;
static char (*p_info_all)(const smk_t, ulong *, ulong *, double *);
static char (*p_info_audio)(const smk_t, u8 *, u8 *, u8 *, ulong *);
static char (*p_enable_audio)(const smk_t, const u8, const u8);
static const u8 *(*p_get_audio)(const smk_t, const u8);
static ulong (*p_get_audio_size)(const smk_t, const u8);

static int lib_load(void)
{
    const char *so, *envso, *last_so = "(nessuno)";
    const char *candidates[4];
    char exe_path[4096], exe_so[4096];
    unsigned i, n;
    long path_len;
    if (g_lib) return 1;
    if (g_tried) return 0;
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
#define SYM(v, name) (*(void **) &(v) = dlsym(g_lib, name), (v) == 0)
    for (i = 0; i < 4; ++i) {
        so = candidates[i];
        if (!so) continue;
        last_so = so;
        g_lib = dlopen(so, RTLD_NOW | RTLD_LOCAL);
        if (!g_lib) continue;
        if (!(SYM(p_info_all, "smk_info_all") || SYM(p_info_audio, "smk_info_audio") ||
              SYM(p_enable_audio, "smk_enable_audio") || SYM(p_get_audio, "smk_get_audio") ||
              SYM(p_get_audio_size, "smk_get_audio_size"))) {
            reasm_elf_log("[smack-audio] libsmacker (audio) caricata: ");
            reasm_elf_log(so); reasm_elf_log("\n");
            return 1;
        }
        reasm_elf_log("[smack-audio] simboli audio incompleti, provo il candidato successivo: ");
        reasm_elf_log(so); reasm_elf_log("\n");
        dlclose(g_lib);
        g_lib = 0;
        p_info_all = 0; p_info_audio = 0; p_enable_audio = 0;
        p_get_audio = 0; p_get_audio_size = 0;
    }
#undef SYM
    {
        const char *err = dlerror();
        reasm_elf_log("[smack-audio] nessun libsmacker utilizzabile; ultimo candidato: ");
        reasm_elf_log(last_so); reasm_elf_log(" motivo: ");
        reasm_elf_log(err ? err : "?"); reasm_elf_log("\n");
    }
    return 0;
}

/* ---------------------------------------------------------------- strumentazione e log */
static smack_audio_stats g_stats;
static smack_audio_sink_fn g_sink;

void smack_audio_set_sink(smack_audio_sink_fn fn) { g_sink = fn; }

static void log_dec(const char *pre, ulong v, const char *post)
{
    char buf[12];
    int i = 11;
    buf[11] = 0;
    if (!v) buf[--i] = '0';
    while (v && i > 0) { buf[--i] = (char) ('0' + (int) (v % 10)); v /= 10; }
    reasm_elf_log(pre); reasm_elf_log(buf + i); if (post) reasm_elf_log(post);
}

static int active(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("SC_SMACK_AUDIO"); v = (e && e[0] == '0') ? 0 : 1; }
    return v;
}

static unsigned fnv(const u8 *p, ulong n)
{
    unsigned h = 2166136261u;
    ulong i;
    for (i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* RMS in unita' int16 (x100), interamente intero: stesso algoritmo nel gate => confronto esatto. */
static ulong isqrt64(u64 v)
{
    u64 x = v, y, b = 1ull << 30;
    ulong r = 0;
    while (b > x) b >>= 2;
    while (b) {
        y = (u64) r + b;
        if (x >= y) { x -= y; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}
static ulong rms_x100(const u8 *p, ulong n, int bits, int ch)
{
    u64 acc = 0;
    ulong i, samples;
    if (bits == 16) {
        samples = n / 2;
        for (i = 0; i < samples; ++i) { long s = *(const short *) (p + 2 * i); acc += (u64) (s * s); }
    } else {
        samples = n;
        for (i = 0; i < samples; ++i) { long s = ((long) p[i] - 128) * 256; acc += (u64) (s * s); }
    }
    (void) ch;
    if (!samples) return 0;
    return isqrt64(acc / samples) * 100ul;
}

/* ---------------------------------------------------------------- stato per oggetto SMACK */
#define SMA_STREAMS    24        /* storm tiene aperti ~18 filmati insieme (8 bottoni x2 + intro) */
#define SMA_MAXTRACK    2        /* oltre: dichiarato non coperto */
#define SMA_RING_MIN   (128ul * 1024ul)
#define SMA_RING_MAX   (1024ul * 1024ul)

typedef struct {
    int on;
    int track;
    int rate, ch, bits;
    int voice;                   /* id voce mixer host (-1 = nessuna) */
    int started;
    u8 *ring;
    ulong ring_bytes;
    ulong written;               /* cursore di scrittura monotono (byte) */
    ulong played_base;           /* host_elf_voice_played() all'ultima risincronizzazione */
    ulong bytes;                 /* byte consegnati (statistica) */
    int vol, pan;                /* volume 0..256, pan -100..100 (host) */
} sma_track;

typedef struct {
    int used;
    void *key;                  /* handle libsmacker: chiave dello stream (esiste PRIMA di smk_first) */
    void *obj;                  /* oggetto SMACK di smack_elf.c: serve a SmackVolumePan */
    int ntracks;
    int seen;
    ulong last_frame;
    sma_track t[SMA_MAXTRACK];
} sma_stream;

static sma_stream g_sma[SMA_STREAMS];

static sma_stream *slot_new(void *key)
{
    int i;
    for (i = 0; i < SMA_STREAMS; ++i) if (!g_sma[i].used) {
        memset(&g_sma[i], 0, sizeof g_sma[i]);
        g_sma[i].used = 1; g_sma[i].key = key;
        return &g_sma[i];
    }
    return 0;
}
static sma_stream *slot_find_key(void *key)
{
    int i;
    for (i = 0; i < SMA_STREAMS; ++i) if (g_sma[i].used && g_sma[i].key == key) return &g_sma[i];
    return 0;
}
static sma_stream *slot_find_obj(void *obj)
{
    int i;
    for (i = 0; i < SMA_STREAMS; ++i) if (g_sma[i].used && g_sma[i].obj == obj) return &g_sma[i];
    return 0;
}

/* ---------------------------------------------------------------- aggancio */
void smack_audio_hook_open(void *lib_smk)
{
    sma_stream *m;
    smk_t s = (smk_t) lib_smk;
    u8 mask = 0, ch[7], bd[7];
    ulong rate[7];
    int i, n;

    if (!active() || !s) return;
    if (!lib_load()) return;
    if (slot_find_key(lib_smk)) return;              /* stesso filmato: gia' agganciato */
    m = slot_new(lib_smk);
    if (!m) {
        reasm_elf_log("[smack-audio] tabella stream piena: audio di questo filmato NON coperto\n");
        return;
    }
    for (i = 0; i < 7; ++i) { ch[i] = 0; bd[i] = 0; rate[i] = 0; }
    if (p_info_audio(s, &mask, ch, bd, rate) != 0) {
        reasm_elf_log("[smack-audio] smk_info_audio errore: traccia audio NON coperta (D19)\n");
        m->used = 0;
        return;
    }
    reasm_elf_log("[smack-audio] header SMK: track_mask="); reasm_elf_log_hex((unsigned) mask); reasm_elf_log("\n");
    for (i = 0, n = 0; i < 7; ++i) {
        if (!(mask & (1u << i))) continue;
        log_dec("[smack-audio] traccia ", (ulong) i, ": ");
        log_dec("", rate[i], " Hz, canali ");
        log_dec("", (ulong) ch[i], ", bit ");
        log_dec("", (ulong) bd[i], "\n");
        if (n >= SMA_MAXTRACK) {
            ++g_stats.uncovered;
            log_dec("[smack-audio] traccia ", (ulong) i, " NON coperta: oltre le 2 per filmato (D19)\n");
            continue;
        }
        if ((bd[i] != 8 && bd[i] != 16) || (ch[i] != 1 && ch[i] != 2) || rate[i] == 0) {
            ++g_stats.uncovered;
            log_dec("[smack-audio] traccia ", (ulong) i, " NON coperta: formato non coperto (D19)\n");
            continue;
        }
        m->t[n].on = 1;
        m->t[n].track = i;
        m->t[n].rate = (int) rate[i];
        m->t[n].ch = (int) ch[i];
        m->t[n].bits = (int) bd[i];
        m->t[n].voice = -1;
        m->t[n].vol = 256;
        m->t[n].pan = 0;
        /* PRIMA di smk_first: se l'audio non e' abilitato qui, il frame 0 (con il pre-roll del
         * keyframe audio SMACK) non viene nemmeno decodificato. */
        p_enable_audio(s, (u8) i, 1);
        ++n;
        ++g_stats.tracks;
    }
    m->ntracks = n;
    ++g_stats.streams;
    if (!n)
        reasm_elf_log("[smack-audio] nessuna traccia gestibile: filmato senza audio (D19)\n");
}

static void voice_open(sma_stream *m, sma_track *t)
{
    host_audio_voice v;
    int out_ok;
    if (t->voice >= 0) return;
    out_ok = com_dsound_out_ready ? com_dsound_out_ready() : host_elf_audio_is_open();
    if (!out_ok) return;                    /* ritenta al prossimo frame; i byte restano contati */
    v.pcm = t->ring;
    v.bytes = t->ring_bytes;
    v.rate = t->rate; v.channels = t->ch; v.bits = t->bits;
    v.loop = 1;                             /* anello: il mixer riparte da 0 a fine buffer */
    v.volume = t->vol; v.pan = t->pan;
    t->voice = host_elf_voice_open(&v);
    if (t->voice < 0) {
        reasm_elf_log("[smack-audio] mixer pieno: traccia non instradata (D19)\n");
        return;
    }
    log_dec("[smack-audio] voce mixer aperta #", (ulong) t->voice, ": ");
    log_dec("", (ulong) t->rate, " Hz, anello ");
    log_dec("", t->ring_bytes, " byte, formato traccia = ");
    log_dec("", (ulong) t->ch, "ch/");
    log_dec("", (ulong) t->bits, "bit\n");
}

static void track_push(sma_stream *m, sma_track *t, const void *pcm, ulong n)
{
    ulong fb, played, freeb, off, first;

    if (!t->ring) {                                   /* primo chunk: anello dimensionato sul pre-roll */
        ulong want = n * 3ul;
        if (want < SMA_RING_MIN) want = SMA_RING_MIN;
        if (want > SMA_RING_MAX) want = SMA_RING_MAX;
        t->ring = (u8 *) calloc(1, want);
        if (!t->ring) {
            reasm_elf_log("[smack-audio] allocazione anello fallita: traccia spenta (D19)\n");
            t->on = 0;
            return;
        }
        t->ring_bytes = want;
        log_dec("[smack-audio] anello traccia ", (ulong) t->track, ": ");
        log_dec("", want, " byte (primo chunk ");
        log_dec("", n, " byte)\n");
    }
    fb = (ulong) t->ch * (ulong) (t->bits / 8);
    played = 0;
    if (t->voice >= 0) {
        ulong p = host_elf_voice_played(t->voice) * fb;   /* monotono: campioni usciti dalla voce */
        if (p - t->played_base > t->written) {
            /* UNDERRUN: il lettore ha superato lo scrittore (gioco piu' lento del tempo reale).
             * Senza correzione, dopo l'anello il mixer rigirerebbe i byte iniziali (eco).
             * Si risincronizza il cursore di lettura su quello di scrittura e si DICHIARA. */
            ++g_stats.underruns;
            if (g_stats.underruns <= 3) {
                ulong over = (p - t->played_base) - t->written;   /* silenzio oltre lo scrittore */
                log_dec("[smack-audio] underrun #", (ulong) g_stats.underruns, ": risincronizzo (campioni non pronti ");
                log_dec("", over, " byte)\n");
            }
            host_elf_voice_stop(t->voice);
            host_elf_voice_set_pos(t->voice, t->written % t->ring_bytes);
            host_elf_voice_play(t->voice);
            t->played_base = p;
        }
        played = p - t->played_base;
    }
    if (played > t->written) played = t->written;
    freeb = t->ring_bytes - (t->written - played);
    if (n > freeb) {                                   /* anello pieno: si scarta e si DICHIARA */
        ulong drop = n - freeb;
        n = freeb;
        g_stats.dropped += drop;
        if (g_stats.dropped == drop || (g_stats.dropped & 0xFFFFul) < drop) {
            log_dec("[smack-audio] anello pieno: scartati ", drop, " byte (totale ");
            log_dec("", g_stats.dropped, ")\n");
        }
    }
    if (n) {
        off = t->written % t->ring_bytes;
        first = t->ring_bytes - off;
        if (first > n) first = n;
        memcpy(t->ring + off, pcm, first);
        if (n > first) memcpy(t->ring, (const u8 *) pcm + first, n - first);
        t->written += n;
        t->bytes += n;
    }
    voice_open(m, t);
    if (t->voice < 0) {
        g_stats.unrouted += n;
        if (!g_stats.unrouted_logs || (g_stats.unrouted_logs < 3)) {
            ++g_stats.unrouted_logs;
            reasm_elf_log("[smack-audio] uscita host non aperta: byte audio NON consegnati (D19)\n");
        }
        return;
    }
    if (!t->started && t->written) {                  /* il device parte col primo dato presente */
        host_elf_voice_play(t->voice);
        t->started = 1;
        reasm_elf_log("[smack-audio] voce mixer avviata (primo chunk consegnato)\n");
    }
}

void smack_audio_hook_frame(void *smk_obj, void *lib_smk)
{
    sma_stream *m;
    smk_t sh = (smk_t) lib_smk;
    ulong cur = 0;
    int k;

    if (!active() || !sh) return;
    m = slot_find_key(lib_smk);
    if (!m) return;
    if (smk_obj) m->obj = smk_obj;                     /* SmackVolumePan riceve l'oggetto, non l'handle */
    if (p_info_all(sh, &cur, 0, 0) != 0) return;
    if (m->seen && cur == m->last_frame) return;      /* stessa frame (storm la ripete): gia' consegnata */
    m->seen = 1; m->last_frame = cur;
    for (k = 0; k < m->ntracks; ++k) {
        sma_track *t = &m->t[k];
        const u8 *p;
        ulong n;
        if (!t->on) continue;
        n = p_get_audio_size(sh, (u8) t->track);
        p = p_get_audio(sh, (u8) t->track);
        if (!n || !p) { ++g_stats.skipped; continue; }
        if (g_sink) g_sink(m->obj, t->track, cur, p, n);
        g_stats.hash_last = fnv(p, n);
        g_stats.rms_last_x100 = rms_x100(p, n, t->bits, t->ch);
        g_stats.last_frame = cur;
        ++g_stats.chunks;
        g_stats.bytes += n;
        if (g_stats.chunks == 1) {
            log_dec("[smack-audio] primo chunk: frame ", cur, ", byte ");
            log_dec("", n, ", hash ");
            reasm_elf_log_hex(g_stats.hash_last);
            log_dec(" RMS(x100) ", g_stats.rms_last_x100, " (dalla libreria, non nostra)\n");
        } else if ((g_stats.chunks % 64u) == 0) {
            log_dec("[smack-audio] chunk consegnati ", (ulong) g_stats.chunks, ", byte ");
            log_dec("", g_stats.bytes, ", scartati ");
            log_dec("", g_stats.dropped, ", frame ");
            log_dec("", cur, "\n");
        }
        track_push(m, t, p, n);
    }
}

/* Async Smacker worker snapshots PCM while it owns smk_t; the game thread later
 * delivers these immutable copies at presentation time without touching libsmacker. */
void smack_audio_hook_frame_snapshot(void *smk_obj, void *lib_smk, unsigned long cur,
                                     const unsigned char *const pcm[7], const unsigned long sizes[7])
{
    sma_stream *m;
    int k;
    if (!active() || !pcm || !sizes) return;
    m = slot_find_key(lib_smk);
    if (!m) return;
    if (smk_obj) m->obj = smk_obj;
    if (!m || (m->seen && cur == m->last_frame)) return;
    m->seen = 1; m->last_frame = cur;
    for (k = 0; k < m->ntracks; ++k) {
        sma_track *t = &m->t[k];
        const u8 *p = pcm[t->track];
        ulong n = sizes[t->track];
        if (!t->on) continue;
        if (!n || !p) { ++g_stats.skipped; continue; }
        if (g_sink) g_sink(m->obj, t->track, cur, p, n);
        g_stats.hash_last = fnv(p, n);
        g_stats.rms_last_x100 = rms_x100(p, n, t->bits, t->ch);
        g_stats.last_frame = cur;
        ++g_stats.chunks;
        g_stats.bytes += n;
        if (g_stats.chunks == 1) {
            log_dec("[smack-audio] primo chunk: frame ", cur, ", byte ");
            log_dec("", n, ", hash ");
            reasm_elf_log_hex(g_stats.hash_last);
            log_dec(" RMS(x100) ", g_stats.rms_last_x100, " (snapshot worker, non ricopiato)\n");
        }
        track_push(m, t, p, n);
    }
}

void smack_audio_hook_close(void *lib_smk)
{
    sma_stream *m;
    int k;
    if (!active() || !lib_smk) return;
    m = slot_find_key(lib_smk);
    if (!m) return;
    for (k = 0; k < m->ntracks; ++k) {
        sma_track *t = &m->t[k];
        if (t->voice >= 0) {
            ulong fb = (ulong) t->ch * (ulong) (t->bits / 8);
            ulong played = host_elf_voice_played(t->voice) * fb - t->played_base;
            if (t->written > played) {                 /* troncamento: dichiarato, non nascosto */
                log_dec("[smack-audio] fine filmato: byte in coda non ancora usciti ", t->written - played, "\n");
            }
            host_elf_voice_close(t->voice);
            t->voice = -1;
        }
        if (t->ring) { free(t->ring); t->ring = 0; }
    }
    memset(m, 0, sizeof *m);
}

/* ---------------------------------------------------------------- SmackSoundUseDirectSound */
int smack_audio_sound_use_dsound(void *idsound)
{
    if (!active()) return 0;
    ++g_stats.use_dsound_calls;
    if (g_stats.use_dsound_calls <= 3) {
        reasm_elf_log("[smack-audio] SmackSoundUseDirectSound(arg=");
        reasm_elf_log_hex((unsigned) (ulong) idsound);
        reasm_elf_log("): audio dei filmati preso in carico sul mixer host (voce), NON su un buffer\n");
        reasm_elf_log("[smack-audio]   DirectSound: l'oggetto IDirectSound del gioco non viene usato per allocare buffer\n");
    }
    return 1;   /* storm ignora il valore: il suo wrapper ritorna 1 comunque (0x1502e8bd) */
}

/* ---------------------------------------------------------------- SmackVolumePan */
int smack_audio_volume_pan(void *obj, unsigned track, unsigned volume, unsigned pan)
{
    sma_stream *m;
    sma_track *t = 0;
    int k;
    if (!active()) return 0;
    ++g_stats.volume_pan_calls;
    if (g_stats.volume_pan_calls <= 3) {
        reasm_elf_log("[smack-audio] SmackVolumePan(track="); reasm_elf_log_hex(track);
        reasm_elf_log(" volume="); reasm_elf_log_hex(volume);
        reasm_elf_log(" pan="); reasm_elf_log_hex(pan);
        reasm_elf_log(") — scala DICHIARATA: volume 0..255, pan 0..127 (64 = centro)\n");
    }
    m = slot_find_obj(obj);
    if (!m) return 0;
    for (k = 0; k < m->ntracks; ++k)
        if (m->t[k].on && (unsigned) m->t[k].track == track) { t = &m->t[k]; break; }
    if (!t) {
        log_dec("[smack-audio] VolumePan su traccia non gestita ", track, " (dichiarato)\n");
        return 0;
    }
    t->vol = (int) ((volume > 255u) ? 256 : (volume * 256u) / 255u);
    if (volume == 0) t->vol = 0;
    {   int p = (int) pan;
        if (p > 127) p = 127;
        t->pan = (p - 64) * 100 / 63;
        if (t->pan > 100) t->pan = 100;
        if (t->pan < -100) t->pan = -100;
    }
    if (t->voice >= 0) {
        host_audio_voice v;
        v.pcm = t->ring; v.bytes = t->ring_bytes;
        v.rate = t->rate; v.channels = t->ch; v.bits = t->bits;
        v.loop = 1; v.volume = t->vol; v.pan = t->pan;
        host_elf_voice_set(t->voice, &v);
    }
    return 1;
}

/* ---------------------------------------------------------------- reset (gate) */
void smack_audio_get_stats(smack_audio_stats *out)
{
    int i;
    if (!out) return;
    *out = g_stats;
    out->open_slots = 0;
    for (i = 0; i < SMA_STREAMS; ++i) if (g_sma[i].used) ++out->open_slots;
}

void smack_audio_reset(void)
{
    int i, k;
    for (i = 0; i < SMA_STREAMS; ++i) {
        if (!g_sma[i].used) continue;
        for (k = 0; k < g_sma[i].ntracks; ++k) {
            if (g_sma[i].t[k].voice >= 0) host_elf_voice_close(g_sma[i].t[k].voice);
            if (g_sma[i].t[k].ring) free(g_sma[i].t[k].ring);
        }
        memset(&g_sma[i], 0, sizeof g_sma[i]);
    }
    memset(&g_stats, 0, sizeof g_stats);
}
