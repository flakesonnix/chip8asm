#ifndef CHIP8_H
#define CHIP8_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ORIGIN 0x200u
#define MEMORY_END 0x1000u

typedef struct { uint8_t *data; size_t len, cap; } Buffer;
_Noreturn void fatal(const char *fmt, ...);
void *resize(void *p, size_t count, size_t size);
char *copy_string(const char *s);
void bytes(Buffer *b, const void *p, size_t n);
void u8(Buffer *b, uint8_t x);
void u16(Buffer *b, uint16_t x);
void u32(Buffer *b, uint32_t x);
void patch32(Buffer *b, size_t pos, size_t x);
void uleb(Buffer *b, uint32_t x);
void sleb(Buffer *b, int64_t x);
void string(Buffer *b, const char *s);
char *absolute_output(const char *path);
void write_output(const char *path, const void *data, size_t size);

typedef struct { uint32_t file, line, column; } SourceLoc;
typedef struct {
    uint32_t address, size;
    SourceLoc loc;
    bool instruction;
} SourceRange;
typedef struct {
    uint8_t rom[MEMORY_END - ORIGIN];
    size_t rom_size;
    char **files;
    size_t file_count;
    SourceRange *ranges;
    size_t range_count;
} Program;
void assemble(Program *p, char **paths, size_t count);
void free_program(Program *p);
Buffer dwarf_line(const Program *p);
Buffer debug_elf(const Program *p);
#endif
