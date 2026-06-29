/* ============================================================================
 * memory.h — Hex file loading and the simple instruction-memory subsystem
 * ==========================================================================*/
#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

/* ----------------------------------------------------------------------
 * memory_t — a flat array of raw 32-bit instruction words plus their
 * load addresses, as read from a hex file. This stands in for "main
 * memory" in the larger CPU simulator this decoder will plug into.
 *
 * `words` is heap-allocated by memory_load_hex() and must be released
 * exactly once with memory_free(). Ownership transfers to whoever holds
 * the memory_t: main.c allocates it, uses it, then frees it.
 * --------------------------------------------------------------------*/
typedef struct {
    u32 *words;       /* heap-allocated array of raw instruction words */
    u32 *addresses;    /* heap-allocated array of matching addresses    */
    size_t count;      /* number of valid entries in words/addresses    */
    size_t capacity;    /* allocated capacity of words/addresses         */
} memory_t;

/* Load a hex file at `path` into `mem`. Each non-blank, non-comment
 * line is expected to contain one 8-hex-digit instruction word (e.g.
 * "00500113"); addresses are assigned sequentially starting at
 * BASE_ADDRESS, incrementing by RISCV_WORD_SIZE per line. Lines
 * beginning with '#' or '//' are treated as comments and skipped.
 *
 * Returns RV_SUCCESS on success (mem->words/addresses populated and
 * owned by the caller — free with memory_free()), or RV_FAILURE if the
 * file could not be opened or contained no valid instruction lines. */
int memory_load_hex(const char *path, memory_t *mem);

/* Release all heap memory owned by `mem` and zero its fields. Safe to
 * call on an already-freed or zero-initialized memory_t. */
void memory_free(memory_t *mem);

#endif /* MEMORY_H */
