#include "froth_vm.h"
#include "froth_repl.h"
#include "froth_evaluator.h"
#include "froth_primitives.h"
#include "froth_lib_core.h"

int main() {
  froth_primitives_register(&froth_vm);
  froth_evaluate_input(froth_lib_core, &froth_vm);
  froth_repl_start(&froth_vm);
  return 0;
}
