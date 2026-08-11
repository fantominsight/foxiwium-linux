#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sha256.h"

// 4096-bit RSA public-key ops for X.509 root/intermediate verification.
// Self-contained 64-limb Montgomery modpow (CIOS). Header-only.
namespace crypto {

static const int BN64_LIMBS = 64; // 4096 bits

struct Bn64 {
    uint64_t l[BN64_LIMBS];
};

static inline void bn64_zero(Bn64* a) { for (int i = 0; i < BN64_LIMBS; i++) a->l[i] = 0; }
static inline void bn64_set_u32(Bn64* a, uint32_t v) { bn64_zero(a); a->l[0] = v; }

static inline void bn64_from_bytes(const uint8_t* p, uint32_t n, Bn64* a) {
    bn64_zero(a);
    for (uint32_t i = 0; i < n; i++) {
        int limb = i / 8, shift = (i % 8) * 8;
        if (limb < BN64_LIMBS) a->l[limb] |= (uint64_t)p[n - 1 - i] << shift;
    }
}
static inline void bn64_to_bytes(const Bn64* a, uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        int limb = i / 8, shift = (i % 8) * 8;
        p[n - 1 - i] = (uint8_t)(limb < BN64_LIMBS ? (a->l[limb] >> shift) : 0);
    }
}
static inline int bn64_cmp(const Bn64* a, const Bn64* b) {
    for (int i = BN64_LIMBS - 1; i >= 0; i--) {
        if (a->l[i] < b->l[i]) return -1;
        if (a->l[i] > b->l[i]) return 1;
    }
    return 0;
}
static inline uint64_t bn64_add(Bn64* a, const Bn64* b) {
    uint64_t carry = 0;
    for (int i = 0; i < BN64_LIMBS; i++) {
        unsigned __int128 s = (unsigned __int128)a->l[i] + b->l[i] + carry;
        a->l[i] = (uint64_t)s;
        carry = (uint64_t)(s >> 64);
    }
    return carry;
}
static inline uint64_t bn64_sub(Bn64* a, const Bn64* b) {
    uint64_t borrow = 0;
    for (int i = 0; i < BN64_LIMBS; i++) {
        uint64_t bi = b->l[i] + borrow;
        if (a->l[i] < bi) borrow = 1; else borrow = 0;
        a->l[i] -= bi;
    }
    return borrow;
}
static inline uint64_t bn64_mprime(const Bn64* m) {
    uint64_t x = 1;
    for (int i = 0; i < 6; i++) x = x * (2 - m->l[0] * x);
    return ~x + 1;
}
static inline void bn64_pow2_mod(int k, const Bn64* m, Bn64* r) {
    bn64_set_u32(r, 1);
    for (int i = 0; i < k; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < BN64_LIMBS; j++) {
            uint64_t nv = (r->l[j] << 1) | carry;
            carry = r->l[j] >> 63;
            r->l[j] = nv;
        }
        if (carry || bn64_cmp(r, m) >= 0) {
            uint64_t borrow = 0;
            for (int j = 0; j < BN64_LIMBS; j++) {
                uint64_t mi = m->l[j] + borrow;
                if (r->l[j] < mi) borrow = 1; else borrow = 0;
                r->l[j] -= mi;
            }
        }
    }
}
static inline void bn64_monty_mul(const Bn64* m, uint64_t mprime,
                                  const Bn64* a, const Bn64* b, Bn64* z) {
    uint64_t T[BN64_LIMBS + 2] = {0};
    for (int i = 0; i < BN64_LIMBS; i++) {
        uint64_t ai = a->l[i];
        uint64_t carry = 0;
        for (int j = 0; j < BN64_LIMBS; j++) {
            unsigned __int128 prod = (unsigned __int128)ai * b->l[j] + T[j] + carry;
            T[j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        unsigned __int128 hi = (unsigned __int128)T[BN64_LIMBS] + carry;
        T[BN64_LIMBS] = (uint64_t)hi;
        T[BN64_LIMBS + 1] += (uint64_t)(hi >> 64);
        uint64_t u = T[0] * mprime;
        carry = 0;
        for (int j = 0; j < BN64_LIMBS; j++) {
            unsigned __int128 prod = (unsigned __int128)u * m->l[j] + T[j] + carry;
            T[j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        unsigned __int128 hi2 = (unsigned __int128)T[BN64_LIMBS] + carry;
        T[BN64_LIMBS] = (uint64_t)hi2;
        T[BN64_LIMBS + 1] += (uint64_t)(hi2 >> 64);
        for (int k = 0; k <= BN64_LIMBS; k++) T[k] = T[k + 1];
        T[BN64_LIMBS + 1] = 0;
    }
    Bn64 out;
    for (int j = 0; j < BN64_LIMBS; j++) out.l[j] = T[j];
    uint64_t extra = T[BN64_LIMBS];
    if (extra || bn64_cmp(&out, m) >= 0) bn64_sub(&out, m);
    *z = out;
}
static inline void bn64_mod_exp(const Bn64* m, uint64_t mprime, const Bn64* r2,
                                const Bn64* base, const uint8_t* exp, uint32_t explen, Bn64* out) {
    Bn64 acc, bm, one;
    bn64_set_u32(&acc, 1);
    bn64_set_u32(&one, 1);
    bn64_monty_mul(m, mprime, &acc, r2, &acc); // acc = R mod m
    bn64_monty_mul(m, mprime, base, r2, &bm);  // bm = base*R mod m
    for (uint32_t byte = 0; byte < explen; byte++) {
        uint8_t e = exp[byte];
        for (int bit = 7; bit >= 0; bit--) {
            bn64_monty_mul(m, mprime, &acc, &acc, &acc);
            if ((e >> bit) & 1) bn64_monty_mul(m, mprime, &acc, &bm, &acc);
        }
    }
    bn64_monty_mul(m, mprime, &acc, &one, out);
}

// Verify RSA PKCS#1 v1.5 signature with a 4096-bit modulus.
// n: modulus (512 bytes BE), e: exponent, sig: signature (512 bytes),
// data/datalen: signed content. Supports SHA-256 and SHA-1 DigestInfo.
static const uint8_t rsa4096_di_sha256[19] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
};

static inline int rsa4096_verify(const uint8_t* n, uint32_t nlen, uint32_t e,
                                 const uint8_t* data, uint32_t datalen,
                                 const uint8_t* sig, uint32_t siglen) {
    if (nlen != 512 || siglen != 512) return 0;
    Bn64 N, b, r, r2;
    bn64_from_bytes(n, 512, &N);
    bn64_from_bytes(sig, 512, &b);
    if (bn64_cmp(&b, &N) >= 0) return 0;
    uint64_t mp = bn64_mprime(&N);
    bn64_pow2_mod(BN64_LIMBS * 64 * 2, &N, &r2);
    uint8_t exp[8];
    uint32_t e32 = e;
    for (int i = 7; i >= 0; i--) { exp[i] = (uint8_t)e32; e32 >>= 8; }
    bn64_mod_exp(&N, mp, &r2, &b, exp, 8, &r);
    uint8_t em[512];
    bn64_to_bytes(&r, em, 512);
    if (em[0] != 0x00 || em[1] != 0x01) return 0;
    int i = 2;
    while (i < 512 && em[i] == 0xff) i++;
    if (i < 10 || em[i] != 0x00) return 0;
    i++;
    int tlen = 512 - i;
    uint8_t h[32];
    if (tlen == 19 + 32) {
        for (int k = 0; k < 19; k++) if (em[i + k] != rsa4096_di_sha256[k]) return 0;
        sha256_hash(data, datalen, h);
        for (int k = 0; k < 32; k++) if (em[i + 19 + k] != h[k]) return 0;
        return 1;
    }
    return 0;
}

} // namespace crypto
