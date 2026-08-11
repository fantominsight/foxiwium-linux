#include "net.h"
#include "rtl8139.h"
#include "port.h"

namespace net {

static uint8_t our_mac[6];
static uint32_t our_ip = 0x0A00020F;      // 10.0.2.15 (QEMU user networking)
static uint32_t gateway_ip = 0x0A000202;  // 10.0.2.2
static uint32_t dns_ip = 0x0A000203;      // 10.0.2.3 (QEMU slirp DNS)
static uint32_t netmask_ip = 0xFFFFFF00;  // 255.255.255.0

static bool up = false;
static uint64_t stat_tx = 0;
static uint64_t stat_rx = 0;
static bool trace = true;

static void dbg(const char* s) {
    if (!trace) return;
    while (*s) {
        port::outb(0xE9, *s);
        s++;
    }
}

static void dbg_putchar(char c) {
    if (!trace) return;
    port::outb(0xE9, c);
}

static constexpr uint16_t DNS_LOCAL_PORT = 53000;

constexpr int MAX_TCP_CONNS = 4;
constexpr uint16_t TCP_WINDOW = 8192;
constexpr uint16_t TCP_FIN = 0x01;
constexpr uint16_t TCP_SYN = 0x02;
constexpr uint16_t TCP_RST = 0x04;
constexpr uint16_t TCP_PSH = 0x08;
constexpr uint16_t TCP_ACK = 0x10;

enum TcpState {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
    TCP_CLOSE_WAIT
};

struct TcpConn {
    TcpState state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t my_seq;
    uint32_t my_ack;
    uint32_t acked;
    uint32_t syn_base;
    uint32_t poll_count;
    uint8_t rbuf[65536];
    uint32_t rlen;
    bool peer_closed;
    uint8_t tx_pending[2048];
    uint32_t tx_pending_len;
    uint32_t tx_pending_base;
    bool tx_active;
};

static TcpConn tcp_conns[MAX_TCP_CONNS];
static uint16_t next_local_port = 40000;
static uint16_t dns_id = 0x1234;
static uint16_t dns_pending_id = 0;
static bool dns_done = false;
static uint32_t dns_result = 0;

static constexpr int ARP_CACHE_MAX = 16;
struct ArpEntry {
    uint32_t ip;
    uint8_t mac[6];
};
static ArpEntry arp_cache[ARP_CACHE_MAX];
static int arp_count = 0;

struct PendingEcho {
    uint16_t id;
    uint16_t seq;
    bool active;
    bool received;
};
static PendingEcho pending = {0, 0, false, false};

static uint16_t next_echo_id = 0x1000;
static uint16_t next_ip_id = 0;

// ---------------- helpers ----------------
static void copy_bytes(uint8_t* dst, const uint8_t* src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t i;
    for (i = 0; i + 1 < len; i += 2) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    }
    if (i < len) sum += (uint32_t)data[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
    return true;
}

static bool mac_is_bcast(const uint8_t* m) {
    for (int i = 0; i < 6; i++) if (m[i] != 0xFF) return false;
    return true;
}

// ---------------- ARP ----------------
static void arp_learn(uint32_t ip, const uint8_t* mac) {
    for (int i = 0; i < arp_count; i++) {
        if (arp_cache[i].ip == ip) {
            copy_bytes(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    if (arp_count < ARP_CACHE_MAX) {
        arp_cache[arp_count].ip = ip;
        copy_bytes(arp_cache[arp_count].mac, mac, 6);
        arp_count++;
    }
}

static int arp_lookup(uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < arp_count; i++) {
        if (arp_cache[i].ip == ip) {
            copy_bytes(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return -1;
}

// ---------------- send paths ----------------
static void eth_send(const uint8_t* dst_mac, uint16_t ethertype,
                     const uint8_t* payload, uint32_t len) {
    if (!up || len + 14 > 1514) return;
    uint8_t buf[1526];
    copy_bytes(buf, dst_mac, 6);
    copy_bytes(buf + 6, our_mac, 6);
    wr16(buf + 12, ethertype);
    copy_bytes(buf + 14, payload, len);
    rtl8139::send(buf, 14 + len);
    stat_tx++;
}

static void arp_send_request(uint32_t ip) {
    dbg("[ARP REQ] for ");
    {
        char b[20];
        ip_to_str(ip, b);
        dbg(b);
    }
    dbg("\n");
    uint8_t p[28];
    wr16(p + 0, 1);           // hw type: Ethernet
    wr16(p + 2, 0x0800);      // proto: IPv4
    p[4] = 6;                 // hw len
    p[5] = 4;                 // proto len
    wr16(p + 6, 1);           // opcode: request
    copy_bytes(p + 8, our_mac, 6);
    wr32(p + 14, our_ip);
    for (int i = 0; i < 6; i++) p[18 + i] = 0;
    wr32(p + 24, ip);
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast, 0x0806, p, 28);
}

static void ip_send(uint32_t dst_ip, uint8_t proto,
                    const uint8_t* data, uint32_t len) {
    if (!up) return;
    uint8_t mac[6];
    uint32_t route_ip = dst_ip;
    if ((dst_ip & netmask_ip) != (our_ip & netmask_ip)) {
        route_ip = gateway_ip;
    }
    if (arp_lookup(route_ip, mac) < 0) {
        arp_send_request(route_ip);
        return;
    }
    uint32_t total = 20 + len;
    if (total > 1514) return;
    uint8_t buf[1522];
    buf[0] = 0x45;            // IPv4, IHL=5
    buf[1] = 0;
    wr16(buf + 2, (uint16_t)total);
    next_ip_id++;
    wr16(buf + 4, next_ip_id);
    wr16(buf + 6, 0);         // flags/fragment
    buf[8] = 64;              // TTL
    buf[9] = proto;
    wr16(buf + 10, 0);        // checksum placeholder
    wr32(buf + 12, our_ip);
    wr32(buf + 16, dst_ip);
    wr16(buf + 10, checksum(buf, 20));
    copy_bytes(buf + 20, data, len);
    eth_send(mac, 0x0800, buf, total);
}

// ---------------- protocol handlers ----------------
static void arp_handle(const uint8_t* p, uint32_t len, const uint8_t* src_mac) {
    (void)src_mac;
    if (len < 28) return;
    uint16_t op = rd16(p + 6);
    uint32_t spa = rd32(p + 14);
    arp_learn(spa, p + 8);
    if (op == 1 && rd32(p + 24) == our_ip) {
        uint8_t r[28];
        wr16(r + 0, 1);
        wr16(r + 2, 0x0800);
        r[4] = 6;
        r[5] = 4;
        wr16(r + 6, 2);       // opcode: reply
        copy_bytes(r + 8, our_mac, 6);
        wr32(r + 14, our_ip);
        copy_bytes(r + 18, p + 8, 6);
        wr32(r + 24, spa);
        eth_send(p + 8, 0x0806, r, 28);
    }
}

static void icmp_handle(const uint8_t* src_mac, const uint8_t* icmp,
                        uint32_t len, uint32_t src_ip) {
    (void)src_mac;
    if (len < 8) return;
    uint8_t type = icmp[0];
    if (type == 0) {
        // echo reply: match against pending ping
        if (pending.active && pending.id == rd16(icmp + 4) &&
            pending.seq == rd16(icmp + 6)) {
            pending.received = true;
        }
    } else if (type == 8) {
        // echo request: reply with the same payload
        uint8_t reply[128];
        uint32_t dl = len - 8;
        if (dl > 120) dl = 120;
        reply[0] = 0;
        reply[1] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 4, rd16(icmp + 4));
        wr16(reply + 6, rd16(icmp + 6));
        copy_bytes(reply + 8, icmp + 8, dl);
        wr16(reply + 2, checksum(reply, 8 + dl));
        ip_send(src_ip, 1, reply, 8 + dl);
    }
}

static void udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                     const uint8_t* data, uint32_t len) {
    if (!up) return;
    if (len > 1200) len = 1200;
    uint8_t seg[8 + 1200];
    wr16(seg + 0, sport);
    wr16(seg + 2, dport);
    wr16(seg + 4, (uint16_t)(8 + len));
    wr16(seg + 6, 0);
    copy_bytes(seg + 8, data, len);
    ip_send(dst_ip, 17, seg, 8 + len);
}

static bool word_at(const uint8_t* d, uint32_t len, uint32_t i, const char* w) {
    for (int k = 0; w[k]; k++) {
        if (i + (uint32_t)k >= len) return false;
        char a = (char)d[i + k];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != w[k]) return false;
    }
    return true;
}

static void dns_handle(const uint8_t* udp, uint32_t len) {
    if (len < 12) return;
    if (rd16(udp) != dns_pending_id) return;
    if ((rd16(udp + 2) & 0x000F) != 0) return;   // RCODE != 0
    uint16_t qd = rd16(udp + 4);
    uint16_t an = rd16(udp + 6);
    int p = 12;
    for (int q = 0; q < qd && p < (int)len; q++) {
        if ((udp[p] & 0xC0) == 0xC0) p += 2;
        else {
            while (p < (int)len && udp[p] != 0) p += 1 + udp[p];
            p++;
        }
        p += 4;
    }
    for (int a = 0; a < an && a < 32 && p < (int)len; a++) {
        if ((udp[p] & 0xC0) == 0xC0) p += 2;
        else {
            while (p < (int)len && udp[p] != 0) p += 1 + udp[p];
            if (p >= (int)len) return;
            p++;
        }
        if (p + 10 > (int)len) return;
        uint16_t type = rd16(udp + p);
        p += 8;
        uint16_t rdlen = rd16(udp + p);
        p += 2;
        if (p + rdlen > (int)len) return;
        if (type == 1 && rdlen == 4) {
            dns_result = rd32(udp + p);
            dns_done = true;
            return;
        }
        p += rdlen;
    }
}

static void udp_handle(const uint8_t* src_mac, const uint8_t* udp,
                       uint32_t len, uint32_t src_ip) {
    (void)src_mac;
    if (len < 8) return;
    uint16_t sport = rd16(udp + 0);
    uint16_t dport = rd16(udp + 2);
    if (dport == DNS_LOCAL_PORT) {
        dns_handle(udp + 8, len - 8);
        return;
    }
    uint32_t data_len = len - 8;
    if (data_len > 1200) data_len = 1200;
    uint8_t reply[1208 + 8];
    wr16(reply + 0, dport);
    wr16(reply + 2, sport);
    wr16(reply + 4, (uint16_t)(8 + data_len));
    wr16(reply + 6, 0);       // no UDP checksum
    copy_bytes(reply + 8, udp + 8, data_len);
    ip_send(src_ip, 17, reply, 8 + data_len);
}

static void poll_internal();

static uint16_t tcp_checksum(uint32_t saddr, uint32_t daddr,
                             const uint8_t* seg, uint32_t len) {
    uint32_t sum = (saddr >> 16) + (saddr & 0xFFFF);
    sum += (daddr >> 16) + (daddr & 0xFFFF);
    sum += 6;
    sum += (len >> 16) + (len & 0xFFFF);
    for (uint32_t i = 0; i + 1 < len; i += 2) {
        sum += ((uint32_t)seg[i] << 8) | seg[i + 1];
    }
    if (len & 1) sum += (uint32_t)seg[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_send_seg(TcpConn* c, uint16_t flags,
                         const uint8_t* payload, uint32_t len,
                         uint32_t seq, uint32_t ack) {
    if (len > 2048) len = 2048;
    uint8_t seg[20 + 2048];
    wr16(seg + 0, c->local_port);
    wr16(seg + 2, c->remote_port);
    wr32(seg + 4, seq);
    wr32(seg + 8, ack);
    seg[12] = 0x50;
    seg[13] = (uint8_t)flags;
    wr16(seg + 14, TCP_WINDOW);
    wr16(seg + 16, 0);
    wr16(seg + 18, 0);
    copy_bytes(seg + 20, payload, len);
    wr16(seg + 16, tcp_checksum(c->remote_ip, our_ip, seg, 20 + len));
    ip_send(c->remote_ip, 6, seg, 20 + len);
}

static TcpConn* tcp_find(uint16_t local_port) {
    for (int i = 0; i < MAX_TCP_CONNS; i++) {
        if (tcp_conns[i].state != TCP_CLOSED &&
            tcp_conns[i].local_port == local_port) {
            return &tcp_conns[i];
        }
    }
    return nullptr;
}

static void tcp_handle(const uint8_t* tcp, uint32_t len) {
    if (len < 20) return;
    uint16_t dport = rd16(tcp + 2);
    uint32_t seq = rd32(tcp + 4);
    uint32_t ack = rd32(tcp + 8);
    uint8_t doff = (uint8_t)((tcp[12] >> 4) * 4);
    uint16_t flags = tcp[13];
    if (doff < 20 || doff > len) return;
    TcpConn* c = tcp_find(dport);
    if (!c) return;
    const uint8_t* payload = tcp + doff;
    uint32_t plen = len - doff;

    if (flags & TCP_RST) {
        c->state = TCP_CLOSED;
        return;
    }

    if (c->state == TCP_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            c->my_ack = seq + 1;
            c->acked = ack;
            tcp_send_seg(c, TCP_ACK, nullptr, 0, c->my_seq, c->my_ack);
            c->state = TCP_ESTABLISHED;
        }
        return;
    }

    if (c->state != TCP_ESTABLISHED && c->state != TCP_FIN_WAIT) return;

    if (flags & TCP_ACK) {
        if (ack >= c->acked) c->acked = ack;
        if (c->tx_active &&
            ack >= c->tx_pending_base + c->tx_pending_len) {
            c->tx_active = false;
        }
    }
    if (plen > 0 && seq == c->my_ack) {
        uint32_t cp = plen;
        if (cp > 65536 - c->rlen) cp = 65536 - c->rlen;
        copy_bytes(c->rbuf + c->rlen, payload, cp);
        c->rlen += cp;
        c->my_ack += cp;
        tcp_send_seg(c, TCP_ACK, nullptr, 0, c->my_seq, c->my_ack);
    }
    if (flags & TCP_FIN) {
        c->my_ack++;
        c->peer_closed = true;
        c->state = TCP_CLOSE_WAIT;
        tcp_send_seg(c, TCP_ACK, nullptr, 0, c->my_seq, c->my_ack);
    }
}

static void tcp_poll(TcpConn* c) {
    poll_internal();
    c->poll_count++;
    if (c->poll_count % 5000 != 0) return;
    if (c->state == TCP_SYN_SENT) {
        tcp_send_seg(c, TCP_SYN, nullptr, 0, c->syn_base, 0);
    } else if (c->tx_active &&
               c->acked < c->tx_pending_base + c->tx_pending_len) {
        tcp_send_seg(c, TCP_ACK | TCP_PSH, c->tx_pending, c->tx_pending_len,
                     c->tx_pending_base, c->my_ack);
    }
}

static void handle_eth(const uint8_t* frame, uint32_t len) {
    if (len < 14) return;
    const uint8_t* dst = frame;
    const uint8_t* src = frame + 6;
    uint16_t type = rd16(frame + 12);
    const uint8_t* payload = frame + 14;
    uint32_t plen = len - 14;

    if (type == 0x0806) {
        if (mac_is_bcast(dst) || mac_eq(dst, our_mac)) {
            arp_handle(payload, plen, src);
        }
    } else if (type == 0x0800) {
        if (!(mac_is_bcast(dst) || mac_eq(dst, our_mac))) return;
        if (plen < 20) return;
        uint32_t ihl = (payload[0] & 0x0F) * 4;
        if (ihl < 20 || ihl > plen) return;
        uint8_t proto = payload[9];
        uint32_t src_ip = rd32(payload + 12);
        uint32_t dst_ip = rd32(payload + 16);
        if (dst_ip != our_ip) return;
        uint32_t total = rd16(payload + 2);
        if (total > plen) total = plen;
        const uint8_t* l4 = payload + ihl;
        uint32_t l4len = total - ihl;
        if (proto == 1) icmp_handle(src, l4, l4len, src_ip);
        else if (proto == 17) udp_handle(src, l4, l4len, src_ip);
        else if (proto == 6) tcp_handle(l4, l4len);
    }
}

static void poll_internal() {
    uint8_t pkt[1526];
    int n;
    while ((n = rtl8139::receive(pkt, sizeof(pkt))) > 0) {
        stat_rx++;
        handle_eth(pkt, (uint32_t)n);
    }
}

static int arp_resolve(uint32_t ip, uint8_t* mac_out) {
    if (arp_lookup(ip, mac_out) == 1) return 1;
    for (int attempt = 0; attempt < 3; attempt++) {
        arp_send_request(ip);
        for (int i = 0; i < 20000; i++) {
            poll_internal();
            if (arp_lookup(ip, mac_out) == 1) return 1;
        }
    }
    return -1;
}

// ---------------- public API ----------------
bool init() {
    if (!rtl8139::init()) return false;
    rtl8139::get_mac(our_mac);
    arp_count = 0;
    for (int i = 0; i < MAX_TCP_CONNS; i++) {
        tcp_conns[i].state = TCP_CLOSED;
        tcp_conns[i].rlen = 0;
        tcp_conns[i].peer_closed = false;
        tcp_conns[i].tx_active = false;
    }
    up = true;
    return true;
}

bool ready() { return up; }

void poll() {
    if (up) poll_internal();
}

void get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = our_mac[i];
}

uint32_t get_ip() { return our_ip; }
uint32_t get_gateway() { return gateway_ip; }

void ip_to_str(uint32_t ip, char* out) {
    int o = 0;
    for (int s = 24; s >= 0; s -= 8) {
        if (o) out[o++] = '.';
        uint32_t v = (ip >> s) & 0xFF;
        if (v >= 100) { out[o++] = (char)('0' + v / 100); v %= 100; }
        if (v >= 10) { out[o++] = (char)('0' + v / 10); v %= 10; }
        out[o++] = (char)('0' + v);
    }
    out[o] = 0;
}

uint32_t parse_ip(const char* s) {
    uint32_t ip = 0;
    int val = 0, parts = 0;
    while (*s) {
        if (*s == '.') {
            ip = (ip << 8) | (uint32_t)(val & 0xFF);
            parts++;
            val = 0;
        } else if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return 0;
        } else {
            return 0;
        }
        s++;
    }
    ip = (ip << 8) | (uint32_t)(val & 0xFF);
    parts++;
    return parts == 4 ? ip : 0;
}

int ping(uint32_t ip, uint32_t max_iter) {
    if (!up) return -1;
    uint8_t mac[6];
    if (arp_resolve(ip, mac) < 0) return -2;

    next_echo_id++;
    uint8_t req[48];
    req[0] = 8;               // echo request
    req[1] = 0;
    wr16(req + 2, 0);
    wr16(req + 4, next_echo_id);
    wr16(req + 6, 1);
    for (int i = 8; i < 48; i++) req[i] = (uint8_t)i;
    wr16(req + 2, checksum(req, 48));

    pending.active = true;
    pending.id = next_echo_id;
    pending.seq = 1;
    pending.received = false;
    ip_send(ip, 1, req, 48);

    int iter = 0;
    for (; iter < (int)max_iter; iter++) {
        poll_internal();
        if (pending.received) break;
    }
    bool ok = pending.received;
    pending.active = false;
    return ok ? iter : -3;
}

static void dns_send_query(const char* host, uint16_t qid) {
    uint8_t q[512];
    wr16(q + 0, qid);
    wr16(q + 2, 0x0100);
    wr16(q + 4, 1);
    wr16(q + 6, 0);
    wr16(q + 8, 0);
    wr16(q + 10, 0);
    int p = 13;
    int seg = 12;
    const char* s = host;
    while (*s && p < 496) {
        if (*s == '.') {
            q[seg] = (uint8_t)(p - seg - 1);
            seg = p;
            p++;
        } else {
            q[p++] = (uint8_t)*s;
        }
        s++;
    }
    q[seg] = (uint8_t)(p - seg - 1);
    q[p++] = 0;
    wr16(q + p, 1);
    wr16(q + p + 2, 1);
    int qlen = p + 4;

    dns_done = false;
    dns_result = 0;
    dns_pending_id = qid;
    udp_send(dns_ip, DNS_LOCAL_PORT, 53, q, (uint32_t)qlen);
}

bool dns_resolve(const char* host, uint32_t* ip_out, uint32_t max_iter) {
    if (host[0] >= '0' && host[0] <= '9') {
        uint32_t ip = parse_ip(host);
        if (ip != 0) { *ip_out = ip; return true; }
    }
    if (!up) return false;

    // Resolve the DNS server's MAC first: ip_send drops the first packet
    // while ARP is pending, and the DNS query is never retried below.
    uint8_t dns_mac[6];
    if (arp_resolve(dns_ip, dns_mac) < 0) {
        rtl8139::dbg_dump_rx();
        return false;
    }

    uint16_t qid = dns_id;
    dns_id++;
    dns_send_query(host, qid);
    for (uint32_t i = 0; i < max_iter; i++) {
        poll_internal();
        if (dns_done) {
            *ip_out = dns_result;
            return true;
        }
    }
    return false;
}

int tcp_open(uint32_t ip, uint16_t port, uint32_t max_iter) {
    if (!up) return -1;
    TcpConn* c = nullptr;
    int idx = -1;
    for (int i = 0; i < MAX_TCP_CONNS; i++) {
        if (tcp_conns[i].state == TCP_CLOSED) { c = &tcp_conns[i]; idx = i; break; }
    }
    if (!c) return -2;
    c->state = TCP_SYN_SENT;
    c->remote_ip = ip;
    c->remote_port = port;
    c->local_port = next_local_port;
    next_local_port++;
    c->my_seq = 8192 + (uint32_t)next_local_port * 17;
    c->my_ack = 0;
    c->acked = 0;
    c->syn_base = c->my_seq;
    c->rlen = 0;
    c->peer_closed = false;
    c->tx_active = false;
    c->poll_count = 0;
    tcp_send_seg(c, TCP_SYN, nullptr, 0, c->syn_base, 0);
    c->my_seq++;
    for (uint32_t i = 0; i < max_iter && c->state != TCP_ESTABLISHED; i++) {
        tcp_poll(c);
        if (c->state == TCP_CLOSED) return -3;
    }
    if (c->state != TCP_ESTABLISHED) {
        c->state = TCP_CLOSED;
        return -4;
    }
    return idx;
}

int tcp_send(int handle, const uint8_t* data, uint32_t len) {
    if (handle < 0 || handle >= MAX_TCP_CONNS) return -1;
    TcpConn* c = &tcp_conns[handle];
    if (c->state != TCP_ESTABLISHED) return -1;
    if (len > 2048) len = 2048;
    copy_bytes(c->tx_pending, data, len);
    c->tx_pending_len = len;
    c->tx_pending_base = c->my_seq;
    c->tx_active = true;
    tcp_send_seg(c, TCP_ACK | TCP_PSH, c->tx_pending, len, c->my_seq, c->my_ack);
    c->my_seq += len;
    return (int)len;
}

int tcp_recv(int handle, uint8_t* out, uint32_t max) {
    if (handle < 0 || handle >= MAX_TCP_CONNS) return -1;
    TcpConn* c = &tcp_conns[handle];
    uint32_t n = c->rlen < max ? c->rlen : max;
    for (uint32_t i = 0; i < n; i++) out[i] = c->rbuf[i];
    if (n > 0) {
        for (uint32_t i = n; i < c->rlen; i++) c->rbuf[i - n] = c->rbuf[i];
        c->rlen -= n;
    }
    return (int)n;
}

int tcp_peer_closed(int handle) {
    if (handle < 0 || handle >= MAX_TCP_CONNS) return 1;
    return tcp_conns[handle].peer_closed ? 1 : 0;
}

void tcp_close(int handle) {
    if (handle < 0 || handle >= MAX_TCP_CONNS) return;
    TcpConn* c = &tcp_conns[handle];
    if (c->state == TCP_CLOSED) return;
    if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) {
        tcp_send_seg(c, TCP_FIN | TCP_ACK, nullptr, 0, c->my_seq, c->my_ack);
        c->my_seq++;
    }
    c->state = TCP_CLOSED;
}

