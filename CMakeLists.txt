cmake_minimum_required(VERSION 3.23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Froth)

add_executable(Froth)

# Board and platform selection
set(FROTH_BOARD "posix" CACHE STRING "Target board (directory name under boards/)")
set(FROTH_PLATFORM "posix" CACHE STRING "Target platform (directory name under platforms/)")

target_sources(Froth
  PRIVATE
    src/main.c
    src/froth_boot.c
    src/froth_stack.c
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
    src/froth_snapshot.c
    src/froth_snapshot_reader.c
    src/froth_snapshot_writer.c
    src/froth_crc32.c
    src/froth_snapshot_prims.c
    src/froth_console_mux.c
    src/froth_transport.c
    src/froth_link.c
    src/froth_tbuf.c
    ${CMAKE_BINARY_DIR}/froth_lib_core.h
    platforms/${FROTH_PLATFORM}/platform.c
    boards/${FROTH_BOARD}/ffi.c
)

target_include_directories(Froth PRIVATE
  src
  ${CMAKE_BINARY_DIR}
  boards/${FROTH_BOARD}
  platforms/${FROTH_PLATFORM}
)

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
set(FROTH_HAS_SNAPSHOTS ON CACHE BOOL "Target supports Froth snapshots")
set(FROTH_HAS_LINK ON CACHE BOOL "Enable FROTH-LINK/1 binary transport")
set(FROTH_SNAPSHOT_BLOCK_SIZE "2048" CACHE STRING "Snapshot size in target memory (bytes)")
set(FROTH_SNAPSHOT_PATH_A "froth_a.snap" CACHE STRING "Snapshot A file path (default froth_a.snap)")
set(FROTH_SNAPSHOT_PATH_B "froth_b.snap" CACHE STRING "Snapshot B file path (default froth_b.snap)")
set(FROTH_TBUF_SIZE 1024 CACHE STRING "Transient string scratch ring size in bytes.")
set(FROTH_TDESC_MAX 32 CACHE STRING "Maximum concurrent transient string descriptors.")
set(FROTH_FFI_MAX_TABLES 8 CACHE STRING "Maximum number of FFI binding tables.")
set(FROTH_USER_PROGRAM "" CACHE STRING "User .froth program for flashing.")

target_compile_definitions(Froth PRIVATE FROTH_CELL_SIZE_BITS=${FROTH_CELL_SIZE_BITS})
target_compile_definitions(Froth PRIVATE FROTH_DS_CAPACITY=${FROTH_DS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_RS_CAPACITY=${FROTH_RS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_CS_CAPACITY=${FROTH_CS_CAPACITY})
target_compile_definitions(Froth PRIVATE FROTH_HEAP_SIZE=${FROTH_HEAP_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_SLOT_TABLE_SIZE=${FROTH_SLOT_TABLE_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_LINE_BUFFER_SIZE=${FROTH_LINE_BUFFER_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_MAX_PERM_SIZE=${FROTH_MAX_PERM_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_VERSION="${FROTH_VERSION}")
target_compile_definitions(Froth PRIVATE FROTH_BOARD_NAME="${FROTH_BOARD}")
target_compile_definitions(Froth PRIVATE FROTH_TBUF_SIZE=${FROTH_TBUF_SIZE})
target_compile_definitions(Froth PRIVATE FROTH_TDESC_MAX=${FROTH_TDESC_MAX})
target_compile_definitions(Froth PRIVATE FROTH_FFI_MAX_TABLES=${FROTH_FFI_MAX_TABLES})
if(FROTH_HAS_SNAPSHOTS)
  target_compile_definitions(Froth PRIVATE FROTH_HAS_SNAPSHOTS)
  target_compile_definitions(Froth PRIVATE FROTH_SNAPSHOT_BLOCK_SIZE=${FROTH_SNAPSHOT_BLOCK_SIZE})
  target_compile_definitions(Froth PRIVATE FROTH_SNAPSHOT_PATH_A="${FROTH_SNAPSHOT_PATH_A}")
  target_compile_definitions(Froth PRIVATE FROTH_SNAPSHOT_PATH_B="${FROTH_SNAPSHOT_PATH_B}")
endif()
if(${FROTH_HAS_LINK})
  target_compile_definitions(Froth PRIVATE FROTH_HAS_LINK)
endif()
if(FROTH_USER_PROGRAM)
  target_sources(Froth PRIVATE ${CMAKE_BINARY_DIR}/froth_user_program.h)
  target_compile_definitions(Froth PRIVATE FROTH_HAS_USER_PROGRAM)
  target_compile_definitions(Froth PRIVATE FROTH_USER_PROGRAM="${FROTH_USER_PROGRAM}")
  # Used to compile .froth user files so that the c process can read them as embedded data.
  add_custom_command(
    COMMAND ${CMAKE_COMMAND} -DINPUT=${FROTH_USER_PROGRAM} -DOUTPUT=${CMAKE_BINARY_DIR}/froth_user_program.h -DVARNAME="froth_user_program" -P ${CMAKE_SOURCE_DIR}/cmake/embed_froth.cmake
    OUTPUT ${CMAKE_BINARY_DIR}/froth_user_program.h
    DEPENDS "${FROTH_USER_PROGRAM}"
    COMMENT "Embedding user .froth file as C data..."
  )
endif()

# Used to compile .froth lib files so that the c process can read them as embedded data.
add_custom_command(
  COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/src/lib/core.froth -DOUTPUT=${CMAKE_BINARY_DIR}/froth_lib_core.h -DVARNAME="froth_lib_core" -P ${CMAKE_SOURCE_DIR}/cmake/embed_froth.cmake
  OUTPUT ${CMAKE_BINARY_DIR}/froth_lib_core.h
  DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/core.froth"
  COMMENT "Embedding .froth files as C data..."
)

