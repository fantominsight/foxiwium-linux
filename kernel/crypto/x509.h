#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "asn1.h"
#include "rsa.h"
#include "rsa4096.h"

// X.509 certificate parsing + RSA chain verification against a pinned trust
// store. RSA (SHA-256) only; SHA-1/ECDSA chains are rejected with an error.
namespace crypto {

enum {
    X509_OK = 0,
    X509_ERR_PARSE = -1,
    X509_ERR_SIG = -2,              // signature did not verify
    X509_ERR_UNSUPPORTED_KEY = -3,  // ECDSA or other non-RSA key
    X509_ERR_UNSUPPORTED_SIG = -4,  // non-RSA-SHA256 signature alg
    X509_ERR_UNTRUSTED = -5,        // chain does not reach a pinned root
};

struct X509Cert {
    const uint8_t* der;
    uint32_t derlen;
    const uint8_t* tbs;     // TBSCertificate DER (includes its SEQUENCE tag)
    uint32_t tbslen;
    const uint8_t* sig;     // signature value bytes (unused-bits prefix stripped)
    uint32_t siglen;
    int sig_sha256;         // signature algorithm is sha256WithRSA
    char subject[72];
    char issuer[72];
    uint8_t  n[512];        // RSA modulus (big-endian)
    uint32_t nlen;
    uint32_t e;
    int has_rsa;
    char not_before[16];
    char not_after[16];
};

static inline int der_oid_is(const DerNode* oid, const uint8_t* expected, uint32_t n) {
    return oid->tag == ASN1_OID && oid->clen == n && memcmp(oid->content, expected, n) == 0;
}

// Extract CN (2.5.4.3) from a Name (p = content of the Name SEQUENCE).
// buf gets the CN string. Returns 1 on success.
static inline int x509_name_cn(const uint8_t* p, uint32_t len, char* buf, uint32_t buflen) {
    buf[0] = 0;
    if (buflen == 0) return 0;
    static const uint8_t oid_cn[3] = {0x55, 0x04, 0x03};
    DerNode rdn, atv, oid, val;
    if (!der_read(p, len, &rdn)) return 0;
    while (rdn.tag == ASN1_SET || rdn.tag == ASN1_SEQUENCE) {
        if (der_child(&rdn, &atv)) {
            while (atv.tag == ASN1_SEQUENCE) {
                if (der_child(&atv, &oid) && der_oid_is(&oid, oid_cn, 3)) {
                    if (der_next(atv.content, atv.clen, &oid, &val)) {
                        uint32_t take = val.clen < buflen - 1 ? val.clen : buflen - 1;
                        memcpy(buf, val.content, take);
                        buf[take] = 0;
                        return 1;
                    }
                }
                if (!der_next(rdn.content, rdn.clen, &atv, &atv)) break;
            }
        }
        if (!der_next(p, len, &rdn, &rdn)) break;
    }
    return 0;
}

// Parse ASN.1 time into a "YYYYMMDDHHMMSS" display string. Returns 0 on success.
static inline int x509_parse_time(const DerNode* t, char* out, uint32_t outlen) {
    if (outlen) out[0] = 0;
    if (t->tag == ASN1_UTCTIME) {
        if (t->clen < 13) return -1;
        int yy = (t->content[0] - '0') * 10 + (t->content[1] - '0');
        int year = (yy < 50) ? 2000 + yy : 1900 + yy;
        if (outlen >= 15) {
            out[0] = (char)('0' + (year / 1000) % 10);
            out[1] = (char)('0' + (year / 100) % 10);
            out[2] = (char)('0' + (year / 10) % 10);
            out[3] = (char)('0' + year % 10);
            for (int i = 0; i < 11; i++) out[4 + i] = (char)t->content[2 + i]; // MMDDHHMMSSZ
            out[15] = 0;
        }
        return 0;
    }
    if (t->tag == ASN1_GENERALIZEDTIME) {
        if (t->clen < 15 || outlen < 15) return -1;
        memcpy(out, t->content, 14);
        out[14] = 0;
        return 0;
    }
    return -1;
}

// OID 1.2.840.113549.1.1.1 (rsaEncryption) = 2a 86 48 86 f7 0d 01 01 01
// OID 1.2.840.113549.1.1.11 (sha256WithRSA) = 2a 86 48 86 f7 0d 01 01 0b
static const uint8_t oid_rsa_encryption[9]  = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
static const uint8_t oid_sha256_with_rsa[9] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b};

