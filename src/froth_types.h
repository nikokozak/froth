#pragma once

#include <inttypes.h>
#include <stdint.h>

#ifndef FROTH_CELL_SIZE_BITS
#error                                                                         \
    "FROTH_CELL_SIZE_BITS is not defined. Please define it to 8, 16, 32, or 64."
#endif

#define FROTH_ARITY_UNKNOWN ((uint8_t)0xFF)

/* Check for word size flag -DFROTH_CELL_SIZE_BITS=8,16,32,64
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
#error                                                                         \
    "Invalid value for FROTH_CELL_SIZE_BITS. Please define it to 8, 16, 32, or 64."
#endif

/* Maximum string length in bytes, enforced at all creation points (ADR-047). */
#ifndef FROTH_STRING_MAX_LEN
#define FROTH_STRING_MAX_LEN 256
#endif

// Sanity check that the size of froth_cell_t actually matches
// FROTH_CELL_SIZE_BITS
_Static_assert(sizeof(froth_cell_t) * 8 == FROTH_CELL_SIZE_BITS,
               "FROTH_CELL_SIZE_BITS does not match the size of froth_cell_t");

/* Forward declaration — full definition in froth_vm.h */
typedef struct froth_vm_t froth_vm_t;

typedef enum {
  FROTH_OK = 0,

  /* Runtime and language errors. These numbers are persisted and may be
   * surfaced through the Froth runtime API. Never reorder.
   * Append new runtime errors after the last assigned value. */
  FROTH_ERROR_STACK_OVERFLOW = 1,
  FROTH_ERROR_STACK_UNDERFLOW = 2,
  FROTH_ERROR_TYPE_MISMATCH = 3,
  FROTH_ERROR_UNDEFINED_WORD = 4,
  FROTH_ERROR_DIVISION_BY_ZERO = 5,
  FROTH_ERROR_HEAP_OUT_OF_MEMORY = 6,
  FROTH_ERROR_PATTERN_INVALID = 7,
  FROTH_ERROR_PATTERN_TOO_LARGE = 8,
  FROTH_ERROR_IO = 9,
  FROTH_ERROR_NOCATCH = 10,
  FROTH_ERROR_LOOP_STACK = 11,
  FROTH_ERROR_VALUE_OVERFLOW = 12,
  FROTH_ERROR_BOUNDS = 13,
  FROTH_ERROR_PROGRAM_INTERRUPTED = 14,
  FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS = 15,
  FROTH_ERROR_SLOT_TABLE_FULL = 16,
  FROTH_ERROR_REDEF_PRIMITIVE = 17,
  FROTH_ERROR_CALL_DEPTH = 18,
  FROTH_ERROR_NO_MARK = 19,
  FROTH_ERROR_RESET = 20,
  FROTH_ERROR_FFI_TABLE_FULL = 21,    /* too many FFI tables registered */
  FROTH_ERROR_TRANSIENT_EXPIRED = 22, /* transient string overwritten */
  FROTH_ERROR_TRANSIENT_FULL = 23,    /* transient descriptor table full */
  FROTH_ERROR_CELLSPACE_FULL = 24,
  FROTH_ERROR_TOPLEVEL_ONLY = 25,
  FROTH_ERROR_BUSY = 26,
  FROTH_ERROR_NOT_PERSISTABLE = 27,
  /* Reader/evaluator errors — occur before execution.
   * Stable numbers, but programs won't typically catch these. */
  FROTH_ERROR_TOKEN_TOO_LONG = 100,
  FROTH_ERROR_UNTERMINATED_QUOTE = 101,
  FROTH_ERROR_UNTERMINATED_COMMENT = 102,
  FROTH_ERROR_UNEXPECTED_PAREN = 103,
  FROTH_ERROR_BSTRING_TOO_LONG = 104,
  FROTH_ERROR_UNTERMINATED_STRING = 105,
  FROTH_ERROR_INVALID_ESCAPE = 106,
  FROTH_ERROR_UNEXPECTED_BRACKET = 107,
  FROTH_ERROR_SIGNATURE = 108,
  FROTH_ERROR_NAMED_FRAME = 109,

  /* Snapshot errors — persistence subsystem (200–299). */
  FROTH_ERROR_SNAPSHOT_OVERFLOW = 200, /* buffer read/write past end */
  FROTH_ERROR_SNAPSHOT_FORMAT = 201,   /* bad magic, version, or unknown tag */
  FROTH_ERROR_SNAPSHOT_UNRESOLVED = 202,  /* heap offset not in object table */
  FROTH_ERROR_SNAPSHOT_BAD_CRC = 203,     /* header or payload CRC mismatch */
  FROTH_ERROR_SNAPSHOT_INCOMPAT = 204,    /* ABI hash mismatch */
  FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT = 205, /* no valid snapshot in storage */
  FROTH_ERROR_SNAPSHOT_BAD_NAME = 206,    /* name exceeds max length */
  FROTH_ERROR_SNAPSHOT_TRANSIENT =
      207, /* transient string in persistable data */

  /* Link errors (250–259) */
  FROTH_ERROR_LINK_OVERFLOW = 250,     /* frame/COBS buffer too small */
  FROTH_ERROR_LINK_COBS_DECODE = 251,  /* invalid COBS encoding */
  FROTH_ERROR_LINK_BAD_MAGIC = 252,    /* magic != "FL" */
  FROTH_ERROR_LINK_BAD_VERSION = 253,  /* unsupported protocol version */
  FROTH_ERROR_LINK_BAD_CRC = 254,      /* CRC32 mismatch */
  FROTH_ERROR_LINK_TOO_LARGE = 255,    /* payload exceeds max */
  FROTH_ERROR_LINK_UNKNOWN_TYPE = 256, /* unrecognized message type */

  /* Internal sentinel — not a user-visible error code. */
  FROTH_ERROR_THROW = -1,
} froth_error_t;

/* Early-return on error. Only works in functions returning froth_error_t. */
#define FROTH_TRY(expr)                                                        \
  do {                                                                         \
    froth_error_t _err = (expr);                                               \
    if (_err != FROTH_OK)                                                      \
      return _err;                                                             \
  } while (0)

/* C primitive hook retained for the slot table and board-facing bindings. */
typedef froth_error_t (*froth_native_word_t)(froth_vm_t *vm);
