cmake_minimum_required(VERSION 3.23)

project(Froth)

add_executable(Froth)

target_sources(Froth
  PRIVATE
    src/main.c
    src/froth_stack.c
    src/platform_posix.c
    src/froth_heap.c
    src/froth_slot_table.c
    src/froth_reader.c
)

set_property(TARGET Froth PROPERTY C_STANDARD 11)
set_property(TARGET Froth PROPERTY C_STANDARD_REQUIRED ON)

set(FROTH_CELL_SIZE_BITS 32 CACHE STRING "Cell width in bits: 8, 16, 32 or 64")
set(FROTH_DS_CAPACITY 256 CACHE STRING "Data Stack (DS) Capacity in number of cells.")
set(FROTH_RS_CAPACITY 256 CACHE STRING "Return Stack (RS) Capacity in number of cells.")
set(FROTH_CS_CAPACITY 256 CACHE STRING "Control Stack (CS) Capacity in number of cells.")
set(FROTH_HEAP_SIZE 4096 CACHE STRING "Heap size in number of bytes.")
set(FROTH_SLOT_TABLE_SIZE 128 CACHE STRING "Maximum number of slots.")

target_compile_definitions(Froth PRIVATE FROTH_CELL_SIZE_BITS=${FROTH_CELL_SIZE_BITS})
target_compile_definitions(Froth PRIVATE FROTH_DS_CAPACITY=${FROTH_DS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_RS_CAPACITY=${FROTH_RS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_CS_CAPACITY=${FROTH_CS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_HEAP_SIZE=${FROTH_HEAP_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_SLOT_TABLE_SIZE=${FROTH_SLOT_TABLE_SIZE})
