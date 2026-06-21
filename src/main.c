/* ============================================================================
 * main.c — Entry point and CLI handling for the RISC-V RV32I decoder
 *
 * Usage:
 *   riscv-decoder <path/to/program.hex>
 *
 * Loads the given hex file into memory, decodes every instruction word
 * it contains, and prints a formatted disassembly table to stdout.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "decoder.h"
#include "memory.h"

#define BANNER_TITLE   "RISC-V RV32I Instruction Decoder"
#define BANNER_RULE_LEN 32   /* length of the "====...." underline */

/* ----------------------------------------------------------------------
 * print_usage — shown when invoked with the wrong number of arguments
 * --------------------------------------------------------------------*/
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <hexfile>\n", prog_name);
    fprintf(stderr, "  Decodes a RISC-V RV32I hex program and prints its disassembly.\n");
}

/* ----------------------------------------------------------------------
 * print_banner — fixed header printed before the disassembly table
 * --------------------------------------------------------------------*/
static void print_banner(void) {
    printf("%s\n", BANNER_TITLE);
    for (int i = 0; i < BANNER_RULE_LEN; i++) {
        putchar('=');
    }
    putchar('\n');
}

/* ----------------------------------------------------------------------
 * print_table_header — column headers for the disassembly listing
 * --------------------------------------------------------------------*/
static void print_table_header(void) {
    printf("%-10s %-10s %s\n", "Addr", "Hex", "Assembly");
    printf("---------- ---------- -------------------------\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *hex_path = argv[1];

    memory_t mem;
    if (memory_load_hex(hex_path, &mem) != RV_SUCCESS) {
        /* memory_load_hex() already printed a descriptive error */
        return EXIT_FAILURE;
    }

    print_banner();
    printf("Loaded %zu instructions from %s\n", mem.count, hex_path);
    print_table_header();

    size_t valid_count = 0;
    size_t unknown_count = 0;
    char assembly[64];

    for (size_t i = 0; i < mem.count; i++) {
        decoded_inst_t inst = decode_instruction(mem.words[i], mem.addresses[i]);
        format_instruction(&inst, assembly, sizeof(assembly));

        printf("0x%08X %08X   %s\n", mem.addresses[i], mem.words[i], assembly);

        if (inst.valid) {
            valid_count++;
        } else {
            unknown_count++;
        }
    }

    printf("Decoded %zu instructions (%zu valid, %zu unknown)\n",
           mem.count, valid_count, unknown_count);

    memory_free(&mem);
    return EXIT_SUCCESS;
}
