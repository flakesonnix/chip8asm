# chip8asm

A small C17 assembler for classic CHIP-8, with optional DWARF v4 debug information.
Produces raw ROMs for CHIP-8 emulators and companion ELF files that map guest
addresses to source files, lines, and columns. No external runtime libraries are
required.

## Features

- Classic CHIP-8 instruction set.
- Two-pass assembly with labels and forward references across source files.
- Byte and word data directives, plus explicit address placement.
- DWARF source mappings without changing the generated ROM.
- Separate parser, line-table encoder, and ELF writer.

## Build

Requirements: a C17 compiler, Make, and POSIX file APIs. Linux is supported;
other platforms have not been tested.

~~~sh
git clone https://github.com/flakesonnix/chip8asm.git
cd chip8asm
make
~~~

## Usage

~~~sh
# Assemble a raw ROM
./chip8asm examples/program.asm -o program.ch8

# Also write program.ch8.debug.elf
./chip8asm examples/program.asm -o program.ch8 --debug-info

# Choose a companion filename
./chip8asm examples/program.asm -o program.ch8 --debug-output program.debug.elf

# Assemble several files in order, sharing one symbol table
./chip8asm main.asm sprites.asm -o program.ch8
~~~

An output path is required. Use --help for the command-line synopsis and -- before
source filenames that begin with a hyphen. Existing outputs are replaced; input
files and output files must not alias each other.

## Assembly syntax

~~~asm
: main
    CLS
    LD V0, 10
    LD V1, 20
    ADD V0, V1
    JP main
~~~

Mnemonics and registers are case insensitive; labels are case sensitive.

| Syntax | Description |
|---|---|
| V0–VF | General-purpose registers |
| I, DT, ST, K, F, B, [I] | Special operands |
| 42, 0x2a | Unsigned decimal or hexadecimal literal |
| : name or name: | Label, optionally followed by an instruction or directive |
| ; comment or # comment | Comment to the end of the line |
| .byte 0xf0, 0x90 | Emit bytes |
| .word 0x1234 | Emit a big-endian 16-bit word |
| .org 0x300 | Move forward to a guest address, filling the gap with zeros |

Spaces, tabs, and commas separate operands. Blank lines, CRLF, and files without a
final newline are accepted. Label names start with a letter or underscore, followed
by letters, digits, or underscores. Avoid register and special-operand names as
labels, since operand syntax takes precedence.

Programs start at 0x200 and may occupy memory through 0xfff. The .org directive
accepts numeric addresses through 0x1000 and cannot move backward. Each data
directive accepts up to 256 values; symbols are allowed when their values fit.
Instructions are not automatically aligned after data.

SHR and SHL accept either Vx or Vx,Vy; the latter preserves the y field in the
opcode. Runtime shift behavior depends on the interpreter.

Only regular source files are supported. Source lines are limited to 65535 bytes.
Expressions, include directives, Super-CHIP, and XO-CHIP are not supported.

## Debug information

The companion is an ELF32 little-endian file containing:

- .text: the exact ROM bytes, at guest address 0x200.
- .debug_line: DWARF v4 source locations and address ranges.
- .debug_info and .debug_abbrev: a compilation unit referencing the line table.

CHIP-8 opcodes remain big endian inside .text. Labels, comments, and padding have
no instruction rows; data directives have non-statement rows. Each line sequence
ends at the actual emitted range boundary, so padding is not attributed to an
instruction.

~~~sh
readelf --sections program.ch8.debug.elf
readelf --debug-dump=decodedline program.ch8.debug.elf
~~~

The example above maps lines 2–6 to addresses 0x200, 0x202, 0x204, 0x206, and
0x208, followed by an end_sequence row at 0x20a.

Debug information includes absolute source paths and is limited to line mappings:
there are no variable/type descriptions, register mappings, or call frames.
EM_NONE identifies the container as architecture-neutral; native GDB execution
of CHIP-8 is not supported. A CHIP-8 debugger must load the companion and match
its .text bytes against the ROM. No build ID or .gnu_debuglink is emitted.

Output replacement is atomic per file, not across the ROM/companion pair. A write
failure can therefore leave a new companion next to an older ROM. Invalid assembly
is rejected before outputs are written.

## Tests

Python 3 and GNU readelf are required for the full test suite. LLVM's
llvm-dwarfdump is used when available.

~~~sh
make test
make sanitize          # Rebuild and run with AddressSanitizer and UBSan
make clean && make     # Restore the regular build
~~~

See [Testing](docs/TESTING.md) for coverage and manual validation commands.

## Project layout

| Path | Responsibility |
|---|---|
| src/assemble.c | Parsing, layout, symbols, and instruction/data encoding |
| src/dwarf.c | DWARF v4 line-table encoding |
| src/elf.c | ELF container and compilation-unit encoding |
| src/main.c | Command-line interface and output orchestration |
| src/util.c | Checked buffers, LEB128, paths, and file output |
| src/chip8.h | Shared data structures and interfaces |
| tests/ | Encoder unit tests and integration tests |
| examples/ | Example assembly programs |

See [DWARF format and architecture](docs/DWARF.md) for implementation details.
