/* ============================================================================
 * memory.c — Hex file loading and the simple instruction-memory subsystem
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "memory.h"

/* Initial capacity for the dynamic word/address arrays. We grow by
 * doubling whenever we run out of room, rather than allocating
 * MAX_INSTRUCTIONS up front, so memory use scales with the actual
 * program size instead of a worst-case constant. */
#define INITIAL_CAPACITY 64u

/* ----------------------------------------------------------------------
 * is_blank_or_comment
 *
 * Returns 1 if `line` has no decodable content: empty, all whitespace,
 * or a comment starting with '#' or "//" (after leading whitespace).
 * --------------------------------------------------------------------*/
static int is_blank_or_comment(const char *line) {
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0') return 1;                /* blank line       */
    if (*line == '#') return 1;                  /* '#' comment      */
    if (line[0] == '/' && line[1] == '/') return 1; /* "//" comment */
    return 0;
}

/* ----------------------------------------------------------------------
 * parse_hex_word
 *
 * Parses the first contiguous run of hex digits in `line` into *out.
 * Tolerates an optional leading "0x"/"0X" prefix and trailing
 * whitespace/comments after the hex digits (e.g. "00500113 # addi").
 * Returns RV_SUCCESS if at least one hex digit was found, RV_FAILURE
 * otherwise (so the caller can skip malformed lines without crashing).
 * --------------------------------------------------------------------*/
static int parse_hex_word(const char *line, u32 *out) {
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
        line += 2;
    }

    if (!isxdigit((unsigned char)*line)) {
        return RV_FAILURE;
    }

    char *endptr = NULL;
    unsigned long value = strtoul(line, &endptr, 16);
    if (endptr == line) {
        return RV_FAILURE;
    }

    *out = (u32)(value & 0xFFFFFFFFu);
    return RV_SUCCESS;
}

/* ----------------------------------------------------------------------
 * ensure_capacity
 *
 * Grows mem->words/addresses (doubling capacity) if `mem->count` has
 * reached `mem->capacity`. Returns RV_SUCCESS/RV_FAILURE; on failure
 * the original arrays are left untouched (realloc failure is safe).
 * --------------------------------------------------------------------*/
static int ensure_capacity(memory_t *mem) {
    if (mem->count < mem->capacity) {
        return RV_SUCCESS;
    }

    size_t new_capacity = (mem->capacity == 0) ? INITIAL_CAPACITY : mem->capacity * 2;

    u32 *new_words = (u32 *)realloc(mem->words, new_capacity * sizeof(u32));
    if (new_words == NULL) {
        return RV_FAILURE;
    }
    mem->words = new_words;

    u32 *new_addresses = (u32 *)realloc(mem->addresses, new_capacity * sizeof(u32));
    if (new_addresses == NULL) {
        /* mem->words already grew and is still valid/owned by mem;
         * we simply fail this push without leaking anything. */
        return RV_FAILURE;
    }
    mem->addresses = new_addresses;

    mem->capacity = new_capacity;
    return RV_SUCCESS;
}

int memory_load_hex(const char *path, memory_t *mem) {
    if (path == NULL || mem == NULL) {
        return RV_FAILURE;
    }

    mem->words = NULL;
    mem->addresses = NULL;
    mem->count = 0;
    mem->capacity = 0;

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: could not open hex file '%s'\n", path);
        return RV_FAILURE;
    }

    char line[MAX_LINE_LENGTH];
    u32 next_address = BASE_ADDRESS;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (is_blank_or_comment(line)) {
            continue;
        }

        u32 word;
        if (parse_hex_word(line, &word) != RV_SUCCESS) {
            /* Skip malformed lines rather than aborting the whole
             * load — a single bad line shouldn't crash the tool. */
            continue;
        }

        if (mem->count >= MAX_INSTRUCTIONS) {
            fprintf(stderr,
                    "Warning: hex file exceeds MAX_INSTRUCTIONS (%u); truncating\n",
                    MAX_INSTRUCTIONS);
            break;
        }

        if (ensure_capacity(mem) != RV_SUCCESS) {
            fprintf(stderr, "Error: out of memory while loading hex file\n");
            fclose(fp);
            memory_free(mem);
            return RV_FAILURE;
        }

        mem->words[mem->count] = word;
        mem->addresses[mem->count] = next_address;
        mem->count++;
        next_address += RISCV_WORD_SIZE;
    }

    fclose(fp);

    if (mem->count == 0) {
        fprintf(stderr, "Error: no valid instruction words found in '%s'\n", path);
        memory_free(mem);
        return RV_FAILURE;
    }

    return RV_SUCCESS;
}

void memory_free(memory_t *mem) {
    if (mem == NULL) {
        return;
    }
    free(mem->words);
    free(mem->addresses);
    mem->words = NULL;
    mem->addresses = NULL;
    mem->count = 0;
    mem->capacity = 0;
}
