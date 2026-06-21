# RISC-V RV32I Instruction Decoder

A command-line tool that reads a hex file containing RISC-V RV32I machine
code, decodes every instruction, and prints human-readable assembly. This
is the front-end of a CPU simulator and was built as the Module 2 Grand
Assignment for MEDS Lab — *C Language for Hardware Engineers*.

## Project Description

`riscv-decoder` takes a plain-text file of 32-bit hex instruction words
(one per line) and disassembles it into RISC-V assembly syntax, the way a
disassembler in a real toolchain (e.g. `objdump`) would. It supports the
full RV32I base instruction set:

| Format | Instructions |
|---|---|
| R-type | `add sub and or xor sll srl sra slt sltu` |
| I-type (arithmetic) | `addi andi ori xori slti sltiu slli srli srai` |
| I-type (load) | `lb lh lw lbu lhu` |
| S-type (store) | `sb sh sw` |
| B-type (branch) | `beq bne blt bge bltu bgeu` |
| U-type | `lui auipc` |
| J-type | `jal` |
| I-type (jump) | `jalr` |

Any instruction word with an opcode outside the RV32I base set is printed
as `UNKNOWN` rather than crashing the tool.

## Repository Structure

```
riscv-decoder/
├── README.md
├── Makefile
├── .gitignore
├── include/
│   ├── common.h        # Shared macros, types, constants
│   ├── decoder.h        # Decoder function prototypes & types
│   └── memory.h          # Memory subsystem prototypes
├── src/
│   ├── main.c            # Entry point, CLI parsing
│   ├── decoder.c          # Instruction decode logic
│   └── memory.c            # Hex file loading & memory ops
├── test/
│   ├── test_decoder.c      # Unit tests for decoder
│   └── programs/            # Test hex files
│       ├── r_type.hex
│       ├── i_type.hex
│       ├── branch.hex
│       ├── mixed.hex
│       └── utype_jtype_unknown.hex
└── docs/
    └── DESIGN.md           # Design decisions & decoder logic
```

## Build Instructions

Requires `gcc` (or any C11-compatible compiler) and `make`. Valgrind is
required only for the `valgrind` target.

```bash
make            # build the release binary -> bin/riscv-decoder
make debug      # build an unoptimized, debug-symbol binary for gdb
make test       # build and run the unit test suite (51 assertions)
make valgrind   # run the decoder under Valgrind's memcheck (zero leaks)
make clean      # remove all build artifacts
```

## Usage

```bash
./bin/riscv-decoder <path/to/program.hex>
```

The input hex file is plain text, one 8-hex-digit instruction word per
line (an optional `0x` prefix is accepted, and `#` / `//` comments and
blank lines are skipped). For example:

```
# my_program.hex
00500113
00A00193
003100B3
```

## Sample Output

```
$ ./bin/riscv-decoder test/programs/mixed.hex
RISC-V RV32I Instruction Decoder
================================
Loaded 8 instructions from test/programs/mixed.hex
Addr       Hex        Assembly
---------- ---------- -------------------------
0x00000000 00500113   addi    x2, x0, 5
0x00000004 00A00193   addi    x3, x0, 10
0x00000008 003100B3   add     x1, x2, x3
0x0000000C 40310133   sub     x2, x2, x3
0x00000010 0020A023   sw      x2, 0(x1)
0x00000014 0000A103   lw      x2, 0(x1)
0x00000018 FE209CE3   bne     x1, x2, -8
0x0000001C 004000EF   jal     x1, 4
Decoded 8 instructions (8 valid, 0 unknown)
```

An invalid opcode is reported without crashing:

```
$ ./bin/riscv-decoder test/programs/utype_jtype_unknown.hex
...
0x00000010 FFFFFFFF   UNKNOWN
Decoded 5 instructions (4 valid, 1 unknown)
```

## Testing

Five hex test programs live under `test/programs/`, covering every
instruction format:

- `r_type.hex` — all 10 R-type instructions
- `i_type.hex` — all 9 I-type arithmetic instructions, all 5 loads, JALR,
  and a negative-immediate sign-extension check
- `branch.hex` — all 6 branch instructions with positive and negative
  offsets
- `mixed.hex` — the assignment's reference program (R/I/S/B/J mixed)
- `utype_jtype_unknown.hex` — LUI, AUIPC, forward/backward JAL, and a
  genuinely invalid opcode to verify `UNKNOWN` handling

A hand-rolled unit test suite (`test/test_decoder.c`, no external
framework dependency) exercises `decode_instruction()` and
`format_instruction()` directly with 62 assertions — covering every
instruction format, sign-extension edge cases, the underlying
`EXTRACT_BITS`/`EXTRACT_BIT`/`SIGN_EXTEND` bit-manipulation macros in
isolation, and crash-safety on invalid/zero/garbage input. Run with
`make test`.

The project has been verified with `make valgrind` to run with zero
memory leaks and zero invalid memory accesses across every test program
and every CLI error path (missing file, missing argument, malformed
lines).

## Design Documentation

See [`docs/DESIGN.md`](docs/DESIGN.md) for a detailed explanation of the
decoder's internal structure, bit-extraction strategy, and key design
decisions (including how immediates are sign-extended and why
`0xDEADBEEF` decodes as a valid `jal` rather than `UNKNOWN`).

## Author

Built by **ibneasifismail** for MEDS Lab Module 2.
