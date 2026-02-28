#pragma once

#include <stdint.h>
#include <inttypes.h>

#ifndef FROTH_CELL_SIZE_BITS
  #error "FROTH_CELL_SIZE_BITS is not defined. Please define it to 8, 16, 32, or 64."
#endif

/* Check for word size flag -DFROTH_CELL_SIZE_BITS=8,16,32,64\e
 * This allows us to determine the size of froth_cell_t and froth_cell_u_t,
 * which is necessary for cross-compilation. */
#if FROTH_CELL_SIZE_BITS == 8
  typedef int8_t froth_cell_t;
  typedef uint8_t froth_cell_u_t;
  /* Adding these FORMAT defines means that we don't need to worry
   * about the size of the cell when using printf and scanf. */
  #define FROTH_CELL_FORMAT PRId8
  #define FROTH_CELL_U_FORMAT PRIu8
#elif FROTH_CELL_SIZE_BITS == 16
  typedef int16_t froth_cell_t;
  typedef uint16_t froth_cell_u_t;
  #define FROTH_CELL_FORMAT PRId16
  #define FROTH_CELL_U_FORMAT PRIu16
#elif FROTH_CELL_SIZE_BITS == 32
  typedef int32_t froth_cell_t;
  typedef uint32_t froth_cell_u_t;
  #define FROTH_CELL_FORMAT PRId32
  #define FROTH_CELL_U_FORMAT PRIu32
#elif FROTH_CELL_SIZE_BITS == 64
  typedef int64_t froth_cell_t;
  typedef uint64_t froth_cell_u_t;
  #define FROTH_CELL_FORMAT PRId64
  #define FROTH_CELL_U_FORMAT PRIu64
#else
  #error "Invalid value for FROTH_CELL_SIZE_BITS. Please define it to 8, 16, 32, or 64."
#endif

#define FROTH_FALSE ((froth_cell_t)0)
#define FROTH_TRUE  ((froth_cell_t)-1)

// Sanity check that the size of froth_cell_t actually matches FROTH_CELL_SIZE_BITS
_Static_assert(sizeof(froth_cell_t) * 8 == FROTH_CELL_SIZE_BITS, "FROTH_CELL_SIZE_BITS does not match the size of froth_cell_t");

typedef enum {
  FROTH_OK = 0,
  FROTH_ERROR_STACK_OVERFLOW,
  FROTH_ERROR_STACK_UNDERFLOW,
  FROTH_ERROR_VALUE_OVERFLOW,
  FROTH_ERROR_IO,
  FROTH_ERROR_HEAP_OUT_OF_MEMORY,
  FROTH_ERROR_SLOT_NAME_NOT_FOUND,
  FROTH_ERROR_SLOT_TABLE_FULL,
  FROTH_ERROR_SLOT_INDEX_EMPTY,
  FROTH_ERROR_TOKEN_TOO_LONG,
} froth_error_t;

typedef enum {
  FROTH_NUMBER = 0,
  FROTH_QUOTE = 1,
  FROTH_SLOT = 2,
  FROTH_PATTERN = 3,
  FROTH_STRING = 4,
  FROTH_CONTRACT = 5,
  FROTH_CALL = 6,    // internal: invoke SlotRef (only inside quotation bodies, see ADR-009)
} froth_cell_tag_t;


/* TAGGED CELL ENCODING
 * Froth uses 3-bit LSB tagging for its cells.
 * The lower 3 bits encode the type tag, the remaining bits carry the payload.
 * Tag 0 (Number) leaves tag bits clear so addition/subtraction work without untagging.
 *
 * Tag table:
 *   0 = Number       (user-visible value)
 *   1 = QuoteRef     (user-visible value)
 *   2 = SlotRef      (user-visible value — literal, pushed onto DS)
 *   3 = PatternRef   (user-visible value)
 *   4 = StringRef    (user-visible value)
 *   5 = ContractRef  (user-visible value)
 *   6 = Call          (internal — invoke SlotRef, only inside quotation bodies)
 *   7 = (reserved)
 *
 * See ADR-004, ADR-005, ADR-009.
 */

#define FROTH_GET_CELL_TAG(val) ((val) & 0x7)
#define FROTH_STRIP_CELL_TAG(val) ((val) >> 3)
#define FROTH_PACK_CELL_TAG(val, tag) (((val) << 3) | (tag))
#define FROTH_CELL_IS_NUMBER(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_NUMBER))
#define FROTH_CELL_IS_QUOTE(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_QUOTE))
#define FROTH_CELL_IS_SLOT(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_SLOT))
#define FROTH_CELL_IS_PATTERN(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_PATTERN))
#define FROTH_CELL_IS_STRING(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_STRING))
#define FROTH_CELL_IS_CONTRACT(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_CONTRACT))
#define FROTH_CELL_IS_CALL(val) ((FROTH_GET_CELL_TAG((val)) == FROTH_CALL))

static inline froth_error_t froth_make_cell(froth_cell_t value, froth_cell_tag_t tag, froth_cell_t* return_value) {
  froth_cell_t max_value = ((froth_cell_t)1 << (FROTH_CELL_SIZE_BITS - 3)) - 1;
  froth_cell_t min_value = -((froth_cell_t)1 << (FROTH_CELL_SIZE_BITS - 3));
  if (!(value >= min_value && value <= max_value)) { return FROTH_ERROR_VALUE_OVERFLOW; }
  *return_value = FROTH_PACK_CELL_TAG(value, tag);
  return FROTH_OK;
}


