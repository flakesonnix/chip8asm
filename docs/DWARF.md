# DWARF format and architecture

## Architecture

The assembler and debug writer form a three-stage pipeline:

1. The two-pass assembler computes guest layout, resolves symbols, and emits a
   ROM buffer together with source locations for every emitted range.
2. The DWARF encoder converts those ranges into a version 4 line table, independent
   of instruction parsing and file I/O.
3. The ELF writer packages the line table, a compilation unit, and the exact ROM
   bytes into a fixed-address debug companion.

Debug generation reads the assembled program without changing its ROM bytes.

## From source to an emitted range

Program starts at 0x200. The first pass parses labels and .org while recording
nodes with SourceLoc {file, line, column} and their final guest {address,size}.
Labels never become emission nodes. Blank/comment lines disappear only after their
line counter has advanced. .org changes layout and inserts ROM zeros but has no
source range. Each actual instruction has a two-byte range; a .byte/.word node
has its actual aggregate emitted byte count and is marked as data.

The second pass resolves labels and writes CHIP-8 words high-byte first. Locations
are copied unchanged from nodes to Program.ranges after successful encoding.
Files are canonical absolute paths indexed from one; each file's line counter
starts at one, so a file switch can require a negative line advance. Ranges are
ordered and non-overlapping. Program is initialized to zero before assembly so
.org padding is zero-filled.

The line encoder accepts Program's source/range metadata and ROM length, not
instruction text or host addresses. It validates positive line/file indices,
range sizes, ascending non-overlapping addresses, guest/ROM bounds, and basic
array/path invariants. Its Buffer result is independent of ELF integration.

## Line header: DWARF32 version 4

The encoding follows the [DWARF v4 specification](https://dwarfstd.org/doc/DWARF4.pdf),
particularly §6.2 (line machine/header/opcodes), §7.5 (CU/abbreviations), §7.6 (LEB128),
and §7.21 (line opcode encodings, Figures 37 and 38).

All DWARF fixed-width integers here are little endian. A unit begins with:

1. Four-byte unit_length, excluding that field itself.
2. Two-byte version = 4.
3. Four-byte header_length, from just after that field to the first opcode.
4. minimum_instruction_length = 1, maximum_operations_per_instruction = 1.
   Byte-granular address units permit data ranges and odd PCs; instruction nodes
   still emit exactly two CHIP-8 bytes. No VLIW operation-index behavior is needed.
5. default_is_stmt = 1, signed line_base = -5, line_range = 14, opcode_base = 13.
6. Twelve standard operand counts: 0,1,1,1,1,0,0,0,1,0,0,1.
7. Empty, NUL-terminated include-directory list.
8. File entries: NUL-terminated canonical absolute path, ULEB128 directory index
   zero, modification time zero, and size zero (unknown); final empty entry.

The specification permits full paths and ignores the directory index for them.
DW_AT_name matches the primary file entry exactly, whose directory index is zero.
No DW_AT_comp_dir is needed with absolute paths. Neither filesystem timestamps
nor host pointer sizes enter the encoding. Lengths are calculated from actual
buffer positions and patched after contents have been written.

## The state machine

A sequence starts with DW_LNE_set_address, encoded as zero, ULEB128 payload length
5, extended opcode 2, then a four-byte guest address. Four-byte addresses agree
with the CU address_size and ELF32; the CHIP-8 used range remains 0x200–0xfff.
The one-past-end terminal address may be 0x1000.

For each source range:

- DW_LNS_advance_pc (2 + ULEB128) moves from the previous emitted row address to
  this range's address. Its unit is one byte with this header.
- DW_LNS_set_file (4 + ULEB128) changes the one-based file index if necessary.
- DW_LNS_advance_line (3 + SLEB128) changes line by a signed delta from the
  machine's initial line = 1 or the preceding row's line.
- DW_LNS_set_column (5 + ULEB128) retains the actual source column.
- DW_LNS_negate_stmt (6) toggles between instruction/breakpoint rows (true) and
  data directive rows (false). Data is mapped, but never advertised as an
  instruction row. is_stmt is a breakpoint recommendation, not a complete
  machine-code/data classification facility for arbitrary consumers.
- DW_LNS_copy (1) appends one row. Its source applies to the half-open address
  interval up to the following row or sequence end, covering all bytes in the
  emitted range rather than assuming one instruction per source line.

At a gap, advance to the previous range's actual end and emit DW_LNE_end_sequence
(zero, ULEB128 length 1, extended opcode 1). This appends a terminal row and resets
**all** line-machine state. Start a new sequence at the next real range. Padding
has no source mapping; a lookup must not carry a row across sequence boundaries.
Trailing padding is also outside the final sequence. File switches alone do not
need a new sequence. Empty programs get a zero-length sequence at 0x200 containing
only a terminal row: no fabricated instruction.

The encoder intentionally uses explicit standard opcodes rather than compressed
special opcodes. This is valid, easier to audit, and sufficient for 3584-byte ROMs.
Unsigned/signed LEB writers handle variable-length values; signed division with
floor correction avoids implementation-defined right shifts of negative integers.

## ELF and associated DIE

The [System V ELF ABI](https://gabi.xinuos.com/elf/) defines the ELF32 container.
All fields are emitted explicitly rather than serializing native C structs.
ET_EXEC denotes fixed guest addresses, EM_NONE avoids pretending CHIP-8 is x86 or
another registered architecture. e_entry and the single PT_LOAD guest address are
0x200; its payload is precisely .text, which is precisely the raw ROM including
padding. Segment alignment is one; section offsets and lengths are computed.

The single DW_TAG_compile_unit has no children and these attributes:

| Attribute | Form | Value |
|---|---|---|
| DW_AT_name | DW_FORM_string | primary absolute source path |
| DW_AT_producer | DW_FORM_string | assembler identifier |
| DW_AT_stmt_list | DW_FORM_sec_offset | 0 into .debug_line |
| DW_AT_low_pc | DW_FORM_addr | 0x200 |
| DW_AT_high_pc | DW_FORM_data4 | ROM size, an offset from low_pc |

The CU is DWARF v4/DWARF32, address_size = 4, abbreviation offset zero. The
abbreviation declaration and table both have their required terminators. The CU's
coverage includes padding, but the line sequences deliberately do not. No separate
.debug_str section or symbol table is needed for these inline strings/line rows.
This is a fixed-address artifact; no relocations or host linker are involved.

## Resource and output policy

Checked buffer growth/allocation multiplication and ELF32/DWARF32 size patching
reject overflow. A fatal error terminates this command-line process; successful
paths free their allocations, while fatal paths rely on process teardown. The
sanitizer tests leave leak detection enabled for successful invocations and disable
only leak reporting for expected fatal exits and malformed-input smoke cases;
address/undefined-behavior checks remain active.

POSIX realpath, stat, mkstemp, fdopen, unlink, and rename support path checks and
per-file atomic replacement. Source/ROM/companion alias checks run before writes.
Short writes and close/rename failures are reported; temp files are removed on
normal reported write failures. Process crashes or power loss are not transactional
and there is no fsync durability promise. The ROM/companion pair is not an atomic
transaction. The implementation is a small CLI, not a hostile concurrent-filesystem
service or a full source-language compiler.
