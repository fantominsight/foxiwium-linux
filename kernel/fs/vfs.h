#pragma once
#include <stdint.h>
#include <stddef.h>
#include "initramfs.h"

namespace vfs {

constexpr int MAX_NODES = 512;
constexpr int MAX_NAME = 64;
constexpr int MAX_PATH = 256;
constexpr int MAX_CHILDREN = 32;
constexpr int MAX_FILE_CONTENT = 4096;

enum NodeType { NODE_DIR, NODE_FILE };

struct VfsNode {
    char name[MAX_NAME];
    NodeType type;
    int parent;
    int children[MAX_CHILDREN];
    int child_count;
    char content[MAX_FILE_CONTENT];
    uint32_t content_size;
    uint32_t mode;
};

static VfsNode nodes[MAX_NODES];
static int node_count = 0;
static int root_idx = -1;

inline int create_node(const char* name, NodeType type, int parent = -1) {
    if (node_count >= MAX_NODES) return -1;
    int idx = node_count++;
    VfsNode& n = nodes[idx];
    int i = 0;
    while (name[i] && i < MAX_NAME - 1) { n.name[i] = name[i]; i++; }
    n.name[i] = 0;
    n.type = type;
    n.parent = parent;
    n.child_count = 0;
    n.content_size = 0;
    n.mode = (type == NODE_DIR) ? 0040755 : 0100644;

    if (parent >= 0 && parent < node_count) {
        VfsNode& p = nodes[parent];
        if (p.child_count < MAX_CHILDREN) {
            p.children[p.child_count++] = idx;
        }
    }
    return idx;
}

inline void write_file(int idx, const char* data, uint32_t size) {
    if (idx < 0 || idx >= node_count) return;
    VfsNode& n = nodes[idx];
    if (size > MAX_FILE_CONTENT) size = MAX_FILE_CONTENT;
    for (uint32_t i = 0; i < size; i++) n.content[i] = data[i];
    n.content_size = size;
}

inline void init() {
    node_count = 0;

    root_idx = create_node("/", NODE_DIR);

    int home = create_node("home", NODE_DIR, root_idx);
    int user = create_node("user", NODE_DIR, home);
    create_node("Desktop", NODE_DIR, user);
    int documents = create_node("Documents", NODE_DIR, user);
    int downloads = create_node("Downloads", NODE_DIR, user);
    int music = create_node("Music", NODE_DIR, user);
    int pictures = create_node("Pictures", NODE_DIR, user);
    int videos = create_node("Videos", NODE_DIR, user);
    int projects = create_node("Projects", NODE_DIR, user);

    int projs_os = create_node("fox-os", NODE_DIR, projects);
    create_node("kernel", NODE_DIR, projs_os);
    create_node("drivers", NODE_DIR, projs_os);
    int projs_web = create_node("web-server", NODE_DIR, projects);
    create_node("src", NODE_DIR, projs_web);
    create_node("public", NODE_DIR, projs_web);

    int readme = create_node("README.md", NODE_FILE, user);
    const char* readme_content =
        "# Foxiwium OS\n"
        "A hobby operating system written from scratch.\n"
        "\n"
        "## Features\n"
        "- x86_64 long mode\n"
        "- Custom GUI desktop environment\n"
        "- Virtual filesystem\n"
        "- Process management\n"
        "- Keyboard and mouse support\n"
        "\n"
        "## Building\n"
        "Run `make` to build the kernel and ISO.\n"
        "Run `make run` to launch in QEMU.\n";
    write_file(readme, readme_content, 223);

    int makefile = create_node("Makefile", NODE_FILE, user);
    const char* makefile_content =
        "all: kernel initramfs iso\n"
        "\tkernel -- build kernel binary\n"
        "\tinitramfs -- pack root filesystem\n"
        "\tiso -- create bootable image\n"
        "\n"
        "clean:\n"
        "\trm -rf build/\n";
    write_file(makefile, makefile_content, 107);

    int foxconf = create_node("fox.conf", NODE_FILE, user);
    const char* foxconf_content =
        "# Foxiwium configuration\n"
        "theme=dark\n"
        "resolution=1920x1080\n"
        "shell=/bin/sh\n"
        "hostname=foxiwium\n"
        "timezone=UTC\n";
    write_file(foxconf, foxconf_content, 106);

    int bashrc = create_node(".bashrc", NODE_FILE, user);
    const char* bashrc_content =
        "# ~/.bashrc\n"
        "export PS1='\\u@foxiwium:\\w$ '\n"
        "export PATH=/bin:/usr/bin\n"
        "alias ll='ls -la'\n"
        "alias cls='clear'\n";
    write_file(bashrc, bashrc_content, 97);

    int doc_notes = create_node("notes.txt", NODE_FILE, documents);
    const char* notes_content =
        "TODO:\n"
        "- Implement ext4 driver\n"
        "- Add network stack\n"
        "- Write more userspace apps\n"
        "- Add sound support\n"
        "- Port a C library\n";
    write_file(doc_notes, notes_content, 105);

    int doc_todo = create_node("todo.md", NODE_FILE, documents);
    const char* todo_content =
        "# Project TODO\n"
        "## High Priority\n"
        "- Fix memory leak in VMM\n"
        "- Implement syscall for file I/O\n"
        "\n"
        "## Low Priority\n"
        "- Add wallpaper selection\n"
        "- Fancy animations\n";
    write_file(doc_todo, todo_content, 126);

    int wiki = create_node("wiki.html", NODE_FILE, documents);
    const char* wiki_content =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <title>Foxiwium Wiki</title>\n"
        "</head>\n"
        "<body>\n"
        "  <h1>Foxiwium OS</h1>\n"
        "  <p>Foxiwium is a <b>hobby operating system</b> written from scratch for x86_64.</p>\n"
        "  <h2>Features</h2>\n"
        "  <ul>\n"
        "    <li><a href=\"file:///home/user/Documents/features.html\">Feature list</a></li>\n"
        "    <li><a href=\"file:///home/user/Documents/todo.md\">Project roadmap</a></li>\n"
        "    <li><a href=\"https://example.com/\">Example.com</a></li>\n"
        "  </ul>\n"
        "  <h2>Get started</h2>\n"
        "  <p>Open the <a href=\"file:///home/user/Documents\">Documents folder</a> to browse files.</p>\n"
        "  <hr>\n"
        "  <p>Built with <b>love</b> and raw assembly.</p>\n"
        "</body>\n"
        "</html>\n";
    write_file(wiki, wiki_content, (uint32_t)sizeof(wiki_content) - 1);

    int features = create_node("features.html", NODE_FILE, documents);
    const char* features_content =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <title>Foxiwium Features</title>\n"
        "</head>\n"
        "<body>\n"
        "  <h1>Features</h1>\n"
        "  <ul>\n"
        "    <li>Long mode x86_64 kernel</li>\n"
        "    <li>Custom GUI desktop</li>\n"
        "    <li>Virtual filesystem</li>\n"
        "    <li>Process management</li>\n"
        "    <li>Network stack with a browser</li>\n"
        "  </ul>\n"
        "  <p>Back to the <a href=\"file:///home/user/Documents/wiki.html\">wiki</a>.</p>\n"
        "</body>\n"
        "</html>\n";
    write_file(features, features_content, (uint32_t)sizeof(features_content) - 1);

    int hello = create_node("hello.html", NODE_FILE, documents);
    const char* hello_content =
        "<html>\n"
        "<head>\n"
        "  <title>Hello Browser</title>\n"
        "</head>\n"
        "<body>\n"
        "  <h1>Hello, browser!</h1>\n"
        "  <p>This page was loaded from the <b>virtual filesystem</b>.</p>\n"
        "  <hr>\n"
        "  <ul>\n"
        "    <li>Headings: <b>h1</b>, <b>h2</b></li>\n"
        "    <li>Bold and <i>italic</i></li>\n"
        "    <li><a href=\"file:///home/user/Documents/wiki.html\">Links</a></li>\n"
        "  </ul>\n"
        "</body>\n"
        "</html>\n";
    write_file(hello, hello_content, (uint32_t)sizeof(hello_content) - 1);

    int dl1 = create_node("update-v1.8.tar.gz", NODE_FILE, downloads);
    write_file(dl1, "(binary data)", 13);

    int dl2 = create_node("wallpaper.png", NODE_FILE, downloads);
    write_file(dl2, "(binary data)", 13);

    int pic1 = create_node("screenshot.png", NODE_FILE, pictures);
    write_file(pic1, "(binary data)", 13);

    int music1 = create_node("chill-beats.wav", NODE_FILE, music);
    write_file(music1, "(binary data)", 13);

    int vid1 = create_node("demo录屏.mp4", NODE_FILE, videos);
    write_file(vid1, "(binary data)", 13);

    int etc = create_node("etc", NODE_DIR, root_idx);
    int hostname_f = create_node("hostname", NODE_FILE, etc);
    write_file(hostname_f, "foxiwium\n", 9);
    int hosts_f = create_node("hosts", NODE_FILE, etc);
    write_file(hosts_f, "127.0.0.1 localhost\n127.0.1.1 foxiwium\n", 39);
    int passwd_f = create_node("passwd", NODE_FILE, etc);
    write_file(passwd_f, "root:x:0:0:root:/root:/bin/sh\nuser:x:1000:1000:user:/home/user:/bin/sh\n", 74);

    int bin = create_node("bin", NODE_DIR, root_idx);
    create_node("sh", NODE_FILE, bin);
    create_node("ls", NODE_FILE, bin);
    create_node("cat", NODE_FILE, bin);
    create_node("echo", NODE_FILE, bin);
    create_node("mkdir", NODE_FILE, bin);
    create_node("clear", NODE_FILE, bin);

    int dev = create_node("dev", NODE_DIR, root_idx);
    create_node("null", NODE_FILE, dev);
    create_node("zero", NODE_FILE, dev);
    create_node("tty0", NODE_FILE, dev);

    int proc = create_node("proc", NODE_DIR, root_idx);
    create_node("cpuinfo", NODE_FILE, proc);
    create_node("meminfo", NODE_FILE, proc);
    create_node("uptime", NODE_FILE, proc);
}

inline int find_child(int parent_idx, const char* name) {
    if (parent_idx < 0 || parent_idx >= node_count) return -1;
    VfsNode& p = nodes[parent_idx];
    for (int i = 0; i < p.child_count; i++) {
        int ci = p.children[i];
        if (ci >= 0 && ci < node_count) {
            const char* a = nodes[ci].name;
            const char* b = name;
            bool match = true;
            while (*a && *b) { if (*a != *b) { match = false; break; } a++; b++; }
            if (match && *a == 0 && *b == 0) return ci;
        }
    }
    return -1;
}

inline int resolve_path(const char* path) {
    if (!path || path[0] == 0) return root_idx;

    int cur = (path[0] == '/') ? root_idx : -1;
    if (cur == -1) return -1;

    const char* p = path + 1;
    while (*p) {
        while (*p == '/') p++;
        if (*p == 0) break;

        char component[MAX_NAME];
        int ci = 0;
        while (*p && *p != '/' && ci < MAX_NAME - 1) {
            component[ci++] = *p++;
        }
        component[ci] = 0;

        if (ci == 1 && component[0] == '.') continue;
        if (ci == 2 && component[0] == '.' && component[1] == '.') {
            cur = (cur >= 0 && nodes[cur].parent >= 0) ? nodes[cur].parent : cur;
            continue;
        }

        cur = find_child(cur, component);
        if (cur == -1) return -1;
    }
    return cur;
}

inline int get_children(int dir_idx, int* out_children, int max_out) {
    if (dir_idx < 0 || dir_idx >= node_count) return 0;
    if (nodes[dir_idx].type != NODE_DIR) return 0;
    int count = nodes[dir_idx].child_count;
    if (count > max_out) count = max_out;
    for (int i = 0; i < count; i++) {
        out_children[i] = nodes[dir_idx].children[i];
    }
    return count;
}

inline VfsNode* get_node(int idx) {
    if (idx < 0 || idx >= node_count) return nullptr;
    return &nodes[idx];
}

inline int get_root() { return root_idx; }
inline int get_node_count() { return node_count; }

// Merge the files from the boot initramfs (cpio archive) into the VFS.
// Existing nodes are kept; only missing paths are created.
inline void mount_initramfs() {
    int n = initramfs::get_entry_count();
    for (int i = 0; i < n; i++) {
        const initramfs::FileEntry& e = initramfs::get_entries()[i];
        const char* name = e.name;
        if (!name || name[0] == 0) continue;
        if (name[0] == '.' && name[1] == 0) continue;
        if (initramfs::str_n_eq(name, "TRAILER!!!", 10)) continue;

        const char* p = name;
        if (p[0] == '.' && p[1] == '/') p += 2;
        if (p[0] == 0) continue;

        bool is_dir = (e.mode & 0170000) == 0040000;

        int parent = root_idx;
        char comp[MAX_NAME];
        int ci = 0;
        const char* cur = p;
        bool ok = true;

        while (ok && *cur) {
            if (*cur == '/') {
                if (ci > 0) {
                    comp[ci] = 0;
                    int child = find_child(parent, comp);
                    if (child == -1) {
                        child = create_node(comp, NODE_DIR, parent);
                        if (child == -1) { ok = false; break; }
                    }
                    parent = child;
                    ci = 0;
                }
                cur++;
            } else {
                if (ci < MAX_NAME - 1) comp[ci++] = *cur;
                cur++;
            }
        }

        if (!ok) continue;

        if (ci > 0) {
            comp[ci] = 0;
            int child = find_child(parent, comp);
            if (child == -1) {
                child = create_node(comp, is_dir ? NODE_DIR : NODE_FILE, parent);
                if (child == -1) continue;
                if (!is_dir) {
                    uint64_t file_size = e.size;
                    const uint8_t* data = initramfs::find_file(name, file_size);
                    uint32_t sz = file_size > MAX_FILE_CONTENT ? MAX_FILE_CONTENT : (uint32_t)file_size;
                    if (data && sz > 0) write_file(child, (const char*)data, sz);
                }
            }
        }
    }
}

}
