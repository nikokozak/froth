#pragma once

#include "frothy_ffi.h"

/*
 * Public Froth-facing aliases for the project FFI surface.
 *
 * The maintained implementation may keep frothy_* internals, but project
 * authors should include this header and use the froth_* / FROTH_* names.
 */
typedef frothy_board_pin_t froth_board_pin_t;
typedef frothy_runtime_t froth_runtime_t;
typedef frothy_value_t froth_value_t;
typedef frothy_native_fn_t froth_native_fn_t;
typedef frothy_ffi_value_type_t froth_ffi_value_type_t;
typedef frothy_ffi_param_t froth_ffi_param_t;
typedef frothy_ffi_error_info_t froth_ffi_error_info_t;
typedef frothy_ffi_entry_t froth_ffi_entry_t;

#define FROTH_FFI_VALUE_VOID FROTHY_FFI_VALUE_VOID
#define FROTH_FFI_VALUE_INT FROTHY_FFI_VALUE_INT
#define FROTH_FFI_VALUE_BOOL FROTHY_FFI_VALUE_BOOL
#define FROTH_FFI_VALUE_NIL FROTHY_FFI_VALUE_NIL
#define FROTH_FFI_VALUE_TEXT FROTHY_FFI_VALUE_TEXT
#define FROTH_FFI_VALUE_CELLS FROTHY_FFI_VALUE_CELLS

#define FROTH_FFI_FLAG_NONE FROTHY_FFI_FLAG_NONE

#define FROTH_FFI_PARAM_INT(name) FROTHY_FFI_PARAM_INT(name)
#define FROTH_FFI_PARAM_BOOL(name) FROTHY_FFI_PARAM_BOOL(name)
#define FROTH_FFI_PARAM_NIL(name) FROTHY_FFI_PARAM_NIL(name)
#define FROTH_FFI_PARAM_TEXT(name) FROTHY_FFI_PARAM_TEXT(name)
#define FROTH_FFI_PARAM_CELLS(name) FROTHY_FFI_PARAM_CELLS(name)
#define FROTH_FFI_PARAM_COUNT(params) FROTHY_FFI_PARAM_COUNT(params)

#define FROTH_FFI_DECLARE(name) extern const froth_ffi_entry_t name[]
#define FROTH_FFI_TABLE_BEGIN(name) const froth_ffi_entry_t name[] = {
#define FROTH_FFI_TABLE_END FROTHY_FFI_TABLE_END

#define froth_ffi_expect_int frothy_ffi_expect_int
#define froth_ffi_expect_bool frothy_ffi_expect_bool
#define froth_ffi_expect_nil frothy_ffi_expect_nil
#define froth_ffi_expect_text frothy_ffi_expect_text
#define froth_ffi_expect_cells frothy_ffi_expect_cells

#define froth_ffi_return_int frothy_ffi_return_int
#define froth_ffi_return_bool frothy_ffi_return_bool
#define froth_ffi_return_nil frothy_ffi_return_nil
#define froth_ffi_return_text frothy_ffi_return_text
#define froth_ffi_return_cells frothy_ffi_return_cells

#define froth_ffi_raise frothy_ffi_raise
#define froth_ffi_clear_last_error frothy_ffi_clear_last_error
#define froth_ffi_get_last_error frothy_ffi_get_last_error
