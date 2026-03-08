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
    src/froth_evaluator.c
    src/froth_repl.c
    src/froth_vm.c
    src/froth_executor.c
    src/froth_primitives.c
    src/froth_fmt.c
    src/froth_ffi.c
    ${CMAKE_BINARY_DIR}/froth_lib_core.h
    boards/posix/ffi_posix.c
)

target_include_directories(Froth PRIVATE src ${CMAKE_BINARY_DIR} boards/posix)

set_property(TARGET Froth PROPERTY C_STANDARD 11)
set_property(TARGET Froth PROPERTY C_STANDARD_REQUIRED ON)

set(FROTH_CELL_SIZE_BITS 32 CACHE STRING "Cell width in bits: 8, 16, 32 or 64")
set(FROTH_DS_CAPACITY 256 CACHE STRING "Data Stack (DS) Capacity in number of cells.")
set(FROTH_RS_CAPACITY 256 CACHE STRING "Return Stack (RS) Capacity in number of cells.")
set(FROTH_CS_CAPACITY 256 CACHE STRING "Control Stack (CS) Capacity in number of cells.")
set(FROTH_HEAP_SIZE 4096 CACHE STRING "Heap size in number of bytes.")
set(FROTH_SLOT_TABLE_SIZE 128 CACHE STRING "Maximum number of slots.")
set(FROTH_LINE_BUFFER_SIZE 1024 CACHE STRING "Input line buffer for REPL in number of bytes.")
set(FROTH_MAX_PERM_SIZE 8 CACHE STRING "Maximum number of indices in a perm pattern.")
set(FROTH_VERSION "0.1.0" CACHE STRING "Froth version string")

target_compile_definitions(Froth PRIVATE FROTH_CELL_SIZE_BITS=${FROTH_CELL_SIZE_BITS})
target_compile_definitions(Froth PRIVATE FROTH_DS_CAPACITY=${FROTH_DS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_RS_CAPACITY=${FROTH_RS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_CS_CAPACITY=${FROTH_CS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_HEAP_SIZE=${FROTH_HEAP_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_SLOT_TABLE_SIZE=${FROTH_SLOT_TABLE_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_LINE_BUFFER_SIZE=${FROTH_LINE_BUFFER_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_MAX_PERM_SIZE=${FROTH_MAX_PERM_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_VERSION="${FROTH_VERSION}")

# Used to compile .froth lib files so that the c process can read them as embedded data.
add_custom_command(
  COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/src/lib/core.froth -DOUTPUT=${CMAKE_BINARY_DIR}/froth_lib_core.h -DVARNAME="froth_lib_core" -P ${CMAKE_SOURCE_DIR}/cmake/embed_froth.cmake
  OUTPUT ${CMAKE_BINARY_DIR}/froth_lib_core.h
  DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/core.froth"
  COMMENT "Embedding .froth files as C data..."
)
