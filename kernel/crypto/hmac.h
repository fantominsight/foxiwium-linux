#pragma once
#include "sha256.h"

// HMAC-SHA256 (RFC 2104 / RFC 4868). Header-only.
namespace crypto {

static inline void hmac_sha256(const uint8_t* key, uint32_t klen,
                               const uint8_t* msg, uint32_t mlen,
                               uint8_t out[32]) {
    uint8_t k[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    for (uint32_t i = 0; i < 64; i++) {
        k[i] = i < klen ? key[i] : 0;
    }
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256Ctx c;
    sha256_init(&c);
    sha256_update(&c, ipad, 64);
    sha256_update(&c, msg, mlen);
    uint8_t inner[32];
    sha256_final(&c, inner);

    sha256_init(&c);
    sha256_update(&c, opad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

} // namespace crypto
