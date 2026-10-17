#include "chip8.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(Buffer *b, const uint8_t *v, size_t n) {
    assert(b->len == n); assert(!memcmp(b->data, v, n));
    free(b->data); *b = (Buffer){0};
}
int main(int argc, char **argv) {
    char *files[] = {"test.asm"};
    SourceRange r = {ORIGIN, 2, {1,1,1}, true};
    Program p = {.rom_size = 2, .files = files, .file_count = 1, .ranges = &r, .range_count = 1};
    if (argc == 2) {
        if (!strcmp(argv[1], "line-zero")) r.loc.line = 0;
        else if (!strcmp(argv[1], "file-zero")) r.loc.file = 0;
        else if (!strcmp(argv[1], "file-high")) r.loc.file = 2;
        else if (!strcmp(argv[1], "address-low")) r.address = 0x1ff;
        else if (!strcmp(argv[1], "address-high")) r.address = UINT32_MAX;
        else if (!strcmp(argv[1], "range-overflow")) r.size = UINT32_MAX;
        else if (!strcmp(argv[1], "empty-range")) r.size = 0;
        else if (!strcmp(argv[1], "rom-bound")) p.rom_size = 1;
        else if (!strcmp(argv[1], "empty-path")) files[0] = "";
        else if (!strcmp(argv[1], "overlap")) {
            static SourceRange overlap[2]; overlap[0] = r; overlap[1] = r;
            p.ranges = overlap; p.range_count = 2;
        }
        else if (!strcmp(argv[1], "null-ranges")) p.ranges = NULL;
        else if (!strcmp(argv[1], "max-line")) {
            r.loc.line = UINT32_MAX;
            Buffer b = dwarf_line(&p);
            assert(fwrite(b.data, 1, b.len, stdout) == b.len); free(b.data); return 0;
        } else return 2;
        Buffer b = dwarf_line(&p); free(b.data); return 3; /* must not succeed */
    }
    Buffer b = {0}; uleb(&b, UINT32_MAX);
    const uint8_t unsigned_max[] = {255,255,255,255,15}; expect(&b, unsigned_max, sizeof(unsigned_max));
    sleb(&b, -65); sleb(&b, -64); sleb(&b, 63); sleb(&b, 64);
    const uint8_t signed_edges[] = {0xbf,0x7f,0x40,0x3f,0xc0,0}; expect(&b, signed_edges, sizeof(signed_edges));
    sleb(&b, INT64_MIN); sleb(&b, INT64_MAX);
    const uint8_t signed_extremes[] = {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x7f,
                                      0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0};
    expect(&b, signed_extremes, sizeof(signed_extremes));
    b = dwarf_line(&p);
    const uint8_t golden[] = {
        0x35,0,0,0, 4,0, 0x20,0,0,0,
        1,1,1,0xfb,14,13, 0,1,1,1,1,0,0,0,1,0,0,1,
        0, 't','e','s','t','.','a','s','m',0, 0,0,0, 0,
        0,5,2,0,2,0,0, 5,1, 1, 2,2, 0,1,1
    };
    expect(&b, golden, sizeof(golden));
    puts("unit: LEB128 boundaries and exact DWARF v4 bytes passed");
    return 0;
}
