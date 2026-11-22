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
extern void free(void *);
extern char *getenv(const char *);
extern int fcntl(int, int, ...);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern int clock_gettime(int, void *);

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
static const char g_provider_description[] = "Local UDP loopback; no remote game advertisement is synthesized.";
/* Nome/descrizione UDP estratti da caps.dat del modulo originale; il trasporto nativo resta
 * locale (nessun protocollo LAN remoto implementato). Le CAPS descrivono il nostro backend. */
static const struct SnpProfile {
    u32 code;
    const char *name, *description;
} g_profiles[] = {
    { 0u, g_provider_name, g_provider_description },
    { 0x5544504eu, "Local Area Network (UDP)",
      "All computers must be connected to a TCP/IP-compatible network." }
};
#define SNP_PROFILE_COUNT (sizeof g_profiles / sizeof g_profiles[0])
static struct SnpCaps g_caps = { 0x24, SNP_CAPS_FLAGS, 500, 16, 8, 1000, 50, 8, 2 };
static int g_socket_fd = -1;
static u32 g_module_handle;
static u32 g_net_poll_ms = 1;
static int g_poll_socket_fd = -1;
static int g_last_poll_valid;
static long long g_last_poll_ns;
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
    u32 port = port_env ? parse_port(port_env) : 0;
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
                        "[snp] SnpQuery index=1 -> Local Area Network (UDP), loopback-only backend\n");
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
    g_game.used = 0;
    snp_log("[snp] spiStopAdvertisingGame -> gioco locale rimosso\n");
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
    snp_log("[snp] spiCompareNetAddresses -> TRUE (16-byte compare; Storm return contract UNKNOWN)\n");
    return 1;
}

int REASM_STDCALL snp_get_game_info(u32 provider, void *arg2, void *arg3, void *out_info) {
    (void) provider; (void) arg2; (void) arg3; (void) out_info;
    snp_log("[snp] spiGetGameInfo -> FALSE (no game info available)\n");
    return 0;
}

int REASM_STDCALL snp_get_performance_data(u32 provider, u32 *out, void *file_time, char *extra) {
    (void) provider; (void) out; (void) file_time; (void) extra;
    snp_log("[snp] spiGetPerformanceData -> FALSE (no samples)\n");
    return 0;
}

int REASM_STDCALL snp_destroy(void) {
    if (g_socket_fd >= 0) { close(g_socket_fd); g_socket_fd = -1; }
    snp_log("[snp] spiDestroy -> FALSE (cleanup attempted; return ignored by Storm)\n");
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

int REASM_STDCALL snp_initialize(void *client, void *user, void *battle, void *module, u32 event) {
    (void) client; (void) user; (void) battle; (void) module; (void) event;
    if (g_socket_fd < 0) g_socket_fd = open_loopback_socket();
    if (g_socket_fd < 0) {
        snp_log("[snp] spiInitialize failed: loopback UDP bind failed\n");
        return 0;
    }
    snp_log("[snp] spiInitialize bound real UDP socket to 127.0.0.1\n");
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
    if (!games || g_socket_fd < 0) return 0;
    /* SnpGame/GAMELIST layout is not established from the provider ABI: fail closed rather than
     * hand Storm an invented record. UDP game discovery is not yet advertised as game listing. */
    *games = 0;
    snp_log("[snp] spiLockGameList -> TRUE, empty (SnpGame layout UNKNOWN)\n");
    return 1;
}

int REASM_STDCALL snp_unlock_game_list(struct SnpGame *games, u32 *reserved) {
    (void)games; (void)reserved;
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
    return 1;
}

int REASM_STDCALL snp_receive(struct SnpNetAddress **address, char **data, u32 *bytes) {
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

int REASM_STDCALL snp_unknown_select_game(void) {
    snp_log("[snp] spiSelectGame non implementata -> FALSE (ABI/call-site UNKNOWN)\n");
    return 0;
}
int REASM_STDCALL snp_unknown_report_result(void) {
    snp_log("[snp] spiReportGameResult non implementata -> FALSE (ABI/call-site UNKNOWN)\n");
    return 0;
}
int REASM_STDCALL snp_send(u32 count, struct SnpNetAddress **addresses, char *data, u32 bytes) {
    u32 i;
    int sent_any = 0;
    if (!count || !addresses || !data || bytes > SNP_MAX_DATAGRAM || g_socket_fd < 0) return 0;
    /* The pointer/array/count prototype matches the provider table. Address bytes are treated as
     * the 16-byte sockaddr_in form requested for the LAN adapter; native live compatibility with
     * Storm's SNETADDR has not yet been established. */
    for (i = 0; i < count; ++i) {
        struct SnpSockaddrIn to;
        const unsigned char *src;
        u32 j;
        if (!addresses[i]) return 0;
        src = addresses[i]->bytes;
        for (j = 0; j < 16; ++j) ((unsigned char *)&to)[j] = src[j];
        if (to.family != SNP_AF_INET || !to.port || !to.address) return 0;
        if (sendto(g_socket_fd, data, bytes, 0, &to, sizeof to) != (int)bytes) return 0;
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
int REASM_STDCALL snp_start_advertising_ladder(void *a1, void *a2, void *a3, void *a4, void *a5,
                                                       void *a6, void *a7, void *a8, void *a9, void *a10) {
    (void) a1; (void) a2; (void) a3; (void) a4; (void) a5;
    (void) a6; (void) a7; (void) a8; (void) a9; (void) a10;
    g_game.used = 1; g_game.flags = 1;
    g_game.name[0] = 0;
    snp_log("[snp] spiStartAdvertisingLadderGame -> gioco locale registrato (+0x40)\n");
    return 1;
}
int REASM_STDCALL snp_start_advertising(const char *name, u32 name_bytes, const char *password, u32 password_bytes) {
    u32 i;
    (void) password; (void) password_bytes;
    for (i = 0; i + 1 < sizeof g_game.name && name && i < name_bytes && name[i]; ++i)
        g_game.name[i] = name[i];
    g_game.name[i] = 0;
    g_game.used = 1; g_game.flags = 0;
    snp_log("[snp] spiStartAdvertisingGame -> gioco locale registrato: ");
    snp_log(g_game.name);
    snp_log("\n");
    return 1;
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
    "snp_unknown_select_game",
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
                        "[snp] SnpBind index=1 -> UDP NetFunctions size=0x70 (loopback-only)\n");
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

static u32 snp_strlen(const char *s) { u32 n = 0; while (s[n]) ++n; return n; }

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
