#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bn.h"
#include "rng.h"

// NIST P-256 (secp256r1) ECDH for TLS 1.2 ECDHE_RSA.
// Coordinates live in the Montgomery domain (mod p) using the bn.h engine.
// Affine point arithmetic; one Fermat inversion per point operation.
namespace crypto {

static const char* ecc_p_hex =
    "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff";
static const char* ecc_b_hex =
    "5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b";
static const char* ecc_gx_hex =
    "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296";
static const char* ecc_gy_hex =
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
static const char* ecc_n_hex =
    "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551";

struct EcCtx {
    Bn p, b, n;
    Bn a_m, two_m, three_m, one_m; // a = p-3, plus 1,2,3 (Montgomery form)
    Bn gx_m, gy_m;                 // generator (Montgomery form)
    MontyCtx fp;                   // field arithmetic mod p
};

static inline void ecc_hex_to_bytes(const char* h, uint8_t* out, uint32_t nbytes) {
    for (uint32_t i = 0; i < nbytes; i++) {
        uint32_t hi = 0, lo = 0;
        char ch = h[i * 2];
        if (ch >= '0' && ch <= '9') hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        ch = h[i * 2 + 1];
        if (ch >= '0' && ch <= '9') lo = ch - '0';
        else if (ch >= 'a' && ch <= 'f') lo = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') lo = ch - 'A' + 10;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static inline void ecc_init(EcCtx* c) {
    uint8_t tmp[32];
    Bn v;
    ecc_hex_to_bytes(ecc_p_hex, tmp, 32);
    bn_from_bytes(tmp, 32, &c->p);
    ecc_hex_to_bytes(ecc_b_hex, tmp, 32);
    bn_from_bytes(tmp, 32, &c->b);
    ecc_hex_to_bytes(ecc_n_hex, tmp, 32);
    bn_from_bytes(tmp, 32, &c->n);
    monty_init(&c->fp, &c->p);

    bn_set_u32(&v, 1);
    monty_to(&c->fp, &v, &c->one_m);
    bn_set_u32(&v, 2);
    monty_to(&c->fp, &v, &c->two_m);
    bn_set_u32(&v, 3);
    monty_to(&c->fp, &v, &c->three_m);
    {
        Bn a = c->p;
        bn_set_u32(&v, 3);
        bn_sub(&a, &v);          // a = p - 3
        monty_to(&c->fp, &a, &c->a_m);
    }

    ecc_hex_to_bytes(ecc_gx_hex, tmp, 32);
    bn_from_bytes(tmp, 32, &v);
    monty_to(&c->fp, &v, &c->gx_m);
    ecc_hex_to_bytes(ecc_gy_hex, tmp, 32);
    bn_from_bytes(tmp, 32, &v);
    monty_to(&c->fp, &v, &c->gy_m);
}

// ---- field ops (values in Montgomery domain, all < p) ----

static inline void fp_mul(const EcCtx* c, const Bn* a, const Bn* b, Bn* r) {
    monty_mul(&c->fp, a, b, r);
}
static inline void fp_sqr(const EcCtx* c, const Bn* a, Bn* r) {
    monty_mul(&c->fp, a, a, r);
}
static inline void fp_add(const EcCtx* c, const Bn* a, const Bn* b, Bn* r) {
    Bn t = *a;
    bn_add(&t, b);
    if (bn_cmp(&t, &c->p) >= 0) bn_sub(&t, &c->p);
    *r = t;
}
static inline void fp_sub(const EcCtx* c, const Bn* a, const Bn* b, Bn* r) {
    Bn t = *a;
    if (bn_sub(&t, b)) bn_add(&t, &c->p);
    *r = t;
}
static inline void fp_neg(const EcCtx* c, const Bn* a, Bn* r) {
    if (bn_is_zero(a)) { bn_zero(r); return; }
    *r = c->p;
    bn_sub(r, a);
}
// Fermat inverse: a^(p-2) mod p. a in Montgomery form.
static inline void fp_inv(const EcCtx* c, const Bn* a, Bn* r) {
    Bn areg, e, out, two;
    monty_from(&c->fp, a, &areg);
    bn_set_u32(&two, 2);
    e = c->p;
    bn_sub(&e, &two);          // e = p - 2
    uint8_t eb[32];
    bn_to_bytes(&e, eb, 32);
    bn_mod_exp(&c->fp, &areg, eb, 32, &out);
    monty_to(&c->fp, &out, r);
}

struct EcPoint {
    Bn x, y; // Montgomery domain
    int inf;
};

static inline void ec_point_double(const EcCtx* c, const EcPoint* p, EcPoint* r) {
    if (p->inf || bn_is_zero(&p->y)) { r->inf = 1; return; }
    Bn x2, num, den, dinv, lam, x3, y3, t;
    fp_sqr(c, &p->x, &x2);
    fp_add(c, &x2, &x2, &t);   // 2x^2
    fp_add(c, &t, &x2, &num);  // 3x^2
    fp_add(c, &num, &c->a_m, &num); // + a
    fp_add(c, &p->y, &p->y, &den);  // 2y
    fp_inv(c, &den, &dinv);
    fp_mul(c, &num, &dinv, &lam);
    fp_sqr(c, &lam, &x3);
    fp_sub(c, &x3, &p->x, &x3);
    fp_sub(c, &x3, &p->x, &x3);     // x3 = lam^2 - 2x
    fp_sub(c, &p->x, &x3, &y3);
    fp_mul(c, &lam, &y3, &y3);
    fp_sub(c, &y3, &p->y, &y3);
    r->x = x3; r->y = y3; r->inf = 0;
}

static inline void ec_point_add(const EcCtx* c, const EcPoint* a, const EcPoint* b, EcPoint* r) {
    if (a->inf) { *r = *b; return; }
    if (b->inf) { *r = *a; return; }
    if (bn_cmp(&a->x, &b->x) == 0) {
        Bn ysum;
        fp_add(c, &a->y, &b->y, &ysum);
        if (bn_is_zero(&ysum)) { r->inf = 1; return; } // P == -Q
        ec_point_double(c, a, r);
        return;
    }
    Bn dy, dx, dinv, lam, x3, y3;
    fp_sub(c, &b->y, &a->y, &dy);
    fp_sub(c, &b->x, &a->x, &dx);
    fp_inv(c, &dx, &dinv);
    fp_mul(c, &dy, &dinv, &lam);
    fp_sqr(c, &lam, &x3);
    fp_sub(c, &x3, &a->x, &x3);
    fp_sub(c, &x3, &b->x, &x3);
    fp_sub(c, &a->x, &x3, &y3);
    fp_mul(c, &lam, &y3, &y3);
    fp_sub(c, &y3, &a->y, &y3);
    r->x = x3; r->y = y3; r->inf = 0;
}

// R = k * P, k big-endian bytes. MSB-first double-and-add.
static inline void ec_point_mul(const EcCtx* c, const uint8_t* k, uint32_t klen, const EcPoint* base, EcPoint* r) {
    EcPoint acc;
    acc.inf = 1;
    for (uint32_t i = 0; i < klen; i++) {
        uint8_t b = k[i];
        for (int bit = 7; bit >= 0; bit--) {
            ec_point_double(c, &acc, &acc);
            if ((b >> bit) & 1) ec_point_add(c, &acc, base, &acc);
        }
    }
    *r = acc;
}

static inline void ec_point_to_bytes(const EcCtx* c, const EcPoint* p, uint8_t xout[32], uint8_t yout[32]) {
    Bn xr, yr;
    monty_from(&c->fp, &p->x, &xr);
    monty_from(&c->fp, &p->y, &yr);
    bn_to_bytes(&xr, xout, 32);
    bn_to_bytes(&yr, yout, 32);
}

// Random private scalar in [1, n-2].
static inline void ecc_gen_scalar(const EcCtx* c, uint8_t d[32]) {
    Bn s;
    do {
        crypto_random_bytes(d, 32);
        bn_from_bytes(d, 32, &s);
        if (bn_cmp(&s, &c->n) >= 0) bn_sub(&s, &c->n); // s < n now (one subtract enough)
        if (bn_is_zero(&s)) bn_set_u32(&s, 1);
    } while (bn_cmp(&s, &c->n) >= 0 || bn_is_zero(&s));
    bn_to_bytes(&s, d, 32);
}

// Generate ephemeral keypair: Q = d*G. Returns 1 on success.
static inline int ecc_gen_keypair(const EcCtx* c, uint8_t priv[32], uint8_t pub[64]) {
    EcPoint g, q;
    g.x = c->gx_m; g.y = c->gy_m; g.inf = 0;
    ecc_gen_scalar(c, priv);
    ec_point_mul(c, priv, 32, &g, &q);
    if (q.inf) return 0;
    uint8_t qx[32], qy[32];
    ec_point_to_bytes(c, &q, qx, qy);
    for (int i = 0; i < 32; i++) { pub[i] = qx[i]; pub[32 + i] = qy[i]; }
    return 1;
}

// ECDH: S = d * Qpeer (x-coordinate). peer pub is 64 bytes (X||Y). Returns 1 on success.
static inline int ecc_shared_secret(const EcCtx* c, const uint8_t priv[32],
                                    const uint8_t peer_pub[64], uint8_t secret[32]) {
    Bn qxr, qyr;
    bn_from_bytes(peer_pub, 32, &qxr);
    bn_from_bytes(peer_pub + 32, 32, &qyr);
    if (bn_cmp(&qxr, &c->p) >= 0 || bn_cmp(&qyr, &c->p) >= 0) return 0;
    EcPoint q, r;
    monty_to(&c->fp, &qxr, &q.x);
    monty_to(&c->fp, &qyr, &q.y);
    q.inf = 0;
    // check on-curve: y^2 = x^3 - 3x + b
    {
        Bn l, t, bm, y2;
        fp_sqr(c, &q.x, &l);
        fp_mul(c, &l, &q.x, &l);       // x^3
        fp_add(c, &q.x, &q.x, &t);
        fp_add(c, &t, &q.x, &t);       // 3x
        fp_sub(c, &l, &t, &l);         // x^3 - 3x
        monty_to(&c->fp, &c->b, &bm);
        fp_add(c, &l, &bm, &l);        // x^3 - 3x + b
        fp_sqr(c, &q.y, &y2);
        if (bn_cmp(&l, &y2) != 0) return 0; // not on curve
    }
    ec_point_mul(c, priv, 32, &q, &r);
    if (r.inf) return 0;
    Bn sx;
    monty_from(&c->fp, &r.x, &sx);
    bn_to_bytes(&sx, secret, 32);
    return 1;
}

} // namespace crypto
