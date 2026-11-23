/* UDP loopback, SC_LAN_PEERS fan-out, and event wakeup test for the native SNP provider. */
#include <stdint.h>
typedef unsigned int u32;
typedef unsigned short u16;
#define STDCALL __attribute__((stdcall))
struct SockaddrIn { u16 family, port; u32 address; unsigned char zero[8]; };
struct SnpNetAddress { unsigned char bytes[16]; };
extern int socket(int, int, int);
extern int bind(int, const void *, unsigned int);
extern int getsockname(int, void *, unsigned int *);
extern int close(int);
extern int sendto(int, const void *, unsigned int, int, const void *, unsigned int);
extern int recvfrom(int, void *, unsigned int, int, void *, unsigned int *);
extern int setenv(const char *, const char *, int);
extern int clock_gettime(int, void *);
extern int snp_elf_test_socket_address(void *, u32);
extern int snp_elf_test_socket_roundtrip(void);
extern u32 snp_peer_count(void);
extern u32 snp_peers_foreach(int (*callback)(const unsigned char address[16], void *context), void *context);
extern int STDCALL provider_initialize(void *, void *, void *, void *, u32) __asm__("snp_initialize");
extern int STDCALL provider_advertise(const char *, u32, const char *, u32) __asm__("snp_start_advertising");
extern int STDCALL provider_send(u32, struct SnpNetAddress **, char *, u32) __asm__("snp_send");
extern int STDCALL provider_receive(struct SnpNetAddress **, char **, u32 *) __asm__("snp_receive");
extern int STDCALL provider_free(struct SnpNetAddress *, char *, u32) __asm__("snp_free");
extern int write(int, const void *, unsigned int);
extern void reasm_elf_log(const char *);
#define AF_INET 2
#define SOCK_DGRAM 2
#define MSG_DONTWAIT 0x40
#define CHECK(x, msg) do { if (!(x)) { reasm_elf_log("FAIL: " msg "\n"); return 1; } } while (0)

static volatile unsigned event_calls;
static volatile unsigned long long event_ns;
/* Test shim: the production runtime resolves this exact Win32 export; the guest HANDLE
 * is deliberately passed through unchanged (the test does not model handle translation). */
int STDCALL SetEvent(void *handle) {
    struct { long sec, nsec; } ts;
    if ((unsigned long)handle != 0x76543210ul) return 0;
    if (clock_gettime(1, &ts) == 0) {
        unsigned long long now = (unsigned long long)ts.sec * 1000000000ull + (unsigned long long)ts.nsec;
        if (!event_calls) event_ns = now;
    }
    __atomic_add_fetch(&event_calls, 1, __ATOMIC_RELAXED);
    return 1;
}
void reasm_elf_log(const char *s) { unsigned int n = 0; while (s[n]) ++n; (void)write(1, s, n); }
void reasm_elf_log_hex(unsigned int value) {
    char text[11] = "0x00000000"; static const char hex[] = "0123456789abcdef"; unsigned int i;
    for (i = 0; i < 8; ++i) text[9 - i] = hex[(value >> (i * 4)) & 15u];
    (void)write(1, text, 10);
}
static int count_peer(const unsigned char address[16], void *context) {
    unsigned int *count = context; (void)address; ++*count; return 1;
}
static void copy_address(struct SnpNetAddress *out, const void *in) {
    const unsigned char *p = (const unsigned char *)in;
    unsigned int i;
    for (i = 0; i < 16; ++i) out->bytes[i] = p[i];
}
static unsigned long long now_ns(void) {
    struct { long sec, nsec; } ts;
    if (clock_gettime(1, &ts) != 0) return 0;
    return (unsigned long long)ts.sec * 1000000000ull + (unsigned long long)ts.nsec;
}
static void append_decimal(char *out, unsigned int *pos, unsigned int value) {
    char digits[5]; unsigned int n = 0, i;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < sizeof digits);
    for (i = 0; i < n; ++i) out[(*pos)++] = digits[n - i - 1];
}
static unsigned int host_port(u16 network_port) { return ((network_port >> 8) | (network_port << 8)) & 0xffffu; }
static int bind_peer(int *fd, struct SockaddrIn *addr) {
    unsigned int n = sizeof *addr; unsigned int i;
    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd < 0) return 0;
    addr->family = AF_INET; addr->port = 0; addr->address = 0x0100007fu;
    for (i = 0; i < 8; ++i) addr->zero[i] = 0;
    return bind(*fd, addr, sizeof *addr) == 0 && getsockname(*fd, addr, &n) == 0;
}
static int expect_payload(int fd, const char *expected, unsigned int expected_len) {
    char buffer[64]; struct SockaddrIn from; unsigned int from_len = sizeof from;
    int n = recvfrom(fd, buffer, sizeof buffer, MSG_DONTWAIT, &from, &from_len);
    return n == (int)expected_len && buffer[0] == expected[0];
}

