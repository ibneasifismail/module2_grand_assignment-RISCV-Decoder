/* ============================================================================
 * decoder.h — Types and function prototypes for RV32I instruction decoding
 * ==========================================================================*/
#ifndef DECODER_H
#define DECODER_H

#include "common.h"

/* ----------------------------------------------------------------------
 * RV32I base opcodes (instruction bits [6:0])
 *
 * RISC-V groups instructions by a 7-bit opcode field. Several different
 * mnemonics can share the same opcode (e.g. all R-type ALU ops share
 * OPCODE_OP); funct3/funct7 then disambiguate within that group.
 * --------------------------------------------------------------------*/
typedef enum {
    OPCODE_LOAD     = 0x03, /* LB, LH, LW, LBU, LHU                */
    OPCODE_STORE    = 0x23, /* SB, SH, SW                          */
    OPCODE_OP_IMM   = 0x13, /* ADDI, ANDI, ORI, XORI, SLTI, SLTIU,
                                SLLI, SRLI, SRAI                   */
    OPCODE_OP       = 0x33, /* ADD, SUB, AND, OR, XOR, SLL, SRL,
                                SRA, SLT, SLTU                     */
    OPCODE_BRANCH   = 0x63, /* BEQ, BNE, BLT, BGE, BLTU, BGEU      */
    OPCODE_JALR     = 0x67, /* JALR                                */
    OPCODE_JAL      = 0x6F, /* JAL                                 */
    OPCODE_LUI      = 0x37, /* LUI                                 */
    OPCODE_AUIPC    = 0x17  /* AUIPC                                */
} opcode_t;

/* ----------------------------------------------------------------------
 * Instruction format — determines how the remaining bits are interpreted
 * --------------------------------------------------------------------*/
typedef enum {
    FMT_R,       /* register-register   : funct7 | rs2 | rs1 | funct3 | rd  */
    FMT_I,       /* register-immediate  : imm[11:0] | rs1 | funct3 | rd     */
    FMT_S,       /* store               : imm[11:5] | rs2 | rs1 | f3 | imm  */
    FMT_B,       /* branch              : split immediate, like S + extra   */
    FMT_U,       /* upper immediate     : imm[31:12] | rd                  */
    FMT_J,       /* jump                : split 20-bit immediate | rd      */
    FMT_UNKNOWN  /* unrecognized opcode/funct combination                  */
} inst_format_t;

/* ----------------------------------------------------------------------
 * Mnemonic enum — every RV32I instruction this decoder supports, plus
 * a sentinel for anything it cannot decode.
 * --------------------------------------------------------------------*/
typedef enum {
    /* R-type */
    INST_ADD, INST_SUB, INST_AND, INST_OR, INST_XOR,
    INST_SLL, INST_SRL, INST_SRA, INST_SLT, INST_SLTU,
    /* I-type arithmetic */
    INST_ADDI, INST_ANDI, INST_ORI, INST_XORI, INST_SLTI, INST_SLTIU,
    INST_SLLI, INST_SRLI, INST_SRAI,
    /* I-type load */
    INST_LB, INST_LH, INST_LW, INST_LBU, INST_LHU,
    /* S-type store */
    INST_SB, INST_SH, INST_SW,
    /* B-type branch */
    INST_BEQ, INST_BNE, INST_BLT, INST_BGE, INST_BLTU, INST_BGEU,
    /* U-type */
    INST_LUI, INST_AUIPC,
    /* J-type */
    INST_JAL,
    /* I-type jump */
    INST_JALR,
    /* Sentinel — must remain last */
    INST_UNKNOWN
} mnemonic_t;

/* ----------------------------------------------------------------------
 * ALU operation enum
 *
 * Although this assignment is a decoder (not an executor), Part A
 * explicitly requires "enums for opcodes and ALU operations" since this
 * module is the front-end of a future CPU simulator. Each R-type/I-type
 * arithmetic instruction maps to exactly one of these.
 * --------------------------------------------------------------------*/
typedef enum {
    ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR,
    ALU_SLL, ALU_SRL, ALU_SRA, ALU_SLT, ALU_SLTU,
    ALU_NONE /* not an ALU instruction (loads, stores, branches, etc.) */
} alu_op_t;

/* ----------------------------------------------------------------------
 * Decoded instruction representation
 *
 * One struct to hold every field any instruction might need. Unused
 * fields for a given format are simply left at their zero-initialized
 * default; `valid` and `format` tell the caller which fields apply.
 * --------------------------------------------------------------------*/
typedef struct {
    u32 address;        /* address this instruction was loaded from   */
    u32 raw;             /* raw 32-bit instruction word                */

    inst_format_t format;
    mnemonic_t     mnemonic;
    alu_op_t       alu_op;
    int            valid;        /* 0 = UNKNOWN, 1 = decoded successfully */

    u8  opcode;          /* bits [6:0]                                  */
    u8  rd;               /* destination register, bits [11:7]          */
    u8  rs1;              /* source register 1, bits [19:15]             */
    u8  rs2;              /* source register 2, bits [24:20]             */
    u8  funct3;           /* bits [14:12]                                */
    u8  funct7;           /* bits [31:25]                                */
    i32 imm;              /* sign-extended immediate (format-dependent)  */

    char mnemonic_str[8];  /* e.g. "addi", "beq" — for printing          */
} decoded_inst_t;

/* ----------------------------------------------------------------------
 * Function prototypes
 * --------------------------------------------------------------------*/

/* Decode a single 32-bit instruction word fetched from `address`.
 * Always returns a fully-populated decoded_inst_t; on failure to
 * recognize the instruction, .valid is set to 0 and .mnemonic is
 * INST_UNKNOWN — the caller should never need to guard against a
 * crash, per the assignment's "must not crash" requirement. */
decoded_inst_t decode_instruction(u32 raw, u32 address);

/* Render a decoded instruction into `out` (a caller-supplied buffer of
 * at least `out_size` bytes) as a human-readable assembly string, e.g.
 * "addi x2, x0, 5" or "UNKNOWN". Returns RV_SUCCESS/RV_FAILURE. */
int format_instruction(const decoded_inst_t *inst, char *out, size_t out_size);

/* Look up the canonical lowercase mnemonic string for a mnemonic_t. */
const char *mnemonic_to_string(mnemonic_t m);

#endif /* DECODER_H */
