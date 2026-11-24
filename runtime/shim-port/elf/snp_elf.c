/* P12: native, in-process standard.snp provider. No PE code loading, no Battle.net emulation.
 *
 * Identita' del provider (misurata, vedi docs/21-SNP-ELF.md): Storm NON usa il campo `code`
 * restituito da SnpQuery. In sub_1503DE90 (0x1503DE90) apre il modulo con SFileOpenArchive
 * (0x15017DD0), legge dal suo MPQ interno la voce "caps.dat" (0x15017960 = SFileOpenFileEx,
 * 0x15016360 = SFileReadFile) con sub_1503DDB0 (0x1503DDB0) e usa QUEL record come identita' del
 * provider di indice 0: a 0x1503E0FD scrive record+0x110 dal campo +4 di caps.dat.
 * Quindi il contratto "il provider dichiara le proprie capacita'" si adempie fornendo noi il
 * caps.dat del nostro modulo: id 0 per single player + UDP per la UI multiplayer. Storm #105
 * (0x1503E2AC) nasconde intenzionalmente l'id 0 nell'enumerazione delle connessioni.
 */
#include "snp_elf.h"
#include "reasm_elf.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef int i32;

extern void *memset(void *, int, unsigned long);
extern int socket(int, int, int);
extern int bind(int, const void *, unsigned int);
extern int getsockname(int, void *, unsigned int *);
extern int close(int);
extern int sendto(int, const void *, unsigned int, int, const void *, unsigned int);
extern int recvfrom(int, void *, unsigned int, int, void *, unsigned int *);
extern int memcmp(const void *, const void *, unsigned int);
extern void *malloc(unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern int *__errno_location(void);
extern void free(void *);
extern char *getenv(const char *);
extern int fcntl(int, int, ...);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern int clock_gettime(int, void *);
extern int nanosleep(const void *, void *);
extern int pthread_create(unsigned long *, const void *, void *(*)(void *), void *);
extern int pthread_detach(unsigned long);
extern int REASM_STDCALL SetEvent(void *) __attribute__((weak));

#define SNP_MODULE 0x72000001u /* standalone gate only; runtime uses the PE resource handle */
#define SNP_AF_INET 2
#define SNP_SOCK_DGRAM 2
#define SNP_MSG_DONTWAIT 0x40
#define SNP_CAPS_FLAGS 0x20000002u
#define SNP_SOL_SOCKET 1
#define SNP_SO_BROADCAST 6
#define SNP_F_GETFL 3
#define SNP_F_SETFL 4
#define SNP_O_NONBLOCK 0x800
#define SNP_MAX_DATAGRAM 65507u

struct SnpCaps {
    u32 size;
    u32 flags;
    u32 max_message_size;
    u32 max_queue_size;
    u32 max_players;
    u32 bytes_per_second;
    u32 latency;
    u32 turns_per_second;
    u32 turns_in_transit;
};

struct SnpNetAddress { unsigned char bytes[16]; };
struct SnpGame;

struct SnpNetFunctions {
    u32 dwSize;
    int (REASM_STDCALL *spiCompareNetAddresses)(struct SnpNetAddress *, struct SnpNetAddress *, u32 *);
    int (REASM_STDCALL *spiDestroy)(void);
    int (REASM_STDCALL *spiFree)(struct SnpNetAddress *, char *, u32);
    int (REASM_STDCALL *spiFreeExternalMessage)(struct SnpNetAddress *, char *, u32);
    int (REASM_STDCALL *spiGetGameInfo)(u32, void *, void *, void *);
    int (REASM_STDCALL *spiGetPerformanceData)(u32, u32 *, void *, char *);
    int (REASM_STDCALL *spiInitialize)(void *, void *, void *, void *, u32);
    int (REASM_STDCALL *spiInitializeDevice)(void *, void *, void *, void *, void *);
    int (REASM_STDCALL *spiLockDeviceList)(void **);
    int (REASM_STDCALL *spiLockGameList)(int, int, struct SnpGame **);
    int (REASM_STDCALL *spiReceive)(struct SnpNetAddress **, char **, u32 *);
    int (REASM_STDCALL *spiReceiveExternalMessage)(struct SnpNetAddress **, char **, u32 *);
    int (REASM_STDCALL *spiSelectGame)(void);
    int (REASM_STDCALL *spiSend)(u32, struct SnpNetAddress **, char *, u32);
    int (REASM_STDCALL *spiSendExternalMessage)(void *, void *, void *, void *, void *);
    int (REASM_STDCALL *spiStartAdvertisingLadderGame)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
    int (REASM_STDCALL *spiStopAdvertisingGame)(void);
    int (REASM_STDCALL *spiUnlockDeviceList)(void *);
    int (REASM_STDCALL *spiUnlockGameList)(struct SnpGame *, u32 *);
    int (REASM_STDCALL *spiStartAdvertisingGame)(const char *, u32, const char *, u32);
    int (REASM_STDCALL *spiReportGameResult)(void);
    void (REASM_STDCALL *spiCheckDataFile)(void *, u32, u32, u32 *);
    int (REASM_STDCALL *spiLeagueCommand)(void *, u32);
    int (REASM_STDCALL *spiLeagueSendReplayPath)(void *, void *, u32);
    int (REASM_STDCALL *spiLeagueGetReplayPath)(void *);
    int (REASM_STDCALL *spiLeagueLogout)(u32);
    int (REASM_STDCALL *spiLeagueGetName)(char *, u32);
};

struct SnpSockaddrIn {
    u16 family;
    u16 port;
    u32 address;
    unsigned char zero[8];
};

static const char g_provider_name[] = "Native POSIX Loopback";
static const char g_provider_description[] = "UDP transport with optional configured IPv4 peers; no remote game advertisement is synthesized.";
/* Nome/descrizione UDP estratti da caps.dat del modulo originale; il trasporto nativo resta
 * locale (nessun protocollo LAN remoto implementato). Le CAPS descrivono il nostro backend. */
static const struct SnpProfile {
    u32 code;
    const char *name, *description;
} g_profiles[] = {
    { 0u, g_provider_name, g_provider_description },
    { 0x5544504eu, "Local Area Network (UDP)",
      "IPv4 UDP transport; peers are configured with SC_LAN_PEERS." }
};
#define SNP_PROFILE_COUNT (sizeof g_profiles / sizeof g_profiles[0])
static struct SnpCaps g_caps = { 0x24, SNP_CAPS_FLAGS, 500, 16, 8, 1000, 50, 8, 2 };
static int g_socket_fd = -1;
static u32 g_module_handle;
static u32 g_net_poll_ms = 1;
static int g_poll_socket_fd = -1;
static int g_last_poll_valid;
static long long g_last_poll_ns;
static volatile int g_poll_thread_running;
static volatile int g_poll_thread_stop;
static volatile int g_event_packet_pending;
static unsigned long g_poll_thread;
static u32 g_receive_event;
#define SNP_MAX_PEERS 32u
static struct SnpSockaddrIn g_peers[SNP_MAX_PEERS];
static u32 g_peer_count;

/* ---- Discovery UDP e GameList ------------------------------------------------------------
 * Contratti misurati in IDA su standard.snp (base 0x1D000000) e storm.orig.dll (0x15000000);
 * vedi reports/ANALYSIS-snp-contracts.md. Sintesi con gli indirizzi che giustificano i campi:
 *  - due socket: discovery 6111, gioco 6112 (snp:1D009D39-1D009E6D);
 *  - datagramma +0x00 checksum (Fletcher mod 255 all'indietro, snp:1D00F060/1D008BD0),
 *    +0x02 u16 lunghezza, +0x04 u16 tipo (0 annuncio/risposta, 1 ritiro, 2 probe 20 byte),
 *    +0x06 u16 zero, +0x08 u32 product, +0x0C u32 versione, +0x10 u32 flags,
 *    +0x14 (tipi 0/1) name\0 statstring\0 blob (snp:1D008F14-1D008F8F);
 *  - probe in broadcast a 255.255.255.255:6111, risposta unicast col tipo 0 conservato
 *    (snp:1D009451, snp:1D008780-1D0087C1);
 *  - TTL voce 10000 ms dal tick di ricezione (snp:1D00904A push 2710h), chiave = sockaddr_in
 *    con porta normalizzata a 6112 (snp:1D0089B6);
 *  - nodo SnpGame = 0x13C byte, offset provati sotto (snp:1D008B7F push 13Ch, storm:15038B06). */
#define SNP_DISC_PORT_DEFAULT 6111u
#define SNP_GAME_PORT_DEFAULT 6112u
#define SNP_WIRE_HEADER 0x14u          /* header fino a +0x13 (name inizia a +0x14) */
#define SNP_WIRE_PROBE_LEN 20u
#define SNP_WIRE_SND_MAX 0x214u        /* buffer annuncio del provider (snp:1D008EAB push 214h) */
#define SNP_NODE_BYTES 0x13Cu          /* dimensione SnpGame nativa */
#define SNP_NODE_NAME 0x28u            /* name[128] inline */
#define SNP_NODE_STAT 0xA8u            /* statstring[128] inline */
#define SNP_NODE_TTL_MS 10000u         /* snp:1D00904A push 2710h */
#define SNP_PROBE_MIN_GAP_MS 400u      /* snp:1D009BC4 cmp ecx,190h */
#define SNP_UNLOCK_POLL_MS 500u        /* snp:1D009BAE mov dword ptr [eax],1F4h */
#define SNP_WIRE_TYPE_ANNOUNCE 0u
#define SNP_WIRE_TYPE_WITHDRAW 1u
#define SNP_WIRE_TYPE_PROBE 2u

/* Campi allineati agli offset provati; gli altri restano zero come li lascia il provider
 * originale quando non ha il dato (snp:1D008ABD rep stosd). */
struct SnpGameNode {
    u32 id;                       /* +0x000 */
    u32 flags;                    /* +0x004 */
    u32 field08;                  /* +0x008 */
    unsigned char host[16];       /* +0x00C host_address inline */
    u32 field1c;                  /* +0x01C */
    u32 tick;                     /* +0x020 tick di ricezione (TTL) */
    u32 elapsed;                  /* +0x024 esposto a Storm, zero in UDP */
    char name[128];               /* +0x028 */
    char stat[128];               /* +0x0A8 */
    struct SnpGameNode *next;     /* +0x128 */
    unsigned char *blob;          /* +0x12C */
    u32 blob_len;                 /* +0x130 */
    u32 product;                  /* +0x134 */
    u32 version;                  /* +0x138 */
};
/* Il compilatore deve vedere esattamente gli offset del binario originale. */
typedef char snp_node_layout_check[
    (sizeof(struct SnpGameNode) == SNP_NODE_BYTES &&
     __builtin_offsetof(struct SnpGameNode, host) == 0x0C &&
     __builtin_offsetof(struct SnpGameNode, tick) == 0x20 &&
     __builtin_offsetof(struct SnpGameNode, name) == SNP_NODE_NAME &&
     __builtin_offsetof(struct SnpGameNode, stat) == SNP_NODE_STAT &&
     __builtin_offsetof(struct SnpGameNode, next) == 0x128 &&
     __builtin_offsetof(struct SnpGameNode, blob) == 0x12C &&
     __builtin_offsetof(struct SnpGameNode, product) == 0x134 &&
     __builtin_offsetof(struct SnpGameNode, version) == 0x138) ? 1 : -1];

static int g_disc_fd = -1;
static struct SnpGameNode *g_nodes;
static u32 g_node_seq;
static unsigned char g_adv[SNP_WIRE_SND_MAX];
static u32 g_adv_len;
static int g_adv_active;
static u32 g_product = 0x53544152u;   /* 'STAR' (StarCraft.exe 0x004727A2) */
static u32 g_version = 0xD3u;        /* 0xD3 (StarCraft.exe 0x004727BF) */
static u32 g_last_probe_ms;

/* Tempo monotono in ms, usato per TTL e cadenza del probe. */
static u32 snp_now_ms(void) {
    struct { long sec, nsec; } ts;
    if (clock_gettime(1 /* CLOCK_MONOTONIC */, &ts) != 0) return 0;
    return (u32)((unsigned long long)ts.sec * 1000ULL + (unsigned long long)(ts.nsec / 1000000));
}

extern unsigned int win32_ui_load_library(const char *) __attribute__((weak));
extern void *arm_bridge_lookup_thunk(const char *) __attribute__((weak));

static void snp_log(const char *text) { reasm_elf_log(text); }

static u16 net_port(u16 host) { return (u16)((host << 8) | (host >> 8)); }

static int parse_ipv4(const char *s, u32 *out) {
    u32 octets[4], i;
    if (!s || !*s) return 0;
    for (i = 0; i < 4; ++i) {
        u32 n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (u32)(*s++ - '0'); if (n > 255) return 0; ++digits; }
        if (!digits) return 0;
        octets[i] = n;
        if (i != 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    *out = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    return 1;
}

static u32 parse_port(const char *s) {
    u32 n = 0;
    if (!s || !*s) return 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (u32)(*s++ - '0'); if (n > 65535) return 0; }
    return *s ? 0 : n;
}

static void peers_clear(void) { g_peer_count = 0; }
static int snp_trace_on(void);
static int peers_parse(void) {
    const char *s = getenv("SC_LAN_PEERS");
    peers_clear();
    if (!s || !*s) return 1;
    while (*s) {
        char ip[16]; u32 n = 0, port; struct SnpSockaddrIn peer;
        while (*s && *s != ':' && *s != ',') {
            if (n + 1 >= sizeof ip) { peers_clear(); return 0; }
            ip[n++] = *s++;
        }
        ip[n] = 0;
        if (*s++ != ':' || !parse_ipv4(ip, &peer.address)) { peers_clear(); return 0; }
        { u32 digits = 0; port = 0;
          while (*s >= '0' && *s <= '9') { port = port * 10u + (u32)(*s++ - '0');
            if (port > 65535u) { peers_clear(); return 0; } ++digits; }
          if (!digits || !port || (*s && *s != ',')) { peers_clear(); return 0; }
        }
        if (g_peer_count >= SNP_MAX_PEERS) { peers_clear(); return 0; }
        peer.family = SNP_AF_INET; peer.port = net_port((u16)port);
        memset(peer.zero, 0, sizeof peer.zero);
        g_peers[g_peer_count++] = peer;
        if (*s == ',') { ++s; if (!*s) { peers_clear(); return 0; } }
    }
    return 1;
}

u32 snp_peer_count(void) { return g_peer_count; }
u32 snp_peers_foreach(int (*callback)(const unsigned char address[16], void *context), void *context) {
    u32 i, visited = 0;
    if (!callback) return 0;
    for (i = 0; i < g_peer_count; ++i) {
        if (!callback((const unsigned char *)&g_peers[i], context)) break;
        ++visited;
    }
    return visited;
}

static u32 net_poll_interval_ms(void) {
    const char *s = getenv("SC_NET_POLL_MS");
    u32 n = 0;
    if (!s || !*s) return 1;
    while (*s >= '0' && *s <= '9') {
        u32 digit = (u32)(*s++ - '0');
        if (n > (0xffffffffu - digit) / 10u) return 1;
        n = n * 10u + digit;
    }
    return *s ? 1 : n;
}

static int open_loopback_socket(void) {
    struct SnpSockaddrIn local;
    const char *bind_ip = getenv("SC_LAN_BIND"), *port_env = getenv("SC_LAN_PORT");
    /* Porta di gioco 6112 come il provider originale (snp:1D009E17): i peer memorizzati nella
     * lista hanno la porta normalizzata a 6112, quindi un socket effimero non riceverebbe il join.
     * SC_LAN_PORT la sostituisce esplicitamente (utile per due istanze sullo stesso IP). */
    u32 port = port_env ? parse_port(port_env) : SNP_GAME_PORT_DEFAULT;
    int one = 1, fd;
    if (port_env && !port && !(port_env[0] == '0' && port_env[1] == 0)) { snp_log("[snp] SC_LAN_PORT invalida\\n"); return -1; }
    fd = socket(SNP_AF_INET, SNP_SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    local.family = SNP_AF_INET; local.port = net_port((u16)port);
    local.address = 0x0100007fu;
    if (bind_ip && !parse_ipv4(bind_ip, &local.address)) { close(fd); return -1; }
    memset(local.zero, 0, sizeof local.zero);
    (void)setsockopt(fd, SNP_SOL_SOCKET, SNP_SO_BROADCAST, &one, sizeof one);
    if (bind(fd, &local, sizeof local) != 0) { close(fd); return -1; }
    { int flags = fcntl(fd, SNP_F_GETFL, 0); if (flags < 0 || fcntl(fd, SNP_F_SETFL, flags | SNP_O_NONBLOCK) < 0) { close(fd); return -1; } }
    g_net_poll_ms = net_poll_interval_ms();
    g_poll_socket_fd = fd;
    g_last_poll_valid = 0;
    return fd;
}

static u32 snp_strlen(const char *s) { u32 n = 0; while (s && s[n]) ++n; return n; }

/* ---- wire format, discovery e lista partite --------------------------------------------- */
static void put16(unsigned char *p, u32 v) { p[0] = (unsigned char)(v & 0xFFu); p[1] = (unsigned char)((v >> 8) & 0xFFu); }
static void put32(unsigned char *p, u32 v) { p[0] = (unsigned char)(v & 0xFFu); p[1] = (unsigned char)((v >> 8) & 0xFFu);
                                              p[2] = (unsigned char)((v >> 16) & 0xFFu); p[3] = (unsigned char)((v >> 24) & 0xFFu); }
static u32 get16(const unsigned char *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 get32(const unsigned char *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* Somma all'indietro modulo 255 (standard.snp 0x1D00F060): alto = s1, basso = s2 % 255. */
static u16 snp_wire_sum(const unsigned char *base, u32 len) {
    u32 s1 = 0, s2 = 0;
    const unsigned char *p;
    if (!len) return 0;
    for (p = base + len - 1; len; --len, --p) {
        s1 += *p;
        if (s1 >= 0xFFu) s1 -= 255u;
        s2 += s1;
    }
    return (u16)(((s1 & 0xFFu) << 8) | (s2 % 255u));
}
/* Sigillo dei due byte a +0x00 sui byte [2..len) (standard.snp 0x1D008BD0). Il mittente
 * originale scrive (hi = ~(s2%255), lo = ~hi), che NON rende nulla la somma sull'intero
 * datagramma (verificato numericamente: 2 casi su 168 la soddisfano). Il controllo del
 * ricevitore originale (`test ax,ax` dopo 0x1D00F060) non e' quindi riproducibile in modo
 * fedele: qui si valida ricalcolando il sigillo e confrontandolo col campo, che e' esattamente
 * cio' che scrive un mittente originale. */
static u16 snp_wire_seal(const unsigned char *base, u32 len) {
    u32 v1, hi, lo;
    if (len < 2) return 0;
    v1 = (u32)(snp_wire_sum(base + 2, len - 2) & 0xFFu);
    hi = (~v1) & 0xFFu;
    lo = (~hi) & 0xFFu;
    return (u16)((hi << 8) | lo);
}
static void snp_wire_stamp(unsigned char *base, u32 len) {
    u16 seal = snp_wire_seal(base, len);
    base[0] = (unsigned char)(seal & 0xFFu);
    base[1] = (unsigned char)((seal >> 8) & 0xFFu);
}

static void addr_set(struct SnpSockaddrIn *a, u32 ip_host_order, u16 port_host_order) {
    memset(a, 0, sizeof *a);
    a->family = SNP_AF_INET;
    a->port = net_port(port_host_order);
    a->address = ip_host_order;      /* gia' in ordine di rete, come parse_ipv4 */
}

static int disc_socket(void) { return g_disc_fd; }
static void disc_poll(void);

static void disc_send_to(const struct SnpSockaddrIn *to, const unsigned char *buf, u32 len) {
    int rc;
    static int sent_logged, err_logged;
    if (g_disc_fd < 0 || !to) return;
    rc = sendto(g_disc_fd, buf, len, 0, to, sizeof *to);
    if (snp_trace_on()) {
        if (rc < 0 && err_logged < 3) {
            ++err_logged;
            snp_log("[snp-trace] discovery sendto errno="); reasm_elf_log_hex((u32)(unsigned)*__errno_location());
            snp_log(" dst="); reasm_elf_log_hex(get32((const unsigned char *)to + 4));
            snp_log(":"); reasm_elf_log_hex(net_port(to->port)); snp_log("\n");
        } else if (rc >= 0 && sent_logged < 8) {
            ++sent_logged;
            snp_log("[snp-trace] discovery sendto ok dst="); reasm_elf_log_hex(get32((const unsigned char *)to + 4));
            snp_log(":"); reasm_elf_log_hex(net_port(to->port)); snp_log(" len="); reasm_elf_log_hex(len); snp_log("\n");
        }
    }
}

/* Broadcast di discovery: 255.255.255.255:6111 piu' i peer configurati (porta discovery). */
static void disc_broadcast(const unsigned char *buf, u32 len) {
    struct SnpSockaddrIn to;
    u32 i;
    addr_set(&to, 0xFFFFFFFFu, (u16)SNP_DISC_PORT_DEFAULT);
    disc_send_to(&to, buf, len);
    /* Broadcast limitato all'interfaccia di loopback (127.255.255.255): il broadcast globale non
     * esce su lo e due istanze sullo stesso host con IP 127.0.0.x non si vedrebbero. Nessun
     * effetto sulle reti reali (indirizzo di loopback). */
    addr_set(&to, 0xFFFFFF7Fu, (u16)SNP_DISC_PORT_DEFAULT);
    disc_send_to(&to, buf, len);
    for (i = 0; i < g_peer_count; ++i) {
        to = g_peers[i];
        to.port = net_port((u16)SNP_DISC_PORT_DEFAULT);
        disc_send_to(&to, buf, len);
    }
    if (snp_trace_on()) {
        snp_log("[snp-trace] discovery tx len="); reasm_elf_log_hex(len);
        snp_log(" peers="); reasm_elf_log_hex(g_peer_count); snp_log("\n");
    }
}

static void node_free(struct SnpGameNode *n) {
    if (!n) return;
    free(n->blob);
    free(n);
}
static void nodes_clear(void) {
    struct SnpGameNode *n = g_nodes;
    while (n) { struct SnpGameNode *next = n->next; node_free(n); n = next; }
    g_nodes = 0;
}
static void nodes_purge(u32 now) {
    struct SnpGameNode **link = &g_nodes;
    while (*link) {
        struct SnpGameNode *n = *link;
        if (now - n->tick > SNP_NODE_TTL_MS) {   /* snp:1D008719-1D008725, confronto unsigned */
            *link = n->next;
            if (snp_trace_on()) { snp_log("[snp-trace] purge game: "); snp_log(n->name); snp_log("\n"); }
            node_free(n);
            continue;
        }
        link = &n->next;
    }
}
static int node_key_equal(const unsigned char *a, const struct SnpSockaddrIn *b) {
    return memcmp(a, b, 12) == 0;    /* family+porta+indirizzo; padding non significativo */
}
static struct SnpGameNode *node_find(const struct SnpSockaddrIn *host) {
    struct SnpGameNode *n;
    for (n = g_nodes; n; n = n->next) if (node_key_equal(n->host, host)) return n;
    return 0;
}
static void node_remove(const struct SnpSockaddrIn *host) {
    struct SnpGameNode **link = &g_nodes;
    while (*link) {
        if (node_key_equal((*link)->host, host)) {
            struct SnpGameNode *dead = *link;
            *link = dead->next;
            if (snp_trace_on()) { snp_log("[snp-trace] withdraw game: "); snp_log(dead->name); snp_log("\n"); }
            node_free(dead);
            return;
        }
        link = &(*link)->next;
    }
}
/* Inserimento/aggiornamento di una voce dalla forma wire (snp:1D008B7F-1D008B9E). */
static void node_upsert(const struct SnpSockaddrIn *host, const unsigned char *wire, u32 len, u32 now) {
    struct SnpGameNode *n = node_find(host);
    const unsigned char *name, *stat, *blob;
    u32 blob_len = 0;
    if (len < SNP_WIRE_HEADER) return;
    name = wire + SNP_WIRE_HEADER;
    stat = name;
    while ((u32)(stat - wire) < len && *stat) ++stat;
    if ((u32)(stat - wire) >= len) return;
    ++stat;
    blob = stat;
    while ((u32)(blob - wire) < len && *blob) ++blob;
    if ((u32)(blob - wire) >= len) return;
    ++blob;
    if ((u32)(blob - wire) <= len) blob_len = len - (u32)(blob - wire);
    if (!n) {
        n = (struct SnpGameNode *)malloc(sizeof *n);
        if (!n) return;
        memset(n, 0, sizeof *n);
        n->id = ++g_node_seq ? g_node_seq : ++g_node_seq;
        n->next = g_nodes;
        g_nodes = n;
    } else {
        free(n->blob); n->blob = 0; n->blob_len = 0;
    }
    memset(n->host, 0, sizeof n->host);
    memcpy(n->host, host, 12);
    n->flags = get32(wire + 0x10);
    n->tick = now;
    n->product = get32(wire + 0x08);
    n->version = get32(wire + 0x0C);
    { u32 i; for (i = 0; i + 1 < sizeof n->name; ++i) { char c = (char)name[i]; n->name[i] = c; if (!c) break; } n->name[sizeof n->name - 1] = 0; }
    { u32 i; for (i = 0; i + 1 < sizeof n->stat; ++i) { char c = (char)stat[i]; n->stat[i] = c; if (!c) break; } n->stat[sizeof n->stat - 1] = 0; }
    if (blob_len) {
        n->blob = (unsigned char *)malloc(blob_len);
        if (n->blob) { memcpy(n->blob, blob, blob_len); n->blob_len = blob_len; }
    }
    if (snp_trace_on()) {
        snp_log("[snp-trace] game list +: "); snp_log(n->name);
        snp_log(" stat="); snp_log(n->stat); reasm_elf_log_hex(n->blob_len); snp_log("\n");
    }
}

/* Costruzione dell'annuncio (tipo 0): header + name\0 statstring\0 blob. */
static u32 wire_build_announce(unsigned char *buf, u32 cap, const char *name, const char *stat,
                               u32 flags, const unsigned char *blob, u32 blob_len) {
    u32 nlen, slen, len;
    if (!buf) return 0;
    nlen = snp_strlen(name);
    slen = snp_strlen(stat);
    if (nlen > 127u) nlen = 127u;
    if (slen > 127u) slen = 127u;
    if (blob_len > cap) return 0;
    len = SNP_WIRE_HEADER + nlen + 1u + slen + 1u + blob_len;
    if (len > cap) return 0;
    memset(buf, 0, SNP_WIRE_HEADER);
    put16(buf + 0x00, 0);
    put16(buf + 0x02, len);
    put16(buf + 0x04, SNP_WIRE_TYPE_ANNOUNCE);
    put16(buf + 0x06, 0);
    put32(buf + 0x08, g_product);
    put32(buf + 0x0C, g_version);
    put32(buf + 0x10, flags);
    if (nlen) memcpy(buf + SNP_WIRE_HEADER, name, nlen);
    buf[SNP_WIRE_HEADER + nlen] = 0;
    if (slen) memcpy(buf + SNP_WIRE_HEADER + nlen + 1u, stat, slen);
    buf[SNP_WIRE_HEADER + nlen + 1u + slen] = 0;
    if (blob_len) memcpy(buf + SNP_WIRE_HEADER + nlen + 1u + slen + 1u, blob, blob_len);
    snp_wire_stamp(buf, len);
    return len;
}

static u32 wire_build_probe(unsigned char *buf, u32 cap) {
    if (!buf || cap < SNP_WIRE_PROBE_LEN) return 0;
    memset(buf, 0, SNP_WIRE_PROBE_LEN);
    put16(buf + 0x02, SNP_WIRE_PROBE_LEN);
    put16(buf + 0x04, SNP_WIRE_TYPE_PROBE);
    put32(buf + 0x08, g_product);
    put32(buf + 0x0C, g_version);
    snp_wire_stamp(buf, SNP_WIRE_PROBE_LEN);
    return SNP_WIRE_PROBE_LEN;
}

static void snp_send_probe(void) {
    unsigned char probe[SNP_WIRE_PROBE_LEN];
    u32 len = wire_build_probe(probe, sizeof probe);
    if (!len) return;
    g_last_probe_ms = snp_now_ms();
    disc_broadcast(probe, len);
}

/* Ricezione discovery: tipo 0 annuncio, 1 ritiro, 2 probe (risposta unicast). */
static void disc_handle(const unsigned char *buf, u32 len, const struct SnpSockaddrIn *from) {
    u32 type;
    struct SnpSockaddrIn key;
    u32 now = snp_now_ms();
    if (len < SNP_WIRE_PROBE_LEN) return;
    if (get16(buf + 0x02) != len) return;
    if (get16(buf) != snp_wire_seal(buf, len)) {
        if (snp_trace_on()) snp_log("[snp-trace] discovery scartato: checksum\n");
        return;
    }
    type = get16(buf + 0x04);
    if (type == SNP_WIRE_TYPE_PROBE) {
        if (g_adv_active && g_adv_len) disc_send_to(from, g_adv, g_adv_len);
        return;
    }
    if (type != SNP_WIRE_TYPE_ANNOUNCE && type != SNP_WIRE_TYPE_WITHDRAW) return;
    /* Chiave = sockaddr_in con porta normalizzata a 6112 (snp:1D0089B6). */
    key = *from;
    key.port = net_port((u16)SNP_GAME_PORT_DEFAULT);
    if (type == SNP_WIRE_TYPE_WITHDRAW) node_remove(&key);
    else node_upsert(&key, buf, len, now);
}

static void disc_poll(void) {
    unsigned char buf[SNP_WIRE_SND_MAX];
    struct SnpSockaddrIn from;
    u32 guard = 0;
    static int rx_logged, err_logged;
    while (g_disc_fd >= 0 && guard++ < 64u) {
        unsigned int from_bytes = sizeof from;
        int n = recvfrom(g_disc_fd, buf, sizeof buf, SNP_MSG_DONTWAIT, &from, &from_bytes);
        if (n <= 0 || from_bytes != sizeof from) {
            if (n < 0 && snp_trace_on() && err_logged < 3) {
                ++err_logged;
                snp_log("[snp-trace] discovery recv errno="); reasm_elf_log_hex((u32)(unsigned)*__errno_location());
                snp_log("\n");
            }
            break;
        }
        if (snp_trace_on() && rx_logged < 12) {
            ++rx_logged;
            snp_log("[snp-trace] discovery rx len="); reasm_elf_log_hex((u32)n);
            snp_log(" type="); reasm_elf_log_hex(get16(buf + 0x04));
            snp_log(" declared="); reasm_elf_log_hex(get16(buf + 0x02));
            snp_log(" seal="); reasm_elf_log_hex(get16(buf));
            snp_log(" calc="); reasm_elf_log_hex((u32)snp_wire_seal(buf, (u32)n));
            snp_log("\n");
        }
        disc_handle(buf, (u32)n, &from);
    }
    nodes_purge(snp_now_ms());
}

static int open_discovery_socket(void) {
    struct SnpSockaddrIn local;
    const char *bind_ip = getenv("SC_LAN_BIND");
    const char *port_env = getenv("SC_DISC_PORT");
    u32 port = port_env ? parse_port(port_env) : SNP_DISC_PORT_DEFAULT;
    int one = 1, fd;
    if (port_env && !port) { snp_log("[snp] SC_DISC_PORT invalida\n"); return -1; }
    fd = socket(SNP_AF_INET, SNP_SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    addr_set(&local, 0x0100007fu, (u16)port);
    if (bind_ip && !parse_ipv4(bind_ip, &local.address)) { close(fd); return -1; }
    (void)setsockopt(fd, SNP_SOL_SOCKET, SNP_SO_BROADCAST, &one, sizeof one);
    if (bind(fd, &local, sizeof local) != 0) { close(fd); return -1; }
    { int flags = fcntl(fd, SNP_F_GETFL, 0);
      if (flags < 0 || fcntl(fd, SNP_F_SETFL, flags | SNP_O_NONBLOCK) < 0) { close(fd); return -1; } }
    return fd;
}

int REASM_STDCALL SnpQuery(u32 index, u32 *code, char **name, char **description, struct SnpCaps **caps) {
    if (index >= SNP_PROFILE_COUNT) {
        snp_log("[snp] SnpQuery index>=2 -> FALSE (end enumeration; outputs untouched)\n");
        return 0;
    }
    if (!code || !name || !description || !caps) {
        snp_log("[snp] SnpQuery -> FALSE (invalid output pointer)\n");
        return 0;
    }
    /* Indice e identita' devono coincidere con caps.dat: id 0 resta disponibile per #117,
     * UDP ha un id non-zero, quindi passa il filtro di enumerazione #105 a 0x1503E2AC. */
    *code = g_profiles[index].code;
    *name = (char *) g_profiles[index].name;
    *description = (char *) g_profiles[index].description;
    *caps = &g_caps;
    snp_log(index == 0 ? "[snp] SnpQuery index=0 -> Native POSIX Loopback\n" :
                        "[snp] SnpQuery index=1 -> Local Area Network (UDP), configured IPv4 peer transport\n");
    return 1;
}

/* --- gioco locale (single player) ---------------------------------------------------------------
 * Il single player crea comunque un gioco SNet: after SnpBind, Storm #138 (0x1503AE50
 * SNetCreateGame) chiama lo slot +0x40 (start advertising) a 0x15037BF8 e testa il valore di
 * ritorno a 0x1503B0A2. Un provider che rifiuta l'advertising fa fallire la creazione del gioco.
 * Noi NON inventiamo una rete: registriamo il gioco in una struttura nostra (loopback locale), che
 * e' esattamente cio' che il provider possiede. Nessun peer remoto e' simulato. */
struct SnpLocalGame {
    u32 used, flags;
    char name[64];
};
static struct SnpLocalGame g_game;

int REASM_STDCALL snp_stop_advertising(void) {
    /* Slot +0x44, 0 argomenti (storm:1503AAE8). Invia il ritiro (tipo 1) con la STESSA lunghezza
     * dell'annuncio e ricalcola il sigillo (standard.snp 0x1D008DAC/1D008E11). */
    if (g_adv_active && g_adv_len) {
        put16(g_adv + 0x04, SNP_WIRE_TYPE_WITHDRAW);
        snp_wire_stamp(g_adv, g_adv_len);
        disc_broadcast(g_adv, g_adv_len);
        if (snp_trace_on()) { snp_log("[snp-trace] withdraw tx len="); reasm_elf_log_hex(g_adv_len); snp_log("\n"); }
    }
    g_adv_active = 0; g_adv_len = 0;
    g_game.used = 0;
    snp_log("[snp] spiStopAdvertisingGame -> ritiro inviato, annuncio liberato\n");
    return 1;
}
int REASM_STDCALL snp_compare_addresses(struct SnpNetAddress *a, struct SnpNetAddress *b, u32 *differences) {
    u32 i, count = 0;
    if (!a || !b || !differences) {
        snp_log("[snp] spiCompareNetAddresses -> FALSE (invalid pointer)\n");
        return 0;
    }
    for (i = 0; i < 16; ++i) if (a->bytes[i] != b->bytes[i]) ++count;
    *differences = count;
    /* Semantica: "TRUE = stesso indirizzo". Prima si restituiva sempre TRUE, quindi Storm non
     * poteva distinguere il proprio peer dagli altri mittenti e i pacchetti SNet di turno
     * venivano attribuiti al peer sbagliato (sintomo: "Waiting for players" fermo a 0:00). */
    if (snp_trace_on()) {
        static int cmp_logged;
        if (cmp_logged < 12) {
            ++cmp_logged;
            snp_log("[snp-trace] compare diff="); reasm_elf_log_hex(count); snp_log("\n");
        }
    }
    return count == 0;
}

static int snp_str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

/* Slot +0x14, 4 argomenti (`ret 0x10`; storm:1503CC83, snp:1D008420):
 * (game_id, name, arg3, out316). Il provider originale azzera il buffer, seleziona per id e/o
 * nome (confronto case-insensitive, snp:1D00847D) e copia il record da 0x13C byte nel buffer del
 * chiamante senza allocare (snp:1D00849D mov ecx,4Fh / rep movsd). arg3 non viene letto. */
int REASM_STDCALL snp_get_game_info(u32 game_id, const char *name, void *arg3, void *out_info) {
    struct SnpGameNode *n, *match = 0;
    (void) arg3;
    disc_poll();
    if (!out_info) {
        snp_log("[snp] spiGetGameInfo -> FALSE (buffer di uscita nullo)\n");
        return 0;
    }
    for (n = g_nodes; n; n = n->next) {
        int by_name = name && name[0] ? snp_str_ieq(n->name, name) : 0;
        int by_id = (!name || !name[0]) && game_id ? (n->id == game_id) : 0;
        if (by_name || by_id) { match = n; break; }
    }
    if (!match) {
        if (snp_trace_on()) {
            snp_log("[snp-trace] spiGetGameInfo miss id="); reasm_elf_log_hex(game_id);
            snp_log(" name="); snp_log(name && name[0] ? name : "(vuoto)"); snp_log("\n");
        }
        return 0;
    }
    memcpy(out_info, match, sizeof *match);
    if (snp_trace_on()) {
        snp_log("[snp-trace] spiGetGameInfo hit: "); snp_log(match->name);
        snp_log(" id="); reasm_elf_log_hex(match->id); snp_log("\n");
    }
    return 1;
}

int REASM_STDCALL snp_get_performance_data(u32 provider, u32 *out, void *file_time, char *extra) {
    (void) provider; (void) out; (void) file_time; (void) extra;
    snp_log("[snp] spiGetPerformanceData -> FALSE (no samples)\n");
    return 0;
}

int REASM_STDCALL snp_destroy(void) {
    g_poll_thread_stop = 1;
    if (g_socket_fd >= 0) { close(g_socket_fd); g_socket_fd = -1; }
    if (g_disc_fd >= 0) { close(g_disc_fd); g_disc_fd = -1; }
    nodes_clear();
    g_adv_active = 0; g_adv_len = 0;
    g_receive_event = 0; g_event_packet_pending = 0; peers_clear();
    snp_log("[snp] spiDestroy -> cleanup eseguito (valore di ritorno ignorato da Storm)\n");
    return 0;
}

/* Provider-owned receive blocks are returned by passing the address base to spiFree. */
int REASM_STDCALL snp_free(struct SnpNetAddress *address, char *data, u32 bytes) {
    (void)data; (void)bytes;
    if (!address) return 0;
    free(address);
    return 1;
}

int REASM_STDCALL snp_free_external(struct SnpNetAddress *address, char *data, u32 bytes) {
    (void)address; (void)data; (void)bytes;
    snp_log("[snp] spiFreeExternalMessage -> FALSE (ownership contract UNKNOWN)\\n");
    return 0;
}

static int snp_signal_event(void) {
    if (!g_receive_event) return 0;
    if (!SetEvent) { snp_log("[snp] SetEvent non disponibile: event wakeup negato\n"); return 0; }
    if (!SetEvent((void *)(unsigned long)g_receive_event)) {
        snp_log("[snp] SetEvent ha rifiutato l'HANDLE guest (nessun thunk/handle conversion applicato)\n");
        return 0;
    }
    return 1;
}

static void *snp_event_poll_thread(void *unused) {
    char byte;
    (void)unused;
    while (!__atomic_load_n(&g_poll_thread_stop, __ATOMIC_RELAXED)) {
        int fd = __atomic_load_n(&g_socket_fd, __ATOMIC_RELAXED);
        if (fd >= 0 && g_receive_event) {
            int n = recvfrom(fd, &byte, 1, SNP_MSG_DONTWAIT | 0x02 /* MSG_PEEK */, 0, 0);
            if (n >= 0 && !__atomic_exchange_n(&g_event_packet_pending, 1, __ATOMIC_RELAXED))
                (void)snp_signal_event();
        }
        { static unsigned tick;
          if ((tick++ % 10u) == 0u && disc_socket() >= 0) disc_poll();
          { struct { long sec, nsec; } delay = { 0, 1000000L }; (void)nanosleep(&delay, 0); } }
    }
    __atomic_store_n(&g_poll_thread_running, 0, __ATOMIC_RELAXED);
    return 0;
}

int REASM_STDCALL snp_initialize(void *client, void *user, void *battle, void *module, u32 event) {
    (void) user; (void) battle; (void) module;
    g_receive_event = event;
    g_event_packet_pending = 0;
    /* Product ID e versione arrivano dal record del client (standard.snp 0x1D009C7C/0x1D009C7F
     * legge +0x0C e +0x10 del primo argomento); accettati solo valori plausibili. */
    if (client) {
        u32 prod = *(const u32 *)((const unsigned char *)client + 0x0C);
        u32 ver = *(const u32 *)((const unsigned char *)client + 0x10);
        if (prod == 0x53544152u || prod == 0x53455850u) g_product = prod;
        if (ver && ver < 0x10000u) g_version = ver;
    }
    if (!peers_parse()) snp_log("[snp] SC_LAN_PEERS non valida: peer list vuota (attesi IPv4:porta numerici)\n");
    if (snp_trace_on()) {
        snp_log("[snp-trace] SC_LAN_PEERS count="); reasm_elf_log_hex(g_peer_count);
        snp_log(" product="); reasm_elf_log_hex(g_product);
        snp_log(" version="); reasm_elf_log_hex(g_version); snp_log("\n");
    }
    if (g_socket_fd < 0) g_socket_fd = open_loopback_socket();
    if (g_socket_fd < 0) {
        snp_log("[snp] spiInitialize failed: loopback UDP bind failed\n");
        return 0;
    }
    /* Socket di discovery separato (6111), come il provider originale (0x1D009D39-1D009E6D).
     * Se la porta e' occupata si continua senza discovery e lo si dichiara nel log. */
    if (g_disc_fd < 0) {
        g_disc_fd = open_discovery_socket();
        if (g_disc_fd < 0) snp_log("[snp] discovery socket non disponibile (porta occupata?): GameList solo via SC_LAN_PEERS\n");
        else snp_log("[snp] discovery socket bound su porta 6111\n");
    }
    if (g_receive_event && SetEvent && !__atomic_load_n(&g_poll_thread_running, __ATOMIC_RELAXED)) {
        g_poll_thread_stop = 0;
        if (pthread_create(&g_poll_thread, 0, snp_event_poll_thread, 0) == 0) {
            __atomic_store_n(&g_poll_thread_running, 1, __ATOMIC_RELAXED);
            (void)pthread_detach(g_poll_thread);
        } else snp_log("[snp] poll thread non avviato: event wakeup limitato a spiReceive\n");
    }
    snp_log("[snp] spiInitialize bound real UDP socket\n");
    if (event == 0) snp_log("[snp] spiInitialize event=0: event wakeup disabilitato (Storm ha passato NULL)\n");
    return 1;
}

/* Device enumeration (#103) is NOT provider enumeration (#105). The original UDP provider
 * returns TRUE with *devices=NULL (standard.snp 0x1D007F90); its InitializeDevice returns FALSE
 * (0x1D007FB0). Advertising a fictitious device id 1 sends BOTH UDP and single player through
 * the game's modem/device branch (0x4B0FB0 -> 0x4AFA30 -> Storm #116), causing an init error.
 * Keep the measured 0x130 record ABI documented, but expose no selectable modem/cable device. */

int REASM_STDCALL snp_lock_device_list(void **devices) {
    if (!devices) {
        snp_log("[snp] spiLockDeviceList -> FALSE (null output)\n");
        return 0;
    }
    *devices = 0;
    snp_log("[snp] spiLockDeviceList -> TRUE, no devices (UDP/single player need no modem)\n");
    return 1;
}

int REASM_STDCALL snp_unlock_device_list(void *devices) {
    snp_log("[snp] spiUnlockDeviceList -> TRUE (lista statica, nulla da liberare)\n");
    (void) devices;
    return 1;
}


int REASM_STDCALL snp_lock_game_list(int flags, int reserved, struct SnpGame **games) {
    (void)flags; (void)reserved;
    if (!games) return 0;
    /* Slot +0x28, 3 argomenti (storm:15038A88). Lock restituisce un PRESTITO della testa;
     * l'ownership dei nodi resta del provider (purge/replace/teardown). */
    disc_poll();
    *games = (struct SnpGame *)g_nodes;
    if (snp_trace_on()) {
        u32 count = 0; struct SnpGameNode *n;
        for (n = g_nodes; n; n = n->next) ++count;
        snp_log("[snp-trace] spiLockGameList head="); reasm_elf_log_hex((u32)(unsigned long)g_nodes);
        snp_log(" count="); reasm_elf_log_hex(count); snp_log("\n");
    }
    return 1;
}

int REASM_STDCALL snp_unlock_game_list(struct SnpGame *games, u32 *reserved) {
    u32 now;
    /* Slot +0x4c, 2 argomenti (storm:15038B62). Se il chiamante fornisce un indirizzo, il
     * provider scrive il poll hint di 500 ms e - solo con un annuncio locale attivo o con la
     * lista non vuota - emette un probe se sono passati piu' di 400 ms (0x1D009BAE/1D009BC4). */
    disc_poll();
    now = snp_now_ms();
    if (reserved) *reserved = SNP_UNLOCK_POLL_MS;
    /* Il provider originale emette il probe quando Unlock riceve un indirizzo, senza guardare
     * se la lista locale e' vuota (0x1D009BAE-0x1D009BD1): un client senza voci deve comunque
     * cercare i giochi. Qui si rispetta la stessa cadenza minima di 400 ms. */
    if (reserved && (u32)(now - g_last_probe_ms) > SNP_PROBE_MIN_GAP_MS)
        snp_send_probe();
    return 1;
}

struct SnpPacket { struct SnpNetAddress address; u32 bytes; char data[1]; };
static int receive_packet(struct SnpNetAddress **address, char **data, u32 *bytes) {
    struct SnpSockaddrIn from;
    unsigned int from_bytes = sizeof from;
    struct SnpPacket *packet;
    char payload[SNP_MAX_DATAGRAM];
    int n;
    u32 i;
    long long poll_time_ns = 0;
    int have_poll_time = 0;
    if (address) *address = 0; if (data) *data = 0; if (bytes) *bytes = 0;
    if (!address || !data || !bytes || g_socket_fd < 0) return 0;
    if (g_net_poll_ms && g_poll_socket_fd == g_socket_fd) {
        struct { long sec, nsec; } ts;
        if (clock_gettime(1 /* CLOCK_MONOTONIC */, &ts) == 0) {
            poll_time_ns = (long long)ts.sec * 1000000000LL + ts.nsec;
            have_poll_time = 1;
            if (g_last_poll_valid) {
                long long elapsed = poll_time_ns - g_last_poll_ns;
                if (elapsed >= 0 && elapsed < (long long)g_net_poll_ms * 1000000LL) return 0;
            }
        }
    }
    /* Receive first into storage large enough for every valid IPv4 UDP datagram. Empty polls and
     * socket errors therefore do not churn the heap; allocate provider-owned memory only after a
     * complete datagram has actually arrived. */
    if (have_poll_time) {
        g_last_poll_ns = poll_time_ns;
        g_last_poll_valid = 1;
    }
    n = recvfrom(g_socket_fd, payload, sizeof payload, SNP_MSG_DONTWAIT, &from, &from_bytes);
    if (n <= 0 || (unsigned int)n > sizeof payload || from_bytes != sizeof from) return 0;
    packet = (struct SnpPacket *)malloc(sizeof(*packet) + (unsigned int)n);
    if (!packet) return 0;
    for (i = 0; i < 16; ++i) packet->address.bytes[i] = ((const unsigned char *)&from)[i];
    packet->bytes = (u32)n;
    for (i = 0; i < (u32)n; ++i) packet->data[i] = payload[i];
    *address = &packet->address; *data = packet->data; *bytes = (u32)n;
    if (snp_trace_on()) {
        static int rx_logged;
        if (rx_logged < 40) {
            ++rx_logged;
            snp_log("[snp-trace] recv len="); reasm_elf_log_hex((u32)n);
            snp_log(" da="); reasm_elf_log_hex(get32((const unsigned char *)&from + 4));
            snp_log(":"); reasm_elf_log_hex(net_port(from.port)); snp_log("\n");
        }
    }
    __atomic_store_n(&g_event_packet_pending, 0, __ATOMIC_RELAXED);
    (void)snp_signal_event();
    return 1;
}

int REASM_STDCALL snp_receive(struct SnpNetAddress **address, char **data, u32 *bytes) {
    if (disc_socket() >= 0) disc_poll();
    return receive_packet(address, data, bytes);
}

int REASM_STDCALL snp_receive_external(struct SnpNetAddress **address, char **data, u32 *bytes) {
    if (address) *address = 0;
    if (data) *data = 0;
    if (bytes) *bytes = 0;
    snp_log("[snp] spiReceiveExternalMessage -> FALSE (contract UNKNOWN)\\n");
    return 0;
}

int REASM_STDCALL snp_initialize_device(void *a1, void *a2, void *a3, void *a4, void *a5) {
    (void) a1; (void) a2; (void) a3; (void) a4; (void) a5;
    snp_log("[snp] spiInitializeDevice -> FALSE (no modem/cable device advertised)\n");
    return 0;
}

/* Slot +0x34, 6 argomenti (`ret 0x18`; storm:15039044-1503906A). Il provider originale apre un
 * dialogo proprio (snp:1D009C47 call storm_208) e conferma via storm_118; la semantica dei sei
 * argomenti non e' ancora provata del tutto. Qui si registra la forma degli argomenti in trace e
 * si risponde TRUE solo se fra i contesti passati compare il nome di una partita in lista: nessuna
 * struttura inventata, nessun trasferimento di ownership. */
static int snp_dword_range_ok(const void *p, u32 bytes) {
    unsigned long v = (unsigned long)p;
    return v >= 0x10000ul && v < 0xC0000000ul && (v + bytes) > v;
}
static struct SnpGameNode *snp_find_game_in_context(void *ctx, u32 bytes) {
    struct SnpGameNode *n;
    const char *s = (const char *)ctx;
    u32 i;
    if (!snp_dword_range_ok(ctx, bytes)) return 0;
    for (n = g_nodes; n; n = n->next) {
        if (!n->name[0]) continue;
        for (i = 0; i + 1u < bytes && s[i]; ++i)
            if (s[i] == n->name[0] && snp_str_ieq(s + i, n->name)) return n;
    }
    return 0;
}

int REASM_STDCALL snp_select_game(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6) {
    struct SnpGameNode *found = 0;
    disc_poll();
    if (!found) found = snp_find_game_in_context(a2, 0x60u);
    if (!found) found = snp_find_game_in_context(a3, 0x10u);
    if (!found) found = snp_find_game_in_context(a4, 0xBCu);
    if (!found) found = snp_find_game_in_context(a5, 0x24u);
    if (snp_trace_on()) {
        snp_log("[snp-trace] spiSelectGame a1="); reasm_elf_log_hex((u32)(unsigned long)a1);
        snp_log(" a2="); reasm_elf_log_hex((u32)(unsigned long)a2);
        snp_log(" a3="); reasm_elf_log_hex((u32)(unsigned long)a3);
        snp_log(" a4="); reasm_elf_log_hex((u32)(unsigned long)a4);
        snp_log(" a5="); reasm_elf_log_hex((u32)(unsigned long)a5);
        snp_log(" a6="); reasm_elf_log_hex((u32)(unsigned long)a6);
        snp_log(" match="); snp_log(found ? found->name : "nessuno"); snp_log("\n");
    }
    if (!found) {
        snp_log("[snp] spiSelectGame -> FALSE (nessuna partita corrispondente nei contesti)\n");
        return 0;
    }
    snp_log("[snp] spiSelectGame -> TRUE: ");
    snp_log(found->name);
    snp_log("\n");
    return 1;
}
int REASM_STDCALL snp_unknown_report_result(void) {
    snp_log("[snp] spiReportGameResult non implementata -> FALSE (ABI/call-site UNKNOWN)\n");
    return 0;
}
int REASM_STDCALL snp_send(u32 count, struct SnpNetAddress **addresses, char *data, u32 bytes) {
    u32 i, targets_count = count ? count : g_peer_count;
    int sent_any = 0;
    if ((!count && !g_peer_count) || (count && !addresses) || !data || bytes > SNP_MAX_DATAGRAM || g_socket_fd < 0) return 0;
    if (snp_trace_on()) { snp_log("[snp-trace] spiSend count="); reasm_elf_log_hex(count);
        snp_log(" fanout="); reasm_elf_log_hex(count ? 0 : g_peer_count); snp_log("\n"); }
    /* The pointer/array/count prototype matches the provider table. Address bytes are treated as
     * the 16-byte sockaddr_in form requested for the LAN adapter; native live compatibility with
     * Storm's SNETADDR has not yet been established. */
    for (i = 0; i < targets_count; ++i) {
        struct SnpSockaddrIn to;
        const unsigned char *src;
        u32 j;
        if (!count) src = (const unsigned char *)&g_peers[i];
        else { if (!addresses[i]) return 0; src = addresses[i]->bytes; }
        for (j = 0; j < 16; ++j) ((unsigned char *)&to)[j] = src[j];
        if (to.family != SNP_AF_INET || !to.port || !to.address) return 0;
        if (sendto(g_socket_fd, data, bytes, 0, &to, sizeof to) != (int)bytes) return 0;
        if (snp_trace_on()) {
            static int tx_logged;
            if (tx_logged < 40) {
                ++tx_logged;
                snp_log("[snp-trace] send len="); reasm_elf_log_hex(bytes);
                snp_log(" a="); reasm_elf_log_hex(to.address);
                snp_log(":"); reasm_elf_log_hex(net_port(to.port)); snp_log("\n");
            }
        }
        sent_any = 1;
    }
    return sent_any;
}
int REASM_STDCALL snp_send_external(void *a1, void *a2, void *a3, void *a4, void *a5) {
    (void) a1; (void) a2; (void) a3; (void) a4; (void) a5;
    snp_log("[snp] spiSendExternalMessage non implementata -> FALSE\n");
    return 0;
}
/* Storm #138 usa QUESTO slot (+0x40) per il gioco single player: 10 argomenti, e il valore di
 * ritorno e' testato dal chiamante (0x1503B0A2). Registriamo il gioco locale: nessun dato di rete
 * e' fabbricato, nessun peer remoto esiste. */
/* Slot +0x40: advertising vero, 10 argomenti (`ret 0x28`), storm:15037BF8 e snp:1D008E70.
 * Ordine provato: name, optstr, statstring, flags, elapsed, d1, d2, d3, blob, blob_len. */
int REASM_STDCALL snp_start_advertising_ladder(const char *name, const char *optstr, const char *stat,
                                               u32 flags, u32 elapsed, u32 d1, u32 d2, u32 d3,
                                               const unsigned char *blob, u32 blob_len) {
    u32 len;
    (void) optstr; (void) elapsed; (void) d1; (void) d2; (void) d3;
    if (!name) name = "";
    if (!stat) stat = "";
    if (!blob) blob_len = 0;
    len = wire_build_announce(g_adv, sizeof g_adv, name, stat, flags, blob, blob_len);
    if (!len) {
        snp_log("[snp] spiStartAdvertisingGame -> FALSE (annuncio troppo grande)\n");
        return 0;
    }
    g_adv_len = len;
    g_adv_active = 1;
    g_game.used = 1; g_game.flags = flags;
    { u32 i; for (i = 0; i + 1 < sizeof g_game.name && name[i]; ++i) g_game.name[i] = name[i]; g_game.name[i] = 0; }
    disc_broadcast(g_adv, len);
    if (snp_trace_on()) {
        snp_log("[snp-trace] announce tx name="); snp_log(g_game.name);
        snp_log(" len="); reasm_elf_log_hex(len); snp_log("\n");
    }
    snp_log("[snp] spiStartAdvertisingGame -> annuncio inviato (discovery 6111)\n");
    return 1;
}
/* Slot +0x50: NON e' l'advertising. Contratto provato: 4 argomenti con due buffer di OUTPUT da
 * 128 byte (storm:1503AF1E/1503AF3A); la semantica delle stringhe non e' dimostrata, quindi resta
 * fail-closed e NON legge i buffer come input. */
int REASM_STDCALL snp_start_advertising(void *buf1, u32 len1, void *buf2, u32 len2) {
    (void) buf1; (void) len1; (void) buf2; (void) len2;
    snp_log("[snp] slot +0x50 (stringhe locali) -> FALSE (semantica non provata)\n");
    return 0;
}
void REASM_STDCALL snp_check_data_file(void *a1, u32 a2, u32 a3, u32 *available) {
    (void) a1; (void) a2; (void) a3;
    if (available) *available = 0;
    snp_log("[snp] spiCheckDataFile non implementata -> FALSE (available=0)\n");
}
int REASM_STDCALL snp_league_command(void *command, u32 bytes) {
    (void) command; (void) bytes;
    snp_log("[snp] spiLeagueCommand non implementata -> FALSE\n");
    return 0;
}
int REASM_STDCALL snp_league_send_replay(void *path, void *data, u32 bytes) {
    (void) path; (void) data; (void) bytes;
    snp_log("[snp] spiLeagueSendReplayPath non implementata -> FALSE\n");
    return 0;
}
int REASM_STDCALL snp_league_get_replay(void *path) {
    (void) path;
    snp_log("[snp] spiLeagueGetReplayPath non implementata -> FALSE\n");
    return 0;
}
int REASM_STDCALL snp_league_logout(u32 user) {
    (void) user;
    snp_log("[snp] spiLeagueLogout non implementata -> FALSE\n");
    return 0;
}
int REASM_STDCALL snp_league_get_name(char *name, u32 bytes) {
    (void) name; (void) bytes;
    snp_log("[snp] spiLeagueGetName non implementata -> FALSE\n");
    return 0;
}

/* La tabella NetFunctions e' letta dal CODICE TRADOTTO del guest: ogni slot viene chiamato con
 * `call *slot` e deve percio' essere un ingresso con ABI del guest (argomenti sullo stack emulato,
 * `ret N` stdcall, ritorno per branch al VA del chiamante). Un puntatore a funzione C finirebbe
 * eseguito con ECX=cpu e la PILA HOST del runtime: misurato, il `ret` del C tornava a 0x1 e
 * lasciava un indirizzo host in `_esp` (fault pc=0x1, esp=0x08582010, _ind_target=snp_initialize,
 * build/qa/gdb-play3-*.log). Gli slot sono quindi gli stub guest-ABI generati dall'import bridge,
 * cercati per nome: se uno manca, SnpBind fallisce (D19) invece di consegnare un puntatore nativo. */
static const char *const g_slot_names[] = {
    "snp_compare_addresses",
    "snp_destroy",
    "snp_free",
    "snp_free_external",
    "snp_get_game_info",
    "snp_get_performance_data",
    "snp_initialize",
    "snp_initialize_device",
    "snp_lock_device_list",
    "snp_lock_game_list",
    "snp_receive",
    "snp_receive_external",
    "snp_select_game",
    "snp_send",
    "snp_send_external",
    "snp_start_advertising_ladder",
    "snp_stop_advertising",
    "snp_unlock_device_list",
    "snp_unlock_game_list",
    "snp_start_advertising",
    "snp_unknown_report_result",
    "snp_check_data_file",
    "snp_league_command",
    "snp_league_send_replay",
    "snp_league_get_replay",
    "snp_league_logout",
    "snp_league_get_name"
};
#define SNP_SLOT_COUNT (sizeof g_slot_names / sizeof g_slot_names[0])
static void *g_slots[1 + SNP_SLOT_COUNT];   /* [0] = dwSize, poi i 27 slot in ordine di struct */

static struct SnpNetFunctions *snp_net_functions(void) {
    u32 i;
    if (g_slots[0]) return (struct SnpNetFunctions *) g_slots;
    g_slots[0] = (void *) (u32) 0x70u;
    for (i = 0; i < SNP_SLOT_COUNT; ++i) {
        void *p = arm_bridge_lookup_thunk ? arm_bridge_lookup_thunk(g_slot_names[i]) : 0;
        if (!p) {
            snp_log("[snp] NetFunctions: nessuno stub guest-ABI per ");
            snp_log(g_slot_names[i]);
            snp_log("\n");
            g_slots[0] = 0;
            return 0;
        }
        g_slots[1 + i] = p;
    }
    return (struct SnpNetFunctions *) g_slots;
}

int REASM_STDCALL SnpBind(u32 index, struct SnpNetFunctions **out_functions) {
    struct SnpNetFunctions *fns;
    if (index >= SNP_PROFILE_COUNT || !out_functions) {
        snp_log("[snp] SnpBind -> FALSE (unknown index or null output)\n");
        return 0;
    }
    fns = snp_net_functions();
    if (!fns) {
        snp_log("[snp] SnpBind -> FALSE (tabella incompleta: nessun puntatore nativo consegnato)\n");
        return 0;
    }
    *out_functions = fns;
    snp_log(index == 0 ? "[snp] SnpBind index=0 -> NetFunctions size=0x70\n" :
                        "[snp] SnpBind index=1 -> UDP NetFunctions size=0x70 (configured IPv4 peers)\n");
    return 1;
}

void *win32_snp_load_library(const char *name) {
    int start = 0, i;
    if (!name) return 0;
    for (i = 0; name[i]; ++i) if (name[i] == '/' || name[i] == '\\') start = i + 1;
    if (name[start] == 's' || name[start] == 'S') {
        const char *want = "standard.snp";
        for (i = 0; want[i] && name[start + i]; ++i) {
            char c = name[start + i];
            if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
            if (c != want[i]) return 0;
        }
        u32 handle;
        if (want[i] != 0 || name[start + i] != 0) return 0;
        if (win32_ui_load_library) {
            handle = win32_ui_load_library(name);
            if (!handle) return 0;
        } else {
            handle = SNP_MODULE;
        }
        g_module_handle = handle;
        return (void *) (unsigned long) handle;
    }
    return 0;
}

void *win32_snp_get_proc(void *module, const char *name) {
    if (!module || (u32) (unsigned long) module != g_module_handle || !name) return 0;
    if (name[0] == 'S' && name[1] == 'n' && name[2] == 'p' && name[3] == 'Q' &&
        name[4] == 'u' && name[5] == 'e' && name[6] == 'r' && name[7] == 'y' && name[8] == 0)
        return arm_bridge_lookup_thunk ? arm_bridge_lookup_thunk(name) : 0;
    if (name[0] == 'S' && name[1] == 'n' && name[2] == 'p' && name[3] == 'B' &&
        name[4] == 'i' && name[5] == 'n' && name[6] == 'd' && name[7] == 0)
        return arm_bridge_lookup_thunk ? arm_bridge_lookup_thunk(name) : 0;
    return 0;
}

/* === caps.dat che il nostro modulo dichiara ==================================================
 * Storm legge il caps.dat DAL FILE del modulo (SFileOpenArchive sul percorso .snp). I file di
 * gioco non si toccano: per il nostro modulo serviamo un archivio MPQ costruito in memoria, con due
 * entry coerenti con g_profiles: id 0 (single player) e UDP (Select Connection). Il resto del
 * file (header PE) non serve: LoadLibraryA per standard.snp e' gia' intercettata da
 * win32_snp_load_library, che non legge codice PE dal file.
 *
 * Formato MPQ v1 (pubblico, verificato sul caps.dat originale di standard.snp, 5 entry):
 *   header 32B | dati | hash table (16 voci, cifrata) | block table (1 voce, cifrata).
 * L'entry di caps.dat e' la stessa struttura che Storm usa come identita' del provider:
 *   u32 size (byte che seguono) | u32 code | nome NUL | descrizione NUL | 9 dword di CAPS.
 */
#define SNP_MPQ_HANDLE 0x5600A001u
#define SNP_MPQ_HASH_ENTRIES 16u
#define SNP_CAPS_MAGIC 0x1a51504du   /* "MPQ\x1a" little-endian */

static u32 g_crypt[0x500];
static int g_crypt_ready;
static unsigned char g_mpq[2048];
static u32 g_mpq_size;
static u32 g_mpq_pos;
static int g_mpq_open;

static void snp_crypt_init(void) {
    u32 seed = 0x00100001u, i, j, s, t1, t2;
    if (g_crypt_ready) return;
    for (i = 0; i < 0x100; ++i) {
        s = i;
        for (j = 0; j < 5; ++j) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            t1 = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            t2 = seed & 0xFFFFu;
            g_crypt[s] = t1 | t2;
            s += 0x100;
        }
    }
    g_crypt_ready = 1;
}

static u32 snp_hash(const char *s, u32 type) {
    u32 seed1 = 0x7FED7FEDu, seed2 = 0xEEEEEEEEu, ch;
    snp_crypt_init();
    while (*s) {
        ch = (unsigned char) *s++;
        if (ch >= 'a' && ch <= 'z') ch -= 0x20;      /* upper-case, come l'MPQ di Blizzard */
        seed1 = g_crypt[(type << 8) + ch] ^ (seed1 + seed2);
        seed2 = ch + seed1 + seed2 + (seed2 << 5) + 3;
    }
    return seed1;
}

static void snp_encrypt(u32 *data, u32 dwords, u32 key) {
    u32 seed2 = 0xEEEEEEEEu, i;
    snp_crypt_init();
    for (i = 0; i < dwords; ++i) {
        u32 plain = data[i];
        seed2 += g_crypt[0x400 + (key & 0xFF)];
        data[i] = plain ^ (key + seed2);
        key = ((~key << 21) + 0x11111111u) | (key >> 11);
        seed2 += plain + (seed2 << 5) + 3;    /* catena sul TESTO IN CHIARO: l'inverso di
                                              * DecryptMpqBlock, che incatena sul decifrato */
    }
}

static void snp_put32(unsigned char *p, u32 v) {
    p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24);
}


/* Costruisce l'MPQ del nostro modulo con due file:
 *   caps.dat     -> due entry di provider (id 0 single player, id UDP multiplayer)
 *   (signature)  -> entry presente ma NON firmata da Blizzard: vedi nota sotto
 * Ritorna la dimensione del blob (0 = errore).
 *
 * NOTA MISURATA (docs/21-SNP-ELF.md): StarCraft 1.16 non si fida di un modulo .snp senza la voce
 * "(signature)" (in sub_1503DDB0, 0x1503DDD8 -> ord. 251 a 0x15014B10; senza la voce il valore di
 * stato diventa 1 e il modulo e' scartato con LastError 0x4DC). Con la voce presente ma non
 * verificabile, la verifica si INTERROMPE prima di dire "valida" (CreateFileMappingA non e'
 * implementata nel runtime: 0x15014C9C) e lo stato resta 0, che e' accettato. Non stiamo
 * contraffacendo una firma: la nostra firma non esiste e non viene mai dichiarata valida. */
static u32 snp_build_mpq(void) {
    u32 total = 0, profile;
    u32 data_off = 32, sig_off, hash_off, block_off, end, i, idx;
    for (profile = 0; profile < SNP_PROFILE_COUNT; ++profile)
        total += 8 + snp_strlen(g_profiles[profile].name) + 1 +
                     snp_strlen(g_profiles[profile].description) + 1 + 36;
    u32 sig_len = 16;
    unsigned char *d = g_mpq;
    u32 *ht, *bt;

    sig_off = data_off + total;
    hash_off = (sig_off + sig_len + 15) & ~15u;
    block_off = hash_off + SNP_MPQ_HASH_ENTRIES * 16;
    end = block_off + 2 * 16;
    if (end > sizeof g_mpq) return 0;
    for (i = 0; i < end; ++i) d[i] = 0;

    /* header */
    snp_put32(d + 0, SNP_CAPS_MAGIC);
    snp_put32(d + 4, 32);
    snp_put32(d + 8, end);
    d[12] = 0; d[13] = 0;                      /* format version 0 */
    d[14] = 3; d[15] = 0;                      /* sector size = 512 << 3 = 4096 */
    snp_put32(d + 16, hash_off);
    snp_put32(d + 20, block_off);
    snp_put32(d + 24, SNP_MPQ_HASH_ENTRIES);
    snp_put32(d + 28, 2);                      /* due blocchi */

    /* caps.dat: stesso ordine/identita' di SnpQuery e SnpBind. */
    {   unsigned char *r = d + data_off;
      for (profile = 0; profile < SNP_PROFILE_COUNT; ++profile) {
        const char *name = g_profiles[profile].name, *desc = g_profiles[profile].description;
        u32 nlen = snp_strlen(name) + 1, dlen = snp_strlen(desc) + 1;
        u32 rec = 4 + nlen + dlen + 36; /* size esclude se stesso */
        u32 o = 0;
        snp_put32(r + o, rec); o += 4;
        snp_put32(r + o, g_profiles[profile].code); o += 4;
        for (i = 0; i < nlen; ++i) r[o + i] = (unsigned char) name[i];
        o += nlen;
        for (i = 0; i < dlen; ++i) r[o + i] = (unsigned char) desc[i];
        o += dlen;
        snp_put32(r + o +  0, g_caps.size);
        snp_put32(r + o +  4, g_caps.flags);
        snp_put32(r + o +  8, g_caps.max_message_size);
        snp_put32(r + o + 12, g_caps.max_queue_size);
        snp_put32(r + o + 16, g_caps.max_players);
        snp_put32(r + o + 20, g_caps.bytes_per_second);
        snp_put32(r + o + 24, g_caps.latency);
        snp_put32(r + o + 28, g_caps.turns_per_second);
        snp_put32(r + o + 32, g_caps.turns_in_transit);
        r += 4 + rec;
      }
    }
    /* "(signature)": versione 0 e almeno 9 byte (i due controlli di Storm), nessun flag extra */
    snp_put32(d + sig_off, 0u);

    /* block table: EXISTS, dati non compressi (nessuna sector table per file non compressi) */
    bt = (u32 *) (d + block_off);
    bt[0] = data_off; bt[1] = total;      bt[2] = total;      bt[3] = 0x80000000u;
    bt[4] = sig_off;  bt[5] = sig_len;    bt[6] = sig_len;    bt[7] = 0x80000000u;
    snp_encrypt(bt, 8, snp_hash("(block table)", 3));

    /* hash table (16 voci): {name1, name2, wLocale, wPlatform, blockIndex} */
    ht = (u32 *) (d + hash_off);
    for (i = 0; i < SNP_MPQ_HASH_ENTRIES; ++i) {
        ht[i * 4 + 0] = 0xFFFFFFFFu;
        ht[i * 4 + 1] = 0xFFFFFFFFu;
        ht[i * 4 + 2] = 0;
        ht[i * 4 + 3] = 0xFFFFFFFFu;
    }
    idx = snp_hash("caps.dat", 0) & (SNP_MPQ_HASH_ENTRIES - 1);
    ht[idx * 4 + 0] = snp_hash("caps.dat", 1);
    ht[idx * 4 + 1] = snp_hash("caps.dat", 2);
    ht[idx * 4 + 2] = 0;                       /* wLocale = 0 (neutro), wPlatform = 0 */
    ht[idx * 4 + 3] = 0;                       /* block index 0 */
    idx = snp_hash("(signature)", 0) & (SNP_MPQ_HASH_ENTRIES - 1);
    ht[idx * 4 + 0] = snp_hash("(signature)", 1);
    ht[idx * 4 + 1] = snp_hash("(signature)", 2);
    ht[idx * 4 + 2] = 0;
    ht[idx * 4 + 3] = 1;                       /* block index 1 */
    snp_encrypt(ht, SNP_MPQ_HASH_ENTRIES * 4, snp_hash("(hash table)", 3));

    g_mpq_size = end;
    return end;
}

/* --- file virtuale: solo il nostro modulo, solo per la lettura di caps.dat ------------------- */
static int snp_basename_is(const char *path, const char *want) {
    int start = 0, i;
    if (!path) return 0;
    for (i = 0; path[i]; ++i) if (path[i] == '/' || path[i] == '\\') start = i + 1;
    for (i = 0; want[i] && path[start + i]; ++i) {
        char c = path[start + i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c != want[i]) return 0;
    }
    return want[i] == 0 && path[start + i] == 0;
}

static int snp_trace_on(void) {
    extern char *getenv(const char *);
    static int cached = -1;
    if (cached < 0) { const char *v = getenv("SC_SNP_TRACE"); cached = (v && *v == '1'); }
    return cached;
}

void *win32_snp_vfile_open(const char *path) {
    if (!snp_basename_is(path, "standard.snp")) return 0;
    if (!g_mpq_size && !snp_build_mpq()) return 0;
    g_mpq_pos = 0; g_mpq_open = 1;
    snp_log("[snp] caps.dat del provider servito in memoria (id 0 + UDP, 2 entry)\n");
    if (snp_trace_on()) { snp_log("[snp-trace] vfile_open size="); reasm_elf_log_hex(g_mpq_size); snp_log("\n"); }
    return (void *) (unsigned long) SNP_MPQ_HANDLE;
}

int win32_snp_vfile_own(void *h) { return (u32) (unsigned long) h == SNP_MPQ_HANDLE && g_mpq_open; }

u32 win32_snp_vfile_size(void *h) { (void) h; return g_mpq_size; }

long win32_snp_vfile_seek(void *h, long off, u32 method) {
    long base = 0;
    (void) h;
    if (snp_trace_on()) { snp_log("[snp-trace] vfile_seek off="); reasm_elf_log_hex((unsigned) off);
        snp_log(" method="); snp_log(method == 1 ? "cur" : method == 2 ? "end" : "set"); snp_log("\n"); }
    if (method == 1) base = (long) g_mpq_pos;
    else if (method == 2) base = (long) g_mpq_size;
    if (base + off < 0) return -1;
    g_mpq_pos = (u32) (base + off);
    return (long) g_mpq_pos;
}

long win32_snp_vfile_read(void *h, void *buf, u32 n) {
    u32 avail = g_mpq_size > g_mpq_pos ? g_mpq_size - g_mpq_pos : 0;
    u32 i;
    (void) h;
    if (snp_trace_on()) { snp_log("[snp-trace] vfile_read n="); reasm_elf_log_hex(n);
        snp_log(" pos="); reasm_elf_log_hex(g_mpq_pos); snp_log("\n"); }
    if (n > avail) n = avail;
    for (i = 0; i < n; ++i) ((unsigned char *) buf)[i] = g_mpq[g_mpq_pos + i];
    g_mpq_pos += n;
    return (long) n;
}

void win32_snp_vfile_close(void *h) { (void) h; g_mpq_open = 0; }

/* Solo per il gate: permette di verificare l'MPQ generato con un parser esterno. */
const unsigned char *snp_elf_caps_mpq(u32 *bytes) {
    if (!g_mpq_size && !snp_build_mpq()) { if (bytes) *bytes = 0; return 0; }
    if (bytes) *bytes = g_mpq_size;
    return g_mpq;
}

int snp_elf_test_socket_address(void *out, u32 bytes) {
    struct SnpSockaddrIn address;
    unsigned int n = sizeof address;
    u32 i;
    if (!out || bytes < sizeof address || g_socket_fd < 0 || getsockname(g_socket_fd, &address, &n) != 0 || n != sizeof address)
        return 0;
    for (i = 0; i < sizeof address; ++i) ((unsigned char *)out)[i] = ((const unsigned char *)&address)[i];
    return 1;
}

int snp_elf_test_socket_roundtrip(void) {
    struct SnpSockaddrIn address;
    unsigned int address_bytes = sizeof address;
    unsigned int from_bytes = sizeof address;
    char sent[4] = { 'S', 'N', 'P', '!' }, received[4] = { 0, 0, 0, 0 };
    char from[16];
    if (g_socket_fd < 0 || getsockname(g_socket_fd, &address, &address_bytes) != 0 || address_bytes != sizeof address)
        return 0;
    if (address.family != SNP_AF_INET || address.address != 0x0100007fu || address.port == 0)
        return 0;
    if (sendto(g_socket_fd, sent, sizeof sent, 0, &address, sizeof address) != sizeof sent)
        return 0;
    if (recvfrom(g_socket_fd, received, sizeof received, SNP_MSG_DONTWAIT, from, &from_bytes) != sizeof received)
        return 0;
    return memcmp(sent, received, sizeof sent) == 0;
}
