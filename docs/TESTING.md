# Testing

## Requirements

- C17 compiler and Make.
- Python 3, using only the standard library.
- GNU readelf for ELF and DWARF validation.
- Optional: llvm-dwarfdump for an additional DWARF verifier.

The test runner reports missing validators explicitly. Without readelf, tests that
require external validation are marked as skipped; install it to run the full
suite. If llvm-dwarfdump is available, the suite also runs its --verify command.

## Run the suite

From the project root:

~~~sh
make test
~~~

This builds and runs the C encoder unit tests and the Python integration tests.
Fixtures and generated artifacts use temporary directories.

To check for memory errors and undefined behavior:

~~~sh
make sanitize
~~~

The sanitizer target rebuilds with AddressSanitizer and UBSan. Successful
invocations retain leak detection. Expected fatal-error cases and malformed-input
smoke tests disable leak reporting because the CLI relies on process teardown
on fatal exits; address and undefined-behavior checks remain active.

Restore the regular optimized build afterward:

~~~sh
make clean && make test
~~~

## Coverage

### Encoder unit tests

The C tests in tests/unit.c check:

- A complete DWARF v4 line table against literal expected bytes.
- Unsigned and signed LEB128 boundary values, including integer extremes.
- Rejection of invalid file indices, line numbers, address ranges, overlaps,
  and missing range data.

### Assembly and integration tests

The Python suite in tests/test.py checks:

- Classic instruction encodings, labels, forward references, and multiple files.
- Comments, blank lines, empty inputs, and files without a final newline.
- Data directives, odd addresses, .org gaps, and trailing padding.
- Guest memory boundaries and large source line numbers.
- Invalid operands, unsupported syntax, path errors, and nonregular sources.
- Source/output alias protection, write failures, and temporary-file cleanup.
- Deterministic output and a fixed-seed malformed-input smoke test.

Each successful assembly fixture compares ROM bytes with and without debug output,
then verifies that the ELF .text section contains the same bytes. An independent
ELF parser checks section and segment layout, compilation-unit linkage, and
abbreviations. An independent line-machine decoder checks file, line, column,
address, statement flags, and sequence termination.

The suite invokes readelf separately for section headers, program headers, raw
line tables, decoded line tables, compilation units, and abbreviations. Validator
warnings on stderr fail the corresponding check.

## Manual ELF/DWARF inspection

~~~sh
./chip8asm examples/program.asm -o program.ch8 --debug-info
readelf --sections program.ch8.debug.elf
readelf --program-headers program.ch8.debug.elf
readelf --debug-dump=rawline program.ch8.debug.elf
readelf --debug-dump=decodedline program.ch8.debug.elf
readelf --debug-dump=info program.ch8.debug.elf
readelf --debug-dump=abbrev program.ch8.debug.elf
~~~

When LLVM tools are installed:

~~~sh
llvm-dwarfdump --verify program.ch8.debug.elf
~~~

Expected decoded rows for examples/program.asm:

| Source line | Guest address | Row kind |
|---|---|---|
| 2 | 0x200 | Statement |
| 3 | 0x202 | Statement |
| 4 | 0x204 | Statement |
| 5 | 0x206 | Statement |
| 6 | 0x208 | Statement |
| — | 0x20a | End of sequence |

The raw line dump should start with set_address 0x200 and end with end_sequence
at 0x20a. ELF .text and the PT_LOAD segment should use guest address 0x200.

## Optional static analysis

GCC supports additional checks with -fanalyzer. Compile each src/*.c file with
-std=c17 -Wall -Wextra -Wpedantic -Werror -fanalyzer, writing object files outside
the source tree. The regular build already enables strict warnings.

## Scope

These checks validate assembly output, source mappings, and binary structure.
They do not test emulator execution or native GDB support, and the malformed-input
smoke test is not an exhaustive fuzzing campaign. Allocation-failure injection,
power-loss durability, concurrent filesystem mutation, and platforms other than
Linux are outside the current test coverage.

See [DWARF format and architecture](DWARF.md) for encoding and output guarantees.
