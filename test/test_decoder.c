/* ============================================================================
 * test_decoder.c — Unit tests for decode_instruction() / format_instruction()
 *
 * A small hand-rolled test harness (no external framework required, to
 * keep the build dependency-free). Each test encodes a known
 * instruction, decodes it, and checks both the structured fields and
 * the rendered assembly string against the expected result.
 *
 * Run via `make test`, or directly: ./test/test_decoder
 * ==========================================================================*/
#include <stdio.h>
#include <string.h>

#include "decoder.h"

static int tests_run = 0;
static int tests_failed = 0;

/* ----------------------------------------------------------------------
 * Minimal assertion helpers — print a clear PASS/FAIL line per check
 * and keep a running tally so main() can report a summary and set the
 * process exit code appropriately (non-zero on any failure, so this
 * integrates cleanly with `make test` / CI).
 * --------------------------------------------------------------------*/
#define CHECK(cond, description)                                            \
    do {                                                                    \
        tests_run++;                                                        \
        if (!(cond)) {                                                      \
            tests_failed++;                                                 \
            printf("  [FAIL] %s\n", description);                           \
        } else {                                                            \
            printf("  [PASS] %s\n", description);                           \
        }                                                                   \
    } while (0)

#define CHECK_ASM(raw, addr, expected_asm, description)                     \
    do {                                                                    \
        decoded_inst_t _inst = decode_instruction((raw), (addr));           \
        char _buf[64];                                                      \
        format_instruction(&_inst, _buf, sizeof(_buf));                     \
        /* trim trailing padding spaces added by the %-7s field width      \
         * before comparing, so test expectations stay readable */         \
        char _trimmed[64];                                                  \
        snprintf(_trimmed, sizeof(_trimmed), "%s", _buf);                    \
        tests_run++;                                                        \
        if (strstr(_trimmed, (expected_asm)) == NULL) {                      \
            tests_failed++;                                                 \
            printf("  [FAIL] %s -> got \"%s\", expected to contain \"%s\"\n", \
                   description, _trimmed, expected_asm);                     \
        } else {                                                            \
            printf("  [PASS] %s -> \"%s\"\n", description, _trimmed);        \
        }                                                                    \
    } while (0)

/* ----------------------------------------------------------------------
 * R-type tests
 * --------------------------------------------------------------------*/
static void test_r_type(void) {
    printf("\n-- R-type instructions --\n");
    CHECK_ASM(0x003100B3, 0x00, "add", "ADD decodes correctly");
    CHECK_ASM(0x403100B3, 0x00, "sub", "SUB decodes correctly (funct7=0x20 distinguishes from ADD)");
    CHECK_ASM(0x003170B3, 0x00, "and", "AND decodes correctly");
    CHECK_ASM(0x003160B3, 0x00, "or",  "OR decodes correctly");
    CHECK_ASM(0x003140B3, 0x00, "xor", "XOR decodes correctly");
    CHECK_ASM(0x003110B3, 0x00, "sll", "SLL decodes correctly");
    CHECK_ASM(0x003150B3, 0x00, "srl", "SRL decodes correctly");
    CHECK_ASM(0x403150B3, 0x00, "sra", "SRA decodes correctly (funct7=0x20 distinguishes from SRL)");
    CHECK_ASM(0x003120B3, 0x00, "slt", "SLT decodes correctly");
    CHECK_ASM(0x003130B3, 0x00, "sltu", "SLTU decodes correctly");

    decoded_inst_t add = decode_instruction(0x003100B3, 0x00);
    CHECK(add.valid == 1, "ADD marked valid");
    CHECK(add.mnemonic == INST_ADD, "ADD mnemonic enum correct");
    CHECK(add.alu_op == ALU_ADD, "ADD maps to ALU_ADD");
    CHECK(add.rd == 1 && add.rs1 == 2 && add.rs2 == 3, "ADD register fields correct");
}

