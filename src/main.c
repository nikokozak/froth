#include "froth_types.h"
#include "froth_stack.h"
#include "froth_heap.h"
#include "froth_slot_table.h"
#include "froth_evaluator.h"
#include <stdio.h>

int main() {

  froth_evaluate_input("1 2", &froth_ds_stack, &froth_heap);
  // Verify: should print depth 2, values 1 and 2
  printf("Stack depth: %" FROTH_CELL_U_FORMAT "\n", froth_stack_depth(&froth_ds_stack));
  for (int i = 0; i < froth_stack_depth(&froth_ds_stack); i++) {
    froth_cell_t cell = froth_ds_stack.data[i];
    printf("  [%d]: %" FROTH_CELL_FORMAT "\n", i, FROTH_STRIP_CELL_TAG(cell));
  }
  return 0;
}