static int parse_url(const char* url, char* host, int hostsz,
                     uint16_t* port, char* path, int pathsz) {
    const char* p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' &&
        p[4] == 's' && p[5] == ':' && p[6] == '/' && p[7] == '/') {
        return -1;   // no TLS
    }
    if (!(p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' &&
          p[4] == ':' && p[5] == '/' && p[6] == '/')) {
        return -1;
    }
    p += 7;
    int hi = 0;
    while (*p && *p != ':' && *p != '/' && hi < hostsz - 1) {
        host[hi++] = *p++;
    }
    host[hi] = 0;
    if (host[0] == 0) return -1;
    *port = 80;
    if (*p == ':') {
        p++;
        uint32_t prt = 0;
        while (*p >= '0' && *p <= '9') {
            prt = prt * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (prt == 0 || prt > 65535) return -1;
        *port = (uint16_t)prt;
    }
    int pi = 0;
    if (*p == '/') {
        while (*p && pi < pathsz - 1) path[pi++] = *p++;
        path[pi] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }
    return 0;
}

static int find_header_end(const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return (int)i;
        }
    }
    return -1;
}

static int find_content_length(const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i + 15 < len; i++) {
        if (word_at(data, len, i, "content-length:")) {
            uint32_t k = i;
            while (k < len && data[k] != ':') k++;
            k++;
            while (k < len && data[k] == ' ') k++;
            int v = 0;
            while (k < len && data[k] >= '0' && data[k] <= '9') {
                v = v * 10 + (data[k] - '0');
                k++;
            }
            return v;
        }
    }
    return -1;
}

