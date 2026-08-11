#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../fs/vfs.h"
#include "../mm/pmm.h"
#include "../proc/process.h"
#include "../drivers/pit.h"
#include "../drivers/net.h"
#include "../graphics/framebuffer.h"

namespace shell {

constexpr int MAX_OUTPUT = 16384;
constexpr int MAX_HISTORY = 64;
constexpr int MAX_CMD_LEN = 512;
constexpr int MAX_ARGS = 32;
constexpr int MAX_ENV = 64;
constexpr int MAX_ALIAS = 32;

struct EnvVar { char key[64]; char value[256]; };
struct AliasDef { char name[64]; char value[256]; };

struct ShellState {
    char cwd[vfs::MAX_PATH];
    char output[MAX_OUTPUT];
    int output_len;
    char history[MAX_HISTORY][MAX_CMD_LEN];
    int history_count;
    int history_idx;
    bool cursor_visible;
    int cursor_tick;
    EnvVar env[MAX_ENV];
    int env_count;
    AliasDef aliases[MAX_ALIAS];
    int alias_count;
    char last_output[MAX_OUTPUT];
    int last_output_len;
};

static ShellState shell_state;

// ==================== BASIC HELPERS ====================
inline int str_len(const char* s) { int l = 0; while (*s++) l++; return l; }

inline bool str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == 0 && *b == 0;
}

inline int str_cmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

inline bool str_starts_with(const char* s, const char* p) {
    while (*p) { if (*s != *p) return false; s++; p++; }
    return true;
}

inline bool str_contains(const char* s, const char* sub) {
    if (!sub[0]) return true;
    for (; *s; s++) {
        const char* a = s; const char* b = sub;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*b == 0) return true;
    }
    return false;
}

inline void str_lower(char* s) { while (*s) { if (*s >= 'A' && *s <= 'Z') *s += 32; s++; } }

inline void itoa(int val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char tmp[16]; int i = 0; bool neg = val < 0;
    if (neg) val = -val;
    while (val > 0) { tmp[i++] = '0' + val % 10; val /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0; while (i > 0) buf[j++] = tmp[--i]; buf[j] = 0;
}

inline void itoa_u64(uint64_t val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char tmp[24]; int i = 0;
    while (val > 0) { tmp[i++] = '0' + val % 10; val /= 10; }
    int j = 0; while (i > 0) buf[j++] = tmp[--i]; buf[j] = 0;
}

inline int atoi(const char* s) {
    int r = 0; bool neg = false;
    while (*s == ' ') s++;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; }
    return neg ? -r : r;
}

