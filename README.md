# CHIP-8 assembler with DWARF v4

Small, dependency-free C17 assembler for classic CHIP-8. The supplied repository,
https://github.com/flakesonnix/chip8asm, was empty (no commits or tracked files),
so this implementation provides a standalone assembler, source-location tracking,
DWARF line encoder, and companion ELF writer.

## Build and run

Requires a C17 compiler, Make, and POSIX file APIs (tested on Linux).
Tests additionally require Python 3 and GNU readelf; llvm-dwarfdump is used when
available and its absence is explicitly reported. No Python packages are needed.

~~~sh
cd chip8asm
make
./chip8asm examples/program.asm -o program.ch8 --debug-info
readelf --sections program.ch8.debug.elf
readelf --debug-dump=rawline program.ch8.debug.elf
readelf --debug-dump=decodedline program.ch8.debug.elf
readelf --debug-dump=info program.ch8.debug.elf
make test
make sanitize                     # ASan + UBSan, rebuilds binaries
make clean && make test            # restore ordinary optimized build
~~~

Without --debug-info only the raw ROM is written. The default companion name is
<ROM-output>.debug.elf; use --debug-output program.debug.elf to override it (this
also enables debug output). Existing outputs are replaced atomically **per file**;
the two-file operation is not transactional. A later ROM-write failure can leave
the newly written companion. Invalid assembly is rejected before any outputs are
written. Outputs may not alias input files or each other, including existing hard
links and symlinks. This is not intended as a security boundary against concurrent
filesystem mutation. Temporary files are created with mode 0600.

## Assembly syntax

- Classic CHIP-8 mnemonics, case insensitive: SYS, CLS, RET, JP, CALL, SE, SNE,
  LD, ADD, OR, AND, XOR, SUB, SHR, SUBN, SHL, RND, DRW, SKP, SKNP.
- Registers V0–VF; special operands I, DT, ST, K, F, B, [I].
- Shift syntax accepts either SHR/SHL Vx or SHR/SHL Vx,Vy. Encoding the latter
  preserves y; interpreter-specific shift behavior is outside this assembler.
- Decimal and 0x-prefixed hexadecimal unsigned integers; no expressions.
- Case-sensitive labels: : main or main:, optionally followed by one instruction
  or directive. Names start with a letter/underscore, then letters/digits/underscore.
  Avoid register/special-operand names as labels because operand syntax takes priority.
- Semicolon and # start comments. Spaces, tabs, and commas separate operands.
  Blank lines and CRLF are accepted. No final newline is required.
- .byte values emits bytes; .word values emits **big-endian** 16-bit words.
  Up to 256 operands per directive; symbols are accepted if their values fit.
- .org address moves forward only, from 0x200 through 0x1000, inserting zero
  padding in the ROM. It accepts a numeric literal, not a symbol. Data can cause
  subsequent instructions to have odd guest addresses; the encoder preserves that
  layout rather than silently realigning it.
- Multiple source arguments are concatenated in command-line order, sharing one
  symbol table. Forward references across files work. There is no include directive.
- Only regular source files are supported. NUL bytes, lines over 65535 bytes,
  unsupported syntax, duplicate/undefined symbols, and operand/address overflows
  produce errors. Source line numbers are one-based uint32 values; overflow errors.

## Example: source to rows

examples/program.asm:

~~~asm
: main
    CLS
    LD V0, 10
    LD V1, 20
    ADD V0, V1
    JP main
~~~

| Guest address | ROM bytes | Source line | Instruction |
|---|---|---|---|
| 0x200 | 00 e0 | 2 | CLS |
| 0x202 | 60 0a | 3 | LD V0, 10 |
| 0x204 | 61 14 | 4 | LD V1, 20 |
| 0x206 | 80 14 | 5 | ADD V0, V1 |
| 0x208 | 12 00 | 6 | JP main |
| 0x20a | — | — | end_sequence |

The decoded line table has exactly those five instruction rows and the terminal
row. The label has no row. Addresses are **CHIP-8 guest addresses**, never host
pointers. In every successful integration fixture the tests compare raw ROM bytes
with and without debug enabled, and compare the ELF .text payload with the ROM.

## Files and responsibilities

- src/chip8.h — shared source locations, emitted ranges, program and buffer contracts.
- src/assemble.c — line parser, layout/symbol pass, instruction/data encoding pass.
- src/dwarf.c — standalone DWARF v4 line-table encoder; no ELF or file I/O.
- src/elf.c — ELF32 headers/segment/sections and minimal CU/abbreviation encoding.
- src/util.c — checked allocations, endian/LEB128 writers, path/output handling.
- src/main.c — CLI, output alias checks, assembly/debug/output orchestration.
- tests/unit.c — exact-byte line-table golden, LEB128 extremes, malformed range cases.
- tests/test.py — independent ELF/line decoder, ROM/row goldens, external validators,
  instruction coverage, errors, boundaries, deterministic malformed-input smoke tests.
- examples/program.asm — the five-instruction example above.
- Makefile, .gitignore — build/test/sanitizer targets and generated-file exclusions.
- README.md, docs/DWARF.md, docs/VALIDATION.md — usage, design/specification notes,
  and observed verification results.

## Scope and limitations

The companion is ELF32 little endian, ET_EXEC, EM_NONE, with one PT_LOAD segment
and .text at guest 0x200. CHIP-8 words in .text remain big endian. It contains real
.debug_line, .debug_info, and .debug_abbrev sections, not a proprietary source map.
The complete embedded ROM makes the companion relationship verifiable by direct
byte comparison; there is no cryptographic build ID or .gnu_debuglink.

This implements a DWARF **line-information subset**, not a full debugger backend:
no variable/type DIEs, register mapping, call frames, functions, relocations, macros,
Super-CHIP/XO-CHIP, or host execution. GDB cannot execute/debug CHIP-8 natively just
because these sections parse. A CHIP-8 debugger must load the companion, verify its
.text against the ROM, and query line sequences using its guest PC.

Canonical absolute source paths use an empty directory table and directory index
zero. This is valid DWARF, but embeds local paths and makes artifacts location
dependent; no source-prefix remapping or reproducibility across different roots.
See [DWARF internals](docs/DWARF.md) and [verification](docs/VALIDATION.md).
