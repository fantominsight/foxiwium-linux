#pragma once
#include <stdint.h>
#include <stddef.h>

#define CPIO_MAGIC "070701"

struct CPIOHeader {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

namespace initramfs {

static uint8_t* ramdisk_base = nullptr;
static uint64_t ramdisk_size = 0;

struct FileEntry {
    const char* name;
    uint64_t offset;
    uint64_t size;
    uint32_t mode;
};

static FileEntry entries[128];
static int entry_count = 0;

inline uint32_t hex_to_uint(const char* s, int len) {
    uint32_t result = 0;
    for (int i = 0; i < len; i++) {
        result <<= 4;
        if (s[i] >= '0' && s[i] <= '9') result |= s[i] - '0';
        else if (s[i] >= 'a' && s[i] <= 'f') result |= s[i] - 'a' + 10;
        else if (s[i] >= 'A' && s[i] <= 'F') result |= s[i] - 'A' + 10;
    }
    return result;
}

inline bool str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

inline bool str_n_eq(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) break;
    }
    return true;
}

inline void init(uint8_t* base, uint64_t size) {
    ramdisk_base = base;
    ramdisk_size = size;
    entry_count = 0;

    if (!base || size < sizeof(CPIOHeader)) return;

    uint64_t offset = 0;
    while (offset + sizeof(CPIOHeader) <= size) {
        CPIOHeader* hdr = (CPIOHeader*)(base + offset);
        if (!str_n_eq(hdr->magic, CPIO_MAGIC, 6)) break;

        uint32_t namesize = hex_to_uint(hdr->namesize, 8);
        uint32_t filesize_val = hex_to_uint(hdr->filesize, 8);
        uint32_t mode = hex_to_uint(hdr->mode, 8);

        const char* name = (const char*)(base + offset + sizeof(CPIOHeader));

        // GNU cpio pads the name field so the data starts on a 4-byte
        // aligned offset relative to the start of the archive (the header
        // is 110 bytes, i.e. 2 mod 4).
        uint64_t data_offset = (offset + sizeof(CPIOHeader) + namesize + 3) & ~3ULL;

        if (namesize > 2 && !str_n_eq(name, "TRAILER!!!", 10)) {
            if (entry_count < 128) {
                entries[entry_count].name = name;
                entries[entry_count].offset = data_offset;
                entries[entry_count].size = filesize_val;
                entries[entry_count].mode = mode;
                entry_count++;
            }
        }

        offset = data_offset + ((filesize_val + 3) & ~3ULL);
        if (namesize == 0 && filesize_val == 0) break;
    }
}

inline const uint8_t* find_file(const char* name, uint64_t& out_size) {
    for (int i = 0; i < entry_count; i++) {
        if (str_eq(entries[i].name, name)) {
            out_size = entries[i].size;
            return ramdisk_base + entries[i].offset;
        }
    }
    return nullptr;
}

inline const FileEntry* get_entries() { return entries; }
inline int get_entry_count() { return entry_count; }

}
