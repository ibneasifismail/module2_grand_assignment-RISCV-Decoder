# DESIGN.md — RISC-V RV32I Instruction Decoder

This document explains how the decoder is structured internally, why the
bit-manipulation code looks the way it does, and the key design decisions
made while building it. It's meant to be read alongside the source files
in `src/` and `include/`.

## 1. High-Level Architecture

The project is split into three translation units, each with a single
responsibility, glued together by `main.c`:

```
 hex file on disk
        │
        ▼
  memory.c  ──►  memory_t { words[], addresses[], count }
        │
        ▼
  decoder.c ──►  decoded_inst_t   (one per instruction word)
        │
        ▼
  main.c    ──►  formatted disassembly table on stdout
```

- **`memory.c`** owns everything related to *getting bytes off disk and
  into memory*: opening the file, skipping blank lines/comments, parsing
  hex digits, and growing a dynamic array as more instructions are read.
  It knows nothing about what the bits *mean* — it just hands back raw
  32-bit words and the addresses they were loaded at.
- **`decoder.c`** owns everything related to *interpreting* a raw 32-bit
  word: pulling out opcode/funct3/funct7/register fields, reconstructing
  sign-extended immediates, and turning the result into a human-readable
  string.
- **`main.c`** is thin on purpose. It parses the one CLI argument, calls
  `memory_load_hex()`, loops over the result calling `decode_instruction()`
  and `format_instruction()`, and prints the table. No bit manipulation or
  file-format logic lives here.

This separation mirrors the structure a real CPU simulator would need
later in the course: a memory subsystem and a decode stage are reusable
building blocks independent of any particular front-end.

## 2. The `decoded_inst_t` Struct

Every instruction, regardless of format, decodes into the same struct
(`include/decoder.h`):

```c
typedef struct {
    u32 address, raw;
    inst_format_t format;
    mnemonic_t     mnemonic;
    alu_op_t       alu_op;
    int            valid;
    u8  opcode, rd, rs1, rs2, funct3, funct7;
    i32 imm;
    char mnemonic_str[8];
} decoded_inst_t;
```

Using one struct for every format (rather than a tagged union per format)
costs a little wasted memory per instruction — a handful of unused bytes
for, say, a U-type instruction that has no `rs2` — but it keeps the API
trivial: `decode_instruction()` always returns a plain value, never a
pointer the caller has to free, and `format_instruction()` can read
whichever fields it needs without a switch inside a union. For a decoder
that processes at most a few thousand instructions, this simplicity is
worth far more than the few wasted bytes per entry.

`valid` is the single source of truth for whether decoding succeeded. The
caller (`main.c`) never has to guess: if `valid == 0`, `mnemonic` is
guaranteed to be `INST_UNKNOWN` and `format_instruction()` will render
`"UNKNOWN"` regardless of what garbage might be sitting in the other
fields. This is what makes "must never crash on unknown opcodes" easy to
guarantee — there is exactly one flag to check, set in exactly one place
per decode path.

## 3. Bit Extraction Strategy

All field extraction goes through two macros in `include/common.h`:

```c
#define BIT_MASK(n)            ((1u << (n)) - 1u)
#define EXTRACT_BITS(value, hi, lo) \
    (((u32)(value) >> (lo)) & BIT_MASK((hi) - (lo) + 1u))
#define EXTRACT_BIT(value, n)  (((u32)(value) >> (n)) & 0x1u)
```

`EXTRACT_BITS(raw, hi, lo)` reads as "give me bits `hi` down to `lo`,
inclusive, right-justified" — which is exactly how the RISC-V spec itself
describes every field (e.g. "funct3 = inst[14:12]"). Writing
`EXTRACT_BITS(raw, 14, 12)` instead of `(raw >> 12) & 0x7` makes the code
self-documenting and makes transcription errors from the spec much less
likely, since the macro call reads the same way the spec table does.

`EXTRACT_BIT` is a convenience for the scattered-immediate formats (B-type
and J-type) where individual *single* bits, not ranges, need to be pulled
from non-contiguous positions and reassembled.