static bool is_chunked(const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i + 18 < len; i++) {
        if (word_at(data, len, i, "transfer-encoding:")) {
            uint32_t k = i;
            while (k < len && data[k] != ':') k++;
            k++;
            while (k < len && data[k] == ' ') k++;
            if (word_at(data, len, k, "chunked")) return true;
        }
    }
    return false;
}

static uint32_t dechunk(const uint8_t* in, uint32_t inlen,
                        char* out, uint32_t max_out) {
    uint32_t ip = 0, op = 0;
    while (ip < inlen && op < max_out) {
        uint32_t size = 0;
        bool got = false;
        while (ip < inlen && in[ip] != '\n') {
            char c = (char)in[ip];
            if (c >= '0' && c <= '9') { size = size * 16 + (uint32_t)(c - '0'); got = true; }
            else if (c >= 'a' && c <= 'f') { size = size * 16 + (uint32_t)(c - 'a' + 10); got = true; }
            else if (c >= 'A' && c <= 'F') { size = size * 16 + (uint32_t)(c - 'A' + 10); got = true; }
            ip++;
        }
        if (ip < inlen) ip++;
        if (!got) break;
        if (size == 0) break;
        if (ip + size > inlen) break;
        for (uint32_t i = 0; i < size && op < max_out; i++) {
            out[op++] = (char)in[ip++];
        }
        if (ip < inlen && in[ip] == '\r') ip++;
        if (ip < inlen && in[ip] == '\n') ip++;
    }
    return op;
}

