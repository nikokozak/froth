#include "froth_reader.h"
#include <string.h>

/* Character classification helpers */

static int is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int is_delimiter(char c) {
  return c == '[' || c == ']' || c == ';' || c == '(' || c == ')' || c == '"' || c == '\'' || c == '\0' || is_whitespace(c);
}

/* Skip past whitespace and comments. A backslash (\) starts a line comment
 * that runs to end-of-input. After this call, reader->position points at
 * the next meaningful character or the null terminator. */
static froth_error_t skip_whitespace_and_comments(froth_reader_t* reader) {
  while (reader->input[reader->position] != '\0') {
    char c = reader->input[reader->position];

    if (is_whitespace(c)) {
      reader->position++;
      continue;
    }

    // Line comment: \ to end of line
    if (c == '\\') {
      while (reader->input[reader->position] != '\0' &&
             reader->input[reader->position] != '\n') {
        reader->position++;
      }
      continue;
    }

    // Paren comment: ( to matching ), with nesting
    if (c == '(') {
      int depth = 1;
      reader->position++;
      while (reader->input[reader->position] != '\0' && depth > 0) {
        if (reader->input[reader->position] == '(') { depth++; }
        else if (reader->input[reader->position] == ')') { depth--; }
        reader->position++;
      }
      if (depth > 0) {
        return FROTH_ERROR_UNTERMINATED_COMMENT;
      }
      continue;
    }

    // Stray )
    if (c == ')') {
      return FROTH_ERROR_UNEXPECTED_PAREN;
    }

    break;
  }
  return FROTH_OK;
}

/* Peek into the next character without advancing the reader. peek(0) returns the current character, peek(1)
 * returns the next character, etc. Returns '\0' if peeking past the end of input. */
static char froth_reader_peek(froth_reader_t* reader, froth_cell_u_t peek_ahead) {
  for (int i = 0; i <= peek_ahead; i++) {
     if (reader->input[reader->position + i] == '\0') {
       return '\0';
     }
  }
  return reader->input[reader->position + peek_ahead];
}

/* Read a contiguous word (non-whitespace, non-delimiter characters) into
 * the provided buffer. Returns FROTH_ERROR_TOKEN_TOO_LONG if the word
 * exceeds max_len - 1 characters. The buffer is always null-terminated. */
static froth_error_t read_word(froth_reader_t* reader, char* buf, froth_cell_u_t max_len) {
  froth_cell_u_t len = 0;

  while (!is_delimiter(reader->input[reader->position])) {
    if (len >= max_len - 1) { return FROTH_ERROR_TOKEN_TOO_LONG; }
    buf[len++] = reader->input[reader->position++];
  }

  buf[len] = '\0';
  return FROTH_OK;
}

/* Try to parse a word as an integer. Returns 1 on success, 0 if the word
 * is not a valid integer. Handles optional leading '-' but only if followed
 * by at least one digit ("-" alone is an identifier, not a number). */
static int try_parse_number(const char* word, froth_cell_t* result) {
  const char* p = word;
  int negative = 0;
  froth_cell_t value = 0;

  if (*p == '-') {
    negative = 1;
    p++;
    if (!is_digit(*p)) { return 0; } // bare "-" is an identifier
  }

  if (!is_digit(*p)) { return 0; }

  if (*p == '0' && p[1] == 'x') { // Number is a hex
    p += 2; // Skip "0x"
    if (*p == '\0') { return 0; } // "0x" followed by whitespace or end of input is an identifier
    // Parse hex string into number
    while (*p != '\0') {
      if (is_digit(*p)) {
        value = (value << 4) | (*p - '0');
      } else if (*p >= 'a' && *p <= 'f') {
        value = (value << 4) | (*p - 'a' + 10);
      } else if (*p >= 'A' && *p <= 'F') {
        value = (value << 4) | (*p - 'A' + 10);
      } else {
        return 0; // Invalid hex character
      }
      p++;
    }
  } else if (*p == '0' && p[1] == 'b') { // Number is a binary
    p += 2; // Skip "0b"
    if (*p == '\0') { return 0; } // "0b" followed by whitespace or end of input is an identifier
    // Parse binary string into number
    while (*p != '\0') {
      if (*p == '0' || *p == '1') {
        value = (value << 1) | (*p - '0');
      } else {
        return 0; // Invalid binary character
      }
      p++;
    }
  } else { // Number is a base 10
    while (is_digit(*p)) {
      value = value * 10 + (*p - '0');
      p++;
    }
  }

  // If there are trailing non-digit characters, it's an identifier (e.g. "3foo")
  if (*p != '\0') { return 0; }

  *result = negative ? -value : value;
  return 1;
}

