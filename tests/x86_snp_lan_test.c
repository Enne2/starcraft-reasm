/* Two-peer loopback UDP test for the native SNP provider. */
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
extern int snp_elf_test_socket_address(void *, u32);
extern int snp_elf_test_socket_roundtrip(void);
extern int STDCALL provider_initialize(void *, void *, void *, void *, u32) __asm__("snp_initialize");
extern int STDCALL provider_advertise(const char *, u32, const char *, u32) __asm__("snp_start_advertising");
extern int STDCALL provider_send(u32, struct SnpNetAddress **, char *, u32) __asm__("snp_send");
extern int STDCALL provider_receive(struct SnpNetAddress **, char **, u32 *) __asm__("snp_receive");
extern int STDCALL provider_free(struct SnpNetAddress *, char *, u32) __asm__("snp_free");
extern void reasm_elf_log(const char *);
#define AF_INET 2
#define SOCK_DGRAM 2
#define MSG_DONTWAIT 0x40
#define CHECK(x, msg) do { if (!(x)) { reasm_elf_log("FAIL: " msg "\n"); return 1; } } while (0)

extern int write(int, const void *, unsigned int);
void reasm_elf_log(const char *s) { unsigned int n = 0; while (s[n]) ++n; (void)write(1, s, n); }
void reasm_elf_log_hex(unsigned int value) { (void)value; }
static void copy_address(struct SnpNetAddress *out, const void *in) {
    const unsigned char *p = (const unsigned char *)in;
    unsigned int i;
    for (i = 0; i < 16; ++i) out->bytes[i] = p[i];
}

int main(void) {
    struct SockaddrIn provider, client_addr, from;
    struct SnpNetAddress client_net, *received_address = 0, *targets[1];
    unsigned int addrlen, fromlen;
    char outbound[] = "client-to-provider", reply[] = "provider-to-client";
    char *owned_data = 0;
    char peer_data[64];
    u32 owned_bytes = 0;
    int client, n;
    CHECK(setenv("SC_LAN_BIND", "127.0.0.1", 1) == 0, "set SC_LAN_BIND");
    CHECK(setenv("SC_LAN_PORT", "0", 1) == 0, "set SC_LAN_PORT");
    CHECK(provider_initialize(0, 0, 0, 0, 0) != 0, "spiInitialize");
    CHECK(snp_elf_test_socket_roundtrip(), "provider socket self-test");
    CHECK(snp_elf_test_socket_address(&provider, sizeof provider), "provider bound address");
    CHECK(provider_advertise("net-test", 8, 0, 0), "spiStartAdvertisingGame");
    client = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(client >= 0, "client socket");
    client_addr.family = AF_INET; client_addr.port = 0; client_addr.address = 0x0100007fu;
    for (n = 0; n < 8; ++n) client_addr.zero[n] = 0;
    CHECK(bind(client, &client_addr, sizeof client_addr) == 0, "client bind");
    addrlen = sizeof client_addr;
    CHECK(getsockname(client, &client_addr, &addrlen) == 0, "client getsockname");
    copy_address(&client_net, &client_addr);

    CHECK(sendto(client, outbound, sizeof outbound, 0, &provider, sizeof provider) == sizeof outbound,
                "peer sends to provider");
    CHECK(provider_receive(&received_address, &owned_data, &owned_bytes), "spiReceive real peer datagram");
    CHECK(owned_bytes == sizeof outbound && owned_data[0] == 'c', "received payload contents");
    targets[0] = &client_net;
    CHECK(provider_send(1, targets, reply, sizeof reply), "spiSend provider reply");
    fromlen = sizeof from;
    n = recvfrom(client, peer_data, sizeof peer_data, MSG_DONTWAIT, &from, &fromlen);
    CHECK(n == sizeof reply && peer_data[0] == 'p', "peer receives provider reply");
    CHECK(provider_free(received_address, owned_data, owned_bytes), "spiFree allocation");
    close(client);
    reasm_elf_log("PASS: two UDP peers exchanged real loopback packets through spiSend/spiReceive/spiFree\n");
    return 0;
}