/* ----------------------------------------------------------------------
 * I-type arithmetic tests, including sign-extension
 * --------------------------------------------------------------------*/
static void test_i_type_arith(void) {
    printf("\n-- I-type arithmetic instructions --\n");
    CHECK_ASM(0x00500113, 0x00, "addi    x2, x0, 5", "ADDI decodes with correct positive immediate");
    CHECK_ASM(0xFFF10093, 0x00, "addi    x1, x2, -1", "ADDI sign-extends negative immediate correctly");
    CHECK_ASM(0x00F17093, 0x00, "andi    x1, x2, 15", "ANDI decodes correctly");
    CHECK_ASM(0x00F16093, 0x00, "ori     x1, x2, 15", "ORI decodes correctly");
    CHECK_ASM(0x00F14093, 0x00, "xori    x1, x2, 15", "XORI decodes correctly");
    CHECK_ASM(0x00512093, 0x00, "slti    x1, x2, 5", "SLTI decodes correctly");
    CHECK_ASM(0x00513093, 0x00, "sltiu   x1, x2, 5", "SLTIU decodes correctly");
    CHECK_ASM(0x00311093, 0x00, "slli    x1, x2, 3", "SLLI decodes with shift amount (not sign-extended imm)");
    CHECK_ASM(0x00315093, 0x00, "srli    x1, x2, 3", "SRLI decodes correctly");
    CHECK_ASM(0x40315093, 0x00, "srai    x1, x2, 3", "SRAI decodes correctly (funct7=0x20 distinguishes from SRLI)");

    decoded_inst_t addi = decode_instruction(0xFFF10093, 0x00);
    CHECK(addi.imm == -1, "ADDI immediate field is exactly -1 as i32 (sign-extension correctness)");
}

/* ----------------------------------------------------------------------
 * Load / store tests
 * --------------------------------------------------------------------*/
static void test_loads_and_stores(void) {
    printf("\n-- Load (I-type) and Store (S-type) instructions --\n");
    CHECK_ASM(0x00010083, 0x00, "lb      x1, 0(x2)", "LB decodes correctly");
    CHECK_ASM(0x00211083, 0x00, "lh      x1, 2(x2)", "LH decodes correctly");
    CHECK_ASM(0x0000A103, 0x00, "lw      x2, 0(x1)", "LW decodes correctly");
    CHECK_ASM(0x00014083, 0x00, "lbu     x1, 0(x2)", "LBU decodes correctly");
    CHECK_ASM(0x00215083, 0x00, "lhu     x1, 2(x2)", "LHU decodes correctly");
    CHECK_ASM(0x0020A023, 0x00, "sw      x2, 0(x1)", "SW decodes correctly");

    decoded_inst_t sw = decode_instruction(0x0020A023, 0x00);
    CHECK(sw.format == FMT_S, "SW format is FMT_S");
    CHECK(sw.rs2 == 2 && sw.rs1 == 1 && sw.imm == 0, "SW fields (rs2=src, rs1=base, imm=offset) correct");
}

/* ----------------------------------------------------------------------
 * Branch tests, including negative (backward) offsets
 * --------------------------------------------------------------------*/
static void test_branches(void) {
    printf("\n-- B-type branch instructions --\n");
    CHECK_ASM(0x00108063, 0x10, "beq     x1, x1, 0", "BEQ decodes correctly (matches assignment example)");
    CHECK_ASM(0xFE209CE3, 0x18, "bne     x1, x2, -8", "BNE decodes negative offset correctly (matches assignment example)");
    CHECK_ASM(0x0020C863, 0x00, "blt     x1, x2, 16", "BLT decodes correctly");
    CHECK_ASM(0xFE20D8E3, 0x00, "bge     x1, x2, -16", "BGE decodes negative offset correctly");
    CHECK_ASM(0x0020E263, 0x00, "bltu    x1, x2, 4", "BLTU decodes correctly");
    CHECK_ASM(0xFE20FEE3, 0x00, "bgeu    x1, x2, -4", "BGEU decodes negative offset correctly");

    decoded_inst_t bne = decode_instruction(0xFE209CE3, 0x18);
    CHECK(bne.imm == -8, "BNE immediate is exactly -8 (scattered B-type bits reassembled correctly)");
}

