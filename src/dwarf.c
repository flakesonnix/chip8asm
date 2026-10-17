#include "chip8.h"
#include <stdlib.h>

/* DWARF v4, sections 6.2 and 7.21. All fixed-width fields are little endian. */
enum {
    LNS_COPY = 1, LNS_ADVANCE_PC = 2, LNS_ADVANCE_LINE = 3,
    LNS_SET_FILE = 4, LNS_SET_COLUMN = 5, LNS_NEGATE_STMT = 6,
    LNE_END_SEQUENCE = 1, LNE_SET_ADDRESS = 2
};
static void set_address(Buffer *b, uint32_t address) {
    u8(b, 0); uleb(b, 5); u8(b, LNE_SET_ADDRESS); u32(b, address);
}
static void advance(Buffer *b, uint32_t delta) {
    if (delta) { u8(b, LNS_ADVANCE_PC); uleb(b, delta); }
}
static void end_sequence(Buffer *b) {
    u8(b, 0); uleb(b, 1); u8(b, LNE_END_SEQUENCE);
}
Buffer dwarf_line(const Program *p) {
    if (!p || !p->files || (p->range_count && !p->ranges) ||
        p->range_count > MEMORY_END - ORIGIN ||
        !p->file_count || p->file_count > UINT32_MAX || p->rom_size > MEMORY_END - ORIGIN)
        fatal("invalid line-table input");
    for (size_t i = 0; i < p->file_count; ++i)
        if (!p->files[i] || !*p->files[i]) fatal("empty source path");
    Buffer b = {0};
    u32(&b, 0); u16(&b, 4);
    size_t header_length = b.len; u32(&b, 0);
    size_t header_start = b.len;
    u8(&b, 1); /* minimum instruction length: byte granular for data/odd PCs */
    u8(&b, 1); /* maximum operations per instruction: non-VLIW */
    u8(&b, 1); /* default is_stmt */
    u8(&b, (uint8_t)-5); u8(&b, 14); u8(&b, 13);
    const uint8_t operands[] = {0,1,1,1,1,0,0,0,1,0,0,1};
    bytes(&b, operands, sizeof(operands));
    u8(&b, 0); /* empty include directory list; canonical absolute file paths */
    for (size_t i = 0; i < p->file_count; ++i) {
        string(&b, p->files[i]); uleb(&b, 0); uleb(&b, 0); uleb(&b, 0);
    }
    u8(&b, 0);
    patch32(&b, header_length, b.len - header_start);
    uint32_t address = 0, line = 1, file = 1, column = 0, range_end = 0;
    bool active = false, is_stmt = true;
    for (size_t i = 0; i < p->range_count; ++i) {
        const SourceRange *r = &p->ranges[i];
        if (!r->size || r->address < ORIGIN || r->address > MEMORY_END ||
            r->size > MEMORY_END - r->address ||
            r->address + r->size > ORIGIN + p->rom_size ||
            !r->loc.line || !r->loc.file || r->loc.file > p->file_count ||
            (i && r->address < range_end)) fatal("invalid source range for line table");
        if (active && r->address != range_end) {
            advance(&b, range_end - address); end_sequence(&b); active = false;
        }
        if (!active) {
            set_address(&b, r->address); address = r->address;
            line = 1; file = 1; column = 0; is_stmt = true; active = true;
        }
        advance(&b, r->address - address); address = r->address;
        if (r->loc.file != file) { u8(&b, LNS_SET_FILE); uleb(&b, r->loc.file); file = r->loc.file; }
        if (r->loc.line != line) {
            u8(&b, LNS_ADVANCE_LINE); sleb(&b, (int64_t)r->loc.line - line); line = r->loc.line;
        }
        if (r->loc.column != column) { u8(&b, LNS_SET_COLUMN); uleb(&b, r->loc.column); column = r->loc.column; }
        if (r->instruction != is_stmt) { u8(&b, LNS_NEGATE_STMT); is_stmt = r->instruction; }
        u8(&b, LNS_COPY);
        range_end = r->address + r->size;
    }
    if (active) { advance(&b, range_end - address); end_sequence(&b); }
    else if (!p->range_count) { set_address(&b, ORIGIN); end_sequence(&b); }
    patch32(&b, 0, b.len - 4);
    return b;
}
