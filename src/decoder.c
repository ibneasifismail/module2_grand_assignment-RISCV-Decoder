/* ============================================================================
 * decoder.c — RV32I instruction decode logic
 *
 * This file implements decode_instruction(), which takes a raw 32-bit
 * instruction word and produces a fully-populated decoded_inst_t. The
 * implementation is organized by instruction format (R/I/S/B/U/J),
 * mirroring the structure of the RISC-V ISA manual's encoding tables.
 * ==========================================================================*/
#include <stdio.h>
#include <string.h>

#include "decoder.h"

/* ----------------------------------------------------------------------
 * Bit-field positions and widths, per the RV32I base instruction
 * encoding (see RISC-V Unprivileged ISA spec, ch. 2).
 *
 *   31........25 24....20 19....15 14..12 11.......7 6......0
 *   funct7        rs2      rs1      f3     rd          opcode   (R-type)
 *   imm[11:0]              rs1      f3     rd          opcode   (I-type)
 *   imm[11:5]    rs2       rs1      f3     imm[4:0]    opcode   (S-type)
 *   imm[12|10:5] rs2       rs1      f3     imm[4:1|11] opcode   (B-type)
 *   imm[31:12]                             rd          opcode   (U-type)
 *   imm[20|10:1|11|19:12]                  rd          opcode   (J-type)
 * --------------------------------------------------------------------*/
#define OPCODE_HI   6
#define OPCODE_LO   0
#define RD_HI       11
#define RD_LO       7
#define FUNCT3_HI   14
#define FUNCT3_LO   12
#define RS1_HI      19
#define RS1_LO      15
#define RS2_HI      24
#define RS2_LO      20
#define FUNCT7_HI   31
#define FUNCT7_LO   25

/* funct7 values that distinguish "subtract" / "arithmetic shift right"
 * from their "add" / "logical shift right" siblings under the same
 * opcode+funct3. Bit 30 of the instruction (part of funct7) is the
 * conventional discriminator the ISA uses for this purpose. */
#define FUNCT7_ALT  0x20

/* I-type SLLI/SRLI/SRAI use funct7 (the top 7 bits of what would be
 * the immediate) to pick the shift variant, while the bottom 5 bits
 * (shamt) hold the actual shift amount. */
#define SHAMT_HI    24
#define SHAMT_LO    20

/* ----------------------------------------------------------------------
 * mnemonic_to_string — lookup table indexed by mnemonic_t
 * --------------------------------------------------------------------*/
const char *mnemonic_to_string(mnemonic_t m) {
    static const char *names[] = {
        [INST_ADD]  = "add",  [INST_SUB]  = "sub",  [INST_AND] = "and",
        [INST_OR]   = "or",   [INST_XOR]  = "xor",  [INST_SLL] = "sll",
        [INST_SRL]  = "srl",  [INST_SRA]  = "sra",  [INST_SLT] = "slt",
        [INST_SLTU] = "sltu",
        [INST_ADDI] = "addi", [INST_ANDI] = "andi", [INST_ORI] = "ori",
        [INST_XORI] = "xori", [INST_SLTI] = "slti", [INST_SLTIU] = "sltiu",
        [INST_SLLI] = "slli", [INST_SRLI] = "srli", [INST_SRAI] = "srai",
        [INST_LB]   = "lb",   [INST_LH]   = "lh",   [INST_LW]  = "lw",
        [INST_LBU]  = "lbu",  [INST_LHU]  = "lhu",
        [INST_SB]   = "sb",   [INST_SH]   = "sh",   [INST_SW]  = "sw",
        [INST_BEQ]  = "beq",  [INST_BNE]  = "bne",  [INST_BLT] = "blt",
        [INST_BGE]  = "bge",  [INST_BLTU] = "bltu", [INST_BGEU] = "bgeu",
        [INST_LUI]  = "lui",  [INST_AUIPC] = "auipc",
        [INST_JAL]  = "jal",
        [INST_JALR] = "jalr",
        [INST_UNKNOWN] = "UNKNOWN"
    };
    if (m < 0 || m >= (mnemonic_t)ARRAY_LEN(names) || names[m] == NULL) {
        return "UNKNOWN";
    }
    return names[m];
}

