#include "chip8.h"
#include <stdlib.h>

/* ELF32 constants (System V ABI); no host structs, packing, or byte order. */
enum { ELF_HEADER_SIZE = 52, PROGRAM_HEADER_SIZE = 32, SECTION_HEADER_SIZE = 40, SECTION_COUNT = 6 };
typedef struct { uint32_t name, type, flags, address, offset, size, align; } Section;
static uint32_t checked(size_t n) {
    if (n > UINT32_MAX) fatal("ELF32 offset overflow");
    return (uint32_t)n;
}
static Buffer abbreviation(void) {
    Buffer b = {0};
    uleb(&b, 1); uleb(&b, 0x11); u8(&b, 0); /* compile_unit, no children */
    const uint8_t attrs[][2] = {
        {0x03,0x08}, /* name: string */
        {0x25,0x08}, /* producer: string */
        {0x10,0x17}, /* stmt_list: sec_offset */
        {0x11,0x01}, /* low_pc: addr */
        {0x12,0x06}  /* high_pc: data4 (size, not absolute address) */
    };
    for (size_t i = 0; i < sizeof(attrs)/sizeof(*attrs); ++i) {
        uleb(&b, attrs[i][0]); uleb(&b, attrs[i][1]);
    }
    uleb(&b, 0); uleb(&b, 0); uleb(&b, 0); return b;
}
static Buffer information(const Program *p) {
    Buffer b = {0};
    u32(&b, 0); u16(&b, 4); u32(&b, 0); u8(&b, 4);
    uleb(&b, 1); string(&b, p->files[0]); string(&b, "chip8asm C17 DWARF4");
    u32(&b, 0); u32(&b, ORIGIN); u32(&b, checked(p->rom_size));
    patch32(&b, 0, b.len - 4); return b;
}
Buffer debug_elf(const Program *p) {
    Buffer line = dwarf_line(p), abbrev = abbreviation(), info = information(p), names = {0}, out = {0};
    const char *section_names[] = {"", ".text", ".debug_line", ".debug_info", ".debug_abbrev", ".shstrtab"};
    Section s[SECTION_COUNT] = {{0}};
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        s[i].name = checked(names.len); string(&names, section_names[i]);
        if (i) { s[i].type = i == 5 ? 3 : 1; s[i].align = 1; }
    }
    s[1].flags = 6; s[1].address = ORIGIN;
    /* Reserve the fixed ABI header and a PT_LOAD descriptor. */
    for (size_t i = 0; i < ELF_HEADER_SIZE + PROGRAM_HEADER_SIZE; ++i) u8(&out, 0);
    const void *data[] = {NULL, p->rom, line.data, info.data, abbrev.data, names.data};
    size_t sizes[] = {0, p->rom_size, line.len, info.len, abbrev.len, names.len};
    for (size_t i = 1; i < SECTION_COUNT; ++i) {
        s[i].offset = checked(out.len); s[i].size = checked(sizes[i]); bytes(&out, data[i], sizes[i]);
    }
    while (out.len % 4) u8(&out, 0);
    uint32_t shoff = checked(out.len);
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        u32(&out, s[i].name); u32(&out, s[i].type); u32(&out, s[i].flags); u32(&out, s[i].address);
        u32(&out, s[i].offset); u32(&out, s[i].size); u32(&out, 0); u32(&out, 0);
        u32(&out, s[i].align); u32(&out, 0);
    }
    Buffer h = {0};
    const uint8_t ident[16] = {0x7f,'E','L','F',1,1,1,0}; bytes(&h, ident, sizeof(ident));
    u16(&h, 2); /* ET_EXEC: fixed guest addresses, not a host executable */
    u16(&h, 0); /* EM_NONE: no standard CHIP-8 ELF machine number */
    u32(&h, 1); u32(&h, ORIGIN); u32(&h, ELF_HEADER_SIZE); u32(&h, shoff); u32(&h, 0);
    u16(&h, ELF_HEADER_SIZE); u16(&h, PROGRAM_HEADER_SIZE); u16(&h, 1);
    u16(&h, SECTION_HEADER_SIZE); u16(&h, SECTION_COUNT); u16(&h, 5);
    u32(&h, 1); u32(&h, s[1].offset); u32(&h, ORIGIN); u32(&h, ORIGIN);
    u32(&h, s[1].size); u32(&h, s[1].size); u32(&h, 5); u32(&h, 1);
    if (h.len != ELF_HEADER_SIZE + PROGRAM_HEADER_SIZE) fatal("internal ELF header size mismatch");
    for (size_t i = 0; i < h.len; ++i) out.data[i] = h.data[i];
    free(h.data); free(line.data); free(abbrev.data); free(info.data); free(names.data);
    return out;
}
