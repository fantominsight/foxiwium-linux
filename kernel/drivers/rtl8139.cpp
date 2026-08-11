#include "rtl8139.h"
#include "port.h"
#include "../mm/pmm.h"

namespace rtl8139 {

constexpr uint16_t PCI_VENDOR_RTL = 0x10EC;
constexpr uint16_t PCI_DEVICE_8139 = 0x8139;
constexpr uint16_t PCI_CONFIG_ADDR = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;

constexpr uint16_t REG_MAC0   = 0x00;
constexpr uint16_t REG_TSD0   = 0x10;
constexpr uint16_t REG_TSAD0  = 0x20;
constexpr uint16_t REG_RBSTART = 0x30;
constexpr uint16_t REG_CAPR   = 0x34;
constexpr uint16_t REG_CMD    = 0x37;
constexpr uint16_t REG_IMR    = 0x3C;
constexpr uint16_t REG_ISR    = 0x3E;
constexpr uint16_t REG_RCR    = 0x44;
constexpr uint16_t REG_CONFIG1 = 0x52;

constexpr uint8_t  CMD_RST    = 0x10;
constexpr uint8_t  CMD_RX_EN  = 0x08;
constexpr uint8_t  CMD_TX_EN  = 0x04;

constexpr uint32_t RX_BUF_SIZE = 8192;
constexpr uint32_t RX_BUF_PAD  = 16;

static uint16_t io_base = 0;
static bool up = false;
static uint8_t mac_addr[6];
static uint8_t* rx_buffer = nullptr;
static uint32_t rx_ptr = 0;
static uint8_t* tx_buffers[4];
static uint32_t tx_cur = 0;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    port::outl(PCI_CONFIG_ADDR, addr);
    return port::inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    port::outl(PCI_CONFIG_ADDR, addr);
    port::outl(PCI_CONFIG_DATA, val);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (uint16_t)(pci_read32(bus, dev, func, offset) >> ((offset & 2) * 8));
}

static void dbg(const char* s) { while (*s) port::outb(0xE9, *s++); }

static void dbgc(char c) { port::outb(0xE9, c); }

bool init() {
    uint8_t found_bus = 0, found_dev = 0;
    bool found = false;

    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_read16(0, dev, 0, 0x00);
        uint16_t device = pci_read16(0, dev, 0, 0x02);
        if (vendor == PCI_VENDOR_RTL && device == PCI_DEVICE_8139) {
            found_bus = 0;
            found_dev = dev;
            found = true;
            break;
        }
    }
    if (!found) {
        dbg("[RTL] not found; bus0 vendors:");
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read16(0, dev, 0, 0x00);
            uint16_t device = pci_read16(0, dev, 0, 0x02);
            if (vendor == 0xFFFF || vendor == 0x0000) continue;
            char b[5];
            for (int i = 3; i >= 0; i--) b[i] = "0123456789abcdef"[vendor & 0xF], vendor >>= 4;
            b[4] = 0;
            dbg(" ");
            dbg(b);
            for (int i = 3; i >= 0; i--) b[i] = "0123456789abcdef"[device & 0xF], device >>= 4;
            dbg(":");
            dbg(b);
        }
        dbg("\n");
        return false;
    }

    for (int bar = 0; bar < 6; bar++) {
        uint32_t v = pci_read32(found_bus, found_dev, 0, 0x10 + bar * 4);
        if (v & 0x1) {
            io_base = (uint16_t)(v & 0xFFFC);
            break;
        }
    }
    dbg("[RTL] found dev=");
    dbg((const char*)(found_dev < 10 ? "0" : ""));
    dbg(" io=");
    {
        uint16_t b = io_base;
        char h[5];
        for (int i = 3; i >= 0; i--) h[i] = "0123456789abcdef"[b & 0xF], b >>= 4;
        h[4] = 0;
        dbg(h);
    }
    dbg("\n");
    if (io_base == 0) {
        dbg("[RTL] io_base==0 FAIL\n");
        return false;
    }
    if (io_base == 0) {
        port::outb(0xE9, '['); port::outb(0xE9, 'N'); port::outb(0xE9, 'I');
        port::outb(0xE9, 'C'); port::outb(0xE9, ']'); port::outb(0xE9, ' ');
        port::outb(0xE9, 'n'); port::outb(0xE9, 'o'); port::outb(0xE9, ' ');
        port::outb(0xE9, 'b'); port::outb(0xE9, 'a'); port::outb(0xE9, 'r');
        port::outb(0xE9, '\n');
        return false;
    }

    for (int i = 0; i < 6; i++) {
        mac_addr[i] = port::inb(io_base + REG_MAC0 + (uint16_t)i);
    }

    uint32_t cmd = pci_read32(found_bus, found_dev, 0, 0x04);
    cmd |= 0x7; // I/O + memory space + bus mastering
    pci_write32(found_bus, found_dev, 0, 0x04, cmd);

    rx_buffer = (uint8_t*)pmm::alloc_pages(3);
    if (!rx_buffer) { dbg("[RTL] rx alloc FAIL\n"); return false; }
    for (int i = 0; i < 4; i++) {
        tx_buffers[i] = (uint8_t*)pmm::alloc_page();
        if (!tx_buffers[i]) {
            char b[32];
            int n = 0;
            uint64_t v = pmm::get_used_pages();
            dbg("[RTL] tx alloc FAIL i=");
            b[n++] = '0' + i;
            b[n++] = ' ';
            uint64_t u = v;
            char tmp[16]; int t = 0;
            if (u == 0) tmp[t++] = '0';
            while (u > 0) { tmp[t++] = '0' + u % 10; u /= 10; }
            while (t > 0) b[n++] = tmp[--t];
            b[n] = 0;
            dbg(b);
            dbg(" used\n");
            return false;
        }
    }

    port::outb(io_base + REG_CONFIG1, 0x00); // power on
    port::outb(io_base + REG_CMD, CMD_RST);  // software reset
    for (int i = 0; i < 100000; i++) {
        if (!(port::inb(io_base + REG_CMD) & CMD_RST)) break;
    }

    for (uint32_t i = 0; i < RX_BUF_SIZE + RX_BUF_PAD; i++) rx_buffer[i] = 0;
    port::outl(io_base + REG_RBSTART, (uint32_t)(uint64_t)rx_buffer);

    port::outw(io_base + REG_IMR, 0x0005); // ROK + TOK
    port::outw(io_base + REG_ISR, 0x0005); // clear status
    port::outl(io_base + REG_RCR, 0x0F | (1 << 7)); // AB+AM+APM+AAP, WRAP
    port::outw(io_base + REG_CAPR, 0);

    rx_ptr = 0;
    tx_cur = 0;
    port::outb(io_base + REG_CMD, CMD_RX_EN | CMD_TX_EN);
    up = true;
    return true;
}