> **Edge case found during review:** `BIT_MASK(n)` originally computed
> `(1u << n) - 1u` unconditionally. No production call site ever requests
> a full 32-bit-wide mask, but while writing unit tests for the macro
> itself, the case `EXTRACT_BITS(value, 31, 0)` was added to confirm a
> full-word extraction works — and it doesn't, because that expands to
> `1u << 32`, which is undefined behavior in C: a left shift by an amount
> greater than or equal to the operand's bit width is never defined,
> even though "all bits set" is the obviously-intended answer. The fix
> special-cases `n >= 32` to return `0xFFFFFFFFu` directly. This is worth
> calling out because the *old* code printed the correct answer under
> `-O2` on this compiler — it just did so by luck of the specific
> compiler/flag combination, which is exactly why "it happened to print
> the right answer" is not the same thing as "it is defined behavior."
> A debug build with different optimization settings, or a different
> compiler version, could have produced a different (wrong) result from
> the same source line.

## 4. Sign Extension

Every RV32I immediate is sign-extended per the spec, so this needed to be
correct for every format, not just the obvious I-type case. The mechanism
is one macro:

```c
#define SIGN_EXTEND(value, bits) \
    ((i32)(((u32)(value)) << (32u - (bits))) >> (32u - (bits)))
```

This works by shifting the value's sign bit up into bit 31 of a 32-bit
word, then doing a signed (arithmetic) right shift back down — which in
C propagates the sign bit through the vacated positions. It's the
standard trick for sign-extending an N-bit two's-complement value to a
full word, and it avoids any branching (`if (value & sign_bit) value -=
...`) which keeps every immediate decoder a single expression.

Each format's immediate is reconstructed by a small dedicated function
(`imm_i`, `imm_s`, `imm_b`, `imm_u`, `imm_j` in `decoder.c`) that:

1. Pulls the relevant bit ranges out of `raw` with `EXTRACT_BITS` /
   `EXTRACT_BIT`,
2. Reassembles them into a single contiguous unsigned value in the
   *logical* immediate's bit order (not the scrambled encoding order),
3. Calls `SIGN_EXTEND` with the correct bit-width for that format.

The B-type and J-type immediates are the trickiest because the ISA
deliberately scatters their bits out of order — this is a real hardware
optimization (it lets several immediate bits share wiring with other
instruction formats so the decoder hardware is simpler), but it means the
software has to carefully undo the scramble:

- **B-type** (13-bit immediate, bit 0 implicitly zero since branch
  targets are 2-byte aligned): `bit12=inst[31]`, `bit11=inst[7]`,
  `bits[10:5]=inst[30:25]`, `bits[4:1]=inst[11:8]`.
- **J-type** (21-bit immediate, bit 0 implicitly zero): `bit20=inst[31]`,
  `bits[10:1]=inst[30:21]`, `bit11=inst[20]`, `bits[19:12]=inst[19:12]`.

Both were verified bit-by-bit against the assignment's own worked
examples (`bne x1, x2, -8` and `jal x1, 4`) before being trusted for the
rest of the test suite — see `test/test_decoder.c` for the regression
tests that pin these exact values down.

## 5. Opcode / Funct3 / Funct7 Dispatch

`decode_instruction()` is a single `switch` on the 7-bit opcode field.
Each case delegates to a small per-format helper (`decode_r_type`,
`decode_i_type_arith`, `decode_i_type_load`, `decode_s_type`,
`decode_b_type`, plus inline handling for the three single-opcode formats
LUI/AUIPC/JAL). Within R-type and I-type-arithmetic, a second `switch` on
`funct3` picks the specific instruction, and for the two cases where
`funct3` is ambiguous (ADD/SUB share `funct3=0`; SRL/SRA share
`funct3=5`), `funct7` bit 5 (`0x20`) is checked exactly as the ISA spec
defines it. SLLI/SRLI/SRAI reuse this same discriminator even though
they're nominally I-type, because the top 7 bits of their "immediate"
field are repurposed as a funct7-like tag — RV32I makes this choice so
that shift-by-immediate can't accidentally request a 12-bit shift amount,
which would be meaningless on a 32-bit machine.

Every `switch` has an explicit `default` that sets `mnemonic = INST_UNKNOWN`
and `valid = 0`. There is no code path that returns from
`decode_instruction()` without `valid` being set one way or the other —
this is what's actually being verified by the "must not crash on unknown
instructions" requirement, more than any particular printed string.

## 6. A Note on `0xDEADBEEF`

