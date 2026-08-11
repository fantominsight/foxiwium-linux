#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sha256.h"

// CTR_DRBG-like CSPRNG (SHA-256 based), seeded from RDRAND (when present) and
// rdtsc timing jitter. Header-only, no allocation, suitable for TLS ephemeral
// keys and client random. Not audited; adequate for a hobby kernel TLS client.
namespace crypto {

static inline int cpu_has_rdrand() {
    uint32_t a = 1, b = 0, c = 0, d = 0;
    asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d) : : "memory");
    (void)b; (void)d;
    return (c >> 30) & 1;
}

static inline int rdrand64(uint64_t* out) {
    uint64_t v;
    uint8_t ok;
    asm volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
    if (!ok) return 0;
    *out = v;
    return 1;
}

static inline uint64_t rdtsc64() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

struct RngState {
    uint8_t secret[32];
    uint64_t ctr;
    int seeded;
};

static inline void rng_seed_from_entropy(RngState* s) {
    uint8_t pool[96];
    uint32_t n = 0;
    uint64_t base = rdtsc64();
    uint64_t state = base ^ 0x9e3779b97f4a7c15ull;
    int have = cpu_has_rdrand();
    for (int i = 0; i < 24 && n + 4 <= sizeof(pool); i++) {
        uint64_t t0 = rdtsc64();
        asm volatile("pause");
        uint64_t t1 = rdtsc64();
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        uint64_t mix = (t1 - t0) ^ state ^ (base >> (i & 63));
        pool[n++] = (uint8_t)mix;
        pool[n++] = (uint8_t)(mix >> 8);
        pool[n++] = (uint8_t)(mix >> 16);
        pool[n++] = (uint8_t)(mix >> 24);
        if (have) {
            uint64_t r;
            if (rdrand64(&r)) {
                for (int k = 0; k < 8 && n < sizeof(pool); k++) pool[n++] = (uint8_t)(r >> (k * 8));
            }
        }
    }
    sha256_hash(pool, n, s->secret);
    s->ctr = rdtsc64() ^ state;
    s->seeded = 1;
}

// Reseed with fresh entropy (call periodically if desired).
static inline void rng_reseed(RngState* s) { rng_seed_from_entropy(s); }

static inline void rng_generate(RngState* s, uint8_t* out, uint32_t n) {
    if (!s->seeded) rng_seed_from_entropy(s);
    uint8_t block[40];
    for (int i = 0; i < 32; i++) block[i] = s->secret[i];
    uint32_t off = 0;
    while (off < n) {
        for (int i = 0; i < 8; i++) block[32 + i] = (uint8_t)(s->ctr >> (i * 8));
        uint8_t h[32];
        sha256_hash(block, 40, h);
        uint32_t take = (n - off) < 32 ? (n - off) : 32;
        for (uint32_t i = 0; i < take; i++) out[off + i] = h[i];
        off += take;
        sha256_hash(s->secret, 32, s->secret);
        s->ctr++;
    }
}

// Global default instance. Reentrancy: TLS runs single-threaded in this kernel.
static inline RngState* crypto_rng() {
    static RngState g;
    return &g;
}

static inline void crypto_random_bytes(uint8_t* out, uint32_t n) {
    rng_generate(crypto_rng(), out, n);
}

} // namespace crypto