/* ----------------------------------------------------------------------
 * imm_i / imm_s / imm_b / imm_u / imm_j
 *
 * Each function reassembles the sign-extended immediate for one
 * instruction format directly from the raw 32-bit word, following the
 * bit-scatter layout documented in the table above. EXTRACT_BITS /
 * EXTRACT_BIT / SIGN_EXTEND (from common.h) do the heavy lifting.
 * --------------------------------------------------------------------*/

/* I-type: a contiguous 12-bit immediate at inst[31:20]. */
static i32 imm_i(u32 raw) {
    u32 imm = EXTRACT_BITS(raw, 31, 20);
    return SIGN_EXTEND(imm, 12);
}

/* S-type: imm[11:5] lives where funct7 would be, imm[4:0] lives where
 * rd would be. We rebuild the contiguous 12-bit value, then sign
 * extend. */
static i32 imm_s(u32 raw) {
    u32 hi = EXTRACT_BITS(raw, 31, 25); /* imm[11:5] */
    u32 lo = EXTRACT_BITS(raw, 11, 7);   /* imm[4:0]  */
    u32 imm = (hi << 5) | lo;
    return SIGN_EXTEND(imm, 12);
}

/* B-type: a 13-bit immediate (bit 0 is implicitly 0, since branch
 * targets are always 2-byte aligned) scattered as:
 *   bit12 = inst[31], bit11 = inst[7], bits[10:5] = inst[30:25],
 *   bits[4:1] = inst[11:8]
 * Note this is almost the S-type layout with bit 11 and bit 12 swapped
 * in meaning, which is why branch offsets and store offsets share so
 * much of their encoding. */
static i32 imm_b(u32 raw) {
    u32 bit12    = EXTRACT_BIT(raw, 31);
    u32 bit11    = EXTRACT_BIT(raw, 7);
    u32 bits10_5 = EXTRACT_BITS(raw, 30, 25);
    u32 bits4_1  = EXTRACT_BITS(raw, 11, 8);

    u32 imm = (bit12 << 12) | (bit11 << 11) | (bits10_5 << 5) | (bits4_1 << 1);
    return SIGN_EXTEND(imm, 13);
}

/* U-type: the immediate occupies inst[31:12] and represents the upper
 * 20 bits of a 32-bit value (the lower 12 bits are implicitly zero).
 * LUI/AUIPC use this directly with no further sign-extension needed
 * beyond shifting into position — but we still report the *raw* 32-bit
 * signed value of imm<<12 for display purposes. */
static i32 imm_u(u32 raw) {
    u32 imm = EXTRACT_BITS(raw, 31, 12);
    return (i32)(imm << 12);
}

/* J-type: a 21-bit immediate (bit 0 implicitly 0) scattered as:
 *   bit20 = inst[31], bits[10:1] = inst[30:21], bit11 = inst[20],
 *   bits[19:12] = inst[19:12]
 */
static i32 imm_j(u32 raw) {
    u32 bit20     = EXTRACT_BIT(raw, 31);
    u32 bits10_1  = EXTRACT_BITS(raw, 30, 21);
    u32 bit11     = EXTRACT_BIT(raw, 20);
    u32 bits19_12 = EXTRACT_BITS(raw, 19, 12);

    u32 imm = (bit20 << 20) | (bits19_12 << 12) | (bit11 << 11) | (bits10_1 << 1);
    return SIGN_EXTEND(imm, 21);
}

/* ----------------------------------------------------------------------
 * decode_r_type — ADD/SUB/AND/OR/XOR/SLL/SRL/SRA/SLT/SLTU
 *
 * All ten R-type ALU instructions share opcode OPCODE_OP; funct3 picks
 * the operation family, and for ADD/SUB and SRL/SRA, funct7 bit 5
 * (0x20) additionally distinguishes the two variants.
 * --------------------------------------------------------------------*/
