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

/* IDA: sub_1501C0F0 @ 0x1501C0F0, wrappers @ 0x1501C6E0 (mono, mask 0x40) and
 * 0x1501C660 (stereo, mask 0x80). Payload = reserved byte, step shift, `channels` signed
 * 16-bit initial samples, then ADPCM codes.
 *
 * Channel selection is NOT a per-code toggle: the binary walks a byte stream and switches
 * channel at the TOP of every iteration (0x1501C163 `if (channels == 2) channel = !channel`),
 * plus one extra switch inside the 0x81/0x82 control branches (0x1501C1AF/0x1501C1A2). Control
 * codes therefore end up NOT advancing the channel (two switches cancel), while 0x80 and every
 * data code do. Verified byte-exact on all 428 stereo sectors of the capture; a plain per-code
 * toggle diverges on the first 0x81 code after the header. */
typedef struct {
    uint8_t *dst;
    uint32_t cap, used;
    unsigned channels, channel;
    unsigned hdr, hdr_need;
    uint8_t header[6];
    uint32_t step[2];
    int32_t sample[2];
} adpcm_state;

static void adpcm_init(adpcm_state *s, void *dst, uint32_t cap, unsigned channels) {
    s->dst = (uint8_t *)dst;
    s->cap = cap;
    s->used = 0;
    s->channels = channels;
    s->channel = channels ? channels - 1u : 0; /* IDA 0x1501C150: v10 = channels - 1 */
    s->hdr = 0;
    s->hdr_need = 2u + 2u * channels;
    s->step[0] = s->step[1] = 44;
    s->sample[0] = s->sample[1] = 0;
}

static void adpcm_emit(adpcm_state *s, unsigned idx) {
    if (s->used + 2u > s->cap) return;
    s->dst[s->used++] = (uint8_t)s->sample[idx];
    s->dst[s->used++] = (uint8_t)((uint32_t)s->sample[idx] >> 8);
}

/* Feeds one payload byte. Returns 0 to continue, 1 when the output buffer is full, -1 on a
 * malformed header. */
static int adpcm_feed(adpcm_state *s, uint8_t byte) {
    if (s->hdr < s->hdr_need) {
        s->header[s->hdr++] = byte;
        if (s->hdr == s->hdr_need) {
            for (unsigned c = 0; c < s->channels; ++c)
                s->sample[c] = (int16_t)((uint16_t)s->header[2u + 2u * c] |
                                         ((uint16_t)s->header[3u + 2u * c] << 8));
            for (unsigned c = 0; c < s->channels; ++c) {
                adpcm_emit(s, c);
                if (s->cap - s->used < 2u) return 1;
            }
        }
        return 0;
    }
    if (s->channels == 2) s->channel = s->channel ? 0u : 1u; /* top-of-loop switch */
    { unsigned idx = s->channel;
      if (byte & 0x80) {
        unsigned control = byte & 0x7fu;
        if (control == 0) {                       /* 0x80: hold sample, step -1 */
            if (s->step[idx]) --s->step[idx];
            adpcm_emit(s, idx);
            if (s->cap - s->used < 2u) return 1;
        } else if (control == 1) {                /* 0x81: step +8, channel switched twice */
            s->step[idx] = s->step[idx] > 80u ? 88u : s->step[idx] + 8u;
            if (s->channels == 2) s->channel = s->channel ? 0u : 1u;
        } else if (control == 2) {                /* 0x82: step -8, channel switched twice */
            s->step[idx] = s->step[idx] < 8u ? 0u : s->step[idx] - 8u;
            if (s->channels == 2) s->channel = s->channel ? 0u : 1u;
        }
      } else {
        unsigned ix = s->step[idx];
        uint32_t delta = adpcm_step[ix] >> (s->header[1] & 31u);
        for (unsigned b = 0; b < 6; ++b)
            if (byte & (1u << b)) delta += adpcm_step[ix] >> b;
        if (byte & 0x40) { int32_t v = s->sample[idx] - (int32_t)delta;
                           s->sample[idx] = v <= -32768 ? -32768 : v; }
        else             { int32_t v = s->sample[idx] + (int32_t)delta;
                           s->sample[idx] = v >= 32767 ? 32767 : v; }
        adpcm_emit(s, idx);
        if (s->cap - s->used < 2u) return 1;
        { int next = (int)ix + adpcm_adjust[byte & 31u];
          s->step[idx] = (uint32_t)(next < 0 ? 0 : next > 88 ? 88 : next); }
      }
    }
    return 0;
}

