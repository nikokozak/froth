#include "froth_ffi.h"

static const froth_ffi_param_t project_echo_int_params[] = {
    FROTH_FFI_PARAM_INT("value"),
};

static froth_error_t project_echo_int(froth_runtime_t *runtime,
                                      const void *context,
                                      const froth_value_t *args,
                                      size_t arg_count, froth_value_t *out) {
  int32_t value = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;
  FROTH_TRY(froth_ffi_expect_int(args, 0, &value));
  return froth_ffi_return_int(value, out);
}

const froth_ffi_entry_t froth_project_bindings[] = {
    {
        .name = "project.echo.int",
        .params = project_echo_int_params,
        .param_count = FROTH_FFI_PARAM_COUNT(project_echo_int_params),
        .arity = 1,
        .result_type = FROTH_FFI_VALUE_INT,
        .help = "Project-maintained FFI test binding.",
        .flags = FROTH_FFI_FLAG_NONE,
        .callback = project_echo_int,
        .stack_effect = "( value -- value )",
    },
    {0},
};