The assignment's own example treats `0xDEADBEEF` as an `UNKNOWN`
instruction. Taken literally against the real RV32I encoding, though,
`0xDEADBEEF`'s low 7 bits are `0x6F` — the `JAL` opcode — making it a
syntactically valid (if semantically nonsensical) jump instruction with a
huge negative-ish offset and `rd = x29`. This decoder treats it as a
valid `JAL`, because rejecting a correctly-encoded instruction just
because it looks like classic "filler garbage" would itself be a decoding
bug — a real disassembler (e.g. `objdump`) does the same thing. To still
demonstrate guaranteed `UNKNOWN` behavior, the test suite instead uses
opcode values genuinely outside the RV32I base set (such as `0x7F`, i.e.
`0xFFFFFFFF`, where every opcode bit is `1`), which has no defined
RV32I meaning and is correctly reported as `UNKNOWN`. See
`test/programs/utype_jtype_unknown.hex` and the corresponding unit tests.

## 7. Memory Subsystem Design

`memory_load_hex()` reads the file line-by-line with `fgets` into a
fixed-size stack buffer (`MAX_LINE_LENGTH`), so a pathological line can
never overflow a buffer — `fgets` always respects the size argument. Each
line is classified as blank/comment (skipped entirely) or as containing a
hex word (parsed with `strtoul`, base 16); malformed lines are skipped
rather than aborting the whole load, so one bad line in a large test file
doesn't take down the tool.

The word/address arrays grow dynamically (`ensure_capacity`, doubling from
an initial capacity of 64) rather than being allocated at a fixed
`MAX_INSTRUCTIONS` size up front. This means a five-instruction test file
uses a small allocation, not a 4096-entry array, while still supporting
programs up to `MAX_INSTRUCTIONS` long. All memory `memory_load_hex()`
allocates is owned by the `memory_t` it populates and is released by a
single call to `memory_free()` — there is exactly one allocation site
(`ensure_capacity`, called only from inside `memory_load_hex`) and exactly
one free site, which is what makes the "zero memory leaks" requirement
straightforward to guarantee and to verify (`make valgrind`).

## 8. Output Formatting

`format_instruction()` switches on `inst->format` (not `mnemonic`)
because the *shape* of the assembly syntax — `mnemonic rd, rs1, rs2` vs.
`mnemonic rd, imm(rs1)` vs. `mnemonic rs1, rs2, imm` — is determined by
the format, with only a couple of mnemonic-level exceptions inside FMT_I
(loads and JALR use the `offset(base)` memory-operand syntax; plain
arithmetic and shifts use the three-operand syntax). Every `snprintf` call
is bounded by the caller-supplied buffer size, so a future change to a
mnemonic string can never overflow the output buffer.

## 9. What Would Change in a Full CPU Simulator

This decoder is explicitly described as the *front-end* of a future
simulator. To turn it into one, the `alu_op_t` field already present in
`decoded_inst_t` is the connection point: an execute stage would look at
`alu_op` (already computed here) to know which ALU operation to perform,
without re-deriving it from `funct3`/`funct7`. Likewise `format` tells an
execute stage how to interpret `imm` (an offset for loads/stores/branches,
an upper-immediate for LUI/AUIPC, a jump target for JAL/JALR) without
re-checking `mnemonic` for every case. The struct was deliberately
designed now with that future use in mind, rather than only with printing
in mind.

## 10. Testing Strategy Summary

Testing happens at two levels, deliberately kept separate:

- **Unit tests** (`test/test_decoder.c`) call `decode_instruction()` and
  `format_instruction()` directly as a library, with no file I/O and no
  process boundary. This is where bit-exact correctness is pinned down —
  including the underlying `EXTRACT_BITS`/`SIGN_EXTEND` macros — fast
  enough to run on every `make test` without needing a hex file on disk.
- **Integration / hex-program tests** (`test/programs/*.hex`) exercise the
  full pipeline end-to-end through the actual CLI binary, including the
  file-loading and table-formatting code that the unit tests don't touch
  at all. These double as living documentation of what a real RV32I
  program looks like for each instruction category.

Keeping these separate means a unit test failure points straight at a
decoding bug, while a hex-program test failure (run manually via
`./bin/riscv-decoder test/programs/<file>.hex`) points at something in
the file-loading or CLI layer instead.

