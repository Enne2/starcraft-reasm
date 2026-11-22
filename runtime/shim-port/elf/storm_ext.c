/* sc-reasm/tools/elf/storm_ext.c — override mirati di API di storm (in-process) per il port ELF.
 *
 * Perche' esiste: la maggior parte di storm va eseguita COSI' COM'E' (e' il motore del gioco:
 * SMem, SFile, SComp). Alcune API pero' hanno un comportamento legato al mondo Windows o a dati
 * che nel port non esistono, e il gioco REAGISCE in modo fatale. In quei pochi casi l'unico punto
 * in cui si puo' intervenire senza toccare il codice del gioco (che deve restare byte-identico) e'
 * la risoluzione dello slot IAT: `reasm_import_lookup` in main_elf.c chiede qui, per ogni ordinale
 * di storm, se esiste un override di progetto.
 *
 * REGOLA: l'override NON sostituisce storm a occhi chiusi. Chiama PRIMA l'originale e interviene
 * SOLO se l'originale ha fallito, e solo per i casi dichiarati. Cosi' se il dato mancante c'e'
 * (es. l'utente installa i filmati), il percorso originale torna a fare il suo lavoro.
 *
 * --- CASO 1: i filmati di intermezzo (.smk) ---
 * `sub_4D4470` nel gioco apre il filmato con storm #454 e, se l'apertura fallisce, mostra il
 * dialogo fatale col nome del file (visto: "smk\blizzard.smk"). I filmati NON sono su questo disco:
 * sono verificati assenti in TUTTI gli archivi aperti dal gioco (StarDat/BrooDat/patch_rt/
 * Starcraft/BroodWar/SEXP: hash della voce assente) e non esistono come file. Erano sul CD e non
 * sono stati installati.
 * Scelta di port, dichiarata: se il filmato non c'e', si SALTA (apertura "riuscita" con handle
 * sentinella, zero fotogrammi, chiusura pulita) invece di uscire col dialogo fatale. I filmati sono
 * contenuto opzionale: il gioco prosegue identico. Con SC_ELF_MOVIES=1 il salto e' disattivato
 * (comportamento originale: dialogo fatale), utile per verificare che il resto sia invariato.
 */
#include "win32_shim.h"
#include "reasm_elf.h"

#define S __attribute__((stdcall))

extern char *getenv(const char *);

/* Additive T2 bridge; no edits to the shared runtime/build script required. */
#include "mpq_diag.c"

/* dalla mappa generata (storm_map.c) */
unsigned int reasm_storm_ordinal_va(int ord);

/* Ordinali di storm del player video (firme verificate sul codice di storm: i `ret` sono
 * 0x1c = 7 argomenti, 0xc = 3 argomenti, 0x4 = 1 argomento). */
#define ORD_VID_OPEN  454
#define ORD_VID_FRAME 457
#define ORD_VID_CLOSE 458

/* handle sentinella: mai confondibile con un oggetto di storm (che sta nell'heap di gioco) */
#define VID_SKIP_HANDLE 0x56000001u

typedef int (S *vid_open_fn)(const char *, unsigned, unsigned, unsigned, unsigned, unsigned, void **);
typedef int (S *vid_frame_fn)(void *, unsigned, void *);
typedef int (S *vid_close_fn)(void *);

static vid_open_fn  g_orig_open;
static vid_frame_fn g_orig_frame;
static vid_close_fn g_orig_close;

static int g_skip_movies = -1;         /* -1 = non ancora letto dall'ambiente */
static unsigned g_skipped;

static int skip_movies(void) {
    if (g_skip_movies < 0) {
        const char *v = getenv("SC_ELF_MOVIES");
        g_skip_movies = (v && v[0] == '1') ? 0 : 1;
        if (!g_skip_movies)
            reasm_elf_log("[video] SC_ELF_MOVIES=1: nessun salto, si usa il percorso originale di storm\n");
    }
    return g_skip_movies;
}

/* #454: apre il filmato. Si chiama SEMPRE l'originale; si interviene solo sul fallimento. */
static int S vid_open(const char *name, unsigned a2, unsigned a3, unsigned a4,
                      unsigned a5, unsigned flags, void **out) {
    int r;
    if (!g_orig_open)
        g_orig_open = (vid_open_fn) (unsigned long) reasm_storm_ordinal_va(ORD_VID_OPEN);
    r = g_orig_open(name, a2, a3, a4, a5, flags, out);
    if (out && *out == 0 && skip_movies()) {
        *out = (void *) (unsigned long) VID_SKIP_HANDLE;
        ++g_skipped;
        reasm_elf_log("[video] filmato assente, saltato: ");
        reasm_elf_log(name ? name : "?");
        reasm_elf_log("\n");
    }
    return r;
}

/* #457: fotogramma successivo. Con l'handle sentinella: nessun fotogramma (0 = fine). */
static int S vid_frame(void *h, unsigned a2, void *out) {
    if (h == (void *) (unsigned long) VID_SKIP_HANDLE) {
        if (out) *(unsigned *) out = 0;
        return 0;
    }
    if (!g_orig_frame)
        g_orig_frame = (vid_frame_fn) (unsigned long) reasm_storm_ordinal_va(ORD_VID_FRAME);
    return g_orig_frame(h, a2, out);
}