static inline int x509_parse(const uint8_t* der, uint32_t len, X509Cert* c) {
    memset(c, 0, sizeof(*c));
    c->der = der; c->derlen = len;
    DerNode cert, tbs, sigalg, sigval, cur;
    if (!der_read(der, len, &cert) || cert.tag != ASN1_SEQUENCE) return X509_ERR_PARSE;
    if (!der_child(&cert, &tbs) || tbs.tag != ASN1_SEQUENCE) return X509_ERR_PARSE;
    c->tbs = tbs.data;
    c->tbslen = tbs.total;
    if (!der_next(cert.content, cert.clen, &tbs, &sigalg)) return X509_ERR_PARSE;   // signatureAlgorithm
    if (!der_next(sigalg.data, (uint32_t)(cert.content + cert.clen - sigalg.data), &sigalg, &sigval)) return X509_ERR_PARSE; // signatureValue
    // signature algorithm OID (first child of sigalg)
    {
        DerNode oid;
        if (der_child(&sigalg, &oid)) {
            c->sig_sha256 = der_oid_is(&oid, oid_sha256_with_rsa, 9) ? 1 : 0;
        }
    }
    // BIT STRING: first content byte = unused bits count
    if (sigval.tag != ASN1_BITSTRING || sigval.clen < 1) return X509_ERR_PARSE;
    if (sigval.content[0] != 0) return X509_ERR_PARSE;
    c->sig = sigval.content + 1;
    c->siglen = sigval.clen - 1;

    // --- TBSCertificate children ---
    DerNode tc, e0;
    if (!der_child(&tbs, &tc)) return X509_ERR_PARSE;
    if (tc.tag == 0xa0) { // [0] EXPLICIT version
        if (!der_next(tbs.content, tbs.clen, &tc, &tc)) return X509_ERR_PARSE;
    }
    e0 = tc; // now tc = serialNumber INTEGER
    // walk: serial(INTEGER) sigAlg(SEQ) issuer(SEQ) validity(SEQ) subject(SEQ) spki(SEQ)
    DerNode serial = e0, siga, issuer, validity, subject, spki;
    if (!der_next(tbs.content, tbs.clen, &serial, &siga)) return X509_ERR_PARSE;
    if (!der_next(siga.data, (uint32_t)(tbs.content + tbs.clen - siga.data), &siga, &issuer)) return X509_ERR_PARSE;
    if (!der_next(issuer.data, (uint32_t)(tbs.content + tbs.clen - issuer.data), &issuer, &validity)) return X509_ERR_PARSE;
    if (!der_next(validity.data, (uint32_t)(tbs.content + tbs.clen - validity.data), &validity, &subject)) return X509_ERR_PARSE;
    if (!der_next(subject.data, (uint32_t)(tbs.content + tbs.clen - subject.data), &subject, &spki)) return X509_ERR_PARSE;

    x509_name_cn(issuer.content, issuer.clen, c->issuer, sizeof(c->issuer));
    x509_name_cn(subject.content, subject.clen, c->subject, sizeof(c->subject));

    // validity: children notBefore, notAfter
    {
        DerNode nb, na;
        if (!der_child(&validity, &nb)) return X509_ERR_PARSE;
        if (!der_next(validity.content, validity.clen, &nb, &na)) return X509_ERR_PARSE;
        x509_parse_time(&nb, c->not_before, sizeof(c->not_before));
        x509_parse_time(&na, c->not_after, sizeof(c->not_after));
    }

    // SPKI: children AlgorithmIdentifier(SEQ), BIT STRING
    {
        DerNode alg, bitstr;
        if (!der_child(&spki, &alg) || alg.tag != ASN1_SEQUENCE) return X509_ERR_PARSE;
        if (!der_next(spki.content, spki.clen, &alg, &bitstr) || bitstr.tag != ASN1_BITSTRING) return X509_ERR_PARSE;
        DerNode oid;
        if (!der_child(&alg, &oid)) return X509_ERR_PARSE;
        if (!der_oid_is(&oid, oid_rsa_encryption, 9)) return X509_ERR_UNSUPPORTED_KEY;
        // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
        if (bitstr.clen < 1 || bitstr.content[0] != 0) return X509_ERR_PARSE;
        DerNode rsa, mod, exp;
        if (!der_read(bitstr.content + 1, bitstr.clen - 1, &rsa) || rsa.tag != ASN1_SEQUENCE) return X509_ERR_PARSE;
        if (!der_child(&rsa, &mod) || mod.tag != ASN1_INTEGER) return X509_ERR_PARSE;
        if (!der_next(rsa.content, rsa.clen, &mod, &exp) || exp.tag != ASN1_INTEGER) return X509_ERR_PARSE;
        if (!der_integer_bytes(&mod, c->n, 512)) return X509_ERR_PARSE;
        c->nlen = mod.clen;
        if (mod.clen > 0 && mod.content[0] == 0) c->nlen = mod.clen - 1;
        uint8_t eb[4];
        if (!der_integer_bytes(&exp, eb, 4)) return X509_ERR_PARSE;
        c->e = ((uint32_t)eb[0] << 24) | ((uint32_t)eb[1] << 16) | ((uint32_t)eb[2] << 8) | eb[3];
        c->has_rsa = 1;
    }
    return X509_OK;
}

