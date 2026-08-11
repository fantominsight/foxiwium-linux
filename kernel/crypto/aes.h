#pragma once
#include <stdint.h>
#include <stddef.h>

// AES-128 block cipher (FIPS-197) + AES-GCM AEAD (SP 800-38D).
// Header-only, no dynamic allocation. GCM uses only the AES encrypt path.
namespace crypto {

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

struct Aes128Key {
    uint32_t rk[44]; // 11 round keys
};

static inline uint32_t aes_rotr8(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static inline void aes128_key_expand(const uint8_t* key, Aes128Key* k) {
    for (int i = 0; i < 4; i++) {
        k->rk[i] = ((uint32_t)key[i * 4] << 24) | ((uint32_t)key[i * 4 + 1] << 16) |
                   ((uint32_t)key[i * 4 + 2] << 8) | (uint32_t)key[i * 4 + 3];
    }
    for (int i = 4; i < 44; i++) {
        uint32_t t = k->rk[i - 1];
        if (i % 4 == 0) {
            // RotWord + SubWord + Rcon (rcon = 2^(round-1) in GF(2^8))
            static const uint8_t aes_rcon[11] = {
                0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
            };
            t = ((uint32_t)aes_sbox[(t >> 16) & 0xff] << 24) |
                ((uint32_t)aes_sbox[(t >> 8) & 0xff] << 16) |
                ((uint32_t)aes_sbox[t & 0xff] << 8) |
                (uint32_t)aes_sbox[(t >> 24) & 0xff];
            t ^= (uint32_t)aes_rcon[i / 4] << 24;
        }
        k->rk[i] = k->rk[i - 4] ^ t;
    }
}

static inline void aes128_encrypt_block(const Aes128Key* k, const uint8_t in[16], uint8_t out[16]) {
    uint32_t s[4];
    for (int i = 0; i < 4; i++) {
        s[i] = ((uint32_t)in[i * 4] << 24) | ((uint32_t)in[i * 4 + 1] << 16) |
               ((uint32_t)in[i * 4 + 2] << 8) | (uint32_t)in[i * 4 + 3];
        s[i] ^= k->rk[i];
    }
    for (int r = 1; r < 10; r++) {
        uint8_t b[16];
        for (int i = 0; i < 16; i++) b[i] = aes_sbox[(s[i >> 2] >> (24 - (i & 3) * 8)) & 0xff];
        uint8_t r0[16];
        for (int c = 0; c < 4; c++) {
            for (int rr = 0; rr < 4; rr++) {
                r0[c * 4 + rr] = b[((c + rr) & 3) * 4 + rr];
            }
        }
        for (int c = 0; c < 4; c++) {
            uint32_t t0 = (uint32_t)r0[c * 4];
            uint32_t t1 = (uint32_t)r0[c * 4 + 1];
            uint32_t t2 = (uint32_t)r0[c * 4 + 2];
            uint32_t t3 = (uint32_t)r0[c * 4 + 3];
            uint32_t x0 = ((t0 << 1) ^ ((t0 & 0x80) ? 0x1b : 0)) & 0xff;
            uint32_t x1 = ((t1 << 1) ^ ((t1 & 0x80) ? 0x1b : 0)) & 0xff;
            uint32_t x2 = ((t2 << 1) ^ ((t2 & 0x80) ? 0x1b : 0)) & 0xff;
            uint32_t x3 = ((t3 << 1) ^ ((t3 & 0x80) ? 0x1b : 0)) & 0xff;
            s[c] = (uint32_t)(x0 ^ x1 ^ t1 ^ t2 ^ t3) << 24 |
                   (uint32_t)(t0 ^ x1 ^ x2 ^ t2 ^ t3) << 16 |
                   (uint32_t)(t0 ^ t1 ^ x2 ^ x3 ^ t3) << 8 |
                   (uint32_t)(x0 ^ t0 ^ t1 ^ t2 ^ x3);
            s[c] ^= k->rk[r * 4 + c];
        }
    }
    // final round: SubBytes, ShiftRows, AddRoundKey
    uint8_t b[16];
    for (int i = 0; i < 16; i++) b[i] = aes_sbox[(s[i >> 2] >> (24 - (i & 3) * 8)) & 0xff];
    uint8_t r0[16];
    for (int c = 0; c < 4; c++) {
        for (int rr = 0; rr < 4; rr++) {
            r0[c * 4 + rr] = b[((c + rr) & 3) * 4 + rr];
        }
    }
    for (int i = 0; i < 4; i++) {
        uint32_t v = ((uint32_t)r0[i * 4] << 24) | ((uint32_t)r0[i * 4 + 1] << 16) |
                     ((uint32_t)r0[i * 4 + 2] << 8) | (uint32_t)r0[i * 4 + 3];
        v ^= k->rk[40 + i];
        out[i * 4] = (uint8_t)(v >> 24);
        out[i * 4 + 1] = (uint8_t)(v >> 16);
        out[i * 4 + 2] = (uint8_t)(v >> 8);
        out[i * 4 + 3] = (uint8_t)v;
    }
}

// ---------------- GCM ----------------
static inline uint64_t gcm_bs64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static inline void gcm_put64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; }
}

// Multiply two 128-bit values in GF(2^128) with the GCM polynomial.
// NIST SP 800-38D Algorithm 2: right-shift of V with reduction R = 0xE1<<120.
static inline void gcm_mult(uint8_t* x, const uint8_t* y) {
    uint8_t z[16] = {0};
    uint8_t v[16];
    for (int i = 0; i < 16; i++) v[i] = y[i];
    for (int i = 0; i < 128; i++) {
        int bit = (x[i >> 3] >> (7 - (i & 7))) & 1;
        if (bit) for (int j = 0; j < 16; j++) z[j] ^= v[j];
        // v >>= 1 (MSB-first)
        uint8_t lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) {
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        }
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    for (int i = 0; i < 16; i++) x[i] = z[i];
}