static void decode_r_type(decoded_inst_t *inst) {
    inst->format = FMT_R;

    switch (inst->funct3) {
        case 0x0:
            if (inst->funct7 == FUNCT7_ALT) {
                inst->mnemonic = INST_SUB; inst->alu_op = ALU_SUB;
            } else {
                inst->mnemonic = INST_ADD; inst->alu_op = ALU_ADD;
            }
            inst->valid = 1;
            break;
        case 0x1: inst->mnemonic = INST_SLL;  inst->alu_op = ALU_SLL;  inst->valid = 1; break;
        case 0x2: inst->mnemonic = INST_SLT;  inst->alu_op = ALU_SLT;  inst->valid = 1; break;
        case 0x3: inst->mnemonic = INST_SLTU; inst->alu_op = ALU_SLTU; inst->valid = 1; break;
        case 0x4: inst->mnemonic = INST_XOR;  inst->alu_op = ALU_XOR;  inst->valid = 1; break;
        case 0x5:
            if (inst->funct7 == FUNCT7_ALT) {
                inst->mnemonic = INST_SRA; inst->alu_op = ALU_SRA;
            } else {
                inst->mnemonic = INST_SRL; inst->alu_op = ALU_SRL;
            }
            inst->valid = 1;
            break;
        case 0x6: inst->mnemonic = INST_OR;   inst->alu_op = ALU_OR;   inst->valid = 1; break;
        case 0x7: inst->mnemonic = INST_AND;  inst->alu_op = ALU_AND;  inst->valid = 1; break;
        default:
            inst->mnemonic = INST_UNKNOWN;
            inst->valid = 0;
            break;
    }
}

/* ----------------------------------------------------------------------
 * decode_i_type_arith — ADDI/ANDI/ORI/XORI/SLTI/SLTIU/SLLI/SRLI/SRAI
 *
 * Shares opcode OPCODE_OP_IMM. SLLI/SRLI/SRAI are special: their
 * "immediate" field is actually a 5-bit shift amount plus a funct7-like
 * discriminator (bit 30) reused from the R-type encoding, since
 * shifting by an arbitrary 12-bit immediate would be meaningless for
 * a 32-bit machine.
 * --------------------------------------------------------------------*/
static void decode_i_type_arith(decoded_inst_t *inst, u32 raw) {
    inst->format = FMT_I;
    inst->imm = imm_i(raw);

    switch (inst->funct3) {
        case 0x0: inst->mnemonic = INST_ADDI;  inst->alu_op = ALU_ADD;  inst->valid = 1; break;
        case 0x4: inst->mnemonic = INST_XORI;  inst->alu_op = ALU_XOR;  inst->valid = 1; break;
        case 0x6: inst->mnemonic = INST_ORI;   inst->alu_op = ALU_OR;   inst->valid = 1; break;
        case 0x7: inst->mnemonic = INST_ANDI;  inst->alu_op = ALU_AND;  inst->valid = 1; break;
        case 0x2: inst->mnemonic = INST_SLTI;  inst->alu_op = ALU_SLT;  inst->valid = 1; break;
        case 0x3: inst->mnemonic = INST_SLTIU; inst->alu_op = ALU_SLTU; inst->valid = 1; break;
        case 0x1:
            /* SLLI: shift amount is the low 5 bits of the immediate field */
            inst->mnemonic = INST_SLLI;
            inst->alu_op = ALU_SLL;
            inst->imm = (i32)EXTRACT_BITS(raw, SHAMT_HI, SHAMT_LO);
            inst->valid = 1;
            break;
        case 0x5:
            /* SRLI vs SRAI distinguished by bit 30, same as R-type */
            inst->imm = (i32)EXTRACT_BITS(raw, SHAMT_HI, SHAMT_LO);
            if (inst->funct7 == FUNCT7_ALT) {
                inst->mnemonic = INST_SRAI; inst->alu_op = ALU_SRA;
            } else {
                inst->mnemonic = INST_SRLI; inst->alu_op = ALU_SRL;
            }
            inst->valid = 1;
            break;
        default:
            inst->mnemonic = INST_UNKNOWN;
            inst->valid = 0;
            break;
    }
}

/* ----------------------------------------------------------------------
 * decode_i_type_load — LB/LH/LW/LBU/LHU (opcode OPCODE_LOAD)
 * --------------------------------------------------------------------*/
static void decode_i_type_load(decoded_inst_t *inst, u32 raw) {
    inst->format = FMT_I;
    inst->imm = imm_i(raw);
    inst->alu_op = ALU_NONE;

    switch (inst->funct3) {
        case 0x0: inst->mnemonic = INST_LB;  inst->valid = 1; break;
        case 0x1: inst->mnemonic = INST_LH;  inst->valid = 1; break;
        case 0x2: inst->mnemonic = INST_LW;  inst->valid = 1; break;
        case 0x4: inst->mnemonic = INST_LBU; inst->valid = 1; break;
        case 0x5: inst->mnemonic = INST_LHU; inst->valid = 1; break;
        default:
            inst->mnemonic = INST_UNKNOWN;
            inst->valid = 0;
            break;
    }
}

