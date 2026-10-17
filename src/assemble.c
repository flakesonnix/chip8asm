#define _XOPEN_SOURCE 700
#include "chip8.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { char *name; uint32_t address; } Label;
typedef struct { char *text; SourceRange range; } Node;
typedef struct {
    Program *p;
    Label *labels; size_t label_count;
    Node *nodes; size_t node_count;
    uint32_t pc;
} Assembly;

static _Noreturn void error(const Program *p, SourceLoc loc, const char *msg) {
    fatal("%s:%u:%u: %s", p->files[loc.file - 1], loc.line, loc.column, msg);
}
static bool equal(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return false;
    }
    return *a == *b;
}
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0; return s;
}
static bool identifier(const char *s) {
    if (!isalpha((unsigned char)*s) && *s != '_') return false;
    for (++s; *s; ++s) if (!isalnum((unsigned char)*s) && *s != '_') return false;
    return true;
}
static bool number(const char *s, uint32_t *n) {
    if (!isdigit((unsigned char)*s)) return false;
    int base = s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10;
    const char *digits = base == 16 ? s + 2 : s;
    if (!*digits) return false;
    for (const char *q = digits; *q; ++q)
        if (!(base == 16 ? isxdigit((unsigned char)*q) : isdigit((unsigned char)*q))) return false;
    errno = 0; char *end;
    unsigned long x = strtoul(s, &end, base);
    if (errno || *end || x > UINT32_MAX) return false;
    *n = (uint32_t)x; return true;
}
static uint32_t value(const Assembly *a, SourceLoc loc, const char *s, uint32_t limit) {
    uint32_t n;
    if (!number(s, &n)) {
        bool found = false;
        for (size_t i = 0; i < a->label_count; ++i) if (!strcmp(s, a->labels[i].name)) {
            n = a->labels[i].address; found = true; break;
        }
        if (!found) error(a->p, loc, "invalid number or undefined symbol");
    }
    if (n > limit) error(a->p, loc, "operand out of range");
    return n;
}
/* Bounded token count also bounds each data directive's emitted size. */
static size_t tokens(char *s, char **out, SourceLoc loc, const Program *p) {
    size_t n = 0;
    while (*s) {
        while (isspace((unsigned char)*s) || *s == ',') ++s;
        if (!*s) break;
        if (n == 257) error(p, loc, "too many operands (maximum 256)");
        out[n++] = s;
        while (*s && !isspace((unsigned char)*s) && *s != ',') ++s;
        if (*s) *s++ = 0;
    }
    return n;
}
static void add_label(Assembly *a, char *name, SourceLoc loc) {
    if (!identifier(name)) error(a->p, loc, "invalid label");
    for (size_t i = 0; i < a->label_count; ++i)
        if (!strcmp(name, a->labels[i].name)) error(a->p, loc, "duplicate label");
    a->labels = resize(a->labels, a->label_count + 1, sizeof(*a->labels));
    a->labels[a->label_count++] = (Label){copy_string(name), a->pc};
}
static void parse_line(Assembly *a, char *line, SourceLoc loc) {
    char *comment = strpbrk(line, ";#"); if (comment) *comment = 0;
    char *s = trim(line);
    if (!*s) return;
    if (*s == ':') {
        char *name = trim(s + 1), *end = name;
        while (*end && !isspace((unsigned char)*end)) ++end;
        char saved = *end; *end = 0; add_label(a, name, loc);
        s = saved ? trim(end + 1) : end;
    } else {
        char *colon = strchr(s, ':');
        if (colon) { *colon = 0; add_label(a, trim(s), loc); s = trim(colon + 1); }
    }
    if (!*s) return;
    loc.column = (uint32_t)(s - line) + 1;
    char *scratch = copy_string(s), *t[257];
    size_t n = tokens(scratch, t, loc, a->p);
    if (!n) error(a->p, loc, "missing instruction");
    bool data = equal(t[0], ".byte") || equal(t[0], ".word");
    if (equal(t[0], ".org")) {
        uint32_t addr;
        if (n != 2 || !number(t[1], &addr)) error(a->p, loc, ".org requires one numeric address");
        if (addr < a->pc || addr > MEMORY_END) error(a->p, loc, ".org must advance within guest memory");
        a->pc = addr; free(scratch); return;
    }
    if (*t[0] == '.' && !data) error(a->p, loc, "unsupported directive");
    if (data && n < 2) error(a->p, loc, "data directive needs operands");
    uint32_t size = data ? (uint32_t)(n - 1) * (equal(t[0], ".word") ? 2u : 1u) : 2;
    if (size > MEMORY_END - a->pc) error(a->p, loc, "program exceeds CHIP-8 memory");
    a->nodes = resize(a->nodes, a->node_count + 1, sizeof(*a->nodes));
    a->nodes[a->node_count++] = (Node){copy_string(s), {a->pc, size, loc, !data}};
    a->pc += size; free(scratch);
}
static int reg(const char *s) {
    if (toupper((unsigned char)s[0]) != 'V' || !s[1] || s[2]) return -1;
    unsigned char c = (unsigned char)toupper((unsigned char)s[1]);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static uint16_t instruction(const Assembly *a, SourceLoc loc, char **t, size_t n) {
    const char *op = t[0], *b = n > 1 ? t[1] : "", *c = n > 2 ? t[2] : "";
    int x = reg(b), y = reg(c);
    uint16_t xy = (uint16_t)((x < 0 ? 0 : x << 8) | (y < 0 ? 0 : y << 4));
#define IMM(s, max) value(a, loc, (s), (max))
#define OP(s) equal(op, (s))
    if (n == 1 && OP("CLS")) return 0x00e0;
    if (n == 1 && OP("RET")) return 0x00ee;
    if (n == 2) {
        if (OP("SYS")) return (uint16_t)IMM(b, 0xfff);
        if (OP("JP")) return (uint16_t)(0x1000 | IMM(b, 0xfff));
        if (OP("CALL")) return (uint16_t)(0x2000 | IMM(b, 0xfff));
        if (x >= 0) {
            if (OP("SKP")) return (uint16_t)(0xe09e | xy);
            if (OP("SKNP")) return (uint16_t)(0xe0a1 | xy);
            if (OP("SHR")) return (uint16_t)(0x8006 | xy);
            if (OP("SHL")) return (uint16_t)(0x800e | xy);
        }
    }
    if (n == 3) {
        if (OP("JP") && equal(b, "V0")) return (uint16_t)(0xb000 | IMM(c, 0xfff));
        if (OP("LD") && equal(b, "I")) return (uint16_t)(0xa000 | IMM(c, 0xfff));
        if (OP("LD") && y >= 0) {
            if (equal(b, "DT")) return (uint16_t)(0xf015 | y << 8);
            if (equal(b, "ST")) return (uint16_t)(0xf018 | y << 8);
            if (equal(b, "F")) return (uint16_t)(0xf029 | y << 8);
            if (equal(b, "B")) return (uint16_t)(0xf033 | y << 8);
            if (equal(b, "[I]")) return (uint16_t)(0xf055 | y << 8);
        }
        if (OP("ADD") && equal(b, "I") && y >= 0) return (uint16_t)(0xf01e | y << 8);
        if (x >= 0) {
            if (OP("LD")) {
                if (y >= 0) return (uint16_t)(0x8000 | xy);
                if (equal(c, "DT")) return (uint16_t)(0xf007 | xy);
                if (equal(c, "K")) return (uint16_t)(0xf00a | xy);
                if (equal(c, "[I]")) return (uint16_t)(0xf065 | xy);
                return (uint16_t)(0x6000 | xy | IMM(c, 255));
            }
            if (OP("SE")) return (uint16_t)((y >= 0 ? 0x5000 : 0x3000) | xy | (y >= 0 ? 0 : IMM(c, 255)));
            if (OP("SNE")) return (uint16_t)((y >= 0 ? 0x9000 : 0x4000) | xy | (y >= 0 ? 0 : IMM(c, 255)));
            if (OP("ADD") && y < 0) return (uint16_t)(0x7000 | xy | IMM(c, 255));
            if (OP("RND")) {
                /* RND requires a byte, not a register operand. */
                return (uint16_t)(0xc000u | ((uint32_t)x << 8) | IMM(c, 255));
            }
            if (y >= 0) {
                const char *names[] = {"OR", "AND", "XOR", "ADD", "SUB", "SHR", "SUBN", "SHL"};
                const uint16_t codes[] = {1, 2, 3, 4, 5, 6, 7, 14};
                for (size_t i = 0; i < sizeof(codes)/sizeof(*codes); ++i)
                    if (OP(names[i])) return (uint16_t)(0x8000 | xy | codes[i]);
            }
        }
    }
    if (n == 4 && OP("DRW") && x >= 0 && y >= 0)
        return (uint16_t)(0xd000 | xy | IMM(t[3], 15));
    error(a->p, loc, "unsupported instruction or operand combination");
    return 0;
#undef OP
#undef IMM
}
void assemble(Program *p, char **paths, size_t count) {
    if (!count || count > UINT32_MAX) fatal("invalid source file count");
    p->files = resize(NULL, count, sizeof(*p->files)); p->file_count = count;
    Assembly a = {.p = p, .pc = ORIGIN};
    for (size_t i = 0; i < count; ++i) {
        p->files[i] = realpath(paths[i], NULL);
        if (!p->files[i]) fatal("source %s: %s", paths[i], strerror(errno));
        struct stat st;
        if (stat(p->files[i], &st) || !S_ISREG(st.st_mode)) fatal("source must be a regular file: %s", paths[i]);
        FILE *f = fopen(p->files[i], "rb");
        if (!f) fatal("read %s: %s", paths[i], strerror(errno));
        Buffer line = {0}; uint32_t line_no = 1; int ch;
        while ((ch = fgetc(f)) != EOF) {
            if (!ch) fatal("%s:%u: NUL in source", paths[i], line_no);
            if (ch == '\n') {
                u8(&line, 0); parse_line(&a, (char *)line.data, (SourceLoc){(uint32_t)i + 1, line_no, 1});
                line.len = 0;
                if (line_no == UINT32_MAX) fatal("too many source lines");
                ++line_no;
            } else {
                if (line.len == 65535) fatal("%s:%u: line exceeds 65535 bytes", paths[i], line_no);
                u8(&line, (uint8_t)ch);
            }
        }
        if (ferror(f)) fatal("read %s failed", paths[i]);
        if (fclose(f)) fatal("close %s failed", paths[i]);
        if (line.len) { u8(&line, 0); parse_line(&a, (char *)line.data, (SourceLoc){(uint32_t)i + 1, line_no, 1}); }
        free(line.data);
    }
    p->rom_size = a.pc - ORIGIN;
    p->ranges = resize(NULL, a.node_count, sizeof(*p->ranges)); p->range_count = a.node_count;
    for (size_t i = 0; i < a.node_count; ++i) {
        Node *node = &a.nodes[i]; char *t[257];
        size_t n = tokens(node->text, t, node->range.loc, p);
        size_t offset = node->range.address - ORIGIN;
        if (node->range.instruction) {
            uint16_t word = instruction(&a, node->range.loc, t, n);
            p->rom[offset] = (uint8_t)(word >> 8); p->rom[offset + 1] = (uint8_t)word;
        } else {
            bool word = equal(t[0], ".word");
            for (size_t j = 1; j < n; ++j) {
                uint32_t v = value(&a, node->range.loc, t[j], word ? 65535 : 255);
                if (word) p->rom[offset++] = (uint8_t)(v >> 8);
                p->rom[offset++] = (uint8_t)v;
            }
        }
        p->ranges[i] = node->range; free(node->text);
    }
    for (size_t i = 0; i < a.label_count; ++i) free(a.labels[i].name);
    free(a.labels); free(a.nodes);
}
void free_program(Program *p) {
    for (size_t i = 0; i < p->file_count; ++i) free(p->files[i]);
    free(p->files); free(p->ranges);
}
