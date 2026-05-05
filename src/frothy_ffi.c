#include "frothy_ffi.h"

#include "ffi.h"
#include "froth_slot_table.h"
#include "froth_vm.h"
#include "platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FROTHY_HAS_BOARD_PINS
#include "frothy_board_pins.h"
#endif

typedef enum {
  FROTHY_FFI_SOURCE_FOREIGN = 0,
  FROTHY_FFI_SOURCE_BOARD = 1,
  FROTHY_FFI_SOURCE_PROJECT = 2,
} frothy_ffi_source_t;

typedef enum {
  FROTHY_FFI_SLOT_EMPTY = 0,
  FROTHY_FFI_SLOT_IMPL = 1,
  FROTHY_FFI_SLOT_PRIM = 2,
} frothy_ffi_slot_binding_kind_t;

typedef struct {
  uint8_t overlay;
  uint8_t in_arity;
  uint8_t out_arity;
  frothy_ffi_slot_binding_kind_t binding_kind;
  froth_cell_t impl;
  froth_native_word_t prim;
} frothy_ffi_slot_state_t;

typedef struct {
  froth_cell_u_t slot_index;
  frothy_ffi_slot_state_t saved_state;
  frothy_value_t value;
  uint8_t in_arity;
  uint8_t out_arity;
} frothy_ffi_pending_slot_impl_t;

#if defined(FROTHY_FORCE_NO_WEAK_SYMBOLS)
#define FROTHY_HAS_WEAK_SYMBOLS 0
#define FROTHY_WEAK_DEF
#elif defined(__APPLE__) && defined(__clang__)
#define FROTHY_HAS_WEAK_SYMBOLS 1
#define FROTHY_WEAK_DEF __attribute__((weak))
#elif defined(__GNUC__) || defined(__clang__)
#define FROTHY_HAS_WEAK_SYMBOLS 1
#define FROTHY_WEAK_DEF __attribute__((weak))
#else
#define FROTHY_HAS_WEAK_SYMBOLS 0
#define FROTHY_WEAK_DEF
#endif

#if FROTHY_HAS_WEAK_SYMBOLS
/*
 * Provide empty weak defaults so optional board/project tables can be probed
 * without forcing a definition on targets that do not supply them.
 */
const frothy_ffi_entry_t frothy_board_bindings[] FROTHY_WEAK_DEF = {{0}};
#ifdef FROTH_HAS_PROJECT_FFI
const frothy_ffi_entry_t frothy_project_bindings[] FROTHY_WEAK_DEF = {{0}};
#endif
#endif

#define FROTHY_FFI_RANDOM_DEFAULT_SEED UINT32_C(0x6d2b79f5)

#ifdef FROTHY_FFI_TESTING
static size_t frothy_ffi_test_slot_replace_calls = 0;
static size_t frothy_ffi_test_slot_replace_fail_at = SIZE_MAX;
static froth_error_t frothy_ffi_test_slot_replace_error = FROTH_OK;

void frothy_ffi_test_fail_slot_replace_at(size_t call_index,
                                          froth_error_t error) {
  frothy_ffi_test_slot_replace_calls = 0;
  frothy_ffi_test_slot_replace_fail_at = call_index;
  frothy_ffi_test_slot_replace_error =
      error == FROTH_OK ? FROTH_ERROR_IO : error;
}

static froth_error_t frothy_ffi_test_maybe_fail_slot_replace(void) {
  if (frothy_ffi_test_slot_replace_fail_at != SIZE_MAX &&
      frothy_ffi_test_slot_replace_calls++ ==
          frothy_ffi_test_slot_replace_fail_at) {
    froth_error_t error = frothy_ffi_test_slot_replace_error;

    frothy_ffi_test_slot_replace_fail_at = SIZE_MAX;
    frothy_ffi_test_slot_replace_error = FROTH_OK;
    return error;
  }
  return FROTH_OK;
}
#endif

