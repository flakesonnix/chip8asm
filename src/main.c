#include "chip8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool same_file(const char *a, const char *b) {
    struct stat sa, sb;
    return !strcmp(a, b) || (!stat(a, &sa) && !stat(b, &sb) && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino);
}
static void usage(void) {
    puts("Usage: chip8asm source.asm [more.asm ...] -o program.ch8 [--debug-info] [--debug-output file.elf]");
}
int main(int argc, char **argv) {
    char **inputs = resize(NULL, (size_t)argc, sizeof(*inputs)); size_t count = 0;
    const char *output = NULL, *debug_output = NULL; bool debug = false, positional = false;
    for (int i = 1; i < argc; ++i) {
        if (!positional && !strcmp(argv[i], "--")) { positional = true; continue; }
        if (!positional && !strcmp(argv[i], "--help")) { usage(); free(inputs); return 0; }
        if (!positional && !strcmp(argv[i], "--debug-info")) { debug = true; continue; }
        if (!positional && (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--debug-output"))) {
            bool rom = !strcmp(argv[i], "-o");
            if (++i == argc) fatal("missing output path");
            if (rom) { if (output) fatal("duplicate -o"); output = argv[i]; }
            else { if (debug_output) fatal("duplicate --debug-output"); debug_output = argv[i]; debug = true; }
            continue;
        }
        if (!positional && argv[i][0] == '-') fatal("unknown option: %s", argv[i]);
        inputs[count++] = argv[i];
    }
    if (!count || !output) { usage(); free(inputs); return EXIT_FAILURE; }
    Program p = {0}; assemble(&p, inputs, count);
    char *rom_path = absolute_output(output), *elf_path = NULL;
    if (debug) {
        if (debug_output) elf_path = absolute_output(debug_output);
        else { Buffer b = {0}; bytes(&b, rom_path, strlen(rom_path)); string(&b, ".debug.elf"); elf_path = (char *)b.data; }
        if (same_file(rom_path, elf_path)) fatal("ROM and debug outputs must differ");
    }
    for (size_t i = 0; i < p.file_count; ++i)
        if (same_file(rom_path, p.files[i]) || (elf_path && same_file(elf_path, p.files[i])))
            fatal("output aliases a source file");
    Buffer elf = {0}; if (debug) elf = debug_elf(&p);
    /* Generate everything before writing. Each file is atomically replaced, not the pair. */
    if (debug) write_output(elf_path, elf.data, elf.len);
    write_output(rom_path, p.rom, p.rom_size);
    free(elf.data); free(rom_path); free(elf_path); free(inputs); free_program(&p);
    return 0;
}
