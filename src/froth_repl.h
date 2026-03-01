#pragma once
#include "froth_types.h"
#include "froth_stack.h"
#include "froth_heap.h"
#include "froth_evaluator.h"
#include "platform.h"

#ifndef FROTH_LINE_BUFFER_SIZE
  #error "FROTH_LINE_BUFFER_SIZE is not defined. Please define it to the desired size of the input buffer for the REPL."
#endif

froth_error_t froth_repl_start(void);
froth_error_t froth_repl_read_line(char* output_buffer);
froth_error_t froth_repl_print_promt(void);
froth_error_t froth_repl_print_stack(froth_stack_t* stack, froth_cell_u_t depth);