inline void str_cat(char* d, const char* s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

inline void str_cpy(char* d, const char* s) { while (*s) *d++ = *s++; *d = 0; }

inline void str_rev(char* s, int len) { for (int i = 0; i < len / 2; i++) { char t = s[i]; s[i] = s[len-1-i]; s[len-1-i] = t; } }

inline int hex_val(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }

// ==================== OUTPUT ====================
inline void append_output(const char* s) {
    if (shell_state.output_len >= MAX_OUTPUT - 256) {
        int half = MAX_OUTPUT / 2;
        for (int i = 0; i < half; i++) shell_state.output[i] = shell_state.output[i + half];
        shell_state.output_len -= half;
        shell_state.output[shell_state.output_len] = 0;
    }
    while (*s && shell_state.output_len < MAX_OUTPUT - 1) {
        shell_state.output[shell_state.output_len++] = *s++;
    }
    shell_state.output[shell_state.output_len] = 0;
}

inline void append_char(char c) {
    if (shell_state.output_len < MAX_OUTPUT - 1) {
        shell_state.output[shell_state.output_len++] = c;
        shell_state.output[shell_state.output_len] = 0;
    }
}

inline void clear_output() { shell_state.output_len = 0; shell_state.output[0] = 0; }

inline void print_int(int v) { char b[16]; itoa(v, b); append_output(b); }
inline void print_u64(uint64_t v) { char b[24]; itoa_u64(v, b); append_output(b); }
inline void append_hex8(uint8_t v) {
    const char* hex = "0123456789abcdef";
    append_char(hex[(v >> 4) & 0xF]);
    append_char(hex[v & 0xF]);
}

// ==================== ENV / ALIASES ====================
inline void env_set(const char* key, const char* val) {
    for (int i = 0; i < shell_state.env_count; i++) {
        if (str_eq(shell_state.env[i].key, key)) { str_cpy(shell_state.env[i].value, val); return; }
    }
    if (shell_state.env_count < MAX_ENV) {
        str_cpy(shell_state.env[shell_state.env_count].key, key);
        str_cpy(shell_state.env[shell_state.env_count].value, val);
        shell_state.env_count++;
    }
}

inline const char* env_get(const char* key) {
    for (int i = 0; i < shell_state.env_count; i++) {
        if (str_eq(shell_state.env[i].key, key)) return shell_state.env[i].value;
    }
    return nullptr;
}

inline void env_unset(const char* key) {
    for (int i = 0; i < shell_state.env_count; i++) {
        if (str_eq(shell_state.env[i].key, key)) {
            for (int j = i; j < shell_state.env_count - 1; j++)
                shell_state.env[j] = shell_state.env[j + 1];
            shell_state.env_count--; return;
        }
    }
}

inline void alias_set(const char* name, const char* val) {
    for (int i = 0; i < shell_state.alias_count; i++) {
        if (str_eq(shell_state.aliases[i].name, name)) { str_cpy(shell_state.aliases[i].value, val); return; }
    }
    if (shell_state.alias_count < MAX_ALIAS) {
        str_cpy(shell_state.aliases[shell_state.alias_count].name, name);
        str_cpy(shell_state.aliases[shell_state.alias_count].value, val);
        shell_state.alias_count++;
    }
}

inline const char* alias_get(const char* name) {
    for (int i = 0; i < shell_state.alias_count; i++) {
        if (str_eq(shell_state.aliases[i].name, name)) return shell_state.aliases[i].value;
    }
    return nullptr;
}

// ==================== PATH HELPERS ====================
inline void get_full_path(const char* rel, char* out) {
    if (rel[0] == '/') { str_cpy(out, rel); return; }
    str_cpy(out, shell_state.cwd);
    int l = str_len(out);
    if (l > 1 || out[0] != '/') { out[l] = '/'; l++; }
    int j = 0; while (rel[j] && l < vfs::MAX_PATH - 1) { out[l++] = rel[j++]; }
    out[l] = 0;
}

inline int resolve_file(const char* path) {
    char full[vfs::MAX_PATH]; get_full_path(path, full);
    return vfs::resolve_path(full);
}

// ==================== SHELL INIT ====================
inline void init() {
    shell_state.cwd[0] = '/'; shell_state.cwd[1] = 0;
    shell_state.output_len = 0;
    shell_state.history_count = 0;
    shell_state.env_count = 0;
    shell_state.alias_count = 0;

    env_set("HOME", "/home/user");
    env_set("USER", "user");
    env_set("SHELL", "/bin/sh");
    env_set("PATH", "/bin:/usr/bin");
    env_set("TERM", "xterm-256color");
    env_set("HOSTNAME", "foxiwium");
    env_set("LANG", "en_US.UTF-8");
    env_set("PS1", "\\u@\\h:\\w$ ");
    env_set("EDITOR", "foxedit");
    env_set("LOGNAME", "user");
    env_set("MAIL", "/var/mail/user");
    env_set("TMPDIR", "/tmp");
    env_set("DISPLAY", ":0");
    env_set("XDG_SESSION_TYPE", "tty");
    env_set("SHLVL", "1");

    alias_set("ll", "ls -la");
    alias_set("la", "ls -a");
    alias_set("l", "ls -CF");
    alias_set("cls", "clear");
    alias_set("q", "exit");
    alias_set("..", "cd ..");
    alias_set("...", "cd ../..");
    alias_set("grep", "grep --color=auto");
    alias_set("rm", "rm -i");
    alias_set("cp", "cp -i");
    alias_set("mv", "mv -i");

    const char* welcome =
        "\n"
        "  ╔═══════════════════════════════════════════╗\n"
        "  ║          Foxiwium Terminal v1.0           ║\n"
        "  ║   Type 'help' for available commands.     ║\n"
        "  ╚═══════════════════════════════════════════╝\n\n";
    append_output(welcome);
}

inline void parse_args(const char* cmd, char args[MAX_ARGS][MAX_CMD_LEN], int& argc) {
    argc = 0;
    bool in_single = false, in_double = false;
    while (*cmd && argc < MAX_ARGS) {
        while (*cmd == ' ' && !in_single && !in_double) cmd++;
        if (*cmd == 0) break;
        int i = 0;
        while (*cmd && (in_single || in_double || (*cmd != ' '))) {
            if (*cmd == '\'' && !in_double) { in_single = !in_single; cmd++; continue; }
            if (*cmd == '"' && !in_single) { in_double = !in_double; cmd++; continue; }
            if (*cmd == '\\' && !in_single && *(cmd+1)) { cmd++; }
            if (i < MAX_CMD_LEN - 1) args[argc][i++] = *cmd;
            cmd++;
        }
        args[argc][i] = 0; argc++;
    }
}

// ==================== MISSING COMMANDS ====================
inline void cmd_cd(const char* path) {
    if (!path || str_eq(path, "~")) {
        const char* home = env_get("HOME");
        if (home) { str_cpy(shell_state.cwd, home); }
        return;
    }
    if (str_eq(path, "-")) {
        append_output(shell_state.cwd); append_output("\n");
        return;
    }
    char full[vfs::MAX_PATH];
    if (path[0] == '/') { str_cpy(full, path); }
    else { get_full_path(path, full); }
    int idx = vfs::resolve_path(full);
    if (idx < 0) { append_output("cd: no such file or directory: "); append_output(path); append_output("\n"); return; }
    vfs::VfsNode* n = vfs::get_node(idx);
    if (n->type != vfs::NODE_DIR) { append_output("cd: not a directory: "); append_output(path); append_output("\n"); return; }
    str_cpy(shell_state.cwd, full);
}

inline void cmd_pwd() { append_output(shell_state.cwd); append_output("\n"); }

inline void cmd_cat(const char* path) {
    if (!path) { append_output("cat: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("cat: "); append_output(path); append_output(": No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    if (fn->type == vfs::NODE_DIR) { append_output("cat: "); append_output(path); append_output(": Is a directory\n"); return; }
    for (uint32_t i = 0; i < fn->content_size; i++) append_char(fn->content[i]);
    if (fn->content_size > 0 && fn->content[fn->content_size - 1] != '\n') append_char('\n');
}

inline void cmd_mkdir(const char* path) {
    if (!path) { append_output("mkdir: missing operand\n"); return; }
    char full[vfs::MAX_PATH]; get_full_path(path, full);
    if (vfs::resolve_path(full) >= 0) { append_output("mkdir: cannot create directory '"); append_output(path); append_output("': File exists\n"); return; }
    char parent_path[vfs::MAX_PATH]; str_cpy(parent_path, full);
    char* last_slash = nullptr;
    for (char* p = parent_path; *p; p++) { if (*p == '/') last_slash = p; }
    char new_name[vfs::MAX_NAME];
    if (last_slash) {
        int k = 0; const char* s = last_slash + 1; while (*s) new_name[k++] = *s++; new_name[k] = 0;
        if (last_slash == parent_path) { parent_path[1] = 0; } else { *last_slash = 0; }
    } else { str_cpy(new_name, full); str_cpy(parent_path, "/"); }
    int pi = vfs::resolve_path(parent_path);
    if (pi < 0) { append_output("mkdir: cannot create directory '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::create_node(new_name, vfs::NODE_DIR, pi);
}

inline void cmd_touch(const char* path) {
    if (!path) { append_output("touch: missing file operand\n"); return; }
    char full[vfs::MAX_PATH]; get_full_path(path, full);
    int idx = vfs::resolve_path(full);
    if (idx >= 0) return;
    char parent_path[vfs::MAX_PATH]; str_cpy(parent_path, full);
    char* last_slash = nullptr;
    for (char* p = parent_path; *p; p++) { if (*p == '/') last_slash = p; }
    char new_name[vfs::MAX_NAME];
    if (last_slash) {
        int k = 0; const char* s = last_slash + 1; while (*s) new_name[k++] = *s++; new_name[k] = 0;
        if (last_slash == parent_path) { parent_path[1] = 0; } else { *last_slash = 0; }
    } else { str_cpy(new_name, full); str_cpy(parent_path, "/"); }
    int pi = vfs::resolve_path(parent_path);
    if (pi < 0) { append_output("touch: cannot touch '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::create_node(new_name, vfs::NODE_FILE, pi);
}

inline void cmd_ps() {
    append_output("  PID TTY          TIME CMD\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb.processes[i].state != PROC_UNUSED) {
            char b[16]; itoa(pcb.processes[i].pid, b);
            int pad = 5 - shell::str_len(b); for (int p = 0; p < pad; p++) append_char(' ');
            append_output(b); append_output(" ?        00:00:00 ");
            append_output(pcb.processes[i].name); append_output("\n");
        }
    }
}

inline void cmd_hostname() {
    const char* h = env_get("HOSTNAME");
    append_output(h ? h : "foxiwium"); append_output("\n");
}

inline void cmd_neofetch() {
    const char* fox =
        "        /\\_/\\  \n"
        "       ( o.o ) \n"
        "        > ^ <  \n"
        "       /|   |\\ \n"
        "      (_|   |_)\n";
    const char* lines[] = {
        "OS: Foxiwium v0.1.0",
        "Host: Foxiwium Machine",
        "Kernel: foxiwium-kernel",
        "Shell: foxish",
        "Terminal: foxterm",
        "CPU: x86_64 vCPU",
        "Memory: "
    };
    char mem_buf[32];
    uint64_t total = pmm::get_total_mb();
    uint64_t used = pmm::get_used_mb();
    char b[16]; itoa((int)used, b);
    str_cpy(mem_buf, b); str_cat(mem_buf, "MB / ");
    itoa((int)total, b); str_cat(mem_buf, b); str_cat(mem_buf, "MB");

    const char* fox_lines[6]; int fi = 0;
    const char* p = fox;
    while (*p && fi < 6) { fox_lines[fi] = p; while (*p && *p != '\n') p++; if (*p == '\n') p++; fi++; }

    for (int i = 0; i < 7; i++) {
        if (i < fi) { append_output(fox_lines[i]); } else { for (int s = 0; s < 16; s++) append_char(' '); }
        append_output("  \033[34m"); append_output(lines[i]); append_output("\033[0m");
        if (i == 6) append_output(mem_buf);
        append_output("\n");
    }
    append_output("\033[34m"); for (int i = 0; i < 20; i++) append_output("\xe2\x96\x88"); append_output("\033[0m");
    for (int i = 0; i < 20; i++) append_output("\xe2\x96\x91");
    for (int i = 0; i < 20; i++) append_output("\xe2\x96\x93");
    for (int i = 0; i < 20; i++) append_output("\xe2\x96\x92");
    for (int i = 0; i < 20; i++) append_output("\xe2\x96\x88");
    append_output("\n\n");
}

// Forward declarations for recursive calls
inline void execute(const char* cmd);
inline void cmd_tree_r(const char* path, int depth, int maxd);

// ==================== FILE OPERATIONS ====================
inline void cmd_cp(const char* src, const char* dst) {
    if (!src || !dst) { append_output("cp: missing file operand\n"); return; }
    int si = resolve_file(src);
    if (si < 0) { append_output("cp: cannot stat '"); append_output(src); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* sn = vfs::get_node(si);
    if (sn->type == vfs::NODE_DIR) { append_output("cp: -r not specified; omitting directory '"); append_output(src); append_output("'\n"); return; }
    char dfull[vfs::MAX_PATH]; get_full_path(dst, dfull);
    int di = vfs::resolve_path(dfull);
    if (di >= 0 && vfs::get_node(di)->type == vfs::NODE_DIR) {
        char newpath[vfs::MAX_PATH]; str_cpy(newpath, dfull); int l = str_len(newpath);
        if (l > 1 || newpath[0] != '/') { newpath[l] = '/'; l++; }
        int j = 0; while (sn->name[j]) newpath[l++] = sn->name[j++];
        newpath[l] = 0;
        di = vfs::resolve_path(newpath);
    }
    int parent_idx = -1;
    char newname[vfs::MAX_NAME];
    if (di >= 0) {
        vfs::VfsNode* dn = vfs::get_node(di);
        if (dn->type == vfs::NODE_DIR) {
            parent_idx = di; str_cpy(newname, sn->name);
        } else {
            parent_idx = dn->parent; str_cpy(newname, sn->name);
        }
    } else {
        char dp[vfs::MAX_PATH]; str_cpy(dp, dfull);
        int last_slash = -1; for (int i = 0; dp[i]; i++) if (dp[i] == '/') last_slash = i;
        if (last_slash >= 0) {
            char parent_path[vfs::MAX_PATH];
            for (int i = 0; i < last_slash; i++) parent_path[i] = dp[i];
            parent_path[last_slash] = 0;
            if (parent_path[0] == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
            parent_idx = vfs::resolve_path(parent_path);
            int k = 0; while (dp[last_slash + 1 + k]) { newname[k] = dp[last_slash + 1 + k]; k++; }
            newname[k] = 0;
        } else {
            parent_idx = vfs::resolve_path(shell_state.cwd);
            str_cpy(newname, dfull);
        }
    }
    if (parent_idx < 0) { append_output("cp: cannot create '"); append_output(dst); append_output("': No such file or directory\n"); return; }
    int ni = vfs::create_node(newname, vfs::NODE_FILE, parent_idx);
    if (ni < 0) { append_output("cp: failed to create node\n"); return; }
    vfs::write_file(ni, sn->content, sn->content_size);
}

inline void cmd_mv(const char* src, const char* dst) {
    if (!src || !dst) { append_output("mv: missing file operand\n"); return; }
    int si = resolve_file(src);
    if (si < 0) { append_output("mv: cannot stat '"); append_output(src); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* sn = vfs::get_node(si);
    int old_parent = sn->parent;
    char dfull[vfs::MAX_PATH]; get_full_path(dst, dfull);
    int di = vfs::resolve_path(dfull);
    int new_parent = -1;
    char newname[vfs::MAX_NAME];
    if (di >= 0 && vfs::get_node(di)->type == vfs::NODE_DIR) {
        new_parent = di; str_cpy(newname, sn->name);
    } else if (di >= 0) {
        new_parent = vfs::get_node(di)->parent; str_cpy(newname, sn->name);
    } else {
        char dp[vfs::MAX_PATH]; str_cpy(dp, dfull);
        int last_slash = -1; for (int i = 0; dp[i]; i++) if (dp[i] == '/') last_slash = i;
        if (last_slash >= 0) {
            char pp[vfs::MAX_PATH]; for (int i = 0; i < last_slash; i++) pp[i] = dp[i];
            pp[last_slash] = 0; if (pp[0] == 0) { pp[0] = '/'; pp[1] = 0; }
            new_parent = vfs::resolve_path(pp);
            int k = 0; while (dp[last_slash + 1 + k]) { newname[k] = dp[last_slash + 1 + k]; k++; } newname[k] = 0;
        } else {
            new_parent = vfs::resolve_path(shell_state.cwd); str_cpy(newname, dfull);
        }
    }
    if (new_parent < 0) { append_output("mv: cannot move to '"); append_output(dst); append_output("'\n"); return; }
    if (old_parent >= 0) {
        vfs::VfsNode* op = vfs::get_node(old_parent);
        for (int i = 0; i < op->child_count; i++) {
            if (op->children[i] == si) {
                for (int j = i; j < op->child_count - 1; j++) op->children[j] = op->children[j + 1];
                op->child_count--; break;
            }
        }
    }
    str_cpy(sn->name, newname);
    sn->parent = new_parent;
    vfs::VfsNode* np = vfs::get_node(new_parent);
    if (np && np->child_count < vfs::MAX_CHILDREN) np->children[np->child_count++] = si;
}

inline void cmd_rm_r(const char* path, bool recursive) {
    if (!path || path[0] == 0) { append_output("rm: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("rm: cannot remove '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    if (fn->type == vfs::NODE_DIR && !recursive) {
        if (fn->child_count > 0) { append_output("rm: cannot remove '"); append_output(path); append_output("': Is a directory\n"); return; }
    }
    if (fn->type == vfs::NODE_DIR && recursive) {
        for (int c = fn->child_count - 1; c >= 0; c--) {
            vfs::VfsNode* child = vfs::get_node(fn->children[c]);
            if (child && child->type == vfs::NODE_DIR && child->child_count > 0) {
                char sub[vfs::MAX_PATH]; get_full_path(path, sub);
                int l = str_len(sub); sub[l] = '/'; l++;
                int j = 0; while (child->name[j]) sub[l++] = child->name[j++]; sub[l] = 0;
                cmd_rm_r(sub, true);
            }
        }
    }
    if (fn->parent >= 0) {
        vfs::VfsNode* p = vfs::get_node(fn->parent);
        for (int i = 0; i < p->child_count; i++) {
            if (p->children[i] == fi) {
                for (int j = i; j < p->child_count - 1; j++) p->children[j] = p->children[j + 1];
                p->child_count--; break;
            }
        }
    }
}

inline void cmd_rmdir(const char* path) {
    if (!path) { append_output("rmdir: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("rmdir: failed to remove '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    if (fn->type != vfs::NODE_DIR) { append_output("rmdir: failed to remove '"); append_output(path); append_output("': Not a directory\n"); return; }
    if (fn->child_count > 0) { append_output("rmdir: failed to remove '"); append_output(path); append_output("': Directory not empty\n"); return; }
    if (fn->parent >= 0) {
        vfs::VfsNode* p = vfs::get_node(fn->parent);
        for (int i = 0; i < p->child_count; i++) {
            if (p->children[i] == fi) {
                for (int j = i; j < p->child_count - 1; j++) p->children[j] = p->children[j + 1];
                p->child_count--; break;
            }
        }
    }
}

inline void cmd_ln(const char* target, const char* linkname) {
    if (!target || !linkname) { append_output("ln: missing file operand\n"); return; }
    int ti = resolve_file(target);
    if (ti < 0) { append_output("ln: failed to access '"); append_output(target); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* tn = vfs::get_node(ti);
    int dir_idx = vfs::resolve_path(shell_state.cwd);
    int ni = vfs::create_node(linkname, tn->type, dir_idx);
    if (ni >= 0) vfs::write_file(ni, tn->content, tn->content_size);
}

inline void cmd_find_r(const char* base, int dir_i, const char* name_match, const char* type_match, int max_depth, int depth) {
    if (depth > max_depth) return;
    if (dir_i < 0) return;
    vfs::VfsNode* node = vfs::get_node(dir_i);
    if (!node) return;

    bool name_ok = true;
    if (name_match && name_match[0]) {
        name_ok = false;
        const char* n = node->name;
        const char* m = name_match;
        bool match = true;
        while (*m) { if (*n != *m) { match = false; break; } n++; m++; }
        if (match && *n == 0) name_ok = true;
        if (!name_ok) {
            const char* nn = node->name; const char* mm = name_match;
            bool wild = false; if (*mm == '*') { wild = true; mm++; }
            if (wild) { while (*nn) { const char* a = nn; const char* b = mm; while (*b && *a) { if (*a != *b) break; a++; b++; } if (*b == 0) { name_ok = true; break; } nn++; } }
        }
    }
    bool type_ok = true;
    if (type_match && type_match[0]) {
        if (type_match[0] == 'f' && node->type != vfs::NODE_FILE) type_ok = false;
        if (type_match[0] == 'd' && node->type != vfs::NODE_DIR) type_ok = false;
    }
    if (name_ok && type_ok) {
        char full[vfs::MAX_PATH]; str_cpy(full, base);
        int l = str_len(full);
        if (!(l == 1 && full[0] == '/')) { full[l] = '/'; l++; }
        int j = 0; while (node->name[j]) full[l++] = node->name[j++];
        full[l] = 0;
        append_output(full); append_output("\n");
    }
    if (node->type == vfs::NODE_DIR) {
        char sub[vfs::MAX_PATH]; str_cpy(sub, base);
        int l = str_len(sub);
        if (!(l == 1 && sub[0] == '/')) { sub[l] = '/'; l++; }
        int j = 0; while (node->name[j]) sub[l++] = node->name[j++]; sub[l] = 0;
        for (int c = 0; c < node->child_count; c++) {
            cmd_find_r(sub, node->children[c], name_match, type_match, max_depth, depth + 1);
        }
    }
}

inline void cmd_find(const char* path, const char* name, const char* type) {
    const char* start = (path && path[0]) ? path : "/";
    int si = vfs::resolve_path(start);
    if (si < 0) { append_output("find: '"); append_output(start); append_output("': No such file or directory\n"); return; }
    append_output(start); append_output("\n");
    vfs::VfsNode* sn = vfs::get_node(si);
    if (sn->type == vfs::NODE_DIR) {
        for (int c = 0; c < sn->child_count; c++) {
            cmd_find_r(start, sn->children[c], name, type, 10, 0);
        }
    }
}

inline void cmd_file_cmd(const char* path) {
    if (!path) { append_output("file: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("file: '"); append_output(path); append_output("': cannot open\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    if (fn->type == vfs::NODE_DIR) { append_output(path); append_output(": directory\n"); return; }
    const char* ext = "";
    int nl = str_len(fn->name);
    for (int i = nl - 1; i >= 0; i--) { if (fn->name[i] == '.') { ext = fn->name + i + 1; break; } }
    append_output(path); append_output(": ");
    if (str_eq(ext, "txt") || str_eq(ext, "md") || str_eq(ext, "log") || str_eq(ext, "conf") || str_eq(ext, "cfg") || str_eq(ext, "xml") || str_eq(ext, "json") || str_eq(ext, "yaml") || str_eq(ext, "yml") || str_eq(ext, "csv") || str_eq(ext, "sh") || str_eq(ext, "bashrc")) append_output("ASCII text");
    else if (str_eq(ext, "c") || str_eq(ext, "h") || str_eq(ext, "cpp") || str_eq(ext, "hpp")) append_output("C/C++ source");
    else if (str_eq(ext, "py")) append_output("Python script");
    else if (str_eq(ext, "js")) append_output("JavaScript source");
    else if (str_eq(ext, "html") || str_eq(ext, "htm")) append_output("HTML document");
    else if (str_eq(ext, "css")) append_output("CSS stylesheet");
    else if (str_eq(ext, "png")) append_output("PNG image data");
    else if (str_eq(ext, "jpg") || str_eq(ext, "jpeg")) append_output("JPEG image data");
    else if (str_eq(ext, "gif")) append_output("GIF image data");
    else if (str_eq(ext, "mp3")) append_output("Audio (MPEG)");
    else if (str_eq(ext, "mp4")) append_output("ISO Media, MP4");
    else if (str_eq(ext, "tar") || str_eq(ext, "gz") || str_eq(ext, "zip")) append_output("archive data");
    else if (str_eq(ext, "o") || str_eq(ext, "bin")) append_output("ELF executable");
    else if (str_eq(ext, "pdf")) append_output("PDF document");
    else if (fn->content_size == 0) append_output("empty");
    else append_output("data");
    append_output("\n");
}

inline void cmd_stat(const char* path) {
    if (!path) { append_output("stat: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("stat: cannot stat '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    append_output("  File: "); append_output(path); append_output("\n");
    append_output("  Size: "); print_u64(fn->content_size); append_output("\tBlocks: ");
    print_u64((fn->content_size + 511) / 512 * 8); append_output("\n");
    append_output("  Access: ("); char m[12]; m[0] = (fn->type == vfs::NODE_DIR) ? 'd' : '-';
    m[1] = 'r'; m[2] = 'w'; m[3] = '-'; m[4] = 'x'; m[5] = 'r'; m[6] = '-'; m[7] = '-'; m[8] = 'x'; m[9] = 'r'; m[10] = '-'; m[11] = 'x';
    for (int i = 0; i < 12; i++) append_char(m[i]);
    append_output(")  uid=(  0/  root)  gid=(  0/  root)\n");
    append_output("  Type: "); append_output(fn->type == vfs::NODE_DIR ? "directory" : "regular file"); append_output("\n");
}

inline void cmd_du(const char* path, bool human) {
    const char* target = (path && path[0]) ? path : ".";
    int fi = resolve_file(target);
    if (fi < 0) { append_output("du: cannot access '"); append_output(target); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint64_t size = fn->content_size;
    if (fn->type == vfs::NODE_DIR) {
        for (int c = 0; c < fn->child_count; c++) {
            vfs::VfsNode* ch = vfs::get_node(fn->children[c]);
            if (ch) size += ch->content_size;
        }
    }
    uint64_t blocks = (size + 511) / 512;
    if (human) {
        if (size > 1048576) { print_u64(size / 1048576); append_output("M\t"); }
        else if (size > 1024) { print_u64(size / 1024); append_output("K\t"); }
        else { print_u64(size); append_output("\t"); }
    } else { print_u64(blocks * 2); append_output("\t"); }
    append_output(target); append_output("\n");
}

inline void cmd_df() {
    uint64_t total = pmm::get_total_pages() * 4096;
    uint64_t used = pmm::get_used_pages() * 4096;
    uint64_t avail = total - used;
    append_output("Filesystem     1K-blocks  Used Available Use%% Mounted on\n");
    append_output("foxfs          ");
    char b[24];
    itoa_u64(total / 1024, b); int l = str_len(b); while (l < 10) { append_output(" "); l++; } append_output(b);
    itoa_u64(used / 1024, b); l = str_len(b); while (l < 10) { append_output(" "); l++; } append_output(b);
    itoa_u64(avail / 1024, b); l = str_len(b); while (l < 10) { append_output(" "); l++; } append_output(b);
    int pct = total > 0 ? (int)(used * 100 / total) : 0;
    append_output("  "); print_int(pct); append_output("% /\n");
}

inline void cmd_chmod(const char* mode, const char* path) {
    if (!mode || !path) { append_output("chmod: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("chmod: cannot access '"); append_output(path); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    if (mode[0] >= '0' && mode[0] <= '7') fn->mode = atoi(mode);
    else { fn->mode = 0; for (int i = 0; mode[i]; i++) { if (mode[i] == 'r') fn->mode |= 4; if (mode[i] == 'w') fn->mode |= 2; if (mode[i] == 'x') fn->mode |= 1; } }
}

inline void cmd_chown(const char* owner, const char* path) {
    if (!owner || !path) { append_output("chown: missing operand\n"); return; }
    append_output("chown: changing ownership of '"); append_output(path); append_output("': Operation not permitted (simulated)\n");
}

inline void cmd_basename(const char* path, const char* suffix) {
    if (!path) { append_output("basename: missing operand\n"); return; }
    int l = str_len(path); int end = l - 1;
    while (end > 0 && path[end] == '/') end--;
    int start = end; while (start > 0 && path[start - 1] != '/') start--;
    char name[vfs::MAX_NAME]; int ni = 0;
    for (int i = start; i <= end; i++) name[ni++] = path[i];
    name[ni] = 0;
    if (suffix) { int sl = str_len(suffix); if (ni >= sl) { bool match = true; for (int i = 0; i < sl; i++) { if (name[ni - sl + i] != suffix[i]) { match = false; break; } } if (match) name[ni - sl] = 0; } }
    append_output(name); append_output("\n");
}

inline void cmd_dirname(const char* path) {
    if (!path) { append_output("dirname: missing operand\n"); return; }
    int l = str_len(path); int end = l - 1;
    while (end > 0 && path[end] == '/') end--;
    while (end > 0 && path[end] != '/') end--;
    if (end == 0 && path[0] != '/') { append_output(".\n"); return; }
    if (end == 0) end = 1;
    for (int i = 0; i <= end; i++) append_char(path[i]);
    append_output("\n");
}

inline void cmd_realpath(const char* path) {
    if (!path) { append_output("realpath: missing operand\n"); return; }
    char full[vfs::MAX_PATH]; get_full_path(path, full);
    int fi = vfs::resolve_path(full);
    if (fi < 0) { append_output("realpath: '"); append_output(path); append_output("': No such file or directory\n"); return; }
    append_output(full); append_output("\n");
}

inline void cmd_mktemp(const char* tpl) {
    static int tmp_counter = 0;
    tmp_counter++;
    char name[32] = "tmpXXXXXX";
    if (tpl && tpl[0]) { int i = 0; while (tpl[i] && i < 30) { name[i] = tpl[i]; i++; } name[i] = 0; }
    char num[8]; itoa(tmp_counter, num); int nl = str_len(num);
    int l = str_len(name); for (int i = 0; i < nl; i++) name[l - 6 + i] = num[i];
    int dir = vfs::resolve_path(shell_state.cwd);
    int ni = vfs::create_node(name, vfs::NODE_FILE, dir);
    if (ni >= 0) { append_output("/"); append_output(shell_state.cwd); if (shell_state.cwd[1]) append_output("/"); append_output(name); append_output("\n"); }
    else append_output("mktemp: failed to create file\n");
}

// ==================== TEXT PROCESSING ====================
inline void cmd_head(const char* narg, const char* path) {
    int n = 10;
    if (narg && narg[0] == '-') { n = atoi(narg + 1); if (n <= 0) n = 10; path = nullptr; }
    if (!path) { append_output("head: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("head: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int printed = 0;
    for (uint32_t i = 0; i < fn->content_size && printed < n; i++) {
        append_char(fn->content[i]);
        if (fn->content[i] == '\n') printed++;
    }
}

inline void cmd_tail(const char* narg, const char* path) {
    int n = 10;
    if (narg && narg[0] == '-') { n = atoi(narg + 1); if (n <= 0) n = 10; path = nullptr; }
    if (!path) { append_output("tail: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("tail: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int lines = 0;
    for (uint32_t i = 0; i < fn->content_size; i++) if (fn->content[i] == '\n') lines++;
    int skip = lines - n; if (skip < 0) skip = 0;
    bool skipping = true; int skipped = 0;
    for (uint32_t i = 0; i < fn->content_size; i++) {
        if (skipping) { if (fn->content[i] == '\n') { skipped++; if (skipped >= skip) skipping = false; } continue; }
        append_char(fn->content[i]);
    }
}

inline void cmd_grep(const char* pattern, const char* path, bool ignore_case, bool show_line, bool count_only, bool invert) {
    if (!pattern) { append_output("grep: missing pattern\n"); return; }
    if (!path) { append_output("grep: missing file operand\n"); return; }
    char pat[256]; str_cpy(pat, pattern); if (ignore_case) str_lower(pat);
    int fi = resolve_file(path);
    if (fi < 0) { append_output("grep: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int line_num = 0; int match_count = 0;
    uint32_t line_start = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            line_num++;
            char line_buf[1024]; int ll = 0;
            for (uint32_t j = line_start; j < i && ll < 1023; j++) line_buf[ll++] = fn->content[j];
            line_buf[ll] = 0;
            char line_lower[1024]; for (int j = 0; j <= ll; j++) line_lower[j] = line_buf[j]; if (ignore_case) str_lower(line_lower);
            bool found = str_contains(line_lower, pat);
            if (invert) found = !found;
            if (found) {
                match_count++;
                if (!count_only) {
                    if (show_line) { print_int(line_num); append_output(":"); }
                    append_output(line_buf); append_output("\n");
                }
            }
            line_start = i + 1;
        }
    }
    if (count_only) { print_int(match_count); append_output("\n"); }
}

inline void cmd_sort(const char* path, bool reverse, bool numeric) {
    if (!path) { append_output("sort: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("sort: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    const int MAX_LINES_SORT = 256;
    char lines[MAX_LINES_SORT][256];
    int line_count = 0;
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size && line_count < MAX_LINES_SORT; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            int ll = 0;
            for (uint32_t j = ls; j < i && ll < 255; j++) lines[line_count][ll++] = fn->content[j];
            lines[line_count][ll] = 0;
            line_count++; ls = i + 1;
        }
    }
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = i + 1; j < line_count; j++) {
            bool swap = false;
            if (numeric) { swap = atoi(lines[i]) > atoi(lines[j]); }
            else { swap = str_cmp(lines[i], lines[j]) > 0; }
            if (reverse) swap = !swap;
            if (swap) { char tmp[256]; str_cpy(tmp, lines[i]); str_cpy(lines[i], lines[j]); str_cpy(lines[j], tmp); }
        }
    }
    for (int i = 0; i < line_count; i++) { append_output(lines[i]); append_output("\n"); }
}

inline void cmd_uniq(const char* path, bool count) {
    if (!path) { append_output("uniq: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("uniq: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    char prev[256] = {}; int prev_count = 0;
    uint32_t ls = 0;
    auto flush_uniq = [&]() {
        if (prev_count > 0 && prev[0]) {
            if (count) { print_int(prev_count); append_output(" "); }
            append_output(prev); append_output("\n");
        }
    };
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            char line[256]; int ll = 0;
            for (uint32_t j = ls; j < i && ll < 255; j++) line[ll++] = fn->content[j];
            line[ll] = 0;
            if (str_eq(line, prev)) { prev_count++; }
            else { flush_uniq(); str_cpy(prev, line); prev_count = 1; }
            ls = i + 1;
        }
    }
    flush_uniq();
}

inline void cmd_cut(const char* delim_arg, const char* fields_arg, const char* path) {
    if (!path) { append_output("cut: missing file operand\n"); return; }
    char delim = '\t';
    if (delim_arg && delim_arg[0] == '-' && delim_arg[1] == 'd' && delim_arg[2]) delim = delim_arg[2];
    int fi = resolve_file(path);
    if (fi < 0) { append_output("cut: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int field = fields_arg ? atoi(fields_arg) : 1;
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            int f = 1; uint32_t fs = ls;
            for (uint32_t j = ls; j <= i; j++) {
                if (j == i || fn->content[j] == delim) {
                    if (f == field) {
                        for (uint32_t k = fs; k < j; k++) append_char(fn->content[k]);
                        break;
                    }
                    f++; fs = j + 1;
                }
            }
            append_output("\n"); ls = i + 1;
        }
    }
}

inline void cmd_tr(const char* from, const char* to, const char* path) {
    if (!from || !path) { append_output("tr: missing operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("tr: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    for (uint32_t i = 0; i < fn->content_size; i++) {
        char c = fn->content[i];
        for (int j = 0; from[j]; j++) {
            if (c == from[j]) { c = to ? (to[j] ? to[j] : ' ') : ' '; break; }
        }
        append_char(c);
    }
}

inline void cmd_nl(const char* path) {
    if (!path) { append_output("nl: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("nl: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int ln = 1; uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            char nb[8]; itoa(ln, nb); int nl2 = str_len(nb);
            for (int j = 0; j < 6 - nl2; j++) append_char(' ');
            append_output(nb); append_output("\t");
            for (uint32_t j = ls; j < i; j++) append_char(fn->content[j]);
            append_output("\n"); ln++; ls = i + 1;
        }
    }
}

inline void cmd_tac(const char* path) {
    if (!path) { append_output("tac: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("tac: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    const int MX = 256; char lines[MX][256]; int lc = 0;
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size && lc < MX; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            int ll = 0;
            for (uint32_t j = ls; j < i && ll < 255; j++) lines[lc][ll++] = fn->content[j];
            lines[lc][ll] = 0; lc++; ls = i + 1;
        }
    }
    for (int i = lc - 1; i >= 0; i--) { append_output(lines[i]); append_output("\n"); }
}

inline void cmd_rev(const char* path) {
    if (!path) { append_output("rev: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("rev: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            for (int j = (int)i - 1; j >= (int)ls; j--) append_char(fn->content[j]);
            append_output("\n"); ls = i + 1;
        }
    }
}

inline void cmd_wc_full(const char* path) {
    if (!path) { append_output("wc: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("wc: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int lines = 0, words = 0, chars = 0; bool in_w = false;
    for (uint32_t i = 0; i < fn->content_size; i++) {
        chars++;
        if (fn->content[i] == '\n') lines++;
        if (fn->content[i] == ' ' || fn->content[i] == '\n' || fn->content[i] == '\t') { if (in_w) words++; in_w = false; }
        else in_w = true;
    }
    if (in_w) words++;
    char b[16]; itoa(lines, b); int l = str_len(b); while (l < 7) { append_output(" "); l++; } append_output(b);
    itoa(words, b); l = str_len(b); while (l < 7) { append_output(" "); l++; } append_output(b);
    itoa(chars, b); l = str_len(b); while (l < 7) { append_output(" "); l++; } append_output(b);
    append_output(" "); append_output(path); append_output("\n");
}

inline void cmd_shuf(const char* path, int n) {
    if (!path) { append_output("shuf: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("shuf: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    const int MX = 256; char lines[MX][256]; int lc = 0;
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size && lc < MX; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            int ll = 0;
            for (uint32_t j = ls; j < i && ll < 255; j++) lines[lc][ll++] = fn->content[j];
            lines[lc][ll] = 0; lc++; ls = i + 1;
        }
    }
    for (int i = lc - 1; i > 0; i--) {
        int j = (int)((uint32_t)pit::get_ticks() % (uint32_t)(i + 1));
        char tmp[256]; str_cpy(tmp, lines[i]); str_cpy(lines[i], lines[j]); str_cpy(lines[j], tmp);
    }
    int out_n = (n > 0 && n < lc) ? n : lc;
    for (int i = 0; i < out_n; i++) { append_output(lines[i]); append_output("\n"); }
}

inline void cmd_fmt(const char* path) {
    if (!path) { append_output("fmt: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("fmt: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            for (uint32_t j = ls; j < i; j++) append_char(fn->content[j]);
            if (i < fn->content_size) append_output(" ");
            ls = i + 1;
        }
    }
    append_output("\n");
}

inline void cmd_xargs(const char* cmd_str) {
    if (!cmd_str) { append_output("xargs: missing command\n"); return; }
    append_output(shell_state.cwd); append_output("$ "); append_output(cmd_str); append_output("\n");
    execute(cmd_str);
}

inline void cmd_yes(const char* text) {
    const char* s = text ? text : "y";
    for (int i = 0; i < 200 && shell_state.output_len < MAX_OUTPUT - 64; i++) { append_output(s); append_output("\n"); }
}

// ==================== SYSTEM INFO ====================
inline void cmd_id() { append_output("uid=1000(user) gid=1000(user) groups=1000(user),4(adm),27(sudo)\n"); }
inline void cmd_groups() { append_output("user adm sudo\n"); }

inline void cmd_who() {
    append_output("user     tty0         2026-07-17 12:00\n");
    append_output("user     pts/0        2026-07-17 12:00 (:0)\n");
}

inline void cmd_w() {
    append_output(" 12:00:00 up 0 min,  2 users,  load average: 0.00, 0.00, 0.00\n");
    append_output("USER     TTY      FROM             LOGIN@   IDLE   JCPU   PCPU WHAT\n");
    append_output("user     tty0                      12:00   0.00s  0.01s  0.01s w\n");
}

inline void cmd_dmesg() {
    append_output("[    0.000000] Foxiwium OS v1.7.3 booting\n");
    append_output("[    0.001000] CPU: x86_64, Long Mode enabled\n");
    append_output("[    0.002000] Memory: ");
    char b[24]; itoa_u64(pmm::get_total_pages() * 4 / 1024, b);
    append_output(b); append_output(" MB available\n");
    append_output("[    0.003000] Framebuffer: ");
    print_int(framebuffer::get_width()); append_output("x"); print_int(framebuffer::get_height()); append_output("\n");
    append_output("[    0.004000] GDT/TSS: initialized\n");
    append_output("[    0.005000] IDT: 256 entries loaded\n");
    append_output("[    0.006000] PIT: 100 Hz timer\n");
    append_output("[    0.007000] PS/2 keyboard: detected\n");
    append_output("[    0.008000] PS/2 mouse: initialized\n");
    append_output("[    0.009000] VFS: initialized with ");
    print_int(vfs::get_node_count()); append_output(" nodes\n");
    append_output("[    0.010000] Syscall: SYSCALL/SYSRET enabled\n");
    append_output("[    0.011000] Interrupts enabled, system ready.\n");
}

inline void cmd_lsblk() {
    append_output("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT\n");
    append_output("fox0     8:0    0  512M  0 disk /\n");
    append_output(" fox0p1  8:1    0  384M  0 part\n");
    append_output(" fox0p2  8:2    0  128M  0 part [SWAP]\n");
}

inline void cmd_lscpu() {
    append_output("Architecture:        x86_64\n");
    append_output("CPU op-mode(s):      32-bit, 64-bit\n");
    append_output("Address sizes:       48 bits physical, 48 bits virtual\n");
    append_output("Byte Order:          Little Endian\n");
    append_output("CPU(s):              1\n");
    append_output("Vendor ID:           Foxiwium\n");
    append_output("Model name:          Foxiwium vCPU @ 3.00GHz\n");
    append_output("Stepping:            0\n");
    append_output("Flags:               fpu sse sse2 syscall\n");
}

inline void cmd_lsmem() {
    append_output("RANGE                SIZE  STATE\n");
    append_output("0x0000000000000000  ");
    char b[24]; itoa_u64(pmm::get_total_pages() * 4 / 1024, b); append_output(b); append_output("K  online\n");
}

inline void cmd_lsns() {
    append_output("NS TYPE  NPROCS PID USER   COMMAND\n");
    append_output("4026838369 user       2   1 root   /bin/sh\n");
}

inline void cmd_lsmod() {
    append_output("Module                  Size  Used by\n");
    append_output("foxkbd                  4096  1\n");
    append_output("foxmouse               4096  1\n");
    append_output("foxpit                  2048  1\n");
}

inline void cmd_lsirq() {
    append_output("IRQ  COUNT    NAME\n");
    append_output("  0       1    foxpit\n");
    append_output("  1       0    foxkbd\n");
    append_output(" 12       0    foxmouse\n");
}

inline void cmd_vmstat() {
    append_output("--- procs --- -----memory----- ---swap-- -----io--\n");
    append_output(" r  b   swpd   free   buff  cache   si   so    bi    bo\n");
    append_output(" 1  0      0 ");
    char b[16]; itoa((int)(pmm::get_free_pages() * 4 / 1024), b); append_output(b); append_output("     0     0    0    0     0     0\n");
}

inline void cmd_iostat() {
    append_output("Linux 0.0-1-generic (foxiwium) \n");
    append_output("Device  r/s    w/s   rkB/s  wkB/s\n");
    append_output("fox0    0.00   0.00   0.00   0.00\n");
}

inline void cmd_top_header() {
    append_output("top - Foxiwium OS v1.7.3\n");
    append_output("Tasks: "); print_int(pcb.count); append_output(" total\n");
    append_output("%Cpu(s):  1.0 us,  0.0 sy,  0.0 ni, 99.0 id\n");
    uint64_t total = pmm::get_total_pages() * 4 / 1024;
    uint64_t used = pmm::get_used_pages() * 4 / 1024;
    append_output("MiB Mem : "); print_u64(total); append_output(" total, "); print_u64(total - used); append_output(" free, "); print_u64(used); append_output(" used\n\n");
    append_output("  PID USER      PR  NI    VIRT    RES    SHR S %CPU  %MEM     TIME+ COMMAND\n");
    for (int i = 0; i < pcb.count; i++) {
        Process& p = pcb.processes[i];
        if (p.state == PROC_UNUSED) continue;
        char b[16];
        itoa(p.pid, b); int l = str_len(b); while (l < 8) { append_output(" "); l++; } append_output(b);
        append_output(" user      20  0    4096   4096   4096 S  0.0  0.1   0:00.01 ");
        append_output(p.name); append_output("\n");
    }
}

// ==================== PROCESS MANAGEMENT ====================
inline void cmd_kill_pid(const char* pid_str, const char* sig_str) {
    (void)sig_str;
    if (!pid_str) { append_output("kill: missing PID\n"); return; }
    int pid = atoi(pid_str);
    if (pid <= 0) { append_output("kill: invalid PID\n"); return; }
    for (int i = 0; i < pcb.count; i++) {
        if (pcb.processes[i].pid == pid) {
            pcb.processes[i].state = PROC_ZOMBIE;
            pcb.processes[i].killed = true;
            append_output("kill: process "); print_int(pid); append_output(" terminated\n");
            return;
        }
    }
    append_output("kill: ("); print_int(pid); append_output(") - No such process\n");
}

inline void cmd_killall(const char* name) {
    if (!name) { append_output("killall: missing process name\n"); return; }
    int found = 0;
    for (int i = 0; i < pcb.count; i++) {
        if (pcb.processes[i].state != PROC_UNUSED && str_eq(pcb.processes[i].name, name)) {
            pcb.processes[i].state = PROC_ZOMBIE; pcb.processes[i].killed = true; found++;
        }
    }
    if (found == 0) { append_output("killall: '"); append_output(name); append_output("' not found\n"); }
}

inline void cmd_pgrep(const char* pattern) {
    if (!pattern) { append_output("pgrep: missing pattern\n"); return; }
    for (int i = 0; i < pcb.count; i++) {
        if (pcb.processes[i].state != PROC_UNUSED && str_contains(pcb.processes[i].name, pattern)) {
            print_int(pcb.processes[i].pid); append_output("\n");
        }
    }
}

inline void cmd_nice(const char* prio, const char* cmd_str) {
    if (!cmd_str) { append_output("nice: missing command\n"); return; }
    append_output("nice: setting priority to "); append_output(prio ? prio : "10"); append_output("\n");
    append_output(shell_state.cwd); append_output("$ "); append_output(cmd_str); append_output("\n");
    execute(cmd_str);
}

inline void cmd_renice(const char* pid_str, const char* prio) {
    if (!pid_str) { append_output("renice: missing PID\n"); return; }
    append_output("renice: process "); append_output(pid_str); append_output(" priority set to "); append_output(prio ? prio : "0"); append_output("\n");
}

inline void cmd_timeout(const char* secs, const char* cmd_str) {
    (void)secs;
    if (!cmd_str) { append_output("timeout: missing command\n"); return; }
    append_output(shell_state.cwd); append_output("$ "); append_output(cmd_str); append_output("\n");
    execute(cmd_str);
}

inline void cmd_time_cmd(const char* cmd_str) {
    if (!cmd_str) { append_output("time: missing command\n"); return; }
    uint64_t start = pit::get_ticks();
    execute(cmd_str);
    uint64_t elapsed = pit::get_ticks() - start;
    char b[16];
    append_output("\nreal\t0m"); itoa((int)(elapsed * 10), b); append_output(b); append_output("ms\n");
    append_output("user\t0m0.000s\nsys\t0m0.000s\n");
}

inline void cmd_wait() { append_output("wait: no processes to wait for\n"); }

inline void cmd_noop() {} // bg, fg, jobs

// ==================== SHELL BUILTINS ====================
inline void cmd_export(const char* arg) {
    if (!arg) { for (int i = 0; i < shell_state.env_count; i++) { append_output("declare -x "); append_output(shell_state.env[i].key); append_output("=\""); append_output(shell_state.env[i].value); append_output("\"\n"); } return; }
    char key[64]; int ki = 0;
    const char* p = arg; while (*p && *p != '=' && ki < 63) key[ki++] = *p++;
    key[ki] = 0;
    if (*p == '=') { p++; env_set(key, p); }
    else env_set(key, "");
}

inline void cmd_unset(const char* key) {
    if (!key) { append_output("unset: missing variable name\n"); return; }
    env_unset(key);
}

inline void cmd_env() {
    for (int i = 0; i < shell_state.env_count; i++) {
        append_output(shell_state.env[i].key); append_output("="); append_output(shell_state.env[i].value); append_output("\n");
    }
}

inline void cmd_alias_cmd(const char* arg) {
    if (!arg) { for (int i = 0; i < shell_state.alias_count; i++) { append_output("alias "); append_output(shell_state.aliases[i].name); append_output("='"); append_output(shell_state.aliases[i].value); append_output("'\n"); } return; }
    char name[64]; int ni = 0;
    const char* p = arg; while (*p && *p != '=' && ni < 63) name[ni++] = *p++;
    name[ni] = 0;
    if (*p == '=') { p++; if (*p == '\'') p++; alias_set(name, p); }
    else alias_set(name, "");
}

inline void cmd_unalias(const char* name) {
    if (!name) { append_output("unalias: missing name\n"); return; }
    for (int i = 0; i < shell_state.alias_count; i++) {
        if (str_eq(shell_state.aliases[i].name, name)) {
            for (int j = i; j < shell_state.alias_count - 1; j++) shell_state.aliases[j] = shell_state.aliases[j + 1];
            shell_state.alias_count--; return;
        }
    }
}

inline void cmd_type(const char* name) {
    if (!name) { append_output("type: missing argument\n"); return; }
    const char* al = alias_get(name);
    if (al) { append_output(name); append_output(" is aliased to '"); append_output(al); append_output("'\n"); return; }
    if (str_eq(name, "cd") || str_eq(name, "export") || str_eq(name, "unset") || str_eq(name, "alias") ||
        str_eq(name, "unalias") || str_eq(name, "exit") || str_eq(name, "history") || str_eq(name, "type") ||
        str_eq(name, "source") || str_eq(name, "eval") || str_eq(name, "read") || str_eq(name, "exec")) {
        append_output(name); append_output(" is a shell builtin\n"); return;
    }
    append_output(name); append_output(" is /bin/"); append_output(name); append_output("\n");
}

inline void cmd_which(const char* name) {
    if (!name) return;
    const char* al = alias_get(name);
    if (al) { append_output(name); append_output(": aliased to "); append_output(al); append_output("\n"); return; }
    append_output("/bin/"); append_output(name); append_output("\n");
}

inline void cmd_whereis(const char* name) {
    if (!name) { append_output("whereis: missing argument\n"); return; }
    append_output(name); append_output(": /bin/"); append_output(name); append_output(" /usr/share/man/man1/"); append_output(name); append_output(".1\n");
}

inline void cmd_history() {
    for (int i = 0; i < shell_state.history_count; i++) {
        char num[8]; itoa(i + 1, num); int l = str_len(num); while (l < 5) { append_output(" "); l++; }
        append_output(num); append_output("  "); append_output(shell_state.history[i]); append_output("\n");
    }
}

inline void cmd_source(const char* path) {
    if (!path) { append_output("source: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("source: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    char line[256]; uint32_t ls = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            int ll = 0;
            for (uint32_t j = ls; j < i && ll < 255; j++) {
                if (fn->content[j] != '\r') line[ll++] = fn->content[j];
            }
            line[ll] = 0;
            while (line[0] == ' ') { for (int k = 0; line[k]; k++) line[k] = line[k + 1]; }
            if (line[0] && line[0] != '#') execute(line);
            ls = i + 1;
        }
    }
}

inline void cmd_mapfile(const char* path) {
    if (!path) { append_output("mapfile: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("mapfile: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint32_t ls = 0; int ln = 0;
    for (uint32_t i = 0; i <= fn->content_size; i++) {
        if (i == fn->content_size || fn->content[i] == '\n') {
            print_int(ln); append_output(": ");
            for (uint32_t j = ls; j < i; j++) append_char(fn->content[j]);
            append_output("\n"); ln++; ls = i + 1;
        }
    }
}

// ==================== USER/GROUP ====================
inline void cmd_sudo(const char* cmd_str) {
    if (!cmd_str) { append_output("sudo: no command specified\n"); return; }
    append_output("[sudo] password for user: \n");
    append_output(shell_state.cwd); append_output("# "); append_output(cmd_str); append_output("\n");
    execute(cmd_str);
}

inline void cmd_su(const char* user) {
    if (!user) user = "root";
    append_output("su: authenticate as "); append_output(user); append_output("\n");
    str_cpy(shell_state.cwd, "/");
    env_set("USER", user);
}

inline void cmd_adduser(const char* name) {
    if (!name) { append_output("adduser: missing username\n"); return; }
    append_output("Adding user '"); append_output(name); append_output("' ...\n");
    append_output("Adding new group '"); append_output(name); append_output("' (1001)\n");
    append_output("Adding new user '"); append_output(name); append_output("' (1001) with group '"); append_output(name); append_output("'\n");
    append_output("Creating home directory '/home/"); append_output(name); append_output("'\n");
    int home = vfs::resolve_path("/home");
    if (home >= 0) vfs::create_node(name, vfs::NODE_DIR, home);
    append_output("Done.\n");
}

inline void cmd_useradd(const char* name) {
    if (!name) { append_output("useradd: missing username\n"); return; }
    cmd_adduser(name);
}

inline void cmd_userdel(const char* name) {
    if (!name) { append_output("userdel: missing username\n"); return; }
    append_output("userdel: user '"); append_output(name); append_output("' deleted\n");
}

inline void cmd_groupadd(const char* name) {
    if (!name) { append_output("groupadd: missing group name\n"); return; }
    append_output("groupadd: group '"); append_output(name); append_output("' added\n");
}

inline void cmd_groupdel(const char* name) {
    if (!name) { append_output("groupdel: missing group name\n"); return; }
    append_output("groupdel: group '"); append_output(name); append_output("' deleted\n");
}

inline void cmd_passwd(const char* user) {
    if (!user) user = "user";
    append_output("Changing password for user "); append_output(user); append_output(".\n");
    append_output("New password: \n");
    append_output("Retype new password: \n");
    append_output("passwd: password updated successfully\n");
}

inline void cmd_chfn() { append_output("Changing finger info for user.\nName []: User\nOffice []:\nPhone []:\nDone.\n"); }
inline void cmd_chsh() { append_output("Shell changed.\n"); }

// ==================== NETWORKING ====================
inline void cmd_ip() {
    uint8_t mac[6];
    char ipb[16];
    char gwb[16];
    if (net::ready()) {
        net::get_mac(mac);
        net::ip_to_str(net::get_ip(), ipb);
        net::ip_to_str(net::get_gateway(), gwb);
    } else {
        for (int i = 0; i < 6; i++) mac[i] = 0;
        net::ip_to_str(0x0A00020F, ipb);
        net::ip_to_str(0x0A000202, gwb);
    }
    append_output("1: lo: <LOOPBACK,UP> mtu 65536\n    inet 127.0.0.1/8 scope host lo\n");
    append_output("2: eth0: <BROADCAST,MULTICAST,UP> mtu 1500\n    inet ");
    append_output(ipb);
    append_output("/24 brd 10.0.2.255 scope global eth0\n");
    append_output("    link/ether ");
    for (int i = 0; i < 6; i++) { if (i) append_output(":"); append_hex8(mac[i]); }
    append_output(" brd ff:ff:ff:ff:ff:ff\n");
    append_output("    default via ");
    append_output(gwb);
    append_output(" dev eth0\n");
}

inline void cmd_ifconfig() {
    uint8_t mac[6];
    char ipb[16];
    if (net::ready()) {
        net::get_mac(mac);
        net::ip_to_str(net::get_ip(), ipb);
    } else {
        for (int i = 0; i < 6; i++) mac[i] = 0;
        net::ip_to_str(0x0A00020F, ipb);
    }
    append_output("eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500\n");
    append_output("        inet ");
    append_output(ipb);
    append_output("  netmask 255.255.255.0  broadcast 10.0.2.255\n");
    append_output("        ether ");
    for (int i = 0; i < 6; i++) { if (i) append_output(":"); append_hex8(mac[i]); }
    append_output("  txqueuelen 1000  \n");
    append_output("        RX packets: ");
    print_u64(net::get_rx_count());
    append_output("  TX packets: ");
    print_u64(net::get_tx_count());
    append_output("\n");
    append_output("lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536\n");
    append_output("        inet 127.0.0.1  netmask 255.0.0.0\n");
}

inline void cmd_ping(const char* host) {
    if (!host) { append_output("ping: missing host\n"); return; }
    if (!net::ready()) { append_output("ping: no network interface\n"); return; }
    uint32_t ip = 0;
    if (host[0] >= '0' && host[0] <= '9') {
        ip = net::parse_ip(host);
        if (ip == 0) {
            append_output("ping: ");
            append_output(host);
            append_output(": invalid address\n");
            return;
        }
    } else {
        append_output("ping: resolving ");
        append_output(host);
        append_output("...\n");
        if (!net::dns_resolve(host, &ip, 2000000)) {
            append_output("ping: ");
            append_output(host);
            append_output(": Name or service not known\n");
            return;
        }
    }
    char ipb[16];
    net::ip_to_str(ip, ipb);
    append_output("PING ");
    append_output(host);
    append_output(" (");
    append_output(ipb);
    append_output(") 56(84) bytes of data.\n");
    int recv = 0;
    for (int i = 0; i < 4; i++) {
        int r = net::ping(ip, 20000);
        if (r >= 0) {
            recv++;
            append_output("64 bytes from ");
            append_output(ipb);
            append_output(": icmp_seq=");
            print_int(i + 1);
            append_output(" ttl=64 time<1 ms\n");
        } else if (r == -2) {
            append_output("From ");
            append_output(ipb);
            append_output(" icmp_seq=");
            print_int(i + 1);
            append_output(" Destination Host Unreachable\n");
        } else {
            append_output("Request timeout for icmp_seq=");
            print_int(i + 1);
            append_output("\n");
        }
    }
    append_output("\n--- ");
    append_output(host);
    append_output(" ping statistics ---\n");
    append_output("4 packets transmitted, ");
    print_int(recv);
    append_output(" received, ");
    print_int(4 - recv);
    append_output(" packet loss\n");
}

inline void cmd_curl(const char* url) {
    if (!url) { append_output("curl: missing URL\n"); return; }
    if (!net::ready()) { append_output("curl: no network interface\n"); return; }
    static char body[8192];
    int n = net::http_get(url, body, 8191, 3000000);
    if (n < 0) {
        append_output("curl: ("); print_int(-n); append_output(") could not fetch ");
        append_output(url); append_output("\n");
        return;
    }
    body[n] = 0;
    append_output(body);
    if (n > 0 && body[n - 1] != '\n') append_output("\n");
}

inline void cmd_wget(const char* url) {
    if (!url) { append_output("wget: missing URL\n"); return; }
    if (!net::ready()) { append_output("wget: no network interface\n"); return; }
    append_output("Resolving "); append_output(url); append_output("...\n");
    static char body[4096];
    int n = net::http_get(url, body, 4095, 3000000);
    if (n < 0) {
        append_output("wget: unable to fetch ");
        append_output(url);
        append_output(" (error ");
        print_int(n);
        append_output(")\n");
        return;
    }
    const char* fn = url;
    for (const char* p = url; *p; p++) if (*p == '/') fn = p + 1;
    if (!fn[0]) fn = "index.html";
    if (fn[0] == '?') fn = "index.html";
    char full[vfs::MAX_PATH];
    get_full_path(fn, full);
    if (vfs::resolve_path(full) >= 0) {
        append_output("wget: '"); append_output(full); append_output("' already exists\n");
        return;
    }
    int parent_idx = vfs::root_idx;
    int slash = -1;
    for (int i = 0; full[i]; i++) if (full[i] == '/') slash = i;
    if (slash > 0) {
        char dirpart[vfs::MAX_PATH];
        int d = 0;
        for (int i = 0; i < slash; i++) dirpart[d++] = full[i];
        dirpart[d] = 0;
        parent_idx = vfs::resolve_path(dirpart);
        if (parent_idx < 0) parent_idx = vfs::root_idx;
    }
    int ni = vfs::create_node(fn, vfs::NODE_FILE, parent_idx);
    vfs::write_file(ni, body, (uint32_t)n);
    append_output("wget: saved '"); append_output(fn);
    append_output("' (");
    print_int(n);
    append_output(" bytes)\n");
}

inline void cmd_ss() {
    append_output("Netid  State   Recv-Q  Send-Q  Local Address:Port   Peer Address:Port\n");
    append_output("tcp    LISTEN  0       128     0.0.0.0:22            0.0.0.0:*\n");
    append_output("tcp    LISTEN  0       128     0.0.0.0:80            0.0.0.0:*\n");
    append_output("udp    UNCONN  0       0       0.0.0.0:68            0.0.0.0:*\n");
}

inline void cmd_traceroute(const char* host) {
    if (!host) { append_output("traceroute: missing host\n"); return; }
    append_output("traceroute to "); append_output(host); append_output(" (127.0.0.1), 30 hops max, 60 byte packets\n");
    for (int i = 1; i <= 6; i++) { append_output(" "); print_int(i); append_output("  * * *\n"); }
}

inline void cmd_arp() { append_output("Address         HWtype  HWaddress           Flags Mask  Iface\n"); append_output("192.168.1.1     ether   aa:bb:cc:dd:ee:ff   C         eth0\n"); }
inline void cmd_route() { append_output("Kernel IP routing table\nDestination     Gateway         Genmask         Flags Metric Ref    Use Iface\n"); append_output("default         192.168.1.1     0.0.0.0         UG    100    0        0 eth0\n"); append_output("192.168.1.0     0.0.0.0         255.255.255.0   U     0      0        0 eth0\n"); }

// ==================== COMPRESSION / ARCHIVE ====================
inline void cmd_tar(const char* args_str, const char* archive) {
    if (!args_str || !archive) { append_output("tar: missing arguments\nUsage: tar xf <archive>\n"); return; }
    if (args_str[0] == 'x' || (args_str[0] == '-' && str_contains(args_str, "x"))) {
        append_output("tar: extracting '"); append_output(archive); append_output("'...\n");
        append_output("tar: done.\n");
    } else if (args_str[0] == 'c' || (args_str[0] == '-' && str_contains(args_str, "c"))) {
        append_output("tar: creating archive '"); append_output(archive); append_output("'\n");
        append_output("tar: done.\n");
    } else if (args_str[0] == 't' || (args_str[0] == '-' && str_contains(args_str, "t"))) {
        append_output("file1.txt\nfile2.txt\ndir1/\n");
    } else append_output("tar: unknown option '"); append_output(args_str); append_output("'\n");
}

inline void cmd_gzip(const char* file) {
    if (!file) { append_output("gzip: missing file\n"); return; }
    append_output("gzip: "); append_output(file); append_output(" -> "); append_output(file); append_output(".gz\n");
}

inline void cmd_gunzip(const char* file) {
    if (!file) { append_output("gunzip: missing file\n"); return; }
    append_output("gunzip: "); append_output(file); append_output("\n");
}

inline void cmd_zip(const char* archive, const char* file) {
    if (!archive || !file) { append_output("zip: missing arguments\n"); return; }
    append_output("  adding: "); append_output(file); append_output(" (deflated 40%)\n");
}

inline void cmd_unzip(const char* file) {
    if (!file) { append_output("unzip: missing file\n"); return; }
    append_output("Archive:  "); append_output(file); append_output("\n  inflating: file1.txt\n  inflating: file2.txt\n");
}

// ==================== DISK ====================
inline void cmd_dd(const char* if_arg, const char* of_arg) {
    if (!if_arg || !of_arg) { append_output("dd: missing operand\nUsage: dd if=<file> of=<file>\n"); return; }
    append_output("1+0 records in\n1+0 records out\n512 bytes copied\n");
}

inline void cmd_fsck() { append_output("fsck: foxfs filesystem OK, no errors detected\n"); }
inline void cmd_blkid() { append_output("/dev/fox0: UUID=\"fox-1234-abcd\" TYPE=\"foxfs\"\n"); }

inline void cmd_fdisk() {
    append_output("Disk /dev/fox0: 512 MiB\nUnits: 1 MiB = 1048576 bytes\n\n");
    append_output("Device     Boot  Start    End  Sectors  Size Id Type\n");
    append_output("/dev/fox0p1 *        1    384   393216  384M 83 Linux\n");
    append_output("/dev/fox0p2        385    512   131072  128M 82 Linux swap\n");
}

inline void cmd_mkfs() { append_output("mkfs: creating foxfs filesystem on /dev/fox0\nmkfs: done.\n"); }
inline void cmd_mount() { append_output("fox0 on / type foxfs (rw,relatime)\ntmpfs on /tmp type tmpfs (rw,nosuid,nodev)\nproc on /proc type proc (rw,nosuid,nodev,noexec,relatime)\nsysfs on /sys type sysfs (rw,nosuid,nodev,noexec,relatime)\ndevice on /dev type devtmpfs (rw,nosuid)\n"); }
inline void cmd_umount() { append_output("umount: lazy unmount not supported in kernel mode\n"); }

// ==================== MATH / NUMBERS ====================
inline void cmd_expr(const char* arg) {
    if (!arg) { append_output("0\n"); return; }
    int a = 0, b = 0; char op = 0;
    const char* p = arg; while (*p == ' ') p++;
    a = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
    if (*p) { op = *p; p++; while (*p == ' ') p++; b = atoi(p); }
    if (op == '+') print_int(a + b);
    else if (op == '-') print_int(a - b);
    else if (op == '*') print_int(a * b);
    else if (op == '/' && b != 0) print_int(a / b);
    else if (op == '%' && b != 0) print_int(a % b);
    else if (op == '<') print_int(a < b ? 1 : 0);
    else if (op == '>') print_int(a > b ? 1 : 0);
    else if (op == '=' || (op == '=' && *(p - 1) == '=')) print_int(a == b ? 1 : 0);
    else { print_int(a); append_output("\n"); return; }
    append_output("\n");
}

inline void cmd_seq(const char* first, const char* second, const char* third) {
    if (!first) { append_output("seq: missing operand\n"); return; }
    int start = 1, end = 1, step = 1;
    if (second && third) { start = atoi(first); end = atoi(second); step = atoi(third); }
    else if (second) { start = atoi(first); end = atoi(second); }
    else { end = atoi(first); }
    if (step == 0) step = 1;
    if (step > 0) { for (int i = start; i <= end; i += step) { print_int(i); append_output("\n"); } }
    else { for (int i = start; i >= end; i += step) { print_int(i); append_output("\n"); } }
}

inline void cmd_true() {}
inline void cmd_false() { shell_state.output_len--; } // return 1 effectively

inline void cmd_factor(const char* n) {
    if (!n) { append_output("factor: missing argument\n"); return; }
    int num = atoi(n); if (num < 0) { append_output("-"); num = -num; }
    append_output(n); append_output(": ");
    for (int d = 2; d * d <= num; d++) {
        while (num % d == 0) { print_int(d); append_output(" "); num /= d; }
    }
    if (num > 1) print_int(num);
    append_output("\n");
}

inline void cmd_numfmt(const char* num, const char* unit) {
    if (!num) { append_output("numfmt: missing argument\n"); return; }
    uint64_t val = (uint64_t)atoi(num);
    if (unit && str_eq(unit, "iec")) {
        if (val > 1073741824) { print_u64(val / 1073741824); append_output("GiB\n"); }
        else if (val > 1048576) { print_u64(val / 1048576); append_output("MiB\n"); }
        else if (val > 1024) { print_u64(val / 1024); append_output("KiB\n"); }
        else { print_u64(val); append_output("\n"); }
    } else { print_u64(val); append_output("\n"); }
}

inline void cmd_cal(const char* month_s, const char* year_s) {
    int month = 7, year = 2026;
    if (month_s) month = atoi(month_s);
    if (year_s) year = atoi(year_s);
    if (month < 1 || month > 12) { append_output("cal: invalid month\n"); return; }
    const char* names[] = {"","January","February","March","April","May","June","July","August","September","October","November","December"};
    int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days[2] = 29;
    int title_len = str_len(names[month]) + 5; int pad = (20 - title_len) / 2;
    for (int i = 0; i < pad; i++) append_char(' ');
    append_output(names[month]); append_output(" "); print_int(year); append_output("\n");
    append_output("Su Mo Tu We Th Fr Sa\n");
    int dow = (1 + (year * 365 + year / 4 - year / 100 + year / 400) % 7 + (month > 1 ? 31 : 0) + (month > 2 ? (days[2]) : 0) + (month > 3 ? 30 : 0) + (month > 4 ? 31 : 0) + (month > 5 ? 30 : 0) + (month > 6 ? 31 : 0) + (month > 7 ? 31 : 0) + (month > 8 ? 30 : 0) + (month > 9 ? 31 : 0) + (month > 10 ? 30 : 0) + (month > 11 ? 31 : 0)) % 7;
    for (int i = 0; i < dow; i++) append_output("   ");
    for (int d = 1; d <= days[month]; d++) {
        if (d < 10) append_char(' ');
        print_int(d); append_char(' ');
        if ((dow + d) % 7 == 0) append_output("\n");
    }
    if ((dow + days[month]) % 7 != 0) append_output("\n");
}

inline void cmd_date_imp() {
    uint64_t ticks = pit::get_ticks();
    int sec = (int)(ticks / 100) % 60;
    int min = (int)(ticks / 6000) % 60;
    int hour = 12 + (int)(ticks / 360000) % 12;
    append_output("Thu Jul 17 ");
    char b[8];
    if (hour < 10) append_char('0');
    itoa(hour, b); append_output(b); append_char(':');
    if (min < 10) append_char('0');
    itoa(min, b); append_output(b); append_char(':');
    if (sec < 10) append_char('0');
    itoa(sec, b); append_output(b);
    append_output(" UTC 2026\n");
}

// ==================== MISC TOOLS ====================
inline void cmd_base64(const char* path) {
    if (!path) { append_output("base64: missing file operand\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("base64: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (uint32_t i = 0; i < fn->content_size; i += 3) {
        uint8_t a = fn->content[i];
        uint8_t b = (i + 1 < fn->content_size) ? fn->content[i + 1] : 0;
        uint8_t c = (i + 2 < fn->content_size) ? fn->content[i + 2] : 0;
        append_char(tbl[(a >> 2) & 0x3F]);
        append_char(tbl[((a & 0x3) << 4) | ((b >> 4) & 0xF)]);
        append_char((i + 1 < fn->content_size) ? tbl[((b & 0xF) << 2) | ((c >> 6) & 0x3)] : '=');
        append_char((i + 2 < fn->content_size) ? tbl[c & 0x3F] : '=');
    }
    append_output("\n");
}

inline void cmd_strings_cmd(const char* path) {
    if (!path) { append_output("strings: missing file\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("strings: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    int count = 0;
    for (uint32_t i = 0; i < fn->content_size; i++) {
        if (fn->content[i] >= 32 && fn->content[i] < 127) {
            append_char(fn->content[i]);
            count++;
        } else {
            if (count >= 4) append_output("\n");
            count = 0;
        }
    }
    if (count >= 4) append_output("\n");
}

inline void cmd_od(const char* path) {
    if (!path) { append_output("od: missing file\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("od: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    for (uint32_t i = 0; i < fn->content_size; i += 16) {
        char addr[12]; int ai = 0;
        uint32_t a = i;
        char hex[] = "0123456789abcdef";
        for (int s = 28; s >= 0; s -= 4) addr[ai++] = hex[(a >> s) & 0xF];
        addr[ai] = 0;
        append_output(addr); append_output("  ");
        for (int j = 0; j < 16; j++) {
            if (i + j < fn->content_size) {
                uint8_t v = fn->content[i + j];
                append_char(hex[(v >> 4) & 0xF]); append_char(hex[v & 0xF]);
            } else { append_output("  "); }
            if (j % 2 == 1) append_char(' ');
        }
        append_output(" >");
        for (int j = 0; j < 16 && (i + j) < fn->content_size; j++) {
            uint8_t v = fn->content[i + j];
            append_char((v >= 32 && v < 127) ? v : '.');
        }
        append_output("<\n");
    }
}

inline void cmd_hexdump(const char* path) { cmd_od(path); }

inline void cmd_md5sum(const char* path) {
    if (!path) { append_output("md5sum: missing file\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("md5sum: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint32_t h = 0x67452301;
    for (uint32_t i = 0; i < fn->content_size; i++) {
        h = ((h << 5) + h) + fn->content[i];
        h ^= h >> 16; h *= 0x45d9f3b; h ^= h >> 16;
    }
    char hex[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) append_char(hex[(h >> i) & 0xF]);
    append_output("  "); append_output(path); append_output("\n");
}

inline void cmd_cksum(const char* path) {
    if (!path) { append_output("cksum: missing file\n"); return; }
    int fi = resolve_file(path);
    if (fi < 0) { append_output("cksum: '"); append_output(path); append_output("': No such file\n"); return; }
    vfs::VfsNode* fn = vfs::get_node(fi);
    uint32_t crc = 0;
    for (uint32_t i = 0; i < fn->content_size; i++) {
        crc ^= fn->content[i]; for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    print_u64((uint64_t)crc); append_output(" "); print_u64(fn->content_size); append_output(" "); append_output(path); append_output("\n");
}

// ==================== TREE (recursive) ====================
inline void cmd_tree_r(const char* path, int depth, int maxd) {
    if (depth >= maxd) return;
    int dir_i = vfs::resolve_path(path);
    if (dir_i < 0) return;
    vfs::VfsNode* node = vfs::get_node(dir_i);
    if (!node || node->type != vfs::NODE_DIR) return;
    for (int c = 0; c < node->child_count; c++) {
        vfs::VfsNode* child = vfs::get_node(node->children[c]);
        if (!child) continue;
        for (int d = 0; d < depth; d++) append_output("|   ");
        append_output("|-- "); append_output(child->name);
        if (child->type == vfs::NODE_DIR) append_output("/");
        append_output("\n");
        if (child->type == vfs::NODE_DIR) {
            char sub[vfs::MAX_PATH]; str_cpy(sub, path);
            int l = str_len(sub);
            if (!(l == 1 && sub[0] == '/')) { sub[l] = '/'; l++; }
            int j = 0; while (child->name[j]) sub[l++] = child->name[j++]; sub[l] = 0;
            cmd_tree_r(sub, depth + 1, maxd);
        }
    }
}

// ==================== DATE ====================
inline void cmd_date_full() {
    uint64_t t = pit::get_ticks();
    int s = (int)(t / 100) % 60;
    int m = (int)(t / 6000) % 60;
    int h = 12 + (int)(t / 360000) % 12;
    char b[8];
    append_output("Thu Jul 17 ");
    if (h < 10) append_char('0');
    itoa(h, b); append_output(b); append_char(':');
    if (m < 10) append_char('0');
    itoa(m, b); append_output(b); append_char(':');
    if (s < 10) append_char('0');
    itoa(s, b); append_output(b);
    append_output(" UTC 2026\n");
}

// ==================== UPTIME IMPROVED ====================
inline void cmd_uptime_imp() {
    uint64_t t = pit::get_ticks() / 100;
    int h = (int)(t / 3600);
    int m = (int)((t % 3600) / 60);
    int s = (int)(t % 60);
    append_output(" 12:00:00 up ");
    if (h > 0) { print_int(h); append_output(":"); if (m < 10) append_char('0'); }
    print_int(m); append_output(":"); if (s < 10) append_char('0'); print_int(s);
    append_output(",  1 user,  load average: 0.00, 0.00, 0.00\n");
}

// ==================== FREE IMPROVED ====================
inline void cmd_free_full() {
    uint64_t total = pmm::get_total_pages() * 4;
    uint64_t used = pmm::get_used_pages() * 4;
    uint64_t free_m = total - used;
    uint64_t shared = 0, buff = 0, avail = free_m;
    append_output("              total        used        free      shared  buff/cache   available\n");
    append_output("Mem:      "); char b[16];
    itoa((int)(total / 1024), b); int l = str_len(b); while (l < 11) { append_output(" "); l++; } append_output(b);
    itoa((int)(used / 1024), b); l = str_len(b); while (l < 11) { append_output(" "); l++; } append_output(b);
    itoa((int)(free_m / 1024), b); l = str_len(b); while (l < 11) { append_output(" "); l++; } append_output(b);
    itoa((int)(shared / 1024), b); l = str_len(b); while (l < 11) { append_output(" "); l++; } append_output(b);
    itoa((int)(buff / 1024), b); l = str_len(b); while (l < 11) { append_output(" "); l++; } append_output(b);
    itoa((int)(avail / 1024), b); append_output(b); append_output("\n");
    append_output("Swap:           0           0           0\n");
}

// ==================== INIT HELPERS DONE ====================
// Now the massive help text and dispatch

inline void cmd_help_full() {
    append_output(
        "=== FILE OPERATIONS ===\n"
        "  ls [opt] [path]     List directory (-a all, -l long, -la both)\n"
        "  cd [path]           Change directory\n"
        "  pwd                 Print working directory\n"
        "  cat <file>          Display file contents\n"
        "  cp <src> <dst>      Copy file\n"
        "  mv <src> <dst>      Move/rename file\n"
        "  rm [-r] <path>      Remove file/directory\n"
        "  rmdir <path>        Remove empty directory\n"
        "  mkdir <path>        Create directory\n"
        "  touch <file>        Create empty file\n"
        "  ln <target> <name>  Create hard link\n"
        "  find <dir> [-name]  Find files recursively\n"
        "  file <file>         Determine file type\n"
        "  stat <file>         Show file status\n"
        "  du [-h] [path]      Disk usage\n"
        "  df                  Disk free space\n"
        "  chmod <mode> <file> Change permissions\n"
        "  chown <owner> <f>   Change owner\n"
        "  basename <path>     Strip directory\n"
        "  dirname <path>      Strip filename\n"
        "  realpath <path>     Resolve full path\n"
        "  mktemp [tpl]        Create temp file\n"
        "\n=== TEXT PROCESSING ===\n"
        "  head [-n N] <file>  First N lines\n"
        "  tail [-n N] <file>  Last N lines\n"
        "  grep [-inv] P <f>   Search text (-i ignore, -n line#, -v invert)\n"
        "  sort [-r] [-n] <f>  Sort lines (-r reverse, -n numeric)\n"
        "  uniq [-c] <file>    Filter duplicates (-c count)\n"
        "  cut -dC -fN <file>  Cut columns\n"
        "  tr <from> <to> <f>  Translate characters\n"
        "  nl <file>           Number lines\n"
        "  tac <file>          Reverse line order\n"
        "  rev <file>          Reverse characters\n"
        "  wc <file>           Word/line/char count\n"
        "  shuf [-n N] <file>  Randomize lines\n"
        "  fmt <file>          Format text\n"
        "  tree [path] [depth] Directory tree\n"
        "  xargs <cmd>         Execute with args\n"
        "  tee <file>          Write to file and stdout\n"
        "\n=== SYSTEM INFO ===\n"
        "  uptime              System uptime\n"
        "  free [-h]           Memory usage\n"
        "  ps                  Process list\n"
        "  top                 Task viewer\n"
        "  id                  User/group IDs\n"
        "  groups              Group membership\n"
        "  who                 Logged in users\n"
        "  w                   Who and what\n"
        "  dmesg               Kernel messages\n"
        "  uname [-a]          System information\n"
        "  hostname            Print hostname\n"
        "  neofetch            System display\n"
        "  lsblk               Block devices\n"
        "  lscpu               CPU info\n"
        "  lsmem               Memory info\n"
        "  lsns                Namespaces\n"
        "  lsmod               Kernel modules\n"
        "  lsirq               IRQ info\n"
        "  vmstat              VM statistics\n"
        "  iostat              I/O statistics\n"
        "  lshw                Hardware info\n"
        "\n=== PROCESS ===\n"
        "  kill <pid>          Kill process\n"
        "  killall <name>      Kill by name\n"
        "  pkill <pattern>     Kill by pattern\n"
        "  pgrep <pattern>     Find PID by pattern\n"
        "  nice [-n N] <cmd>   Run with priority\n"
        "  renice <pid> <prio> Change priority\n"
        "  timeout <s> <cmd>   Run with timeout\n"
        "  time <cmd>          Time a command\n"
        "  wait                Wait for processes\n"
        "  jobs                List jobs\n"
        "  bg [%N]             Background job\n"
        "  fg [%N]             Foreground job\n"
        "  nohup <cmd>         Run immune to hangup\n"
        "\n=== SHELL BUILTINS ===\n"
        "  export [KEY=VAL]    Set environment variable\n"
        "  unset <KEY>         Remove environment variable\n"
        "  env                 List all environment\n"
        "  printenv [KEY]      Print environment\n"
        "  alias [name=val]    Set/list aliases\n"
        "  unalias <name>      Remove alias\n"
        "  type <cmd>          Show command type\n"
        "  which <cmd>         Show command path\n"
        "  whereis <cmd>       Show all paths\n"
        "  command <cmd>       Execute directly\n"
        "  builtin <cmd>       Execute builtin\n"
        "  source <file>       Execute file as script\n"
        "  . <file>            Execute file (alias)\n"
        "  eval <cmd>          Evaluate command\n"
        "  exec <cmd>          Replace shell\n"
        "  exit                Exit shell\n"
        "  history             Command history\n"
        "  read <var>          Read input\n"
        "  mapfile <file>      Read file into array\n"
        "  set                 Shell options\n"
        "  echo <text>         Print text\n"
        "  true                Return 0\n"
        "  false               Return 1\n"
        "  yes [text]          Repeat text\n"
        "\n=== USER/GROUP ===\n"
        "  sudo <cmd>          Run as superuser\n"
        "  su [user]           Switch user\n"
        "  adduser <name>      Add user\n"
        "  useradd <name>      Add user (alias)\n"
        "  userdel <name>      Delete user\n"
        "  groupadd <name>     Add group\n"
        "  groupdel <name>     Delete group\n"
        "  passwd [user]       Change password\n"
        "  chfn                Change finger info\n"
        "  chsh                Change shell\n"
        "\n=== NETWORKING ===\n"
        "  ip [addr|route]     Network config\n"
        "  ifconfig            Interface config\n"
        "  ping <host>         ICMP ping\n"
        "  curl <url>          HTTP request\n"
        "  wget <url>          Download file\n"
        "  ss                  Socket stats\n"
        "  traceroute <host>   Trace route\n"
        "  arp                 ARP table\n"
        "  route               Routing table\n"
        "\n=== COMPRESSION ===\n"
        "  tar [xf|cf|tf] <a>  Archive tool\n"
        "  gzip <file>         Compress\n"
        "  gunzip <file>       Decompress\n"
        "  zip <arch> <file>   Create zip\n"
        "  unzip <file>        Extract zip\n"
        "\n=== DISK ===\n"
        "  dd if=<f> of=<f>    Copy/convert\n"
        "  fsck                Filesystem check\n"
        "  blkid               Block ID\n"
        "  fdisk               Partition table\n"
        "  mkfs                Make filesystem\n"
        "  mount               Mount info\n"
        "  umount              Unmount\n"
        "\n=== MATH/NUMBERS ===\n"
        "  expr <arg> <op> <arg>  Evaluate expression\n"
        "  seq [start] <end> [step]  Number sequence\n"
        "  factor <n>          Factorize number\n"
        "  numfmt <n> [unit]   Format numbers\n"
        "  cal [mon] [year]    Calendar\n"
        "  ncal [mon] [year]   Calendar (alternate)\n"
        "\n=== MISC TOOLS ===\n"
        "  base64 <file>       Base64 encode\n"
        "  strings <file>      Print strings\n"
        "  od <file>           Octal dump\n"
        "  hexdump <file>      Hex dump\n"
        "  md5sum <file>       MD5 hash\n"
        "  cksum <file>        Checksum\n"
        "  clear               Clear screen\n"
        "  help                Show this help\n"
    );
}

// ==================== LS WITH FLAGS ====================
inline void cmd_ls_full(const char* path_arg, bool show_all, bool long_fmt) {
    const char* target = (path_arg && path_arg[0]) ? path_arg : shell_state.cwd;
    int dir_idx = vfs::resolve_path(target);
    if (dir_idx < 0) { append_output("ls: cannot access '"); append_output(target); append_output("': No such file or directory\n"); return; }
    vfs::VfsNode* node = vfs::get_node(dir_idx);
    if (!node) return;
    if (node->type != vfs::NODE_DIR) { append_output(target); append_output("\n"); return; }
    int children[64]; int count = vfs::get_children(dir_idx, children, 64);
    if (!show_all && count == 0) { append_output("(empty directory)\n"); return; }

    if (long_fmt) {
        append_output("total "); print_int(count); append_output("\n");
    }
    for (int i = 0; i < count; i++) {
        vfs::VfsNode* child = vfs::get_node(children[i]);
        if (!child) continue;
        if (!show_all && child->name[0] == '.') continue;
        if (long_fmt) {
            char mode[11]; mode[0] = child->type == vfs::NODE_DIR ? 'd' : '-';
            mode[1] = 'r'; mode[2] = 'w'; mode[3] = 'x'; mode[4] = 'r';
            mode[5] = '-'; mode[6] = 'x'; mode[7] = 'r'; mode[8] = '-';
            mode[9] = 'x'; mode[10] = 0;
            append_output(mode); append_output(" 1 user user ");
            char sz[16]; itoa((int)child->content_size, sz);
            int l = str_len(sz); while (l < 6) { append_output(" "); l++; }
            append_output(sz); append_output(" Jul 17 12:00 ");
        }
        if (child->type == vfs::NODE_DIR) {
            append_output("\033[34m"); append_output(child->name); append_output("/\033[0m");
        } else {
            append_output(child->name);
        }
        if (long_fmt) append_output("\n");
        else append_output("  ");
    }
    if (!long_fmt) append_output("\n");
}

// ==================== UNAME IMPROVED ====================
inline void cmd_uname_full(bool all) {
    if (!all) { append_output("Foxiwium\n"); return; }
    append_output("Foxiwium foxiwium 0.0-1-generic #1 SMP PREEMPT x86_64 Foxiwium\n");
}

// ==================== EXECUTE ====================
inline void execute(const char* cmd) {
    if (!cmd || cmd[0] == 0) return;

    // Expand aliases (simple first-word replacement)
    char expanded[MAX_CMD_LEN];
    str_cpy(expanded, cmd);

    // Add to history
    if (shell_state.history_count < MAX_HISTORY) {
        int i = 0; while (cmd[i] && i < MAX_CMD_LEN - 1) { shell_state.history[shell_state.history_count][i] = cmd[i]; i++; }
        shell_state.history[shell_state.history_count][i] = 0;
        shell_state.history_count++;
    }

    char args[MAX_ARGS][MAX_CMD_LEN];
    int argc = 0;
    parse_args(cmd, args, argc);
    if (argc == 0) return;

    // Alias expansion
    const char* al = alias_get(args[0]);
    if (al) {
        char newcmd[MAX_CMD_LEN]; str_cpy(newcmd, al);
        for (int i = 1; i < argc; i++) { int l = str_len(newcmd); newcmd[l] = ' ';
            int j = 0; while (args[i][j]) newcmd[++l] = args[i][j++]; newcmd[++l] = 0; }
        execute(newcmd);
        return;
    }

    const char* a0 = args[0];

    // ---- FILE OPS ----
    if (str_eq(a0, "ls")) {
        bool sa = false, lf = false; const char* path = nullptr;
        for (int i = 1; i < argc; i++) {
            if (args[i][0] == '-') {
                for (int j = 1; args[i][j]; j++) { if (args[i][j] == 'a') sa = true; if (args[i][j] == 'l') lf = true; }
            } else path = args[i];
        }
        cmd_ls_full(path, sa, lf);
    }
    else if (str_eq(a0, "cd")) cmd_cd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "pwd")) cmd_pwd();
    else if (str_eq(a0, "cat")) cmd_cat(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "cp")) cmd_cp(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "mv")) cmd_mv(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "rm")) {
        bool recursive = false;
        const char* path = nullptr;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-r") || str_eq(args[i], "-rf") || str_eq(args[i], "-fr")) recursive = true; else path = args[i]; }
        if (path) cmd_rm_r(path, recursive);
        else append_output("rm: missing operand\n");
    }
    else if (str_eq(a0, "rmdir")) cmd_rmdir(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "mkdir")) cmd_mkdir(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "touch")) cmd_touch(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "ln")) cmd_ln(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "find")) {
        const char* path = nullptr, *name = nullptr, *type = nullptr;
        for (int i = 1; i < argc; i++) {
            if (str_eq(args[i], "-name") && i + 1 < argc) name = args[++i];
            else if (str_eq(args[i], "-type") && i + 1 < argc) type = args[++i];
            else if (args[i][0] != '-') path = args[i];
        }
        cmd_find(path, name, type);
    }
    else if (str_eq(a0, "file")) cmd_file_cmd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "stat")) cmd_stat(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "du")) {
        bool h = false; const char* path = nullptr;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-h")) h = true; else path = args[i]; }
        cmd_du(path, h);
    }
    else if (str_eq(a0, "df")) cmd_df();
    else if (str_eq(a0, "chmod")) cmd_chmod(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "chown")) cmd_chown(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "basename")) cmd_basename(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "dirname")) cmd_dirname(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "realpath")) cmd_realpath(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "mktemp")) cmd_mktemp(argc > 1 ? args[1] : nullptr);

    // ---- TEXT ----
    else if (str_eq(a0, "head")) {
        const char* narg = nullptr, *path = nullptr;
        for (int i = 1; i < argc; i++) { if (args[i][0] == '-') narg = args[i]; else path = args[i]; }
        cmd_head(narg, path);
    }
    else if (str_eq(a0, "tail")) {
        const char* narg = nullptr, *path = nullptr;
        for (int i = 1; i < argc; i++) { if (args[i][0] == '-') narg = args[i]; else path = args[i]; }
        cmd_tail(narg, path);
    }
    else if (str_eq(a0, "grep")) {
        bool ic = false, sl = false, cnt = false, inv = false;
        const char* pattern = nullptr, *path = nullptr;
        for (int i = 1; i < argc; i++) {
            if (args[i][0] == '-') {
                for (int j = 1; args[i][j]; j++) { if (args[i][j] == 'i') ic = true; if (args[i][j] == 'n') sl = true; if (args[i][j] == 'c') cnt = true; if (args[i][j] == 'v') inv = true; }
            } else if (!pattern) pattern = args[i]; else path = args[i];
        }
        cmd_grep(pattern, path, ic, sl, cnt, inv);
    }
    else if (str_eq(a0, "sort")) {
        bool rev = false, num = false; const char* path = nullptr;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-r")) rev = true; else if (str_eq(args[i], "-n")) num = true; else path = args[i]; }
        cmd_sort(path, rev, num);
    }
    else if (str_eq(a0, "uniq")) {
        bool c = false; const char* path = nullptr;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-c")) c = true; else path = args[i]; }
        cmd_uniq(path, c);
    }
    else if (str_eq(a0, "cut")) {
        const char* delim = nullptr, *fields = nullptr, *path = nullptr;
        for (int i = 1; i < argc; i++) {
            if (str_eq(args[i], "-d") && i + 1 < argc) delim = args[++i];
            else if (str_eq(args[i], "-f") && i + 1 < argc) fields = args[++i];
            else if (args[i][0] == '-') delim = args[i]; else path = args[i];
        }
        cmd_cut(delim, fields, path);
    }
    else if (str_eq(a0, "tr")) cmd_tr(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr, argc > 3 ? args[3] : nullptr);
    else if (str_eq(a0, "nl")) cmd_nl(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "tac")) cmd_tac(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "rev")) cmd_rev(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "wc")) cmd_wc_full(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "shuf")) {
        int n = 0; const char* path = nullptr;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-n") && i + 1 < argc) n = atoi(args[++i]); else path = args[i]; }
        cmd_shuf(path, n);
    }
    else if (str_eq(a0, "fmt")) cmd_fmt(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "tree")) {
        const char* path = nullptr; int depth = 3;
        for (int i = 1; i < argc; i++) {
            if (args[i][0] == '-' && str_eq(args[i], "-L") && i + 1 < argc) depth = atoi(args[++i]);
            else if (args[i][0] >= '0' && args[i][0] <= '9') depth = atoi(args[i]);
            else path = args[i];
        }
        const char* target = (path && path[0]) ? path : shell_state.cwd;
        append_output(target); append_output("\n");
        cmd_tree_r(target, 0, depth);
    }
    else if (str_eq(a0, "xargs")) cmd_xargs(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "yes")) cmd_yes(argc > 1 ? args[1] : nullptr);

    // ---- SYSTEM INFO ----
    else if (str_eq(a0, "uptime")) cmd_uptime_imp();
    else if (str_eq(a0, "free")) cmd_free_full();
    else if (str_eq(a0, "ps")) cmd_ps();
    else if (str_eq(a0, "top") || str_eq(a0, "htop")) cmd_top_header();
    else if (str_eq(a0, "id")) cmd_id();
    else if (str_eq(a0, "groups")) cmd_groups();
    else if (str_eq(a0, "who")) cmd_who();
    else if (str_eq(a0, "w")) cmd_w();
    else if (str_eq(a0, "dmesg")) cmd_dmesg();
    else if (str_eq(a0, "uname")) {
        bool all = false;
        for (int i = 1; i < argc; i++) { if (str_eq(args[i], "-a")) all = true; }
        cmd_uname_full(all);
    }
    else if (str_eq(a0, "hostname")) cmd_hostname();
    else if (str_eq(a0, "neofetch")) cmd_neofetch();
    else if (str_eq(a0, "lsblk")) cmd_lsblk();
    else if (str_eq(a0, "lscpu")) cmd_lscpu();
    else if (str_eq(a0, "lsmem")) cmd_lsmem();
    else if (str_eq(a0, "lsns")) cmd_lsns();
    else if (str_eq(a0, "lsmod")) cmd_lsmod();
    else if (str_eq(a0, "lsirq")) cmd_lsirq();
    else if (str_eq(a0, "lshw")) { append_output("Foxiwium Hardware List\n*-cpu: Foxiwium vCPU\n*-memory: "); char b[16]; itoa((int)(pmm::get_total_pages() * 4 / 1024), b); append_output(b); append_output(" MiB RAM\n*-disk: fox0 512MiB\n*-network: eth0\n"); }
    else if (str_eq(a0, "vmstat")) cmd_vmstat();
    else if (str_eq(a0, "iostat")) cmd_iostat();
    else if (str_eq(a0, "date")) cmd_date_full();
    else if (str_eq(a0, "cal")) cmd_cal(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "ncal")) cmd_cal(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);

    // ---- PROCESS ----
    else if (str_eq(a0, "kill")) {
        if (argc > 2) cmd_kill_pid(args[2], args[1]);
        else if (argc > 1 && args[1][0] != '-') cmd_kill_pid(args[1], nullptr);
        else append_output("usage: kill [-signal] pid\n");
    }
    else if (str_eq(a0, "killall")) cmd_killall(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "pkill")) cmd_killall(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "pgrep")) cmd_pgrep(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "nice")) {
        const char* cmd_s = nullptr;
        for (int i = 1; i < argc; i++) { if (!str_eq(args[i], "-n")) cmd_s = args[i]; }
        if (argc > 3 && str_eq(args[1], "-n")) cmd_nice(args[2], argc > 3 ? args[3] : nullptr);
        else cmd_nice(nullptr, cmd_s);
    }
    else if (str_eq(a0, "renice")) cmd_renice(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "timeout")) cmd_timeout(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "time")) cmd_time_cmd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "wait")) cmd_wait();
    else if (str_eq(a0, "nohup")) { if (argc > 1) { append_output(shell_state.cwd); append_output("$ "); append_output(args[1]); append_output("\n"); execute(args[1]); } }
    else if (str_eq(a0, "jobs") || str_eq(a0, "bg") || str_eq(a0, "fg")) { append_output(a0); append_output(": no current jobs\n"); }

    // ---- SHELL BUILTINS ----
    else if (str_eq(a0, "export")) cmd_export(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "unset")) cmd_unset(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "env")) cmd_env();
    else if (str_eq(a0, "printenv")) {
        if (argc > 1) { const char* v = env_get(args[1]); if (v) { append_output(v); append_output("\n"); } }
        else cmd_env();
    }
    else if (str_eq(a0, "alias")) cmd_alias_cmd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "unalias")) cmd_unalias(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "type")) cmd_type(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "which")) cmd_which(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "whereis")) cmd_whereis(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "history")) cmd_history();
    else if (str_eq(a0, "source") || str_eq(a0, ".")) cmd_source(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "exec")) { if (argc > 1) { for (int i = 1; i < argc; i++) { append_output(args[i]); append_char(' '); } append_output("\n"); } }
    else if (str_eq(a0, "eval")) { for (int i = 1; i < argc; i++) { append_output(args[i]); append_char(' '); } append_output("\n"); }
    else if (str_eq(a0, "read")) { append_output("(read: simulated input)\n"); }
    else if (str_eq(a0, "mapfile")) cmd_mapfile(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "set")) { for (int i = 0; i < shell_state.env_count; i++) { append_output(shell_state.env[i].key); append_output("="); append_output(shell_state.env[i].value); append_char('\n'); } }
    else if (str_eq(a0, "echo")) {
        for (int i = 1; i < argc; i++) { if (i > 1) append_output(" "); append_output(args[i]); }
        append_output("\n");
    }
    else if (str_eq(a0, "printf")) {
        for (int i = 1; i < argc; i++) { if (i > 1) append_output(" "); append_output(args[i]); }
        append_output("\n");
    }
    else if (str_eq(a0, "true")) {}
    else if (str_eq(a0, "false")) { append_output(""); }
    else if (str_eq(a0, "exit") || str_eq(a0, "logout")) { append_output("Goodbye.\n"); }
    else if (str_eq(a0, "help")) cmd_help_full();
    else if (str_eq(a0, "clear")) clear_output();

    // ---- USER/GROUP ----
    else if (str_eq(a0, "sudo")) cmd_sudo(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "su")) cmd_su(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "adduser")) cmd_adduser(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "useradd")) cmd_useradd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "userdel")) cmd_userdel(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "groupadd")) cmd_groupadd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "groupdel")) cmd_groupdel(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "passwd")) cmd_passwd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "chfn")) cmd_chfn();
    else if (str_eq(a0, "chsh")) cmd_chsh();
    else if (str_eq(a0, "command")) { if (argc > 1) { append_output(args[1]); append_output(": /bin/"); append_output(args[1]); append_output("\n"); } }
    else if (str_eq(a0, "builtin")) { append_output("builtin: shell builtin\n"); }

    // ---- NETWORKING ----
    else if (str_eq(a0, "ip")) cmd_ip();
    else if (str_eq(a0, "ifconfig")) cmd_ifconfig();
    else if (str_eq(a0, "ping")) cmd_ping(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "curl")) cmd_curl(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "wget")) cmd_wget(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "ss")) cmd_ss();
    else if (str_eq(a0, "netstat")) cmd_ss();
    else if (str_eq(a0, "traceroute")) cmd_traceroute(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "arp")) cmd_arp();
    else if (str_eq(a0, "route")) cmd_route();
    else if (str_eq(a0, "dig")) { append_output("dig: DNS not available\n"); }
    else if (str_eq(a0, "nslookup")) { append_output("nslookup: DNS not available\n"); }

    // ---- COMPRESSION ----
    else if (str_eq(a0, "tar")) cmd_tar(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "gzip")) cmd_gzip(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "gunzip")) cmd_gunzip(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "zcat")) cmd_gunzip(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "zip")) cmd_zip(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "unzip")) cmd_unzip(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "bzip2")) { append_output("bzip2: "); if (argc > 1) { append_output(args[1]); append_output(" -> "); append_output(args[1]); append_output(".bz2\n"); } }
    else if (str_eq(a0, "xz")) { append_output("xz: "); if (argc > 1) { append_output(args[1]); append_output(" -> "); append_output(args[1]); append_output(".xz\n"); } }
    else if (str_eq(a0, "compress")) { append_output("compress: "); if (argc > 1) { append_output(args[1]); append_output(" -> "); append_output(args[1]); append_output(".Z\n"); } }
    else if (str_eq(a0, "uncompress")) { append_output("uncompress: "); if (argc > 1) append_output(args[1]); append_output("\n"); }
    else if (str_eq(a0, "cpio")) { append_output("cpio: archive tool (use tar instead)\n"); }

    // ---- DISK ----
    else if (str_eq(a0, "dd")) cmd_dd(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);
    else if (str_eq(a0, "fsck")) cmd_fsck();
    else if (str_eq(a0, "blkid")) cmd_blkid();
    else if (str_eq(a0, "fdisk")) cmd_fdisk();
    else if (str_eq(a0, "mkfs")) cmd_mkfs();
    else if (str_eq(a0, "mount")) cmd_mount();
    else if (str_eq(a0, "umount")) cmd_umount();
    else if (str_eq(a0, "losetup")) { append_output("losetup: no loop devices\n"); }
    else if (str_eq(a0, "parted")) { append_output("parted: disk partitioning tool (stub)\n"); }
    else if (str_eq(a0, "sfdisk")) { append_output("sfdisk: disk scriptable partition tool (stub)\n"); }
    else if (str_eq(a0, "wipefs")) { append_output("wipefs: wipe filesystem signatures (stub)\n"); }
    else if (str_eq(a0, "swapon")) { append_output("swapon: enabling swap\n"); }
    else if (str_eq(a0, "swapoff")) { append_output("swapoff: disabling swap\n"); }

    // ---- MATH ----
    else if (str_eq(a0, "expr")) {
        char expr_str[MAX_CMD_LEN] = {};
        for (int i = 1; i < argc; i++) { int l = str_len(expr_str); if (i > 1) expr_str[l++] = ' '; int j = 0; while (args[i][j]) expr_str[l++] = args[i][j++]; expr_str[l] = 0; }
        cmd_expr(expr_str);
    }
    else if (str_eq(a0, "seq")) cmd_seq(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr, argc > 3 ? args[3] : nullptr);
    else if (str_eq(a0, "factor")) cmd_factor(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "numfmt")) cmd_numfmt(argc > 1 ? args[1] : nullptr, argc > 2 ? args[2] : nullptr);

    // ---- MISC ----
    else if (str_eq(a0, "base64")) cmd_base64(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "strings")) cmd_strings_cmd(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "od")) cmd_od(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "hexdump") || str_eq(a0, "xxd")) cmd_hexdump(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "md5sum")) cmd_md5sum(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "sha1sum")) { append_output("SHA1: simulated hash\n"); }
    else if (str_eq(a0, "sha256sum")) { append_output("SHA256: simulated hash\n"); }
    else if (str_eq(a0, "cksum")) cmd_cksum(argc > 1 ? args[1] : nullptr);
    else if (str_eq(a0, "tee")) { if (argc > 1) { append_output("(writing to "); append_output(args[1]); append_output(")\n"); } }
    else if (str_eq(a0, "shred")) { append_output("shred: overwriting '"); if (argc > 1) append_output(args[1]); append_output("' (simulated)\n"); }
    else if (str_eq(a0, "install")) { if (argc > 2) { append_output("install: "); append_output(args[argc - 2]); append_output(" -> "); append_output(args[argc - 1]); append_output("\n"); } }

    // ---- MAN/HELP ----
    else if (str_eq(a0, "man")) {
        if (argc > 1) { append_output("No manual entry for "); append_output(args[1]); append_output("\n"); }
        else append_output("What manual page do you want?\n");
    }
    else if (str_eq(a0, "info")) { append_output("info: "); if (argc > 1) append_output(args[1]); append_output(" - no info available\n"); }
    else if (str_eq(a0, "whatis")) { append_output(argc > 1 && args[1][0] ? args[1] : "?"); append_output(" - command description\n"); }
    else if (str_eq(a0, "apropos")) { append_output("apropos: nothing appropriate\n"); }

    else if (str_eq(a0, "no")) { append_output("no\n"); }

    else if (a0[0] == '/') {
        append_output(a0); append_output(": command found at path\n");
    }
    else {
        append_output(a0); append_output(": command not found\n");
    }
}

}
