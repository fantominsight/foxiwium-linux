#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace net {

bool init();
bool ready();
void poll();

void get_mac(uint8_t out[6]);
uint32_t get_ip();
uint32_t get_gateway();
void ip_to_str(uint32_t ip, char* out);
uint32_t parse_ip(const char* s);

// Blocking ICMP echo. Returns >=0 (latency proxy) on reply,
// -1 NIC down, -2 ARP resolution failed, -3 no reply.
int ping(uint32_t ip, uint32_t max_iter);

// Resolve a hostname to an IPv4 address (UDP DNS, 10.0.2.3).
// Accepts dotted-quad IPs directly. Returns true on success.
bool dns_resolve(const char* host, uint32_t* ip_out, uint32_t max_iter);

// Minimal TCP client sockets (active open only).
// Returns a handle (>=0) or a negative error code.
int tcp_open(uint32_t ip, uint16_t port, uint32_t max_iter);
int tcp_send(int handle, const uint8_t* data, uint32_t len);
int tcp_recv(int handle, uint8_t* out, uint32_t max);
int tcp_peer_closed(int handle);
void tcp_close(int handle);

// Fetch an HTTP page. Writes the response body into out (max_out bytes).
// Returns body length, or a negative error code.
int http_get(const char* url, char* out, uint32_t max_out, uint32_t max_iter);

// Asynchronous HTTP fetch (single in-flight job).
// http_begin starts it, http_poll advances it by a bounded amount of work
// and returns 0 (still running), >0 (done, body length) or <0 (error).
// http_cancel aborts the current job.
bool http_begin(const char* url, char* out, uint32_t max_out);
int  http_poll(uint32_t budget);
void http_cancel();

uint64_t get_tx_count();
uint64_t get_rx_count();

} // namespace net
