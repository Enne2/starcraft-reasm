/* Native fast paths and an exact lifted-code fallback for Storm #552 SCompDecompress.
 * Codec ordering and edge behavior are verified against storm.orig.dll at 0x1501D160.
 * SC_SCOMP_DUMP records decrypted inputs/results for differential fixtures.
 */
#ifndef SC_SCOMP_TEST_ONLY
#include "cpu_llasm.h"
#endif

#include <stdint.h>
#include <stddef.h>
extern char *getenv(const char *);
extern void *memmove(void *, const void *, size_t);
extern int mkdir(const char *, unsigned);
extern int open(const char *, int, ...);
extern long write(int, const void *, unsigned long);
extern int close(int);

#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000

#ifndef SC_SCOMP_TEST_ONLY
extern int arm_guest_call_fn(void *entry_fastcc, unsigned nargs, const uint32_t *args, int *status);
extern void orig_storm_552(_cpu *cpu);
#endif

#ifndef SC_SCOMP_TEST_ONLY
static uint32_t dump_serial;
#endif

/* Direct-call API used by the differential test. Returns Storm's EAX result and writes the
 * resulting length only on the same successful paths as the original wrapper. */
static const uint32_t adpcm_step[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
/* DWORD table at 0x15050948 in storm.orig.dll (IDA read-only). */
static const int8_t adpcm_adjust[32] = {
 -1,0,-1,4,-1,2,-1,6,-1,1,-1,5,-1,3,-1,7,
 -1,1,-1,5,-1,3,-1,7,-1,2,-1,4,-1,6,-1,8
};

/* IDA: sub_1501C0F0 @ 0x1501C0F0, wrappers @ 0x1501C6E0/0x1501C660.
 * Payload is reserved byte, step shift, initial signed samples, then ADPCM codes. */
static int adpcm_decode(void *dst, uint32_t *out_len, const uint8_t *payload,
                        uint32_t payload_len, uint32_t dst_cap, unsigned channels) {
    uint32_t pos = 2, used = 0, step[2] = {44,44};
    int sample[2] = {0,0};
    unsigned channel = 0;
    if (!dst || !out_len || !payload || payload_len < 2u + 2u * channels) return -1;
    for (unsigned c=0; c<channels; ++c) {
        sample[c] = (int16_t)((uint16_t)payload[pos] | ((uint16_t)payload[pos+1] << 8));
        pos += 2;
        if (dst_cap - used < 2) { *out_len=used; return 1; }
        ((uint8_t *)dst)[used++] = (uint8_t)sample[c];
        ((uint8_t *)dst)[used++] = (uint8_t)(sample[c] >> 8);
    }
    while (pos < payload_len) {
        uint8_t code=payload[pos++];
        unsigned idx = channels == 2 ? channel : 0;
        if (code & 0x80) {
            unsigned control=code & 0x7f;
            if (control == 0) { if (step[idx]) --step[idx]; }
            else if (control == 1) { step[idx] = step[idx] > 80 ? 88 : step[idx]+8; }
            else if (control == 2) { step[idx] = step[idx] < 8 ? 0 : step[idx]-8; }
            if (control == 0) {
                if (dst_cap-used < 2) { *out_len=used; return 1; }
                ((uint8_t *)dst)[used++] = (uint8_t)sample[idx];
                ((uint8_t *)dst)[used++] = (uint8_t)(sample[idx] >> 8);
            }
        } else {
            unsigned ix=step[idx];
            uint32_t delta=adpcm_step[ix] >> (payload[1] & 31);
            for (unsigned b=0;b<6;b++) if (code & (1u<<b)) delta += adpcm_step[ix] >> b;
            if (code & 0x40) { int v=sample[idx]-(int)delta; sample[idx]=v <= -32768 ? -32768 : v; }
            else { int v=sample[idx]+(int)delta; sample[idx]=v >= 32767 ? 32767 : v; }
            if (dst_cap-used < 2) { *out_len=used; return 1; }
            ((uint8_t *)dst)[used++] = (uint8_t)sample[idx];
            ((uint8_t *)dst)[used++] = (uint8_t)(sample[idx] >> 8);
            { int next=(int)step[idx]+adpcm_adjust[code & 31];
              step[idx]=(uint32_t)(next<0 ? 0 : next>88 ? 88 : next); }
        }
        if (channels == 2) channel ^= 1;
    }
    *out_len=used;
    return 1;
}

/* --- PKWARE DCL "explode" (MPQ mask 0x08) ------------------------------------------------
 * IDA: #552 dispatches 0x08 -> sub_1501D500 @0x1501D500 -> sub_15004810 @0x15004810
 * (header: lit @+8756, dict @+8757, bit stream from +8758 LSB-first, sub_15004CC0 @0x15004CC0)
 * and the emit loop sub_15005030 @0x15005030; canned code lengths live at 0x150591F0 (256
 * literal lengths), 0x150591D0 (16 length-code lengths), 0x15059120/0x15059160 (64 distance
 * codes). Bit-exact verified against 3798 captured 0x08 sectors produced by orig_storm_552.
 * Format reference: PKWARE DCL explode (M. Adler, zlib contrib/blast); tables re-derived
 * from the binary, RLE form as shipped by PKWARE. */
#define PKW_MAXBITS 13

typedef struct { uint16_t count[PKW_MAXBITS + 1]; uint16_t symbol[320]; } pkw_huff;
/* RLE code-length tables as shipped by PKWARE (0x150591F0 / 0x150591D0 / 0x15059120 regions
 * after expansion; nibble form: high nibble + 1 repeats, low nibble is the code length). */
static const uint8_t pkw_litrep[] = {
 11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,9,7,6,7,8,7,6,55,8,23,24,12,11,
 7,9,11,12,6,7,22,5,7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,8,12,5,38,
 5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,
 44,173};
static const uint8_t pkw_lenrep[] = {2,35,36,53,38,23};
static const uint8_t pkw_distrep[] = {2,20,53,230,247,151,248};
static const uint16_t pkw_base[16] = {3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264};
static const uint8_t pkw_extra[16] = {0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8};

typedef struct { const uint8_t *d; uint32_t n, pos; uint32_t buf; unsigned cnt; int err; } pkw_bits;

static unsigned pkw_get(pkw_bits *b, unsigned need) {
    uint32_t v = b->buf;
    while (b->cnt < need) {
        if (b->pos >= b->n) { b->err = 1; return 0; }
        v |= (uint32_t)b->d[b->pos++] << b->cnt;
        b->cnt += 8;
    }
    b->buf = v >> need;
    b->cnt -= need;
    return v & ((1u << need) - 1u);
}

static int pkw_construct(pkw_huff *h, const uint8_t *len, unsigned n);

static int pkw_construct_rle(pkw_huff *h, const uint8_t *rep, unsigned nrep,
                             uint8_t *scratch, unsigned nscratch) {
    unsigned i, k = 0;
    for (i = 0; i < nrep; ++i) {
        unsigned left = (rep[i] >> 4) + 1u, len = rep[i] & 15u;
        while (left--) { if (k < nscratch) scratch[k] = (uint8_t)len; k++; }
    }
    if (k > nscratch) return -1;
    return pkw_construct(h, scratch, k);
}

static int pkw_construct(pkw_huff *h, const uint8_t *len, unsigned n) {
    unsigned l, s, offs[PKW_MAXBITS + 2], left = 1;
    for (l = 0; l <= PKW_MAXBITS; ++l) h->count[l] = 0;
    for (s = 0; s < n; ++s) h->count[len[s]]++;
    for (l = 1; l <= PKW_MAXBITS; ++l) { left <<= 1; left -= h->count[l]; if ((int)left < 0) return -1; }
    offs[1] = 0;
    for (l = 1; l < PKW_MAXBITS; ++l) offs[l + 1] = offs[l] + h->count[l];
    for (s = 0; s < n; ++s) if (len[s]) h->symbol[offs[len[s]]++] = (uint16_t)s;
    return 0;
}

static int pkw_decode(pkw_bits *b, const pkw_huff *h) {
    unsigned len = 1, code = 0, first = 0, index = 0;
    for (;;) {
        code |= pkw_get(b, 1) ^ 1u;
        { unsigned c = h->count[len];
          if (code < first + c) return h->symbol[index + (code - first)];
          index += c; first = (first + c) << 1; code <<= 1; }
        if (++len > PKW_MAXBITS) return -1;
    }
}

/* payload = bytes after the compression mask byte; Lake City DCL: lit, dict, bitstream. */
static int pkw_explode(uint8_t *out, uint32_t *out_len, const uint8_t *payload,
                       uint32_t payload_len, uint32_t dst_cap) {
    static pkw_huff lit_h, len_h, dist_h;
    static int ready;
    pkw_bits b;
    uint32_t used = 0;
    int lit, dict;
    if (!ready) {
        uint8_t scratch[320];
        if (pkw_construct_rle(&lit_h, pkw_litrep, sizeof pkw_litrep, scratch, 256) < 0 ||
            pkw_construct_rle(&len_h, pkw_lenrep, sizeof pkw_lenrep, scratch, 16) < 0 ||
            pkw_construct_rle(&dist_h, pkw_distrep, sizeof pkw_distrep, scratch, 64) < 0)
            return -1;
        ready = 1;
    }
    if (payload_len < 2) return -1;
    b.d = payload; b.n = payload_len; b.pos = 0; b.buf = 0; b.cnt = 0; b.err = 0;
    lit = (int)pkw_get(&b, 8);
    dict = (int)pkw_get(&b, 8);
    if (b.err || lit > 1 || dict < 4 || dict > 6) return -1;
    for (;;) {
        if (pkw_get(&b, 1)) {
            int sym = pkw_decode(&b, &len_h);
            uint32_t len, dist;
            if (sym < 0) return -1;
            len = pkw_base[sym] + pkw_get(&b, pkw_extra[sym]);
            if (len == 519) break;
            sym = pkw_decode(&b, &dist_h);
            if (sym < 0) return -1;
            dist = (uint32_t)sym << (len == 2 ? 2 : dict);
            dist += pkw_get(&b, len == 2 ? 2 : dict);
            dist += 1;
            if (b.err || dist > used || used + len > dst_cap) return -1;
            while (len--) { out[used] = out[used - dist]; used++; }
        } else {
            int sym = lit ? pkw_decode(&b, &lit_h) : (int)pkw_get(&b, 8);
            if (sym < 0 || b.err || used >= dst_cap) return -1;
            out[used++] = (uint8_t)sym;
        }
    }
    *out_len = used;
    return 1;
}

int storm_scomp_decompress(void *dst, uint32_t *dst_len, const void *src, uint32_t src_len) {
    uint32_t cap;
    const uint8_t *in=(const uint8_t *)src;
    if (!dst || !dst_len || !src || src_len == 0) return -1;
    cap = *dst_len;
    if (cap < src_len) return -1;
    if (src_len == cap) {
        if (dst != src) memmove(dst, src, src_len);
        return 1;
    }
    if (in[0] == 0) {
        uint32_t n = src_len - 1u;
        if (n && dst != in + 1) memmove(dst, in + 1, n);
        *dst_len = n;
        return 1;
    }
    if (in[0] == 0x08) {
        uint32_t n=0;
        int r=pkw_explode((uint8_t *)dst,&n,in+1,src_len-1,cap);
        if (r >= 0) { *dst_len=n; return r; }
    }
    if ((in[0] == 0x40 || in[0] == 0x80) && src_len >= 4) {
        uint32_t n=0;
        int r=adpcm_decode(dst,&n,in+1,src_len-1,cap,in[0]==0x40 ? 1u : 2u);
        if (r >= 0) { *dst_len=n; return r; }
    }
    return -1; /* unsupported/malformed codecs must use the original lifted routine */
}

#ifndef SC_SCOMP_TEST_ONLY
static unsigned append_u32(char *p, unsigned v) {
    char r[10]; unsigned n=0, i;
    do { r[n++]=(char)('0'+v%10u); v/=10u; } while(v);
    for(i=0;i<n;i++) p[i]=r[n-1u-i];
    return n;
}
static void dump_file(const char *path, const void *data, uint32_t size) {
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0) { if(size) (void)write(fd,data,size); (void)close(fd); }
}
static void dump_call(const char *dir, uint32_t mask, const void *src, uint32_t src_len,
                      uint32_t dst_cap, int ret, uint32_t dst_len, const void *dst) {
    char path[1024], meta[160]; unsigned n=0; uint32_t id=__sync_fetch_and_add(&dump_serial,1u);
    int i; static const char hex[]="0123456789abcdef";
    (void)mkdir(dir,0755);
    for(i=0;dir[i] && n<900;i++) path[n++]=dir[i];
    if(n>=900) return;
    path[n++]='/';
    for(i=7;i>=0;i--) path[n++]=hex[(id>>(i*4))&15u];
    for(i=0;i<8;i++) path[n-8+i]=hex[(id>>(28-4*i))&15u];
    path[n]=0;
    { unsigned m=0; const char *keys[]={"mask=0x","\nsrc_len=","\ndst_cap=","\nret=","\ndst_len="};
      unsigned vals[]={0,src_len,dst_cap,(uint32_t)ret,dst_len};
      for(i=0;i<5;i++) { unsigned j; for(j=0;keys[i][j];j++) meta[m++]=(char)keys[i][j];
        if(i==0) { meta[m++]=hex[(mask>>4)&15u]; meta[m++]=hex[mask&15u]; }
        else m+=append_u32(meta+m,vals[i]); }
      meta[m++]='\n';
      { char suffix[]=".meta"; for(i=0;i<5;i++) path[n+i]=suffix[i]; path[n+5]=0; dump_file(path,meta,m); }
    }
    { char suffix[]=".src"; for(i=0;i<4;i++) path[n+i]=suffix[i]; path[n+4]=0; dump_file(path,src,src_len); }
    if(dst_len<=dst_cap) { char suffix[]=".dst"; for(i=0;i<4;i++) path[n+i]=suffix[i]; path[n+4]=0; dump_file(path,dst,dst_len); }
}
#endif