int main(void) {
    struct SockaddrIn provider, peer1_addr, peer2_addr;
    struct SnpNetAddress peer1_net, *received_address = 0, *targets[1];
    unsigned int fromlen, pos = 0;
    char peer_list[64], outbound[] = "client-to-provider", reply[] = "provider-to-client";
    char fanout[] = "fanout", *owned_data = 0;
    char peer_data[64];
    u32 owned_bytes = 0;
    unsigned int iterated = 0;
    int peer1, peer2, n;
    unsigned long long arrival, latency;
    CHECK(bind_peer(&peer1, &peer1_addr), "peer 1 bind");
    CHECK(bind_peer(&peer2, &peer2_addr), "peer 2 bind");
    { const char *ip = "127.0.0.1"; while (*ip) peer_list[pos++] = *ip++; }
    peer_list[pos++] = ':';
    append_decimal(peer_list, &pos, host_port(peer1_addr.port)); peer_list[pos++] = ',';
    { const char *ip = "127.0.0.1"; while (*ip) peer_list[pos++] = *ip++; }
    peer_list[pos++] = ':';
    append_decimal(peer_list, &pos, host_port(peer2_addr.port)); peer_list[pos] = 0;
    reasm_elf_log("[test] SC_LAN_PEERS="); reasm_elf_log(peer_list); reasm_elf_log("\n");
    CHECK(setenv("SC_LAN_BIND", "127.0.0.1", 1) == 0, "set SC_LAN_BIND");
    CHECK(setenv("SC_LAN_PORT", "0", 1) == 0, "set SC_LAN_PORT");
    CHECK(setenv("SC_LAN_PEERS", peer_list, 1) == 0, "set SC_LAN_PEERS");
    CHECK(setenv("SC_SNP_TRACE", "1", 1) == 0, "enable SNP trace");
    CHECK(provider_initialize(0, 0, 0, 0, 0x76543210u) != 0, "spiInitialize");
    CHECK(snp_peer_count() == 2, "parse two SC_LAN_PEERS entries");
    CHECK(snp_peers_foreach(count_peer, &iterated) == 2 && iterated == 2, "snp_peers_foreach visits configured peers");
    CHECK(snp_elf_test_socket_roundtrip(), "provider socket self-test");
    CHECK(snp_elf_test_socket_address(&provider, sizeof provider), "provider bound address");
    CHECK(provider_advertise("net-test", 8, 0, 0), "spiStartAdvertisingGame");

    CHECK(provider_send(0, 0, fanout, sizeof fanout), "spiSend(count=0) fan-out");
    CHECK(expect_payload(peer1, fanout, sizeof fanout), "fan-out reaches peer 1");
    CHECK(expect_payload(peer2, fanout, sizeof fanout), "fan-out reaches peer 2");

    copy_address(&peer1_net, &peer1_addr);
    arrival = now_ns();
    CHECK(sendto(peer1, outbound, sizeof outbound, 0, &provider, sizeof provider) == sizeof outbound,
                "peer sends to provider");
    /* The internal poller must signal before the caller consumes the packet. */
    for (n = 0; n < 50 && !event_calls; ++n) {
        struct { long sec, nsec; } delay = { 0, 1000000L };
        extern int nanosleep(const void *, void *);
        (void)nanosleep(&delay, 0);
    }
    CHECK(event_calls != 0, "SetEvent called on packet arrival");
    latency = event_ns >= arrival ? event_ns - arrival : 0;
    CHECK(latency < 50000000ull, "SetEvent observed within 50 ms");
    reasm_elf_log("[test] SetEvent latency ns="); reasm_elf_log_hex((u32)latency); reasm_elf_log("\n");
    CHECK(provider_receive(&received_address, &owned_data, &owned_bytes), "spiReceive real peer datagram");
    CHECK(owned_bytes == sizeof outbound && owned_data[0] == 'c', "received payload contents");
    targets[0] = &peer1_net;
    CHECK(provider_send(1, targets, reply, sizeof reply), "spiSend provider reply");
    fromlen = sizeof peer2_addr;
    n = recvfrom(peer1, peer_data, sizeof peer_data, MSG_DONTWAIT, &peer2_addr, &fromlen);
    CHECK(n == sizeof reply && peer_data[0] == 'p', "peer receives provider reply");
    CHECK(provider_free(received_address, owned_data, owned_bytes), "spiFree allocation");
    close(peer1); close(peer2);
    reasm_elf_log("PASS: two UDP peers exchanged packets; SC_LAN_PEERS fan-out and event wakeup <50ms\n");
    return 0;
}