froth_cell_t frothy_ffi_wrap_uptime_ms(uint32_t uptime_ms) {
  return froth_wrap_payload((froth_cell_u_t)uptime_ms);
}

froth_error_t frothy_ffi_emit_string(const char *str) {
  for (const char *p = str; *p != '\0'; p++) {
    FROTH_TRY(platform_emit((uint8_t)*p));
  }
  return FROTH_OK;
}

void frothy_ffi_poll(froth_vm_t *vm) { platform_check_interrupt(vm); }

const char *frothy_ffi_format_number(froth_cell_t number) {
  static char buf[32];

  snprintf(buf, sizeof(buf), "%" FROTH_CELL_FORMAT, number);
  return buf;
}

uint32_t frothy_ffi_random_seed(uint32_t seed) {
  return seed == 0 ? FROTHY_FFI_RANDOM_DEFAULT_SEED : seed;
}

uint32_t frothy_ffi_random_next_bits(uint32_t *state) {
  uint32_t value = frothy_ffi_random_seed(state != NULL ? *state : 0);

  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  value = frothy_ffi_random_seed(value);
  if (state != NULL) {
    *state = value;
  }
  return value;
}

int32_t frothy_ffi_random_next_int(uint32_t *state) {
  return (int32_t)(frothy_ffi_random_next_bits(state) & UINT32_C(0x1fffffff));
}

