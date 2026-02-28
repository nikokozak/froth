#pragma once

#include "froth_types.h"

#define FROTH_TOKEN_NAME_MAX 32

typedef enum {
  FROTH_TOKEN_NUMBER,           // A parsed integer value
  FROTH_TOKEN_IDENTIFIER,       // A bare name (e.g. "foo") — evaluated as a call at top level
  FROTH_TOKEN_TICK_IDENTIFIER,  // A tick-quoted name (e.g. "'foo") — pushes a SlotRef
  FROTH_TOKEN_OPEN_BRACKET,     // "[" — begins a quotation
  FROTH_TOKEN_CLOSE_BRACKET,    // "]" — ends a quotation
  FROTH_TOKEN_EOF,              // No more tokens in the input
} froth_token_type_t;

/* A single token produced by the reader.
 * Carries either a numeric value or a name string, depending on type. */
typedef struct {
  froth_token_type_t type;
  union {
    froth_cell_t number;              // Valid when type == FROTH_TOKEN_NUMBER
    char name[FROTH_TOKEN_NAME_MAX];  // Valid when type == IDENTIFIER or TICK_IDENTIFIER
  };
} froth_token_t;

/* Reader state. Tracks position within a line of input.
 * Initialize with froth_reader_init before calling froth_reader_next_token. */
typedef struct {
  const char* input;      // The input string being tokenized (not owned)
  froth_cell_u_t position; // Current read position within input
} froth_reader_t;

void froth_reader_init(froth_reader_t* reader, const char* input);
froth_error_t froth_reader_next_token(froth_reader_t* reader, froth_token_t* token);
