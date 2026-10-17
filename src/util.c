#define _XOPEN_SOURCE 700
#include "chip8.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Noreturn void fatal(const char *fmt, ...) {
    va_list ap;
    fputs("chip8asm: ", stderr);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}
void *resize(void *p, size_t count, size_t size) {
    if (!count || !size) { free(p); return NULL; }
    if (count > SIZE_MAX / size) fatal("allocation size overflow");
    void *q = p ? realloc(p, count * size) : malloc(count * size);
    if (!q) fatal("out of memory");
    return q;
}
char *copy_string(const char *s) {
    size_t n = strlen(s);
    if (n == SIZE_MAX) fatal("string too long");
    char *p = resize(NULL, n + 1, 1);
    if (!p) fatal("invalid string allocation size");
    memcpy(p, s, n + 1); return p;
}
void bytes(Buffer *b, const void *p, size_t n) {
    if (!b || b->len > b->cap || (b->cap && !b->data)) fatal("invalid buffer");
    if (!n) return;
    if (!p) fatal("NULL buffer input");
    if (n > SIZE_MAX - b->len) fatal("buffer size overflow");
    size_t needed = b->len + n;
    if (needed > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2) { cap = needed; break; }
            cap *= 2;
        }
        b->data = resize(b->data, cap, 1); b->cap = cap;
    }
    if (!b->data) fatal("invalid buffer allocation size");
    memcpy(b->data + b->len, p, n);
    b->len = needed;
}
void u8(Buffer *b, uint8_t x) { bytes(b, &x, 1); }
void u16(Buffer *b, uint16_t x) { u8(b, (uint8_t)x); u8(b, (uint8_t)(x >> 8)); }
void u32(Buffer *b, uint32_t x) { u16(b, (uint16_t)x); u16(b, (uint16_t)(x >> 16)); }
void patch32(Buffer *b, size_t pos, size_t x) {
    if (x > UINT32_MAX || pos > b->len || b->len - pos < 4)
        fatal("DWARF/ELF32 length overflow");
    for (unsigned i = 0; i < 4; ++i) b->data[pos + i] = (uint8_t)(x >> (8 * i));
}
void uleb(Buffer *b, uint32_t x) {
    do { uint8_t v = (uint8_t)(x & 127); x >>= 7; u8(b, v | (x ? 128 : 0)); } while (x);
}
void sleb(Buffer *b, int64_t x) {
    /* Division avoids implementation-defined right shifts of negative values. */
    for (;;) {
        uint8_t v = (uint8_t)((uint64_t)x & 127);
        int64_t next = x / 128;
        if (x < 0 && x % 128) --next;
        bool done = (next == 0 && !(v & 64)) || (next == -1 && (v & 64));
        u8(b, v | (done ? 0 : 128));
        if (done) break;
        x = next;
    }
}
void string(Buffer *b, const char *s) { bytes(b, s, strlen(s) + 1); }

char *absolute_output(const char *path) {
    char *s = copy_string(path), *slash = strrchr(s, '/');
    const char *base = slash ? slash + 1 : s;
    if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) fatal("invalid output path: %s", path);
    char *name = copy_string(base);
    if (slash) *slash = 0;
    char *dir = realpath(slash ? (*s ? s : "/") : ".", NULL);
    if (!dir) fatal("output directory %s: %s", path, strerror(errno));
    Buffer b = {0}; bytes(&b, dir, strlen(dir)); u8(&b, '/'); string(&b, name);
    free(s); free(name); free(dir); return (char *)b.data;
}
void write_output(const char *path, const void *data, size_t size) {
    Buffer tmp = {0}; bytes(&tmp, path, strlen(path)); string(&tmp, ".tmp.XXXXXX");
    int fd = mkstemp((char *)tmp.data);
    if (fd < 0) fatal("create %s: %s", path, strerror(errno));
    FILE *f = fdopen(fd, "wb");
    if (!f) { int e = errno; close(fd); unlink((char *)tmp.data); fatal("open output: %s", strerror(e)); }
    bool ok = !size || fwrite(data, 1, size, f) == size;
    if (fclose(f)) ok = false;
    if (!ok) { unlink((char *)tmp.data); fatal("write %s failed", path); }
    if (rename((char *)tmp.data, path)) {
        int e = errno; unlink((char *)tmp.data); fatal("rename %s: %s", path, strerror(e));
    }
    free(tmp.data);
}