void dbg_dump_rx() {
    dbg("[RING] ptr=");
    {
        uint32_t v = rx_ptr;
        char tmp[16]; int t = 0;
        if (v == 0) tmp[t++] = '0';
        while (v > 0) { tmp[t++] = '0' + v % 10; v /= 10; }
        while (t > 0) port::outb(0xE9, tmp[--t]);
    }
    dbg(" buf:");
    for (int i = 0; i < 96; i++) {
        uint8_t b = ((uint8_t*)rx_buffer)[i];
        dbgc("0123456789abcdef"[b >> 4]);
        dbgc("0123456789abcdef"[b & 0xF]);
        if ((i & 15) == 15) dbgc('|');
    }
    dbg(" ccr=0x");
    {
        uint32_t b = port::inl(io_base + REG_CMD);
        char tmp[9];
        for (int i = 7; i >= 0; i--) tmp[i] = "0123456789abcdef"[b & 0xF], b >>= 4;
        tmp[8] = 0;
        dbg(tmp);
    }
    dbg("\n");
}

bool ready() { return up; }

void get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac_addr[i];
}

void send(const uint8_t* data, uint32_t len) {
    if (!up || len > 1514) return;
    uint8_t* buf = tx_buffers[tx_cur];
    for (uint32_t i = 0; i < len; i++) buf[i] = data[i];
    port::outl(io_base + REG_TSAD0 + tx_cur * 4, (uint32_t)(uint64_t)buf);
    port::outl(io_base + REG_TSD0 + tx_cur * 4, len);
    tx_cur = (tx_cur + 1) % 4;

    // Let QEMU's main loop pick up the TX descriptor and run slirp before
    // the caller starts busy-polling for the reply. A benign register read
    // exits to the emulator so the async RX (and any retransmit timers)
    // get processed promptly.
    for (int y = 0; y < 8; y++) port::inl(io_base + REG_CMD);
}

int receive(uint8_t* out, uint32_t max) {
    if (!up) return -1;
    volatile uint16_t* header = (volatile uint16_t*)(rx_buffer + rx_ptr);
    uint16_t status = header[0];
    if (!(status & 0x0001)) {
        // No packet yet. Yield via a benign register read so QEMU's main
        // loop runs and slirp can deliver the pending async DMA reply.
        // Without this, busy-wait poll loops would spin in pure guest
        // code and the reply would never arrive.
        for (int y = 0; y < 8; y++) port::inl(io_base + REG_CMD);
        return -1;
    }

    // QEMU's slirp delivers packets asynchronously from the main loop:
    // the ROK bit can land in the ring before the payload DMA completes.
    // Yield via a benign register read so the device write finishes
    // before we copy the frame.
    for (int y = 0; y < 16; y++) port::inl(io_base + REG_CMD);

    uint16_t size = header[1];
    uint32_t payload_len = (uint32_t)size - 4; // strip CRC
    if (payload_len > max) payload_len = max;

    const uint8_t* src = rx_buffer + rx_ptr + 4;
    uint32_t remaining = RX_BUF_SIZE - (rx_ptr + 4);
    uint32_t first = payload_len < remaining ? payload_len : remaining;
    for (uint32_t i = 0; i < first; i++) out[i] = src[i];
    for (uint32_t i = first; i < payload_len; i++) out[i] = rx_buffer[i - first];

    rx_ptr += (uint32_t)size + 4;
    rx_ptr = (rx_ptr + 3) & ~3;
    if (rx_ptr >= RX_BUF_SIZE) rx_ptr = 0;
    port::outw(io_base + REG_CAPR, rx_ptr - 0x10);
    return (int)payload_len;
}

} // namespace rtl8139
