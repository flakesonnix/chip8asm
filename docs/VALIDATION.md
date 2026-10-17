# Verification report — 2026-10-17

Repository inspected: https://github.com/flakesonnix/chip8asm. It had no commits,
tracked sources, build system, or existing tests. HTTPS cloning succeeded; SSH
access initially failed host-key verification. No pre-existing assembler behavior
was available to regression-test.

Environment: Linux x86-64, GCC 14.2.0, GNU Make, GNU readelf 2.44, Python 3.13.5.

## Executed checks

~~~sh
make clean && make test
make sanitize
make clean && make test
./chip8asm examples/program.asm -o /tmp/chip8-final.ch8 --debug-info
readelf --sections /tmp/chip8-final.ch8.debug.elf
readelf --program-headers /tmp/chip8-final.ch8.debug.elf
readelf --debug-dump=rawline /tmp/chip8-final.ch8.debug.elf
readelf --debug-dump=decodedline /tmp/chip8-final.ch8.debug.elf
readelf --debug-dump=info /tmp/chip8-final.ch8.debug.elf
readelf --debug-dump=abbrev /tmp/chip8-final.ch8.debug.elf
~~~

The exact-byte C unit tests and all **16 Python test groups passed**, both in the
optimized build and under AddressSanitizer + UndefinedBehaviorSanitizer. The
successful test invocations include leak checks; expected fatal-error invocations
turn off leak reporting because the CLI intentionally relies on process teardown
there. Address/undefined-behavior checks remain enabled for those invocations.

The integration suite runs all six readelf modes independently for every successful
assembly fixture and rejects warnings on stderr. An independent Python ELF parser
checks ELF/segment/section layout, CU linkage, abbreviation bytes, and embedded ROM.
An independent line-machine decoder verifies source files, addresses, line/column
numbers, statement flags, terminal rows, and sequence boundaries.

Golden checks include a complete fixed-path .debug_line unit as literal expected
bytes in tests/unit.c, exact ROM instruction bytes, and exact decoded rows for the
five-instruction example. Every successful assembly fixture compares ROM output
with and without debug information and verifies ELF .text equals the ROM.

Coverage includes all classic instruction encodings implemented, comments/blanks,
labels and forward jumps, multiple files with the same basename and negative line
deltas, .org gaps/trailing padding, odd-address data/instructions, .byte/.word,
empty/label-only files, 0xfff/0x1000 boundaries, source line 70001, synthetic
UINT32_MAX line numbers, signed/unsigned LEB128 extremes, malformed internal ranges,
invalid input syntax/paths, source/output aliasing, short-write failure with
RLIMIT_FSIZE, temporary-file cleanup, and rejection of nonregular input (FIFO).
A fixed-seed set of 100 malformed byte streams was also exercised. This is smoke
coverage, not an exhaustive fuzzing campaign or proof of memory safety.

GCC's -fanalyzer was also run on all five C source files with warnings treated as
errors. Its initial allocator-path diagnostics prompted explicit zero-allocation
handling and buffer/allocation postcondition checks; the final run completed
without diagnostics. Optimized builds additionally use -Wall -Wextra -Wpedantic
-Wconversion -Wshadow -Werror.

## Observed example rows

~~~text
source          line    guest PC   statement
program.asm        2       0x200   yes
program.asm        3       0x202   yes
program.asm        4       0x204   yes
program.asm        5       0x206   yes
program.asm        6       0x208   yes
program.asm        -       0x20a   end_sequence
~~~

Readelf's raw dump shows set_address 0x200, advance_line/copy operations,
two-byte advance_pc operations, and an explicit end_sequence at 0x20a.
The .text address and PT_LOAD virtual/physical addresses are 0x200; EM_NONE and
ET_EXEC are reported as intended.

## Not verified / limitations

- llvm-dwarfdump is **not installed**, so LLVM validation could not be performed.
  The test runner reports this explicitly and will invoke --verify if available.
- No actual CHIP-8 debugger/GDB integration or emulator execution was tested.
  Parsing valid ELF/DWARF is not equivalent to native CHIP-8 debugger support.
- Allocation-failure injection, power-loss durability, malicious filesystem races,
  other operating systems, and exhaustive fuzzing were not tested.
- See README.md and DWARF.md for the deliberately limited assembler/DWARF scope.
