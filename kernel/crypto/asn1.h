#pragma once
#include <stdint.h>
#include <stddef.h>

// Minimal DER (definite-length) reader for X.509. No allocation.
namespace crypto {

struct DerNode {
    const uint8_t* data;     // start of the TLV (tag byte)
    const uint8_t* content;  // start of the value
    uint32_t clen;           // content length
    uint32_t tag;            // tag byte
    uint32_t total;          // total encoded length (tag+len+content)
};

static const uint32_t ASN1_BOOLEAN = 0x01;
static const uint32_t ASN1_INTEGER = 0x02;
static const uint32_t ASN1_BITSTRING = 0x03;
static const uint32_t ASN1_OCTETSTRING = 0x04;
static const uint32_t ASN1_NULL = 0x05;
static const uint32_t ASN1_OID = 0x06;
static const uint32_t ASN1_UTF8STRING = 0x0c;
static const uint32_t ASN1_SEQUENCE = 0x30;
static const uint32_t ASN1_SET = 0x31;
static const uint32_t ASN1_PRINTABLESTRING = 0x13;
static const uint32_t ASN1_IA5STRING = 0x16;
static const uint32_t ASN1_UTCTIME = 0x17;
static const uint32_t ASN1_GENERALIZEDTIME = 0x18;

// Parse one TLV at p. Returns 1 on success, 0 on failure.
static inline int der_read(const uint8_t* p, uint32_t len, DerNode* n) {
    if (len < 2) return 0;
    uint32_t tag = p[0];
    uint32_t i = 1;
    uint32_t vlen;
    if ((p[1] & 0x80) == 0) {
        vlen = p[1];
        i = 2;
    } else {
        uint32_t nb = p[1] & 0x7f;
        if (nb == 0 || nb > 4 || i + 1 + nb > len) return 0;
        vlen = 0;
        for (uint32_t k = 0; k < nb; k++) vlen = (vlen << 8) | p[i + 1 + k];
        i += 1 + nb;
    }
    if (i + vlen > len) return 0;
    n->data = p;
    n->tag = tag;
    n->content = p + i;
    n->clen = vlen;
    n->total = i + vlen;
    return 1;
}

// Step to the next sibling after `prev` (whose content started at prev->content).
static inline int der_next(const uint8_t* base, uint32_t limit, const DerNode* prev, DerNode* n) {
    const uint8_t* p = prev->content + prev->clen;
    if (p >= base + limit) return 0;
    return der_read(p, (uint32_t)(base + limit - p), n);
}

// First child of a constructed node. Fails if child doesn't fit in content.
static inline int der_child(const DerNode* parent, DerNode* n) {
    if (parent->clen == 0) return 0;
    return der_read(parent->content, parent->clen, n);
}

// Read a positive INTEGER as big-endian bytes into out[0..max-1] (zero-padded).
// Returns 1 if it fits.
static inline int der_integer_bytes(const DerNode* integer, uint8_t* out, uint32_t max) {
    if (integer->tag != ASN1_INTEGER || integer->clen == 0) return 0;
    const uint8_t* c = integer->content;
    uint32_t cl = integer->clen;
    if (c[0] & 0x80) return 0; // negative not supported
    if (c[0] == 0) { c++; cl--; } // strip leading zero
    if (cl > max) return 0;
    for (uint32_t i = 0; i < max; i++) out[i] = 0;
    for (uint32_t i = 0; i < cl; i++) out[max - cl + i] = c[i];
    return 1;
}

} // namespace crypto