#ifndef SC_SCOMP_TEST_ONLY
/* Replaces the stdcall frame [return][dst][dst_len][src][src_len] with the equivalent guest
 * return slot expected by reasm_extern, then either handles a proven copy case or calls the
 * original lifted entry at its original VA. */
void storm_552_arm_body(_cpu *cpu) {
    uint32_t sp, retva, a[4], cap = 0, out_len = 0;
    void *dst;
    uint32_t *dst_len;
    const void *src;
    uint32_t src_len, mask = 0;
    int result = -1, status = 0;
    const char *dump_dir;

    if (!cpu) return;
    sp = cpu->_esp;
    retva = cpu_llasm_read32(sp);
    a[0] = cpu_llasm_read32(sp + 4u);
    a[1] = cpu_llasm_read32(sp + 8u);
    a[2] = cpu_llasm_read32(sp + 12u);
    a[3] = cpu_llasm_read32(sp + 16u);
    cpu_llasm_write32(sp + 16u, retva);
    cpu->_esp = sp + 16u;

    dst = (void *)(uintptr_t)a[0];
    dst_len = (uint32_t *)(uintptr_t)a[1];
    src = (const void *)(uintptr_t)a[2];
    src_len = a[3];
    if (dst_len) cap = *dst_len;
    if (src && src_len) mask = *(const uint8_t *)src;

    if (dst && dst_len && src && src_len && src_len <= cap &&
        (src_len == cap || mask == 0 || mask == 0x40 || mask == 0x80)) {
        result = storm_scomp_decompress(dst, dst_len, src, src_len);
        if (result >= 0) out_len = *dst_len;
    }
    if (result < 0) {
        result = arm_guest_call_fn((void *)orig_storm_552, 4, a, &status);
        if (status != 0) result = 0;
        if (dst_len) out_len = *dst_len;
    }
    cpu->_eax = (uint32_t)result;
    dump_dir = getenv("SC_SCOMP_DUMP");
    if (dump_dir && *dump_dir && src && src_len && dst && dst_len && out_len <= cap)
        dump_call(dump_dir, mask, src, src_len, cap, result, out_len, dst);
}
#endif