/* Scan a string body after the opening '"' has been consumed.
 * Resolves escape sequences (\n, \t, \r, \\, \") into raw bytes.
 * Unknown escapes are a reader error. */
static froth_error_t read_string(froth_reader_t* reader, uint8_t* buf, froth_cell_u_t* out_len) {
  froth_cell_u_t len = 0;

  while (1) {
    char c = reader->input[reader->position];
    if (c == '\0') { return FROTH_ERROR_UNTERMINATED_STRING; }
    if (c == '"') { reader->position++; break; }
    if (len >= FROTH_STRING_MAX_LEN) { return FROTH_ERROR_BSTRING_TOO_LONG; }

    if (c == '\\') {
      reader->position++;
      c = reader->input[reader->position];
      if (c == '\0') { return FROTH_ERROR_UNTERMINATED_STRING; }
      if      (c == '\\') { buf[len++] = '\\'; }
      else if (c == '"')  { buf[len++] = '"'; }
      else if (c == 'n')  { buf[len++] = '\n'; }
      else if (c == 'r')  { buf[len++] = '\r'; }
      else if (c == 't')  { buf[len++] = '\t'; }
      else { return FROTH_ERROR_INVALID_ESCAPE; }
      reader->position++;
      continue;
    }

    buf[len++] = c;
    reader->position++;
  }

  *out_len = len;
  return FROTH_OK;
}

void froth_reader_init(froth_reader_t* reader, const char* input) {
  reader->input = input;
  reader->position = 0;
}

froth_error_t froth_reader_next_token(froth_reader_t* reader, froth_token_t* token) {
  FROTH_TRY(skip_whitespace_and_comments(reader));

  char c = reader->input[reader->position];

  // End of input
  if (c == '\0') {
    token->type = FROTH_TOKEN_EOF;
    return FROTH_OK;
  }

  if (c == '"') {
    reader->position++; // skip the opening quote
    FROTH_TRY(read_string(reader, token->bstring_bytes, &token->bstring_len));
    token->type = FROTH_TOKEN_BSTRING;
    return FROTH_OK;
  }

  // Open bracket
  if (c == '[') {
    token->type = FROTH_TOKEN_OPEN_BRACKET;
    reader->position++;
    return FROTH_OK;
  }

  if (c == 'p' && froth_reader_peek(reader, 1) == '[') {
    token->type = FROTH_TOKEN_OPEN_PAT;
    reader->position += 2; // skip both 'p' and '['
    return FROTH_OK;
  }

  // Close bracket
  if (c == ']' || c == ';') {
    token->type = FROTH_TOKEN_CLOSE_BRACKET;
    reader->position++;
    return FROTH_OK;
  }

  // Tick-quoted identifier
  if (c == '\'') {
    reader->position++; // skip the tick
    froth_error_t err = read_word(reader, token->name, FROTH_TOKEN_NAME_MAX);
    if (err != FROTH_OK) { return err; }
    token->type = FROTH_TOKEN_TICK_IDENTIFIER;
    return FROTH_OK;
  }

  // Word: could be a number or an identifier
  char word[FROTH_TOKEN_NAME_MAX];
  froth_error_t err = read_word(reader, word, FROTH_TOKEN_NAME_MAX);
  if (err != FROTH_OK) { return err; }

  froth_cell_t number;
  if (try_parse_number(word, &number)) {
    token->type = FROTH_TOKEN_NUMBER;
    token->number = number;
  } else {
    token->type = FROTH_TOKEN_IDENTIFIER;
    memcpy(token->name, word, strlen(word) + 1);
  }

  return FROTH_OK;
}
