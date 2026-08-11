#pragma once
#include "hmac.h"

// TLS 1.2 PRF based on HMAC-SHA256 (RFC 5246 5.HMAC).
// PRF(secret, label, seed, out, outlen) = P_SHA256(secret, label||seed)
namespace crypto {

static inline uint32_t cstrlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline void tls_prf(const uint8_t* secret, uint32_t slen,
                           const char* label,
                           const uint8_t* seed, uint32_t seedlen,
                           uint8_t* out, uint32_t outlen) {
    uint8_t a[32];
    uint8_t hmac[32];
    uint8_t ls[160];
    uint32_t lsl = 0;
    const char* lp = label;
    while (*lp && lsl < sizeof(ls) - 32) ls[lsl++] = (uint8_t)*lp++;
    for (uint32_t i = 0; i < seedlen && lsl < sizeof(ls); i++) ls[lsl++] = seed[i];

    // A(1) = HMAC(secret, label || seed)
    hmac_sha256(secret, slen, ls, lsl, a);

    uint32_t o = 0;
    while (o < outlen) {
        uint8_t block[192];
        for (int i = 0; i < 32; i++) block[i] = a[i];
        for (uint32_t i = 0; i < lsl; i++) block[32 + i] = ls[i];
        hmac_sha256(secret, slen, block, 32 + lsl, hmac);
        uint32_t take = outlen - o < 32 ? outlen - o : 32;
        for (uint32_t i = 0; i < take; i++) out[o++] = hmac[i];
        hmac_sha256(secret, slen, a, 32, a);
    }
}

} // namespace crypto