static int adpcm_decode(void *dst, uint32_t *out_len, const uint8_t *payload,
                        uint32_t payload_len, uint32_t dst_cap, unsigned channels) {
    adpcm_state s;
    if (!dst || !out_len || !payload || payload_len < 2u + 2u * channels) return -1;
    adpcm_init(&s, dst, dst_cap, channels);
    for (uint32_t pos = 0; pos < payload_len; ++pos) {
        int r = adpcm_feed(&s, payload[pos]);
        if (r != 0) { *out_len = s.used; return 1; }
    }
    *out_len = s.used;
    return 1;
}

/* --- Adaptive Huffman (MPQ mask 0x01) ----------------------------------------------------
 * IDA: #552 dispatches 0x01 -> sub_1501D090 @0x1501D090, which initialises the tree with
 * sub_1501CDB0 @0x1501CDB0 (515 items, weights from 0x15050030 + 258*type via
 * sub_1501C980 @0x1501C980) and then runs the decode loop sub_1501CF40 @0x1501CF40 with the
 * FGK update sub_15003E30 @0x15003E30 and the escape/branch helper sub_1501C7F0 @0x1501C7F0.
 * The fast 7-bit quick-link cache of sub_15004000 @0x15004000 is a pure optimisation (it is
 * invalidated whenever the update pass bumps MinValidValue at tree+4), so this port walks the
 * tree directly and gets the same symbols.
 * Semantics of the original, all reproduced here:
 *   - payload[0] = data type; the bit stream starts at payload[1], LSB-first.
 *   - symbols 0..255 are literal bytes, 256 is EOS, 257 is the NYT escape followed by the raw
 *     byte in the next 8 bits.
 *   - weights are only updated on escapes for every non-sparse type (0x1501CF40: the update
 *     after the escape is guarded by `*tree == 0`, and the update after an ordinary symbol by
 *     `*tree != 0`, where `*tree` is set to (type == 0)). Sparse type 0 updates after output.
 *   - decoding stops at EOS or when the destination capacity is reached.
 * StormLib (src/huffman/huff.cpp) implements the same algorithm; the weight rows were compared
 * byte for byte with DataDistribution_* (identical, 0 differences) and the tree/list helpers
 * match CreateNewItem/FixupItemPosByWeight/IncWeightsAndRebalance/InsertNewBranchAndRebalance.
 */
/* Byte-to-weight tables: 9 rows x 258 bytes, read-only dump of VA 0x15050030 in
 * storm.orig.dll. Row = Huffman data type (first payload byte for mask 0x01). The binary
 * indexes 258-byte rows but the tree builder only consumes the first 256 entries. */
