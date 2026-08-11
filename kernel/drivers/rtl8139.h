#pragma once
#include <stdint.h>

namespace rtl8139 {

bool init();
bool ready();
void get_mac(uint8_t out[6]);
void send(const uint8_t* data, uint32_t len);
int receive(uint8_t* out, uint32_t max);
void dbg_dump_rx();

} // namespace rtl8139