// ---------------- asynchronous HTTP ----------------
// One in-flight job, advanced by http_poll() in bounded slices so the
// desktop loop stays responsive while a page loads.
struct HttpJob {
    uint8_t* out;
    uint32_t max_out;
    char host[256];
    char path[512];
    uint16_t port;
    uint32_t ip;
    int conn;
    int state;          // 1 dns, 2 tcp, 3 http, 4 done; <0 error
    uint32_t result;
    bool dns_arp_ok;
    bool dns_arp_started;
    int dns_arp_attempt;
    uint32_t dns_arp_wait;
    bool dns_query_sent;
    uint32_t dns_wait;
    bool tcp_started;
    uint32_t tcp_wait;
    bool req_sent;
    uint32_t http_wait;
    int content_length;
    bool chunked;
};
static HttpJob hj;

bool http_begin(const char* url, char* out, uint32_t max_out) {
    for (uint64_t j = 0; j < sizeof(HttpJob); j++) ((uint8_t*)&hj)[j] = 0;
    hj.out = (uint8_t*)out;
    hj.max_out = max_out;
    if (!up) { hj.state = -1; return false; }
    if (parse_url(url, hj.host, sizeof(hj.host), &hj.port, hj.path, sizeof(hj.path)) < 0) {
        hj.state = -2;
        return false;
    }
    if (hj.host[0] >= '0' && hj.host[0] <= '9') {
        uint32_t ip = parse_ip(hj.host);
        if (ip == 0) { hj.state = -2; return false; }
        hj.ip = ip;
        hj.state = 2;
    } else {
        hj.state = 1;
    }
    return true;
}

