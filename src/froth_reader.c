#include "froth_reader.h"
#include <string.h>
#include <stdlib.h>

/* Character classification helpers */

static int is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int is_delimiter(char c) {
  return c == '[' || c == ']' || c == '\'' || c == '\0' || is_whitespace(c);
}

/* Skip past whitespace and comments. A backslash (\) starts a line comment
 * that runs to end-of-input. After this call, reader->position points at
 * the next meaningful character or the null terminator. */
static void skip_whitespace_and_comments(froth_reader_t* reader) {
  while (reader->input[reader->position] != '\0') {
    char c = reader->input[reader->position];

    if (is_whitespace(c)) {
      reader->position++;
      continue;
    }

    // Line comment: \ to end of input
    if (c == '\\') {
      while (reader->input[reader->position] != '\0') {
        reader->position++;
      }
      return;
    }

    break;
  }
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

  if (*p == '-') {
    negative = 1;
    p++;
    if (!is_digit(*p)) { return 0; } // bare "-" is an identifier
  }

  if (!is_digit(*p)) { return 0; }

  froth_cell_t value = 0;
  while (is_digit(*p)) {
    value = value * 10 + (*p - '0');
    p++;
  }

  // If there are trailing non-digit characters, it's an identifier (e.g. "3foo")
  if (*p != '\0') { return 0; }

  *result = negative ? -value : value;
  return 1;
}

void froth_reader_init(froth_reader_t* reader, const char* input) {
  reader->input = input;
  reader->position = 0;
}

froth_error_t froth_reader_next_token(froth_reader_t* reader, froth_token_t* token) {
  skip_whitespace_and_comments(reader);

  char c = reader->input[reader->position];

  // End of input
  if (c == '\0') {
    token->type = FROTH_TOKEN_EOF;
    return FROTH_OK;
  }

  // Open bracket
  if (c == '[') {
    token->type = FROTH_TOKEN_OPEN_BRACKET;
    reader->position++;
    return FROTH_OK;
  }

  // Close bracket
  if (c == ']') {
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