froth_error_t frothy_ffi_random_below(uint32_t *state, uint32_t limit,
                                      uint32_t *out) {
  uint32_t candidate = 0;
  uint32_t threshold = 0;

  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (limit == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  /* Reject the biased tail so modulo stays uniform across the generator's
   * full 32-bit domain. */
  threshold = (uint32_t)(0u - limit) % limit;
  do {
    candidate = frothy_ffi_random_next_bits(state);
  } while (candidate < threshold);

  *out = candidate % limit;
  return FROTH_OK;
}

static bool frothy_ffi_table_present(const frothy_ffi_entry_t *table) {
  return table != NULL && table[0].name != NULL;
}

static void frothy_ffi_store_error_text(char *storage, size_t capacity,
                                        const char **field,
                                        const char *text) {
  if (storage == NULL || capacity == 0 || field == NULL) {
    return;
  }

  storage[0] = '\0';
  *field = NULL;
  if (text == NULL || text[0] == '\0') {
    return;
  }

  strncpy(storage, text, capacity - 1);
  storage[capacity - 1] = '\0';
  *field = storage;
}

void frothy_ffi_clear_last_error(frothy_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

  runtime->last_ffi_error_code = FROTH_OK;
  runtime->last_ffi_error_kind = NULL;
  runtime->last_ffi_error_origin = NULL;
  runtime->last_ffi_error_detail = NULL;
  runtime->last_ffi_error_kind_storage[0] = '\0';
  runtime->last_ffi_error_origin_storage[0] = '\0';
  runtime->last_ffi_error_detail_storage[0] = '\0';
}

void frothy_ffi_get_last_error(const frothy_runtime_t *runtime,
                               frothy_ffi_error_info_t *out) {
  if (out == NULL) {
    return;
  }

  memset(out, 0, sizeof(*out));
  if (runtime == NULL) {
    out->code = FROTH_ERROR_BOUNDS;
    return;
  }

  out->code = runtime->last_ffi_error_code;
  out->kind = runtime->last_ffi_error_kind;
  out->origin = runtime->last_ffi_error_origin;
  out->detail = runtime->last_ffi_error_detail;
}

froth_error_t frothy_ffi_raise(frothy_runtime_t *runtime, froth_error_t code,
                               const char *kind, const char *origin,
                               const char *detail) {
  if (runtime != NULL) {
    runtime->last_ffi_error_code = code;
    frothy_ffi_store_error_text(runtime->last_ffi_error_kind_storage,
                                sizeof(runtime->last_ffi_error_kind_storage),
                                &runtime->last_ffi_error_kind, kind);
    frothy_ffi_store_error_text(runtime->last_ffi_error_origin_storage,
                                sizeof(runtime->last_ffi_error_origin_storage),
                                &runtime->last_ffi_error_origin, origin);
    frothy_ffi_store_error_text(runtime->last_ffi_error_detail_storage,
                                sizeof(runtime->last_ffi_error_detail_storage),
                                &runtime->last_ffi_error_detail, detail);
  }

  return code;
}

froth_error_t frothy_ffi_expect_int(const frothy_value_t *args, size_t arg_index,
                                    int32_t *out) {
  if (args == NULL || out == NULL || !frothy_value_is_int(args[arg_index])) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *out = frothy_value_as_int(args[arg_index]);
  return FROTH_OK;
}

froth_error_t frothy_ffi_expect_bool(const frothy_value_t *args,
                                     size_t arg_index, bool *out) {
  if (args == NULL || out == NULL || !frothy_value_is_bool(args[arg_index])) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *out = frothy_value_as_bool(args[arg_index]);
  return FROTH_OK;
}

froth_error_t frothy_ffi_expect_nil(const frothy_value_t *args,
                                    size_t arg_index) {
  if (args == NULL || !frothy_value_is_nil(args[arg_index])) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  return FROTH_OK;
}

froth_error_t frothy_ffi_expect_text(frothy_runtime_t *runtime,
                                     const frothy_value_t *args,
                                     size_t arg_index, const char **text_out,
                                     size_t *length_out) {
  const char *ignored_text = NULL;
  size_t ignored_length = 0;

  if (runtime == NULL || args == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_runtime_get_text(runtime, args[arg_index],
                                 text_out != NULL ? text_out : &ignored_text,
                                 length_out != NULL ? length_out
                                                    : &ignored_length);
}

froth_error_t frothy_ffi_expect_cells(frothy_runtime_t *runtime,
                                      const frothy_value_t *args,
                                      size_t arg_index, size_t *length_out,
                                      froth_cell_t *base_out) {
  size_t ignored_length = 0;

  if (runtime == NULL || args == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_runtime_get_cells(runtime, args[arg_index],
                                  length_out != NULL ? length_out
                                                     : &ignored_length,
                                  base_out);
}

froth_error_t frothy_ffi_return_int(int32_t value, frothy_value_t *out) {
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_value_make_int(value, out);
}

froth_error_t frothy_ffi_return_bool(bool value, frothy_value_t *out) {
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  *out = frothy_value_make_bool(value);
  return FROTH_OK;
}

froth_error_t frothy_ffi_return_nil(frothy_value_t *out) {
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_ffi_return_text(frothy_runtime_t *runtime, const char *text,
                                     size_t length, frothy_value_t *out) {
  if (runtime == NULL || out == NULL || (text == NULL && length > 0)) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_runtime_alloc_text(runtime, text, length, out);
}

froth_error_t frothy_ffi_return_cells(frothy_runtime_t *runtime, size_t length,
                                      frothy_value_t *out) {
  if (runtime == NULL || out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_runtime_alloc_cells(runtime, length, out);
}

static bool frothy_ffi_value_type_is_valid(frothy_ffi_value_type_t type,
                                           bool allow_void) {
  switch (type) {
  case FROTHY_FFI_VALUE_INT:
  case FROTHY_FFI_VALUE_BOOL:
  case FROTHY_FFI_VALUE_NIL:
  case FROTHY_FFI_VALUE_TEXT:
  case FROTHY_FFI_VALUE_CELLS:
    return true;
  case FROTHY_FFI_VALUE_VOID:
    return allow_void;
  }

  return false;
}

static froth_error_t
frothy_ffi_validate_entry_shape(const frothy_ffi_entry_t *entry) {
  if (entry == NULL || entry->name == NULL || entry->name[0] == '\0' ||
      entry->callback == NULL) {
    return FROTH_ERROR_SIGNATURE;
  }
  if (entry->arity > 0 &&
      (entry->params == NULL || entry->param_count != entry->arity)) {
    return FROTH_ERROR_SIGNATURE;
  }
  if (entry->arity == 0 && entry->param_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }
  if (!frothy_ffi_value_type_is_valid(entry->result_type, true)) {
    return FROTH_ERROR_SIGNATURE;
  }

  return FROTH_OK;
}

static froth_error_t
frothy_ffi_validate_table_entries(const frothy_ffi_entry_t *table,
                                  size_t *count_out) {
  size_t i;

  if (count_out != NULL) {
    *count_out = 0;
  }
  if (table == NULL) {
    return FROTH_OK;
  }

  for (i = 0; table[i].name != NULL; i++) {
    size_t j;

    FROTH_TRY(frothy_ffi_validate_entry_shape(&table[i]));
    for (j = 0; j < i; j++) {
      if (strcmp(table[j].name, table[i].name) == 0) {
        return FROTH_ERROR_SIGNATURE;
      }
    }
  }

  if (count_out != NULL) {
    *count_out = i;
  }
  return FROTH_OK;
}

static froth_error_t frothy_ffi_validate_arg(frothy_runtime_t *runtime,
                                             const frothy_ffi_param_t *param,
                                             const frothy_value_t *args,
                                             size_t arg_index) {
  switch (param->type) {
  case FROTHY_FFI_VALUE_INT:
    return frothy_ffi_expect_int(args, arg_index, &(int32_t){0});
  case FROTHY_FFI_VALUE_BOOL:
    return frothy_ffi_expect_bool(args, arg_index, &(bool){false});
  case FROTHY_FFI_VALUE_NIL:
    return frothy_ffi_expect_nil(args, arg_index);
  case FROTHY_FFI_VALUE_TEXT:
    return frothy_ffi_expect_text(runtime, args, arg_index, NULL, NULL);
  case FROTHY_FFI_VALUE_CELLS:
    return frothy_ffi_expect_cells(runtime, args, arg_index, NULL, NULL);
  case FROTHY_FFI_VALUE_VOID:
    return FROTH_ERROR_SIGNATURE;
  }

  return FROTH_ERROR_SIGNATURE;
}

static froth_error_t frothy_ffi_validate_result(frothy_runtime_t *runtime,
                                                const frothy_ffi_entry_t *entry,
                                                frothy_value_t value) {
  switch (entry->result_type) {
  case FROTHY_FFI_VALUE_VOID:
  case FROTHY_FFI_VALUE_NIL:
    return frothy_value_is_nil(value) ? FROTH_OK : FROTH_ERROR_TYPE_MISMATCH;
  case FROTHY_FFI_VALUE_INT:
    return frothy_value_is_int(value) ? FROTH_OK : FROTH_ERROR_TYPE_MISMATCH;
  case FROTHY_FFI_VALUE_BOOL:
    return frothy_value_is_bool(value) ? FROTH_OK : FROTH_ERROR_TYPE_MISMATCH;
  case FROTHY_FFI_VALUE_TEXT:
    return frothy_ffi_expect_text(runtime, &value, 0, NULL, NULL);
  case FROTHY_FFI_VALUE_CELLS:
    return frothy_ffi_expect_cells(runtime, &value, 0, NULL, NULL);
  }

  return FROTH_ERROR_SIGNATURE;
}

static froth_error_t frothy_ffi_dispatch_entry_common(frothy_runtime_t *runtime,
                                                      const void *context,
                                                      const frothy_value_t *args,
                                                      size_t arg_count,
                                                      frothy_value_t *out) {
  const frothy_ffi_entry_t *entry = (const frothy_ffi_entry_t *)context;
  froth_error_t err = FROTH_OK;
  size_t i;

  if (out == NULL || entry == NULL || entry->callback == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  frothy_ffi_clear_last_error(runtime);
  *out = frothy_value_make_nil();
  FROTH_TRY(frothy_ffi_validate_entry_shape(entry));
  if (arg_count != entry->arity) {
    return FROTH_ERROR_SIGNATURE;
  }

  for (i = 0; i < arg_count; i++) {
    err = frothy_ffi_validate_arg(runtime, &entry->params[i], args, i);
    if (err != FROTH_OK) {
      return err;
    }
  }

  err = entry->callback(runtime, entry->context, args, arg_count, out);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_ffi_validate_result(runtime, entry, *out);
  if (err != FROTH_OK) {
    (void)frothy_value_release(runtime, *out);
    *out = frothy_value_make_nil();
  }
  return err;
}

static froth_error_t frothy_ffi_dispatch_entry(frothy_runtime_t *runtime,
                                               const void *context,
                                               const frothy_value_t *args,
                                               size_t arg_count,
                                               frothy_value_t *out) {
  return frothy_ffi_dispatch_entry_common(runtime, context, args, arg_count,
                                          out);
}

static froth_error_t frothy_ffi_dispatch_entry_board(frothy_runtime_t *runtime,
                                                     const void *context,
                                                     const frothy_value_t *args,
                                                     size_t arg_count,
                                                     frothy_value_t *out) {
  return frothy_ffi_dispatch_entry_common(runtime, context, args, arg_count,
                                          out);
}

static froth_error_t
frothy_ffi_dispatch_entry_project(frothy_runtime_t *runtime, const void *context,
                                  const frothy_value_t *args, size_t arg_count,
                                  frothy_value_t *out) {
  return frothy_ffi_dispatch_entry_common(runtime, context, args, arg_count,
                                          out);
}

bool frothy_ffi_native_is_foreign(frothy_native_fn_t fn, const void *context) {
  (void)context;

  return fn == frothy_ffi_dispatch_entry ||
         fn == frothy_ffi_dispatch_entry_board ||
         fn == frothy_ffi_dispatch_entry_project;
}

const char *frothy_ffi_native_owner(frothy_native_fn_t fn, const void *context) {
  (void)context;

  if (fn == frothy_ffi_dispatch_entry_board) {
    return "board ffi";
  }
  if (fn == frothy_ffi_dispatch_entry_project) {
    return "project ffi";
  }
  if (frothy_ffi_native_is_foreign(fn, context)) {
    return "foreign binding";
  }

  return NULL;
}

const char *frothy_ffi_native_effect(frothy_native_fn_t fn, const void *context) {
  if (fn == frothy_ffi_dispatch_entry || fn == frothy_ffi_dispatch_entry_board ||
      fn == frothy_ffi_dispatch_entry_project) {
    const frothy_ffi_entry_t *entry = (const frothy_ffi_entry_t *)context;

    if (entry == NULL || entry->stack_effect == NULL ||
        entry->stack_effect[0] == '\0') {
      return NULL;
    }

    return entry->stack_effect;
  }
  return NULL;
}

const char *frothy_ffi_native_help(frothy_native_fn_t fn, const void *context) {
  if (fn == frothy_ffi_dispatch_entry || fn == frothy_ffi_dispatch_entry_board ||
      fn == frothy_ffi_dispatch_entry_project) {
    const frothy_ffi_entry_t *entry = (const frothy_ffi_entry_t *)context;

    if (entry == NULL || entry->help == NULL || entry->help[0] == '\0') {
      return NULL;
    }

    return entry->help;
  }
  return NULL;
}

static froth_error_t frothy_ffi_find_slot_for_binding(const char *name,
                                                      froth_cell_u_t *slot_out) {
  return froth_slot_find_name_or_create(&froth_vm.heap, name, slot_out);
}

static froth_error_t frothy_ffi_capture_slot_state(
    froth_cell_u_t slot_index, frothy_ffi_slot_state_t *state) {
  froth_error_t err = FROTH_OK;

  if (state == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(state, 0, sizeof(*state));
  state->overlay = froth_slot_is_overlay(slot_index) ? 1u : 0u;
  FROTH_TRY(
      froth_slot_get_arity(slot_index, &state->in_arity, &state->out_arity));

  err = froth_slot_get_impl(slot_index, &state->impl);
  if (err == FROTH_OK) {
    state->binding_kind = FROTHY_FFI_SLOT_IMPL;
    return FROTH_OK;
  }

  err = froth_slot_get_prim(slot_index, &state->prim);
  if (err == FROTH_OK) {
    state->binding_kind = FROTHY_FFI_SLOT_PRIM;
    return FROTH_OK;
  }

  state->binding_kind = FROTHY_FFI_SLOT_EMPTY;
  return FROTH_OK;
}

static froth_error_t frothy_ffi_restore_slot_state(
    froth_cell_u_t slot_index, const frothy_ffi_slot_state_t *state) {
  if (state == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(froth_slot_clear_binding(slot_index));
  switch (state->binding_kind) {
  case FROTHY_FFI_SLOT_IMPL:
    FROTH_TRY(froth_slot_set_impl(slot_index, state->impl));
    break;
  case FROTHY_FFI_SLOT_PRIM:
    FROTH_TRY(froth_slot_set_prim(slot_index, state->prim));
    break;
  case FROTHY_FFI_SLOT_EMPTY:
    break;
  }

  if (state->in_arity == FROTH_SLOT_ARITY_UNKNOWN ||
      state->out_arity == FROTH_SLOT_ARITY_UNKNOWN) {
    FROTH_TRY(froth_slot_clear_arity(slot_index));
  } else {
    FROTH_TRY(froth_slot_set_arity(slot_index, state->in_arity,
                                   state->out_arity));
  }

  return froth_slot_set_overlay(slot_index, state->overlay);
}

static void
frothy_ffi_release_replaced_slot_impl(const frothy_ffi_slot_state_t *state,
                                      frothy_value_t replacement) {
  if (state == NULL || state->binding_kind != FROTHY_FFI_SLOT_IMPL) {
    return;
  }
  if (state->impl == frothy_value_to_cell(replacement)) {
    return;
  }
  (void)frothy_value_release(&froth_vm.frothy_runtime,
                             frothy_value_from_cell(state->impl));
}

/*
 * On success, ownership of value transfers to the slot table. On failure, the
 * caller still owns value and must release it if it holds a runtime object.
 */
static froth_error_t frothy_ffi_replace_slot_impl(
    froth_cell_u_t slot_index, frothy_value_t value, uint8_t overlay,
    uint8_t in_arity, uint8_t out_arity) {
  frothy_ffi_slot_state_t saved_state;
  froth_error_t err = FROTH_OK;
  froth_error_t rollback_err = FROTH_OK;

  FROTH_TRY(frothy_ffi_capture_slot_state(slot_index, &saved_state));
#ifdef FROTHY_FFI_TESTING
  FROTH_TRY(frothy_ffi_test_maybe_fail_slot_replace());
#endif

  err = froth_slot_clear_binding(slot_index);
  if (err != FROTH_OK) {
    return err;
  }

  err = froth_slot_set_impl(slot_index, frothy_value_to_cell(value));
  if (err != FROTH_OK) {
    goto rollback;
  }
  if (in_arity == FROTH_SLOT_ARITY_UNKNOWN ||
      out_arity == FROTH_SLOT_ARITY_UNKNOWN) {
    err = froth_slot_clear_arity(slot_index);
  } else {
    err = froth_slot_set_arity(slot_index, in_arity, out_arity);
  }
  if (err != FROTH_OK) {
    goto rollback;
  }

  err = froth_slot_set_overlay(slot_index, overlay);
  if (err != FROTH_OK) {
    goto rollback;
  }

  return FROTH_OK;

rollback:
  rollback_err = frothy_ffi_restore_slot_state(slot_index, &saved_state);
  if (rollback_err != FROTH_OK) {
    return rollback_err;
  }
  return err;
}

static void frothy_ffi_release_pending_slot_impls(
    frothy_ffi_pending_slot_impl_t *pending, size_t count) {
  size_t i;

  if (pending == NULL) {
    return;
  }

  for (i = 0; i < count; i++) {
    (void)frothy_value_release(&froth_vm.frothy_runtime, pending[i].value);
    pending[i].value = frothy_value_make_nil();
  }
}

static froth_error_t frothy_ffi_rollback_pending_slot_impls(
    frothy_ffi_pending_slot_impl_t *pending, size_t count) {
  froth_error_t first_err = FROTH_OK;

  while (count > 0) {
    froth_error_t err = FROTH_OK;

    count--;
    err = frothy_ffi_restore_slot_state(pending[count].slot_index,
                                        &pending[count].saved_state);
    if (err == FROTH_OK) {
      (void)frothy_value_release(&froth_vm.frothy_runtime, pending[count].value);
      pending[count].value = frothy_value_make_nil();
    } else if (first_err == FROTH_OK) {
      first_err = err;
    }
  }

  return first_err;
}

static frothy_native_fn_t
frothy_ffi_dispatch_for_source(frothy_ffi_source_t source) {
  switch (source) {
  case FROTHY_FFI_SOURCE_BOARD:
    return frothy_ffi_dispatch_entry_board;
  case FROTHY_FFI_SOURCE_PROJECT:
    return frothy_ffi_dispatch_entry_project;
  case FROTHY_FFI_SOURCE_FOREIGN:
    return frothy_ffi_dispatch_entry;
  }

  return NULL;
}

static froth_error_t frothy_ffi_bind_pin(const frothy_board_pin_t *pin) {
  frothy_value_t value = frothy_value_make_nil();
  froth_cell_u_t slot_index = 0;
  froth_error_t err = FROTH_OK;

  FROTH_TRY(frothy_value_make_int((int32_t)pin->value, &value));
  err = frothy_ffi_find_slot_for_binding(pin->name, &slot_index);
  if (err == FROTH_OK) {
    err = frothy_ffi_replace_slot_impl(slot_index, value, 0,
                                       FROTH_SLOT_ARITY_UNKNOWN,
                                       FROTH_SLOT_ARITY_UNKNOWN);
  }
  if (err != FROTH_OK) {
    (void)frothy_value_release(&froth_vm.frothy_runtime, value);
  }
  return err;
}

static froth_error_t
frothy_ffi_install_table_for_source(const frothy_ffi_entry_t *table,
                                    frothy_ffi_source_t source) {
  frothy_ffi_pending_slot_impl_t *pending = NULL;
  frothy_native_fn_t dispatch = frothy_ffi_dispatch_for_source(source);
  size_t applicable_count = 0;
  size_t staged_count = 0;
  size_t i;
  froth_error_t err = FROTH_OK;

  if (table == NULL) {
    return FROTH_OK;
  }
  if (dispatch == NULL) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_ffi_validate_table_entries(table, &applicable_count));

  if (applicable_count == 0) {
    return FROTH_OK;
  }
  if (applicable_count > SIZE_MAX / sizeof(*pending)) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  pending = (frothy_ffi_pending_slot_impl_t *)calloc(applicable_count,
                                                     sizeof(*pending));
  if (pending == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  for (i = 0; table[i].name != NULL; i++) {
    pending[staged_count].value = frothy_value_make_nil();
    pending[staged_count].in_arity = table[i].arity;
    pending[staged_count].out_arity = 1;
    err = frothy_ffi_find_slot_for_binding(table[i].name,
                                           &pending[staged_count].slot_index);
    if (err != FROTH_OK) {
      goto cleanup;
    }
    err = frothy_ffi_capture_slot_state(pending[staged_count].slot_index,
                                        &pending[staged_count].saved_state);
    if (err != FROTH_OK) {
      goto cleanup;
    }
    err = frothy_runtime_alloc_native(&froth_vm.frothy_runtime, dispatch,
                                      table[i].name, table[i].arity, &table[i],
                                      &pending[staged_count].value);
    if (err != FROTH_OK) {
      goto cleanup;
    }
    staged_count++;
  }

  for (i = 0; i < staged_count; i++) {
    err = frothy_ffi_replace_slot_impl(pending[i].slot_index, pending[i].value, 0,
                                       pending[i].in_arity,
                                       pending[i].out_arity);
    if (err != FROTH_OK) {
      froth_error_t rollback_err =
          frothy_ffi_rollback_pending_slot_impls(pending, i);

      frothy_ffi_release_pending_slot_impls(pending + i, staged_count - i);
      free(pending);
      return rollback_err != FROTH_OK ? rollback_err : err;
    }
  }

  for (i = 0; i < staged_count; i++) {
    frothy_ffi_release_replaced_slot_impl(&pending[i].saved_state,
                                          pending[i].value);
  }
  free(pending);
  return FROTH_OK;

cleanup:
  frothy_ffi_release_pending_slot_impls(pending, staged_count);
  free(pending);
  return err;
}

static froth_error_t
frothy_ffi_install_pin_table_entries(const frothy_board_pin_t *pins) {
  size_t i;

  if (pins == NULL) {
    return FROTH_OK;
  }

  for (i = 0; pins[i].name != NULL; i++) {
    FROTH_TRY(frothy_ffi_bind_pin(&pins[i]));
  }

  return FROTH_OK;
}

froth_error_t frothy_ffi_install_table(const frothy_ffi_entry_t *table) {
  return frothy_ffi_install_table_for_source(table, FROTHY_FFI_SOURCE_FOREIGN);
}

froth_error_t frothy_ffi_install_pin_table(const frothy_board_pin_t *pins) {
  return frothy_ffi_install_pin_table_entries(pins);
}

static froth_error_t frothy_ffi_install_project_bindings(void) {
#ifdef FROTH_HAS_PROJECT_FFI
#if FROTHY_HAS_WEAK_SYMBOLS
  if (frothy_ffi_table_present(frothy_project_bindings)) {
    return frothy_ffi_install_table_for_source(frothy_project_bindings,
                                               FROTHY_FFI_SOURCE_PROJECT);
  }

  return frothy_ffi_raise(&froth_vm.frothy_runtime, FROTH_ERROR_UNDEFINED_WORD,
                          "project-ffi", "frothy_project_bindings",
                          "missing project FFI export");
#else
  extern const frothy_ffi_entry_t frothy_project_bindings[];
  return frothy_ffi_install_table_for_source(frothy_project_bindings,
                                             FROTHY_FFI_SOURCE_PROJECT);
#endif
#else
  return FROTH_OK;
#endif
}

froth_error_t frothy_ffi_install_board_base_slots(void) {
#ifdef FROTHY_HAS_BOARD_PINS
  FROTH_TRY(frothy_ffi_install_pin_table_entries(frothy_generated_board_pins));
#endif
#if FROTHY_HAS_WEAK_SYMBOLS
  if (frothy_ffi_table_present(frothy_board_bindings)) {
    FROTH_TRY(frothy_ffi_install_table_for_source(frothy_board_bindings,
                                                  FROTHY_FFI_SOURCE_BOARD));
  }
#else
  extern const frothy_ffi_entry_t frothy_board_bindings[];
  FROTH_TRY(frothy_ffi_install_table_for_source(frothy_board_bindings,
                                                FROTHY_FFI_SOURCE_BOARD));
#endif
  return frothy_ffi_install_project_bindings();
}