void http_cancel() {
    if (hj.state == 2 || hj.state == 3) {
        if (hj.conn >= 0 && hj.conn < MAX_TCP_CONNS) {
            tcp_conns[hj.conn].state = TCP_CLOSED;
        }
    }
    hj.state = 0;
}

static uint32_t http_dns_step() {
    if (!hj.dns_query_sent) {
        if (!hj.dns_arp_ok) {
            if (!hj.dns_arp_started) {
                // Send the first ARP request immediately, like the old
                // synchronous arp_resolve() did. Polling for 20000 empty
                // iterations before the first request wastes time and, with
                // QEMU's async slirp delivery, can time out.
                hj.dns_arp_started = true;
                arp_send_request(dns_ip);
            }
            poll_internal();
            uint8_t mac[6];
            if (arp_lookup(dns_ip, mac) == 1) {
                hj.dns_arp_ok = true;
            } else {
                hj.dns_arp_wait++;
                if (hj.dns_arp_wait >= 20000) {
                    hj.dns_arp_wait = 0;
                    hj.dns_arp_attempt++;
                    arp_send_request(dns_ip);
                    if (hj.dns_arp_attempt > 3) hj.state = -3;
                }
            }
            return 1;
        }
        uint16_t qid = dns_id;
        dns_id++;
        dns_send_query(hj.host, qid);
        hj.dns_query_sent = true;
        return 1;
    }
    poll_internal();
    if (dns_done) {
        hj.ip = dns_result;
        hj.state = 2;
        return 1;
    }
    hj.dns_wait++;
    if (hj.dns_wait >= 500000) hj.state = -3;
    return 1;
}