/* ----------------------------------------------------------------------
 * decode_jalr — opcode OPCODE_JALR, single funct3 value (0x0) is valid
 * --------------------------------------------------------------------*/
static void decode_jalr(decoded_inst_t *inst, u32 raw) {
    inst->format = FMT_I;
    inst->alu_op = ALU_NONE;
    inst->imm = imm_i(raw);

    if (inst->funct3 == 0x0) {
        inst->mnemonic = INST_JALR;
        inst->valid = 1;
    } else {
        inst->mnemonic = INST_UNKNOWN;
        inst->valid = 0;
    }
}

/* ----------------------------------------------------------------------
 * decode_s_type — SB/SH/SW (opcode OPCODE_STORE)
 * --------------------------------------------------------------------*/
static void decode_s_type(decoded_inst_t *inst, u32 raw) {
    inst->format = FMT_S;
    inst->imm = imm_s(raw);
    inst->alu_op = ALU_NONE;

    switch (inst->funct3) {
        case 0x0: inst->mnemonic = INST_SB; inst->valid = 1; break;
        case 0x1: inst->mnemonic = INST_SH; inst->valid = 1; break;
        case 0x2: inst->mnemonic = INST_SW; inst->valid = 1; break;
        default:
            inst->mnemonic = INST_UNKNOWN;
            inst->valid = 0;
            break;
    }
}

/* ----------------------------------------------------------------------
 * decode_b_type — BEQ/BNE/BLT/BGE/BLTU/BGEU (opcode OPCODE_BRANCH)
 * --------------------------------------------------------------------*/
static void decode_b_type(decoded_inst_t *inst, u32 raw) {
    inst->format = FMT_B;
    inst->imm = imm_b(raw);
    inst->alu_op = ALU_NONE;

    switch (inst->funct3) {
        case 0x0: inst->mnemonic = INST_BEQ;  inst->valid = 1; break;
        case 0x1: inst->mnemonic = INST_BNE;  inst->valid = 1; break;
        case 0x4: inst->mnemonic = INST_BLT;  inst->valid = 1; break;
        case 0x5: inst->mnemonic = INST_BGE;  inst->valid = 1; break;
        case 0x6: inst->mnemonic = INST_BLTU; inst->valid = 1; break;
        case 0x7: inst->mnemonic = INST_BGEU; inst->valid = 1; break;
        default:
            inst->mnemonic = INST_UNKNOWN;
            inst->valid = 0;
            break;
    }
}

/* ----------------------------------------------------------------------
 * decode_instruction — top-level dispatch by opcode
 * --------------------------------------------------------------------*/
decoded_inst_t decode_instruction(u32 raw, u32 address) {
    decoded_inst_t inst;
    memset(&inst, 0, sizeof(inst));

    inst.address = address;
    inst.raw = raw;
    inst.alu_op = ALU_NONE;
    inst.format = FMT_UNKNOWN;
    inst.mnemonic = INST_UNKNOWN;
    inst.valid = 0;

    inst.opcode = (u8)EXTRACT_BITS(raw, OPCODE_HI, OPCODE_LO);
    inst.rd     = (u8)EXTRACT_BITS(raw, RD_HI, RD_LO);
    inst.funct3 = (u8)EXTRACT_BITS(raw, FUNCT3_HI, FUNCT3_LO);
    inst.rs1    = (u8)EXTRACT_BITS(raw, RS1_HI, RS1_LO);
    inst.rs2    = (u8)EXTRACT_BITS(raw, RS2_HI, RS2_LO);
    inst.funct7 = (u8)EXTRACT_BITS(raw, FUNCT7_HI, FUNCT7_LO);

    switch (inst.opcode) {
        case OPCODE_OP:
            decode_r_type(&inst);
            break;
        case OPCODE_OP_IMM:
            decode_i_type_arith(&inst, raw);
            break;
        case OPCODE_LOAD:
            decode_i_type_load(&inst, raw);
            break;
        case OPCODE_JALR:
            decode_jalr(&inst, raw);
            break;
        case OPCODE_STORE:
            decode_s_type(&inst, raw);
            break;
        case OPCODE_BRANCH:
            decode_b_type(&inst, raw);
            break;
        case OPCODE_LUI:
            inst.format = FMT_U;
            inst.mnemonic = INST_LUI;
            inst.imm = imm_u(raw);
            inst.alu_op = ALU_NONE;
            inst.valid = 1;
            break;
        case OPCODE_AUIPC:
            inst.format = FMT_U;
            inst.mnemonic = INST_AUIPC;
            inst.imm = imm_u(raw);
            inst.alu_op = ALU_NONE;
            inst.valid = 1;
            break;
        case OPCODE_JAL:
            inst.format = FMT_J;
            inst.mnemonic = INST_JAL;
            inst.imm = imm_j(raw);
            inst.alu_op = ALU_NONE;
            inst.valid = 1;
            break;
        default:
            inst.format = FMT_UNKNOWN;
            inst.mnemonic = INST_UNKNOWN;
            inst.valid = 0;
            break;
    }

    strncpy(inst.mnemonic_str, mnemonic_to_string(inst.mnemonic),
            sizeof(inst.mnemonic_str) - 1);
    inst.mnemonic_str[sizeof(inst.mnemonic_str) - 1] = '\0';

    return inst;
}