static const uint8_t huff_weights[9][258] = {
  { /* type 0 (sparse) */
    10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,2,0,0,
  },
  { /* type 1 (binary) */
    84,22,22,13,12,8,6,5,6,5,6,3,4,4,3,5,14,11,
    20,19,19,9,11,6,5,4,3,2,3,2,2,2,13,7,9,6,
    6,4,3,2,4,3,3,3,3,3,2,2,9,6,4,4,4,4,
    3,2,3,2,2,2,2,3,2,4,8,3,4,7,9,5,3,3,
    3,3,2,2,2,3,2,2,3,2,2,2,2,2,2,2,2,1,
    1,1,2,1,2,2,6,10,8,8,6,7,4,3,4,4,2,2,
    4,2,3,3,4,3,7,7,9,6,4,3,3,2,1,2,2,2,
    2,2,10,2,2,3,2,2,1,1,2,2,2,6,3,5,2,3,
    2,1,1,1,1,1,1,1,1,1,1,2,3,1,1,1,2,1,
    1,1,1,1,1,2,4,4,4,7,9,8,12,2,1,1,1,1,
    1,1,1,1,1,1,1,1,2,1,1,3,4,1,2,4,5,1,
    1,1,1,1,1,1,2,1,1,1,4,1,1,1,1,1,2,1,
    1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,3,1,
    1,1,1,1,1,1,2,1,1,1,1,1,1,2,2,1,1,2,
    2,2,6,75,0,0,
  },
  { /* type 2 (text) */
    0,0,0,0,0,0,0,0,0,3,39,0,0,35,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,1,1,1,
    1,1,1,1,2,2,1,1,6,14,16,4,6,8,5,4,4,3,
    3,2,2,3,3,1,1,2,1,1,1,4,2,4,2,2,2,1,
    1,4,1,1,2,3,3,2,3,1,3,6,4,1,1,1,1,1,
    1,2,1,2,1,1,1,41,7,22,18,64,10,10,17,37,1,3,
    23,16,38,42,16,1,35,35,47,16,6,7,2,9,1,1,1,1,
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
  { /* type 3 (general) */
    255,11,7,5,11,2,2,2,6,2,2,1,4,2,1,3,9,1,
    1,1,3,4,1,1,2,1,1,1,2,1,1,1,5,1,1,1,
    13,1,1,1,1,1,1,1,1,1,1,1,2,1,1,3,1,1,
    1,1,1,1,1,2,1,1,1,1,10,4,2,1,6,3,2,1,
    1,1,1,1,3,1,1,1,5,2,3,4,3,3,3,2,1,1,
    1,2,1,2,3,3,1,3,1,1,2,5,1,1,4,3,5,1,
    3,1,3,3,2,1,4,3,10,6,1,1,1,1,1,1,1,1,
    1,1,2,2,1,10,2,5,1,1,2,7,2,23,1,5,1,1,
    14,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,6,2,1,4,5,1,
    1,2,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,7,1,
    1,2,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,
    1,1,1,17,0,0,
  },
  { /* type 4 (ADPCM 4-bit) */
    255,251,152,154,132,133,99,100,62,62,34,34,19,19,24,23,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
  { /* type 5 (ADPCM 6-bit) */
    255,241,157,158,154,155,154,151,147,147,140,142,134,136,128,130,124,124,
    114,115,105,107,95,96,85,86,74,75,64,65,55,55,47,47,39,39,
    33,33,27,28,23,23,19,19,16,16,13,13,11,11,9,9,8,8,
    7,7,6,5,5,4,4,4,25,24,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
  { /* type 6 (stereo 3-bit) */
    195,203,245,65,255,123,247,33,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,191,204,242,64,253,124,247,34,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,122,70,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
  { /* type 7 (stereo 4-bit) */
    195,217,239,61,249,124,233,30,253,171,241,44,252,91,254,23,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,189,217,236,61,245,125,232,29,
    251,174,240,44,251,92,255,24,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,112,108,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
  { /* type 8 (stereo 5-bit) */
    186,197,218,51,227,109,216,24,229,148,218,35,223,74,209,16,238,175,
    228,44,234,90,222,21,244,135,233,33,246,67,252,18,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,176,199,216,51,227,107,214,24,
    231,149,216,35,219,73,208,17,233,178,226,43,232,92,221,21,241,135,
    231,32,247,68,255,19,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,95,158,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,
  },
};

#define HUFF_ITEM_NONE (-1)
#define HUFF_ITEM_COUNT 515

typedef struct {
    int32_t next, prev;      /* doubly linked list, sorted descending by weight from head.next */
    int32_t value;           /* 0..255 literal, 256 EOS, 257 NYT */
    int32_t weight;
    int32_t parent, child;
} huff_item;

/* Static storage: the guest runtime and the test harness are single threaded, and the original
 * object is a 13.7 KiB heap block reused for every call (0x1501CDB0 clears it in place). */
typedef struct {
    huff_item node[HUFF_ITEM_COUNT];
    int32_t items[258];      /* value -> item index; 256 = EOS, 257 = NYT (IDA tree+3099) */
    int32_t head;            /* sentinel item index, also used as the "not found" marker */
    int32_t spare;           /* next unused pool slot */
    int32_t max_weight;
} huff_tree;

static void huff_unlink(huff_tree *t, int32_t i) {
    int32_t n = t->node[i].next, p = t->node[i].prev;
    if (n != HUFF_ITEM_NONE) {
        t->node[p].next = n;
        t->node[n].prev = p;
        t->node[i].next = t->node[i].prev = HUFF_ITEM_NONE;
    }
}

static void huff_insert_after(huff_tree *t, int32_t at, int32_t i) {
    int32_t n;
    huff_unlink(t, i);
    n = t->node[at].next;
    t->node[i].next = n;
    t->node[i].prev = t->node[n].prev;
    t->node[n].prev = i;
    t->node[at].next = i;
}

static int32_t huff_higher(huff_tree *t, int32_t from, int32_t weight) {
    int32_t n = from;
    while (n != t->head) {
        if (t->node[n].weight >= weight) return n;
        n = t->node[n].prev;
    }
    return t->head;
}

static int32_t huff_new_item(huff_tree *t, int32_t value, int32_t weight, int after_head) {
    int32_t i;
    if (t->spare >= HUFF_ITEM_COUNT) return HUFF_ITEM_NONE;
    i = t->spare++;
    t->node[i].value = value;
    t->node[i].weight = weight;
    t->node[i].parent = HUFF_ITEM_NONE;
    t->node[i].child = HUFF_ITEM_NONE;
    t->node[i].next = t->node[i].prev = HUFF_ITEM_NONE;
    if (after_head) huff_insert_after(t, t->head, i);            /* at the top of the list */
    else            huff_insert_after(t, t->node[t->head].prev, i); /* at the end of the list */
    return i;
}

static void huff_fixup(huff_tree *t, int32_t i) {
    if (t->node[i].weight < t->max_weight)
        huff_insert_after(t, huff_higher(t, t->node[t->head].prev, t->node[i].weight), i);
    else
        t->max_weight = t->node[i].weight;
}

/* IDA 0x15003E30: climb to the root, +1 weight, keep the list sorted, swap subtrees. */
static void huff_increment(huff_tree *t, int32_t i) {
    while (i != HUFF_ITEM_NONE) {
        int32_t higher, child_hi, parent, child_lo;
        t->node[i].weight++;
        higher = huff_higher(t, t->node[i].prev, t->node[i].weight);
        child_hi = t->node[higher].next;
        if (child_hi != i) {
            huff_unlink(t, child_hi);
            huff_insert_after(t, i, child_hi);
            huff_unlink(t, i);
            huff_insert_after(t, higher, i);
            parent = t->node[i].parent;
            child_lo = t->node[t->node[child_hi].parent].child;
            if (t->node[parent].child == i) t->node[parent].child = child_hi;
            if (child_lo == child_hi) t->node[t->node[child_hi].parent].child = i;
            parent = t->node[i].parent;
            t->node[i].parent = t->node[child_hi].parent;
            t->node[child_hi].parent = parent;
        }
        i = t->node[i].parent;
    }
}

/* IDA 0x1501C980: build the tree from the weight row of the payload data type. */
static int huff_build(huff_tree *t, unsigned type) {
    int32_t i, child_lo;
    t->head = HUFF_ITEM_COUNT - 1;
    t->node[t->head].next = t->node[t->head].prev = t->head;
    t->node[t->head].parent = t->node[t->head].child = HUFF_ITEM_NONE;
    t->spare = 0;
    t->max_weight = 0;
    for (i = 0; i < 258; ++i) t->items[i] = HUFF_ITEM_NONE;
    for (i = 0; i < 256; ++i) {
        int32_t w = huff_weights[type][i];
        if (!w) continue;
        t->items[i] = huff_new_item(t, i, w, 1);
        if (t->items[i] == HUFF_ITEM_NONE) return -1;
        huff_fixup(t, t->items[i]);
    }
    t->items[256] = huff_new_item(t, 256, 1, 0);   /* EOS */
    t->items[257] = huff_new_item(t, 257, 1, 0);   /* NYT */
    if (t->items[256] == HUFF_ITEM_NONE || t->items[257] == HUFF_ITEM_NONE) return -1;
    child_lo = t->node[t->head].prev;
    while (child_lo != t->head) {
        int32_t child_hi = t->node[child_lo].prev, parent;
        if (child_hi == t->head) break;
        parent = huff_new_item(t, 0, t->node[child_hi].weight + t->node[child_lo].weight, 1);
        if (parent == HUFF_ITEM_NONE) return -1;
        t->node[child_lo].parent = parent;
        t->node[child_hi].parent = parent;
        t->node[parent].child = child_lo;
        huff_fixup(t, parent);
        child_lo = t->node[child_hi].prev;
    }
    return 0;
}

/* IDA 0x1501C7F0: replace the NYT leaf by a branch (old NYT + new literal) and update. */
static int huff_new_branch(huff_tree *t, int32_t value) {
    int32_t last = t->node[t->head].prev, high, low;
    high = huff_new_item(t, t->node[last].value, t->node[last].weight, 0);
    if (high == HUFF_ITEM_NONE) return -1;
    low = huff_new_item(t, value, 0, 0);
    if (low == HUFF_ITEM_NONE) return -1;
    t->node[high].parent = last;
    t->node[low].parent = last;
    t->node[last].child = low;
    t->items[t->node[high].value] = high;
    t->items[value] = low;
    huff_increment(t, low);
    return 0;
}

typedef struct { const uint8_t *d; uint32_t n, pos; uint32_t buf; unsigned cnt; int err; } huff_bits;

static unsigned huff_get1(huff_bits *b) {
    if (b->cnt == 0) {
        if (b->pos >= b->n) { b->err = 1; return 0; }
        b->buf = b->d[b->pos++];
        b->cnt = 8;
    }
    { unsigned v = b->buf & 1u; b->buf >>= 1; b->cnt--; return v; }
}

static unsigned huff_get8(huff_bits *b) {
    if (b->cnt < 8) {
        if (b->pos >= b->n) { b->err = 1; return 0; }
        b->buf |= (uint32_t)b->d[b->pos++] << b->cnt;
        b->cnt += 8;
    }
    { unsigned v = b->buf & 0xffu; b->buf >>= 8; b->cnt -= 8; return v; }
}

/* IDA 0x1501CF40 decode loop. Every decoded byte is handed to `push`; the loop stops at EOS,
 * when `cap` bytes were produced, or when `push` reports that the consumer is full. */
static int huff_decode(const uint8_t *payload, uint32_t payload_len, uint32_t cap,
                       int (*push)(void *, uint8_t), void *ctx, uint32_t *out_len) {
    static huff_tree tree;
    huff_bits bits;
    uint32_t produced = 0;
    unsigned type, sparse;
    if (!payload || payload_len < 1 || !out_len) return -1;
    type = payload[0];
    if (type >= 9u) return -1;
    if (huff_build(&tree, type) < 0) return -1;
    sparse = (type == 0u);
    bits.d = payload + 1; bits.n = payload_len - 1u; bits.pos = 0; bits.buf = 0; bits.cnt = 0; bits.err = 0;
    while (produced < cap) {
        /* The root is looked up on every symbol: the rebalance pass reorders the list, so a
         * cached head.next would go stale (the original re-reads it in sub_15004000). */
        int32_t node = tree.node[tree.head].next;
        int32_t sym;
        if (node == tree.head) return -1;
        while (tree.node[node].child != HUFF_ITEM_NONE) {
            unsigned bit = huff_get1(&bits);
            if (bits.err) return -1;
            node = bit ? tree.node[tree.node[node].child].prev : tree.node[node].child;
        }
        sym = tree.node[node].value;
        if (sym == 256) break;                       /* EOS */
        if (sym == 257) {                            /* NYT escape: raw byte follows */
            unsigned raw = huff_get8(&bits);
            if (bits.err) return -1;
            if (huff_new_branch(&tree, (int32_t)raw) < 0) return -1;
            if (!sparse) huff_increment(&tree, tree.items[(int32_t)raw]);
            sym = (int32_t)raw;
        }
        if (produced >= cap) break;
        if (push(ctx, (uint8_t)sym) != 0) { *out_len = produced; return 1; }
        produced++;
        if (sparse && tree.items[sym] != HUFF_ITEM_NONE) huff_increment(&tree, tree.items[sym]);
    }
    *out_len = produced;
    return 1;
}

typedef struct { uint8_t *dst; uint32_t cap, used; } huff_plain_sink;

static int huff_plain_push(void *ctx, uint8_t byte) {
    huff_plain_sink *s = (huff_plain_sink *)ctx;
    if (s->used >= s->cap) return 1;
    s->dst[s->used++] = byte;
    return 0;
}

static int huff_plain(const uint8_t *payload, uint32_t payload_len, void *dst,
                      uint32_t *out_len, uint32_t dst_cap) {
    huff_plain_sink sink;
    sink.dst = (uint8_t *)dst; sink.cap = dst_cap; sink.used = 0;
    if (huff_decode(payload, payload_len, dst_cap, huff_plain_push, &sink, out_len) < 0) return -1;
    *out_len = sink.used;
    return 1;
}

static int huff_adpcm_push(void *ctx, uint8_t byte) {
    return adpcm_feed((adpcm_state *)ctx, byte);
}

/* Huffman -> ADPCM chain (masks 0x41 and 0x81) with the intermediate stream kept implicit: the
 * Huffman bytes are fed straight into the ADPCM state machine, so no full sector buffer is
 * needed. The decoded result is identical to materialising the intermediate buffer. */
static int huff_then_adpcm(const uint8_t *payload, uint32_t payload_len, void *dst,
                           uint32_t *out_len, uint32_t dst_cap, unsigned channels) {
    adpcm_state st;
    adpcm_init(&st, dst, dst_cap, channels);
    if (huff_decode(payload, payload_len, dst_cap, huff_adpcm_push, &st, out_len) < 0) return -1;
    *out_len = st.used;
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
    /* Huffman chains. The original order inside #552 is 0x08, 0x01, 0x80, 0x40 (table
     * 0x15050B4C..0x15050B6B walked downwards), so 0x41 = Huffman -> ADPCM mono and
     * 0x81 = Huffman -> ADPCM stereo; 0x01 alone is Huffman only. */
    if (in[0] == 0x01 && src_len >= 2) {
        uint32_t n=0;
        int r=huff_plain(in+1,src_len-1,dst,&n,cap);
        if (r >= 0) { *dst_len=n; return r; }
    }
    if ((in[0] == 0x41 || in[0] == 0x81) && src_len >= 4) {
        uint32_t n=0;
        int r=huff_then_adpcm(in+1,src_len-1,dst,&n,cap,in[0]==0x41 ? 1u : 2u);
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
/* Emits one line on stderr when a sector has to use the lifted original. Gated by
 * SC_SCOMP_TRACE so that normal runs stay silent; the isolated smoke test uses it to prove that
 * no sector falls back any more. */
static void trace_fallback(uint32_t mask, uint32_t src_len, uint32_t cap) {
    char line[96]; unsigned n = 0; static const char hex[] = "0123456789abcdef";
    const char *tag = "[scomp] fallback mask=0x";
    while (*tag) line[n++] = *tag++;
    line[n++] = hex[(mask >> 4) & 15u];
    line[n++] = hex[mask & 15u];
    tag = " src_len=";
    while (*tag) line[n++] = *tag++;
    n += append_u32(line + n, src_len);
    tag = " dst_cap=";
    while (*tag) line[n++] = *tag++;
    n += append_u32(line + n, cap);
    line[n++] = '\n';
    (void)write(2, line, n);
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
        (src_len == cap || mask == 0 || mask == 0x08 || mask == 0x40 || mask == 0x80 ||
         mask == 0x01 || mask == 0x41 || mask == 0x81)) {
        result = storm_scomp_decompress(dst, dst_len, src, src_len);
        if (result >= 0) out_len = *dst_len;
    }
    if (result < 0) {
        const char *trace = getenv("SC_SCOMP_TRACE");
        if (trace && *trace) trace_fallback(mask, src_len, cap);
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
