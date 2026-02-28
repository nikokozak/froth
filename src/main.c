#include "froth_types.h"
#include "froth_stack.h"
#include "froth_heap.h"
#include "froth_slot_table.h"
#include <stdio.h>

int main() {

  // Hand encoding "[ 1 2 ]"

  froth_cell_t quote_ref_cell;
  froth_cell_u_t quote_ref_raw;
  froth_heap_allocate_cells((froth_cell_u_t)3, &froth_heap, &quote_ref_raw);

  froth_cell_t* quote_body = froth_heap_cell_ptr(&froth_heap, quote_ref_raw);

  quote_body[0] = 2; // length of quote body
  froth_make_cell(1, FROTH_NUMBER, &quote_body[1]);
  froth_make_cell(2, FROTH_NUMBER, &quote_body[2]);
  froth_make_cell((froth_cell_t)quote_ref_raw, FROTH_QUOTE, &quote_ref_cell);

  // Push the QuoteRef onto the data stack
  froth_stack_push(&froth_ds_stack, quote_ref_cell);

  // Execute the quotation: pop QuoteRef, walk body, push literals
  froth_cell_t quote_ref_from_stack;

  froth_stack_pop(&froth_ds_stack, &quote_ref_from_stack);
  if (FROTH_GET_CELL_TAG(quote_ref_from_stack) == FROTH_QUOTE) {
    froth_cell_t* quote_start = froth_heap_cell_ptr(&froth_heap, FROTH_STRIP_CELL_TAG(quote_ref_from_stack));
    froth_cell_t quote_len = quote_start[0];

    for (int i = 0; i < quote_len; i++) {
      froth_stack_push(&froth_ds_stack, quote_start[i + 1]);
    }
  }

  // Verify: should print depth 2, values 1 and 2
  printf("Stack depth: %" FROTH_CELL_U_FORMAT "\n", froth_stack_depth(&froth_ds_stack));
  for (int i = 0; i < froth_stack_depth(&froth_ds_stack); i++) {
    froth_cell_t cell = froth_ds_stack.data[i];
    printf("  [%d]: %" FROTH_CELL_FORMAT "\n", i, FROTH_STRIP_CELL_TAG(cell));
  }

  return 0;
}