/* #458: chiude il filmato. */
static int S vid_close(void *h) {
    if (h == (void *) (unsigned long) VID_SKIP_HANDLE) return 1;
    if (!g_orig_close)
        g_orig_close = (vid_close_fn) (unsigned long) reasm_storm_ordinal_va(ORD_VID_CLOSE);
    return g_orig_close(h);
}

/* p12: logging SFile (solo REASM_TRACE) ------------------------------------------------------
 * Serve a rispondere a "quale file finisce in quale memoria" SENZA GDB. Comportamento identico:
 * si chiama SEMPRE l'originale e si ritorna il suo esito; si aggiungono solo righe di log, e solo
 * quando REASM_TRACE e' attivo. Ordinali e firme dal contratto Storm (SFile*): OpenArchive 4
 * argomenti, OpenFileEx 4, ReadFile 5. */
#define ORD_SFILE_OPEN_ARCHIVE 266
#define ORD_SFILE_OPEN_FILE_EX 268
#define ORD_SFILE_READ_FILE    269

typedef int (S *sf_open_archive_fn)(const char *, unsigned, unsigned, void **);
typedef int (S *sf_open_file_fn)(void *, const char *, unsigned, void **);
typedef int (S *sf_read_file_fn)(void *, void *, unsigned, unsigned *, void *);

static sf_open_archive_fn g_sf_archive;
static sf_open_file_fn    g_sf_open_file;
static sf_read_file_fn    g_sf_read_file;
static int g_sf_trace = -1;

static int sf_trace_on(void) {
    if (g_sf_trace < 0) { const char *v = getenv("REASM_TRACE"); g_sf_trace = (v && v[0]) ? 1 : 0; }
    return g_sf_trace;
}

static int S sf_open_archive(const char *name, unsigned prio, unsigned flags, void **out) {
    int r;
    if (!g_sf_archive) g_sf_archive = (sf_open_archive_fn) (unsigned long) reasm_storm_ordinal_va(ORD_SFILE_OPEN_ARCHIVE);
    r = g_sf_archive(name, prio, flags, out);
    if (sf_trace_on()) {
        reasm_elf_log("[sfile] OpenArchive ");
        reasm_elf_log(name ? name : "(null)");
        reasm_elf_log(r ? " -> ok arch=" : " -> FALLITO err=");
        reasm_elf_log_hex(r ? (unsigned) (ulong) (out ? *out : 0) : 0);
        reasm_elf_log("\n");
    }
    return r;
}

static int S sf_open_file(void *arch, const char *name, unsigned scope, void **out) {
    int r;
    if (!g_sf_open_file) g_sf_open_file = (sf_open_file_fn) (unsigned long) reasm_storm_ordinal_va(ORD_SFILE_OPEN_FILE_EX);
    r = g_sf_open_file(arch, name, scope, out);
    if (sf_trace_on()) {
        reasm_elf_log("[sfile] OpenFileEx ");
        reasm_elf_log(name ? name : "(null)");
        reasm_elf_log(r ? " -> ok h=" : " -> FALLITO h=");
        reasm_elf_log_hex(r ? (unsigned) (ulong) (out ? *out : 0) : 0);
        reasm_elf_log("\n");
    }
    return r;
}

static int S sf_read_file(void *h, void *buf, unsigned n, unsigned *got, void *ovl) {
    int r;
    unsigned before = got ? *got : 0;
    if (!g_sf_read_file) g_sf_read_file = (sf_read_file_fn) (unsigned long) reasm_storm_ordinal_va(ORD_SFILE_READ_FILE);
    r = g_sf_read_file(h, buf, n, got, ovl);
    if (sf_trace_on()) {
        reasm_elf_log("[sfile] ReadFile h=");
        reasm_elf_log_hex((unsigned) (ulong) h);
        reasm_elf_log(" buf=");
        reasm_elf_log_hex((unsigned) (ulong) buf);
        reasm_elf_log(" want=");
        reasm_elf_log_hex(n);
        reasm_elf_log(" got=");
        reasm_elf_log_hex(got ? *got : before);
        reasm_elf_log(ovl ? " overlap\n" : " sync\n");
    }
    return r;
}

/* Chiamata da main_elf.c: override di progetto per un ordinale di storm, o 0 (nessuno). */
void *win32_storm_override(int ord) {
    /* Deferred until Storm's named IAT slots are installed. The resolver is
     * queried again by iat_summary, before DllMain/worker startup. */
    mpq_diag_install();
    switch (ord) {
    case ORD_VID_OPEN:  return (void *) (unsigned long) vid_open;
    case ORD_VID_FRAME: return (void *) (unsigned long) vid_frame;
    case ORD_VID_CLOSE: return (void *) (unsigned long) vid_close;
    /* p12: logging SFile (solo REASM_TRACE): nessun cambio di comportamento. */
    case ORD_SFILE_OPEN_ARCHIVE: return (void *) (unsigned long) sf_open_archive;
    case ORD_SFILE_OPEN_FILE_EX: return (void *) (unsigned long) sf_open_file;
    case ORD_SFILE_READ_FILE:    return (void *) (unsigned long) sf_read_file;
    default: return 0;
    }
}

/* Quanti filmati sono stati saltati (per il riepilogo finale). */
unsigned win32_storm_skipped_movies(void) { return g_skipped; }