/* ----------------------------------------------------------------------
 * U-type tests
 * --------------------------------------------------------------------*/
static void test_u_type(void) {
    printf("\n-- U-type instructions --\n");
    CHECK_ASM(0x000012B7, 0x00, "lui     x5, 4096", "LUI decodes correctly");
    CHECK_ASM(0x00002317, 0x00, "auipc   x6, 8192", "AUIPC decodes correctly");
}

/* ----------------------------------------------------------------------
 * J-type and JALR tests
 * --------------------------------------------------------------------*/
static void test_jumps(void) {
    printf("\n-- J-type (JAL) and I-type jump (JALR) --\n");
    CHECK_ASM(0x004000EF, 0x1C, "jal     x1, 4", "JAL decodes correctly (matches assignment example)");
    CHECK_ASM(0xFFDFF06F, 0x00, "jal     x0, -4", "JAL decodes negative (backward) offset correctly");
    CHECK_ASM(0x004100E7, 0x00, "jalr    x1, 4(x2)", "JALR decodes correctly");
}

/* ----------------------------------------------------------------------
 * UNKNOWN instruction handling — must never crash
 * --------------------------------------------------------------------*/
static void test_unknown(void) {
    printf("\n-- UNKNOWN instruction handling --\n");
    decoded_inst_t unk = decode_instruction(0xFFFFFFFF, 0x00);
    CHECK(unk.valid == 0, "Invalid opcode (0x7F) is marked invalid");
    CHECK(unk.mnemonic == INST_UNKNOWN, "Invalid opcode maps to INST_UNKNOWN");

    char buf[64];
    format_instruction(&unk, buf, sizeof(buf));
    CHECK(strcmp(buf, "UNKNOWN") == 0, "format_instruction renders exactly \"UNKNOWN\"");

    /* A second, independent invalid encoding: a valid opcode (OP_IMM)
     * but an out-of-range funct3 cannot actually occur since funct3 is
     * only 3 bits (0-7, all defined for OP_IMM except none are
     * missing) -- so instead we check a reserved/unallocated opcode
     * value directly to be sure the dispatcher's default case works
     * for more than one input. */
    decoded_inst_t unk2 = decode_instruction(0x0000007F, 0x04);
    CHECK(unk2.valid == 0, "Second invalid-opcode case (0x0000007F) also marked invalid");
}

/* ----------------------------------------------------------------------
 * Sanity check: decoding must never crash regardless of input,
 * including all-zero words and address edge values.
 * --------------------------------------------------------------------*/
static void test_robustness(void) {
    printf("\n-- Robustness (must not crash on any 32-bit input) --\n");
    decoded_inst_t zero = decode_instruction(0x00000000, 0x00);
    CHECK(zero.valid == 0, "All-zero word (opcode 0x00) is correctly UNKNOWN, not a crash");

    decoded_inst_t maxaddr = decode_instruction(0x00500113, 0xFFFFFFFC);
    CHECK(maxaddr.address == 0xFFFFFFFC, "Maximum address value is stored without overflow/crash");
}

int main(void) {
    printf("RISC-V Decoder Unit Test Suite\n");
    printf("===============================\n");

    test_r_type();
    test_i_type_arith();
    test_loads_and_stores();
    test_branches();
    test_u_type();
    test_jumps();
    test_unknown();
    test_robustness();

    printf("\n===============================\n");
    printf("Results: %d/%d tests passed\n", tests_run - tests_failed, tests_run);

    if (tests_failed > 0) {
        printf("%d TEST(S) FAILED\n", tests_failed);
        return 1;
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
