#pragma once
#include <stdint.h>
#include <stddef.h>

// Big integers for RSA. Fixed-size: 2048-bit numbers = 32 limbs of 64-bit,
// little-endian limb order. Modular exponentiation via Montgomery (CIOS).
namespace crypto {

static const int BN_LIMBS = 32;

struct Bn {
    uint64_t l[BN_LIMBS];
};

// Montgomery context for one modulus m (odd).
struct MontyCtx {
    Bn m;      // modulus
    Bn r2;     // R^2 mod m, R = 2^(64*BN_LIMBS)
    uint64_t mprime; // -m^{-1} mod 2^64
};

static inline void bn_zero(Bn* a) { for (int i = 0; i < BN_LIMBS; i++) a->l[i] = 0; }

static inline int bn_is_zero(const Bn* a) {
    for (int i = 0; i < BN_LIMBS; i++) if (a->l[i]) return 0;
    return 1;
}

static inline void bn_set_u32(Bn* a, uint32_t v) {
    bn_zero(a);
    a->l[0] = v;
}

// Big-endian wire bytes -> Bn. Extra leading bytes ignored.
static inline void bn_from_bytes(const uint8_t* p, uint32_t n, Bn* a) {
    bn_zero(a);
    for (uint32_t i = 0; i < n; i++) {
        int limb = i / 8;
        int shift = (i % 8) * 8;
        if (limb < BN_LIMBS) a->l[limb] |= (uint64_t)p[n - 1 - i] << shift;
    }
}

// Bn -> exactly n big-endian bytes (n <= 32).
static inline void bn_to_bytes(const Bn* a, uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        int limb = i / 8;
        int shift = (i % 8) * 8;
        p[n - 1 - i] = (uint8_t)(limb < BN_LIMBS ? (a->l[limb] >> shift) : 0);
    }
}

// a -= b. Returns borrow.
static inline uint64_t bn_sub(Bn* a, const Bn* b) {
    uint64_t borrow = 0;
    for (int i = 0; i < BN_LIMBS; i++) {
        uint64_t bi = b->l[i] + borrow;
        if (a->l[i] < bi) borrow = 1; else borrow = 0;
        a->l[i] -= bi;
    }
    return borrow;
}

// a += b. Returns carry.
static inline uint64_t bn_add(Bn* a, const Bn* b) {
    uint64_t carry = 0;
    for (int i = 0; i < BN_LIMBS; i++) {
        unsigned __int128 sum = (unsigned __int128)a->l[i] + b->l[i] + carry;
        a->l[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    return carry;
}

// strcmp-style: -1 if a<b, 0 equal, 1 if a>b.
static inline int bn_cmp(const Bn* a, const Bn* b) {
    for (int i = BN_LIMBS - 1; i >= 0; i--) {
        if (a->l[i] < b->l[i]) return -1;
        if (a->l[i] > b->l[i]) return 1;
    }
    return 0;
}

// r = 2^k mod m, using double-and-subtract. m odd non-zero.
static inline void bn_pow2_mod(int k, const Bn* m, Bn* r) {
    bn_set_u32(r, 1);
    for (int i = 0; i < k; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            uint64_t nv = (r->l[j] << 1) | carry;
            carry = r->l[j] >> 63;
            r->l[j] = nv;
        }
        // r now = 2r mod 2^2048; true value = carry ? 2^2048 + r : r.
        // Reduce mod m. carry implies 2r >= 2^2048 > m, so subtract m once
        // (the borrow out of the n-limb subtraction adds back the 2^2048).
        if (carry || bn_cmp(r, m) >= 0) {
            uint64_t borrow = 0;
            for (int j = 0; j < BN_LIMBS; j++) {
                uint64_t mi = m->l[j] + borrow;
                if (r->l[j] < mi) borrow = 1; else borrow = 0;
                r->l[j] -= mi;
            }
        }
    }
}

// mprime = -m^{-1} mod 2^64, via Newton iteration. m must be odd.
static inline uint64_t bn_mprime(const Bn* m) {
    uint64_t x = 1;
    uint64_t ml = m->l[0];
    for (int i = 0; i < 6; i++) {
        x = x * (2 - ml * x);
    }
    return ~x + 1;
}

// Montgomery multiplication: z = a*b*R^{-1} mod m. a,b < m.
static inline void monty_mul(const MontyCtx* c, const Bn* a, const Bn* b, Bn* z) {
    uint64_t T[BN_LIMBS + 2] = {0};
    for (int i = 0; i < BN_LIMBS; i++) {
        uint64_t ai = a->l[i];
        uint64_t carry = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            unsigned __int128 prod = (unsigned __int128)ai * b->l[j] + T[j] + carry;
            T[j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        unsigned __int128 hi = (unsigned __int128)T[BN_LIMBS] + carry;
        T[BN_LIMBS] = (uint64_t)hi;
        T[BN_LIMBS + 1] += (uint64_t)(hi >> 64);

        uint64_t u = T[0] * c->mprime;
        carry = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            unsigned __int128 prod = (unsigned __int128)u * c->m.l[j] + T[j] + carry;
            T[j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        unsigned __int128 hi2 = (unsigned __int128)T[BN_LIMBS] + carry;
        T[BN_LIMBS] = (uint64_t)hi2;
        T[BN_LIMBS + 1] += (uint64_t)(hi2 >> 64);

        for (int k = 0; k <= BN_LIMBS; k++) T[k] = T[k + 1];
        T[BN_LIMBS + 1] = 0;
    }
    Bn out;
    for (int j = 0; j < BN_LIMBS; j++) out.l[j] = T[j];
    uint64_t extra = T[BN_LIMBS];
    int need = 0;
    if (extra) need = 1;
    else if (bn_cmp(&out, &c->m) >= 0) need = 1;
    if (need) {
        uint64_t borrow = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            uint64_t mi = c->m.l[j] + borrow;
            if (out.l[j] < mi) borrow = 1; else borrow = 0;
            out.l[j] -= mi;
        }
    }
    *z = out;
}

// Initialize Montgomery context. m odd, non-zero.
static inline void monty_init(MontyCtx* c, const Bn* m) {
    c->m = *m;
    c->mprime = bn_mprime(m);
    bn_pow2_mod(BN_LIMBS * 64 * 2, m, &c->r2); // R^2 = 2^(2*64*BN_LIMBS) mod m
}

// x -> x*R mod m
static inline void monty_to(const MontyCtx* c, const Bn* x, Bn* out) {
    monty_mul(c, x, &c->r2, out);
}

// Montgomery back: x*R -> x mod m
static inline void monty_from(const MontyCtx* c, const Bn* x, Bn* out) {
    Bn one;
    bn_set_u32(&one, 1);
    monty_mul(c, x, &one, out);
}

// Modular exponentiation with Montgomery. exp bytes big-endian.
static inline void bn_mod_exp(const MontyCtx* c, const Bn* base, const uint8_t* exp, uint32_t explen, Bn* out) {
    Bn acc, bm;
    bn_set_u32(&acc, 1);
    monty_to(c, &acc, &acc);
    monty_to(c, base, &bm);
    for (uint32_t byte = 0; byte < explen; byte++) {
        uint8_t e = exp[byte];
        for (int bit = 7; bit >= 0; bit--) {
            monty_mul(c, &acc, &acc, &acc);
            if ((e >> bit) & 1) monty_mul(c, &acc, &bm, &acc);
        }
    }
    monty_from(c, &acc, out);
}

} // namespace crypto
