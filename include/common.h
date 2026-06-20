/* ============================================================================
 * common.h — Shared macros, types, and constants for the RISC-V decoder
 *
 * This header centralizes everything that more than one module needs:
 * fixed-width integer aliases, bit-manipulation macros, memory-size
 * constants, and small helper macros used across decoder.c / memory.c /
 * main.c. Keeping these in one place avoids "magic numbers" scattered
 * through the codebase and gives every constant a descriptive name.
 * ==========================================================================*/
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>

/* ----------------------------------------------------------------------
 * Fixed-width type aliases
 *
 * RISC-V RV32I operates on 32-bit words and registers. Using explicit
 * widths (rather than plain int/unsigned) makes the bit-width of every
 * value obvious at the point of use, which matters a lot when you are
 * shifting and masking bits out of an instruction encoding.
 * --------------------------------------------------------------------*/
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int32_t  i32;

/* ----------------------------------------------------------------------
 * Program limits / memory subsystem constants
 * --------------------------------------------------------------------*/
#define RISCV_WORD_SIZE        4u          /* bytes per RV32 instruction      */
#define MAX_INSTRUCTIONS       4096u       /* max instructions we will load   */
#define MAX_LINE_LENGTH        256         /* max chars per line in hex file  */
#define HEX_DIGITS_PER_WORD    8           /* "DEADBEEF" -> 8 hex chars       */
#define BASE_ADDRESS           0x00000000u /* first instruction loads here    */

/* ----------------------------------------------------------------------
 * Bit-manipulation macros
 *
 * EXTRACT_BITS(value, hi, lo) pulls out the inclusive bit range [hi:lo]
 * from `value`, right-justified. This mirrors how the RISC-V spec itself
 * describes instruction fields (e.g. "funct3 = inst[14:12]").
 *
 * Example: EXTRACT_BITS(0x003100B3, 6, 0) -> 0x33 (the opcode field)
 * --------------------------------------------------------------------*/
#define BIT_MASK(n)            ((1u << (n)) - 1u)
#define EXTRACT_BITS(value, hi, lo) \
    (((u32)(value) >> (lo)) & BIT_MASK((hi) - (lo) + 1u))

/* Single-bit extraction, used heavily when re-assembling scattered
 * immediate fields (B-type and J-type encodings split the immediate
 * across several non-contiguous bit ranges). */
#define EXTRACT_BIT(value, n)  (((u32)(value) >> (n)) & 0x1u)

/* Sign-extend a value that occupies `bits` significant bits (1..32) up
 * to a full 32-bit signed integer. Used for every immediate field,
 * since RV32I immediates are always sign-extended per the ISA spec. */
#define SIGN_EXTEND(value, bits) \
    ((i32)(((u32)(value)) << (32u - (bits))) >> (32u - (bits)))

/* ----------------------------------------------------------------------
 * Convenience macros
 * --------------------------------------------------------------------*/
#define ARRAY_LEN(arr)         (sizeof(arr) / sizeof((arr)[0]))

/* Return codes used throughout the project for consistent error handling */
#define RV_SUCCESS             0
#define RV_FAILURE             (-1)

#endif /* COMMON_H */