static uint32_t http_tcp_step() {
    if (!hj.tcp_started) {
        TcpConn* c = nullptr;
        int idx = -1;
        for (int i = 0; i < MAX_TCP_CONNS; i++) {
            if (tcp_conns[i].state == TCP_CLOSED) { c = &tcp_conns[i]; idx = i; break; }
        }
        if (!c) { hj.state = -4; return 1; }
        c->state = TCP_SYN_SENT;
        c->remote_ip = hj.ip;
        c->remote_port = hj.port;
        c->local_port = next_local_port;
        next_local_port++;
        c->my_seq = 8192 + (uint32_t)next_local_port * 17;
        c->my_ack = 0;
        c->acked = 0;
        c->syn_base = c->my_seq;
        c->rlen = 0;
        c->peer_closed = false;
        c->tx_active = false;
        c->poll_count = 0;
        tcp_send_seg(c, TCP_SYN, nullptr, 0, c->syn_base, 0);
        c->my_seq++;
        hj.conn = idx;
        hj.tcp_started = true;
        return 1;
    }
    TcpConn* c = &tcp_conns[hj.conn];
    tcp_poll(c);
    if (c->state == TCP_ESTABLISHED) { hj.state = 3; return 1; }
    if (c->state == TCP_CLOSED) { hj.state = -4; return 1; }
    hj.tcp_wait++;
    if (hj.tcp_wait >= 500000) {
        dbg("[http] TCP timeout: state=");
        {
            char nb[16]; int ni=0; int v=c->state; if(v==0) nb[ni++]='0'; while(v>0){nb[ni++]='0'+v%10;v/=10;} while(ni>0) dbg_putchar(nb[--ni]);
        }
        dbg("\n");
        hj.state = -5;
    }
    return 1;
}

