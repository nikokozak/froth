#pragma once

#include "froth_types.h"
#include "froth_stack.h"
#include "froth_heap.h"

froth_error_t froth_evaluate_input(char* input, froth_stack_t* froth_ds_stack, froth_heap_t* froth_heap);