// GHASH over AAD || pad(AAD) || CT || pad(CT) || [len(AAD)64 || len(CT)64]
static inline void gcm_ghash(const uint8_t* h, const uint8_t* aad, uint32_t aadlen,
                             const uint8_t* ct, uint32_t ctlen, uint8_t out[16]) {
    uint8_t y[16] = {0};
    uint8_t blk[16];
    uint32_t i = 0;
    while (i + 16 <= aadlen) {
        for (int j = 0; j < 16; j++) y[j] ^= aad[i + j];
        gcm_mult(y, h);
        i += 16;
    }
    if (aadlen & 15) {
        uint32_t rem = aadlen & 15;
        for (int j = 0; j < 16; j++) blk[j] = 0;
        for (uint32_t j = 0; j < rem; j++) blk[j] = aad[i + j];
        for (int j = 0; j < 16; j++) y[j] ^= blk[j];
        gcm_mult(y, h);
    }
    i = 0;
    while (i + 16 <= ctlen) {
        for (int j = 0; j < 16; j++) y[j] ^= ct[i + j];
        gcm_mult(y, h);
        i += 16;
    }
    if (ctlen & 15) {
        uint32_t rem = ctlen & 15;
        for (int j = 0; j < 16; j++) blk[j] = 0;
        for (uint32_t j = 0; j < rem; j++) blk[j] = ct[i + j];
        for (int j = 0; j < 16; j++) y[j] ^= blk[j];
        gcm_mult(y, h);
    }
    for (int j = 0; j < 16; j++) blk[j] = 0;
    gcm_put64(blk, (uint64_t)aadlen * 8);
    gcm_put64(blk + 8, (uint64_t)ctlen * 8);
    for (int j = 0; j < 16; j++) y[j] ^= blk[j];
    gcm_mult(y, h);
    for (int i2 = 0; i2 < 16; i2++) out[i2] = y[i2];
}

// AES-128-GCM encrypt. out may equal in. tag 16 bytes.
static inline void aes128_gcm_encrypt(const uint8_t* key, const uint8_t* iv, uint32_t ivlen,
                                      const uint8_t* aad, uint32_t aadlen,
                                      const uint8_t* in, uint32_t inlen,
                                      uint8_t* out, uint8_t tag[16]) {
    Aes128Key k;
    aes128_key_expand(key, &k);
    uint8_t h[16] = {0};
    aes128_encrypt_block(&k, h, h);

    // J0: if ivlen==12, IV || 0x00000001; else GHASH(pad(IV)||len)
    uint8_t j0[16];
    if (ivlen == 12) {
        for (uint32_t i = 0; i < 12; i++) j0[i] = iv[i];
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    } else {
        gcm_ghash(h, iv, ivlen, nullptr, 0, j0);
        // gcm_ghash with aad=iv, ct=nullptr, but ctlen must be 0 and the
        // function copies nothing when ct is null. OK.
    }

    // CTR keystream: E(K, inc32(J0)) starting at J0+1
    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = j0[i];
    uint32_t n = 0;
    while (n < inlen) {
        // increment counter (last 32 bits)
        for (int i = 15; i >= 12; i--) {
            if (++ctr[i] != 0) break;
        }
        uint8_t ks[16];
        aes128_encrypt_block(&k, ctr, ks);
        uint32_t take = inlen - n < 16 ? inlen - n : 16;
        for (uint32_t j = 0; j < take; j++) out[n + j] = in[n + j] ^ ks[j];
        n += take;
    }

    // Tag: GHASH(AAD, CT) xor E(K, J0)
    uint8_t s[16];
    gcm_ghash(h, aad, aadlen, out, inlen, s);
    uint8_t ekj0[16];
    aes128_encrypt_block(&k, j0, ekj0);
    for (int i = 0; i < 16; i++) tag[i] = s[i] ^ ekj0[i];
}

// AES-128-GCM decrypt. out may equal in. Returns 0 on success, -1 if tag mismatch.
static inline int aes128_gcm_decrypt(const uint8_t* key, const uint8_t* iv, uint32_t ivlen,
                                     const uint8_t* aad, uint32_t aadlen,
                                     const uint8_t* in, uint32_t inlen,
                                     const uint8_t* tag, uint8_t* out) {
    Aes128Key k;
    aes128_key_expand(key, &k);
    uint8_t h[16] = {0};
    aes128_encrypt_block(&k, h, h);
    uint8_t j0[16];
    if (ivlen == 12) {
        for (uint32_t i = 0; i < 12; i++) j0[i] = iv[i];
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    } else {
        gcm_ghash(h, iv, ivlen, nullptr, 0, j0);
    }
    uint8_t s[16];
    gcm_ghash(h, aad, aadlen, in, inlen, s);
    uint8_t ekj0[16];
    aes128_encrypt_block(&k, j0, ekj0);
    for (int i = 0; i < 16; i++) s[i] ^= ekj0[i];
    int bad = 0;
    for (int i = 0; i < 16; i++) bad |= tag[i] ^ s[i];
    if (bad) return -1;

    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = j0[i];
    uint32_t n = 0;
    while (n < inlen) {
        for (int i = 15; i >= 12; i--) {
            if (++ctr[i] != 0) break;
        }
        uint8_t ks[16];
        aes128_encrypt_block(&k, ctr, ks);
        uint32_t take = inlen - n < 16 ? inlen - n : 16;
        for (uint32_t j = 0; j < take; j++) out[n + j] = in[n + j] ^ ks[j];
        n += take;
    }
    return 0;
}

} // namespace crypto
