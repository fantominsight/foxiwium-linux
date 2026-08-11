#pragma once
#include "bn.h"
#include "sha256.h"

// RSA public-key operations (PKCS#1 v1.5). Header-only.
namespace crypto {

struct RsaPubKey {
    Bn n;
    uint32_t e;
};

// Compute m = base^e mod n. base/e are big-endian.
static inline int rsa_public_op(const uint8_t* n_bytes, uint32_t nlen,
                                uint32_t e,
                                const uint8_t* base, uint32_t baselen,
                                uint8_t* out, uint32_t outlen) {
    Bn n, b, r;
    bn_from_bytes(n_bytes, nlen, &n);
    bn_from_bytes(base, baselen, &b);
    if (bn_cmp(&b, &n) >= 0) return -1;
    MontyCtx c;
    monty_init(&c, &n);
    uint8_t exp[8];
    uint32_t e32 = e;
    for (int i = 7; i >= 0; i--) { exp[i] = (uint8_t)e32; e32 >>= 8; }
    bn_mod_exp(&c, &b, exp, 8, &r);
    if (outlen > 32 * 8) return -1;
    bn_to_bytes(&r, out, outlen);
    return 0;
}

// PKCS#1 v1.5 DigestInfo prefix for SHA-256.
static const uint8_t rsa_digestinfo_sha256[19] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
};

// Verify RSA-SHA256 PKCS#1 v1.5 signature over `data`.
// n/e: RSA public key. sig: signature (nlen bytes).
// Returns 1 if valid, 0 if invalid.
static inline int rsa_verify_sha256(const uint8_t* n, uint32_t nlen, uint32_t e,
                                    const uint8_t* data, uint32_t datalen,
                                    const uint8_t* sig, uint32_t siglen) {
    if (nlen > 256 || siglen != nlen) return 0;
    uint8_t em[256];
    if (rsa_public_op(n, nlen, e, sig, siglen, em, nlen) != 0) return 0;
    if (em[0] != 0x00 || em[1] != 0x01) return 0;
    int i = 2;
    while (i < (int)nlen && em[i] == 0xff) i++;
    if (i < 10 || em[i] != 0x00) return 0;
    i++;
    int tlen = (int)nlen - i;
    if (tlen != 19 + 32) return 0;
    for (int k = 0; k < 19; k++) if (em[i + k] != rsa_digestinfo_sha256[k]) return 0;
    uint8_t h[32];
    sha256_hash(data, datalen, h);
    for (int k = 0; k < 32; k++) if (em[i + 19 + k] != h[k]) return 0;
    return 1;
}

} // namespace crypto