static uint32_t http_recv_step() {
    TcpConn* c = &tcp_conns[hj.conn];
    if (!hj.req_sent) {
        char req[640];
        int rl = 0;
        const char* h1 = "GET ";
        const char* h2 = " HTTP/1.1\r\nHost: ";
        const char* h3 = "\r\nUser-Agent: Foxiwium/1.0\r\nConnection: close\r\n\r\n";
        for (int i = 0; h1[i] && rl < 639; i++) req[rl++] = h1[i];
        for (int i = 0; hj.path[i] && rl < 639; i++) req[rl++] = hj.path[i];
        for (int i = 0; h2[i] && rl < 639; i++) req[rl++] = h2[i];
        for (int i = 0; hj.host[i] && rl < 639; i++) req[rl++] = hj.host[i];
        for (int i = 0; h3[i] && rl < 639; i++) req[rl++] = h3[i];
        req[rl] = 0;
        tcp_send(hj.conn, (const uint8_t*)req, (uint32_t)rl);
        hj.req_sent = true;
        return 1;
    }
    tcp_poll(c);
    if (c->state == TCP_CLOSED) { hj.state = -4; return 1; }
    bool done = false;
    if (c->rlen > 4) {
        int he = find_header_end(c->rbuf, c->rlen);
        if (he >= 0) {
            hj.content_length = find_content_length(c->rbuf, c->rlen);
            hj.chunked = is_chunked(c->rbuf, c->rlen);
            if (!hj.chunked && hj.content_length >= 0 &&
                (uint32_t)(he + 4) + (uint32_t)hj.content_length <= c->rlen) {
                done = true;
            } else if (c->peer_closed) {
                done = true;
            }
        } else if (c->peer_closed) {
            done = true;
        }
    }
    if (done) {
        int he = find_header_end(c->rbuf, c->rlen);
        const uint8_t* body = (he >= 0) ? c->rbuf + he + 4 : c->rbuf;
        uint32_t blen = (he >= 0) ? c->rlen - (uint32_t)(he + 4) : c->rlen;
        uint32_t n = 0;
        if (hj.chunked) {
            n = dechunk(body, blen, (char*)hj.out, hj.max_out);
        } else {
            if (hj.content_length >= 0 && blen > (uint32_t)hj.content_length)
                blen = (uint32_t)hj.content_length;
            if (blen > hj.max_out) blen = hj.max_out;
            copy_bytes(hj.out, body, blen);
            n = blen;
        }
        tcp_close(hj.conn);
        hj.result = n;
        hj.state = 4;
        return 1;
    }
    hj.http_wait++;
    if (hj.http_wait >= 500000) hj.state = -5;
    return 1;
}

int http_poll(uint32_t budget) {
    if (hj.state < 0) return hj.state;
    if (hj.state == 0) return -2;
    if (hj.state == 4) return (int)hj.result;
    for (uint32_t k = 0; k < budget; k++) {
        switch (hj.state) {
            case 1: http_dns_step(); break;
            case 2: http_tcp_step(); break;
            case 3: http_recv_step(); break;
            default: return -2;
        }
        if (hj.state < 0) return hj.state;
        if (hj.state == 4) return (int)hj.result;
        if (hj.state == 0) return -2;
    }
    return 0;
}

int http_get(const char* url, char* out, uint32_t max_out, uint32_t max_iter) {
    if (!http_begin(url, out, max_out)) return hj.state;
    uint32_t total = 0;
    while (hj.state > 0 && hj.state < 4) {
        int r = http_poll(1500);
        total += 1500;
        if (r != 0) return r;
        if (total >= max_iter) {
            http_cancel();
            return -5;
        }
    }
    return (int)hj.result;
}

uint64_t get_tx_count() { return stat_tx; }
uint64_t get_rx_count() { return stat_rx; }

} // namespace net
