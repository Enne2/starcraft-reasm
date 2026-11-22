/* tools/elf/build_guard.c — identita' di build e rifiuto dei binari obsoleti.
 *
 * Perche' esiste (incidente 2026-10-27): tre diagnosi fatte su artefatti obsoleti (binario senza
 * SMACK, binario senza il fix delle attese) e una schermata senza animazioni attribuita a una
 * regressione quando era una variabile d'ambiente. Il rimedio strutturale: il binario dichiara
 * git+timestamp+hash degli input e, se il codice e' cambiato dopo la build, RIFIUTA di partire
 * (exit 3) invece di comportarsi in modo inspiegabile.
 *
 * Deroga esplicita e rumorosa: SC_ALLOW_STALE=1.
 * Solo verifica: SC_BUILD_CHECK_ONLY=1 (esce 0 se allineato, 3 se obsoleto, senza avviare il gioco).
 *
 * L'hash deve essere IDENTICO a quello di tools/elf/build_id_gen.py:
 *   FNV-1a 32 bit, per ogni input in ordine alfabetico: percorso relativo NUL-terminato + byte del file.
 */
#include "reasm_elf.h"

extern const char sc_build_git[];
extern const char sc_build_time[];
extern const char sc_build_srcroot[];
extern const char sc_build_inputs[];
extern const unsigned int sc_build_hash;
extern const unsigned int sc_build_input_count;

extern char *getenv(const char *);
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern int close(int);
extern void exit(int);
#define O_RDONLY 0

#define FNV_OFFSET 0x811C9DC5u
#define FNV_PRIME  0x01000193u

static unsigned int g_hash;
static unsigned int g_files, g_bad;
static char g_badname[64];

static void h_bytes(const unsigned char *p, unsigned int n) {
    unsigned int i;
    for (i = 0; i < n; ++i) g_hash = (g_hash ^ p[i]) * FNV_PRIME;
}

static void h_str(const char *s) {
    while (*s) { g_hash = (g_hash ^ (unsigned char) *s) * FNV_PRIME; ++s; }
    g_hash = (g_hash ^ 0u) * FNV_PRIME;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* Percorso assoluto dell'input: srcroot + "/" + relativo (i relativi sono gia' normalizzati). */
static void join(char *dst, unsigned long cap, const char *root, const char *rel) {
    unsigned long i = 0, j = 0;
    while (root[i] && i + 1 < cap) { dst[i] = root[i]; ++i; }
    if (i && dst[i - 1] != '/' && i + 1 < cap) dst[i++] = '/';
    while (rel[j] && i + 1 < cap) dst[i++] = rel[j++];
    dst[i] = 0;
}

static void hash_one(const char *root, const char *rel) {
    char path[512];
    unsigned char buf[4096];
    int fd;
    long n;
    join(path, sizeof path, root, rel);
    ++g_files;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (!g_bad) { unsigned long i = 0; while (rel[i] && i + 1 < sizeof g_badname) { g_badname[i] = rel[i]; ++i; } g_badname[i] = 0; }
        ++g_bad;
        return;
    }
    h_str(rel);
    while ((n = read(fd, buf, sizeof buf)) > 0) h_bytes(buf, (unsigned int) n);
    close(fd);
}

static void print_line(const char *a, const char *b, const char *c) {
    reasm_elf_log(a ? a : "");
    reasm_elf_log(b ? b : "");
    reasm_elf_log(c ? c : "");
    reasm_elf_log("\n");
}

/* Tracciabilita' delle variabili che CAMBIANO il comportamento visibile: senza questa riga una
 * schermata senza animazioni (SC_SMACK=0) sembra una regressione del codice. */
static void log_flags(void) {
    const char *smack = getenv("SC_SMACK");
    const char *smack_audio = getenv("SC_SMACK_AUDIO");
    const char *novideo = getenv("SC_ELF_NO_VIDEO");
    int smack_on = !(smack && smack[0] == '0' && !smack[1]);   /* default: ON */
    reasm_elf_log("[build] flags: SC_SMACK=");
    reasm_elf_log(smack_on ? "1" : "0");
    if (!smack || !*smack) reasm_elf_log(" (default)");
    reasm_elf_log(" SC_SMACK_AUDIO=");
    reasm_elf_log((smack_audio && smack_audio[0] == '0' && !smack_audio[1]) ? "0" : "1 (default)");
    reasm_elf_log(" no-video=");
    reasm_elf_log((novideo && novideo[0] == '1' && !novideo[1]) ? "1" : "0");
    reasm_elf_log("\n");
}

/* Ritorna 0 = allineato (o non verificabile), 3 = obsoleto (il chiamante deve uscire).
 * Se check_only != 0 non stampa nulla oltre l'esito e non tocca il gioco. */
int build_guard_check(int check_only) {
    const char *rel = sc_build_inputs;
    const char *allow = getenv("SC_ALLOW_STALE");
    reasm_elf_log("[build] git=");
    reasm_elf_log(sc_build_git);
    reasm_elf_log(" time=");
    reasm_elf_log(sc_build_time);
    reasm_elf_log(" inputs=");
    { char b[16]; unsigned int v = sc_build_input_count, i = 0; char t[12]; int k = 0;
      if (!v) t[k++] = '0';
      while (v) { t[k++] = (char) ('0' + (v % 10u)); v /= 10u; }
      while (k) b[i++] = t[--k];
      b[i] = 0; reasm_elf_log(b); }
    reasm_elf_log("\n");

    g_hash = FNV_OFFSET; g_files = 0; g_bad = 0; g_badname[0] = 0;
    while (rel && *rel) { hash_one(sc_build_srcroot, rel); rel += 1; while (*rel) ++rel; ++rel; }

    if (g_bad) {
        /* Sorgenti non raggiungibili (binario copiato altrove): non si blocca cio' che non si puo'
         * verificare, ma lo si dice. */
        reasm_elf_log("[build] ATTENZIONE: sorgenti non verificabili (");
        reasm_elf_log(g_badname);
        reasm_elf_log("): eseguo senza garanzia di allineamento\n");
        return 0;
    }
    if (g_hash == sc_build_hash) {
        print_line("[build] allineata al codice (", sc_build_git, ")");
        log_flags();
        return 0;
    }
    reasm_elf_log("[build] BINARIO OBSOLETO: gli input sono cambiati dopo la build (hash ");
    { char b[16]; unsigned int v = g_hash, i = 0; char t[12]; int k = 0, n = 0;
      if (!v) t[k++] = '0';
      while (v) { t[k++] = "0123456789abcdef"[v & 15u]; v >>= 4; }
      b[i++] = '0'; b[i++] = 'x';
      while (k) b[i++] = t[--k];
      b[i] = 0; reasm_elf_log(b); }
    reasm_elf_log(" atteso ");
    { char b[16]; unsigned int v = sc_build_hash, i = 0; char t[12]; int k = 0;
      if (!v) t[k++] = '0';
      while (v) { t[k++] = "0123456789abcdef"[v & 15u]; v >>= 4; }
      b[i++] = '0'; b[i++] = 'x';
      while (k) b[i++] = t[--k];
      b[i] = 0; reasm_elf_log(b); }
    reasm_elf_log(")\n[build] ricostruire con tools/elf/build.sh (o usare tools/elf/run.sh); "
                  "per eseguire comunque: SC_ALLOW_STALE=1\n");
    if (check_only) return 3;
    if (allow && allow[0] == '1' && !allow[1]) {
        reasm_elf_log("[build] ATTENZIONE: binario obsoleto eseguito per scelta (SC_ALLOW_STALE=1)\n");
        log_flags();
        return 0;
    }
    return 3;
}