/* ----------------------------------------------------------------------
 * format_instruction
 *
 * Renders a decoded_inst_t into assembly syntax appropriate to its
 * format. snprintf is used throughout to guarantee `out` is never
 * overrun regardless of input.
 * --------------------------------------------------------------------*/
int format_instruction(const decoded_inst_t *inst, char *out, size_t out_size) {
    if (inst == NULL || out == NULL || out_size == 0) {
        return RV_FAILURE;
    }

    if (!inst->valid) {
        snprintf(out, out_size, "UNKNOWN");
        return RV_SUCCESS;
    }

    const char *mn = inst->mnemonic_str;

    switch (inst->format) {
        case FMT_R:
            /* e.g. "add x1, x2, x3" */
            snprintf(out, out_size, "%-7s x%d, x%d, x%d",
                      mn, inst->rd, inst->rs1, inst->rs2);
            break;

        case FMT_I:
            if (inst->mnemonic == INST_JALR) {
                /* e.g. "jalr x1, 4(x2)" */
                snprintf(out, out_size, "%-7s x%d, %d(x%d)",
                          mn, inst->rd, inst->imm, inst->rs1);
            } else if (inst->mnemonic == INST_LB || inst->mnemonic == INST_LH ||
                       inst->mnemonic == INST_LW || inst->mnemonic == INST_LBU ||
                       inst->mnemonic == INST_LHU) {
                /* e.g. "lw x2, 0(x1)" */
                snprintf(out, out_size, "%-7s x%d, %d(x%d)",
                          mn, inst->rd, inst->imm, inst->rs1);
            } else if (inst->mnemonic == INST_SLLI || inst->mnemonic == INST_SRLI ||
                       inst->mnemonic == INST_SRAI) {
                /* shift amount is unsigned, never sign-extended */
                snprintf(out, out_size, "%-7s x%d, x%d, %d",
                          mn, inst->rd, inst->rs1, inst->imm);
            } else {
                /* e.g. "addi x2, x0, 5" */
                snprintf(out, out_size, "%-7s x%d, x%d, %d",
                          mn, inst->rd, inst->rs1, inst->imm);
            }
            break;

        case FMT_S:
            /* e.g. "sw x2, 0(x1)" */
            snprintf(out, out_size, "%-7s x%d, %d(x%d)",
                      mn, inst->rs2, inst->imm, inst->rs1);
            break;

        case FMT_B:
            /* e.g. "beq x1, x1, 0" — immediate is the branch offset */
            snprintf(out, out_size, "%-7s x%d, x%d, %d",
                      mn, inst->rs1, inst->rs2, inst->imm);
            break;

        case FMT_U:
            /* e.g. "lui x5, 4096" */
            snprintf(out, out_size, "%-7s x%d, %d",
                      mn, inst->rd, inst->imm);
            break;

        case FMT_J:
            /* e.g. "jal x1, 4" */
            snprintf(out, out_size, "%-7s x%d, %d",
                      mn, inst->rd, inst->imm);
            break;

        default:
            snprintf(out, out_size, "UNKNOWN");
            break;
    }

    return RV_SUCCESS;
}
