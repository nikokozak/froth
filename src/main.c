#include "froth_vm.h"
#include "froth_repl.h"
#include "froth_primitives.h"

int main() {
  froth_primitives_register(&froth_vm);
  froth_repl_start(&froth_vm);
  return 0;
}