// Sign a cert with a 2048-bit key. sig_len checked vs key nlen.
static inline int x509_verify_sig_with(const X509Cert* child, const X509Cert* parent) {
    if (!parent->has_rsa) return X509_ERR_UNSUPPORTED_KEY;
    if (!child->sig_sha256) return X509_ERR_UNSUPPORTED_SIG;
    // signature must be < modulus; left-pad if shorter
    uint8_t sigbuf[512];
    uint32_t plen = parent->nlen;
    if (plen > 512) plen = 512;
    memset(sigbuf, 0, plen);
    if (child->siglen > plen) return X509_ERR_PARSE;
    memcpy(sigbuf + plen - child->siglen, child->sig, child->siglen);

    uint8_t nbuf[512];
    memset(nbuf, 0, 512);
    // parent->n holds the modulus right-aligned in 512 bytes
    memcpy(nbuf + 512 - parent->nlen, parent->n + (512 - parent->nlen), parent->nlen);

    if (parent->nlen <= 256) {
        return rsa_verify_sha256(nbuf + 512 - parent->nlen, parent->nlen, parent->e,
                                 child->tbs, child->tbslen, sigbuf + plen - parent->nlen, parent->nlen) ? X509_OK : X509_ERR_SIG;
    }
    if (parent->nlen == 512) {
        return rsa4096_verify(nbuf, 512, parent->e, child->tbs, child->tbslen, sigbuf, 512) ? X509_OK : X509_ERR_SIG;
    }
    return X509_ERR_UNSUPPORTED_KEY;
}

struct TrustAnchor {
    const uint8_t* der;
    uint32_t derlen;
};

// Verify a chain of parsed certs (leaf first) against pinned roots (given as
// raw DER). Returns X509_OK or an error code.
// Path building: each cert's signature is checked against the next cert's key;
// the last cert must either be byte-identical to a pinned root, or its
// signature must verify with a pinned root's public key (cross-signed case).
static inline int x509_verify_chain(const X509Cert* certs, int n,
                                    const TrustAnchor* roots, int nroots,
                                    int* trust_index_out) {
    if (n < 1) return X509_ERR_PARSE;
    for (int i = 0; i < n - 1; i++) {
        int r = x509_verify_sig_with(&certs[i], &certs[i + 1]);
        if (r != X509_OK) return r;
    }
    const X509Cert* last = &certs[n - 1];
    // exact pin match?
    for (int r = 0; r < nroots; r++) {
        if (roots[r].derlen == last->derlen && memcmp(roots[r].der, last->der, last->derlen) == 0) {
            if (trust_index_out) *trust_index_out = r;
            return X509_OK;
        }
    }
    // last cert issued by a pinned root: verify its signature with the root key
    static X509Cert root_cache[8];
    static int root_cached = 0;
    if (!root_cached && nroots <= 8) {
        for (int r = 0; r < nroots; r++) {
            if (x509_parse(roots[r].der, roots[r].derlen, &root_cache[r]) != X509_OK) break;
        }
        root_cached = 1;
    }
    if (root_cached) {
        for (int r = 0; r < nroots; r++) {
            if (!root_cache[r].has_rsa) continue;
            if (x509_verify_sig_with(last, &root_cache[r]) == X509_OK) {
                if (trust_index_out) *trust_index_out = r;
                return X509_OK;
            }
        }
    }
    return X509_ERR_UNTRUSTED;
}

} // namespace crypto
