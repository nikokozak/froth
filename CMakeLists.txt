cmake_minimum_required(VERSION 3.23)

project(Froth)

add_executable(Froth)

target_sources(Froth
  PRIVATE
    src/main.c
    src/froth_stack.c
    src/platform_posix.c
)

set_property(TARGET Froth PROPERTY C_STANDARD 11)
set_property(TARGET Froth PROPERTY C_STANDARD_REQUIRED ON)

set(FROTH_CELL_SIZE_BITS 32 CACHE STRING "Cell width in bits: 8, 16, 32 or 64")
set(FROTH_DS_CAPACITY 256 CACHE STRING "Data Stack (DS) Capacity in number of cells.")
set(FROTH_RS_CAPACITY 256 CACHE STRING "Return Stack (RS) Capacity in number of cells.")
set(FROTH_CS_CAPACITY 256 CACHE STRING "Control Stack (CS) Capacity in number of cells.")

target_compile_definitions(Froth PRIVATE FROTH_CELL_SIZE_BITS=${FROTH_CELL_SIZE_BITS})
target_compile_definitions(Froth PRIVATE FROTH_DS_CAPACITY=${FROTH_DS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_RS_CAPACITY=${FROTH_RS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_CS_CAPACITY=${FROTH_CS_CAPACITY})
