set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

set(FROTH_KERNEL_SRCS
    ${FROTH_ROOT}/src/froth_boot.c
    ${FROTH_ROOT}/src/froth_stack.c
    ${FROTH_ROOT}/src/froth_heap.c
    ${FROTH_ROOT}/src/froth_slot_table.c
    ${FROTH_ROOT}/src/froth_reader.c
    ${FROTH_ROOT}/src/froth_evaluator.c
    ${FROTH_ROOT}/src/froth_repl.c
    ${FROTH_ROOT}/src/froth_vm.c
    ${FROTH_ROOT}/src/froth_executor.c
    ${FROTH_ROOT}/src/froth_primitives.c
    ${FROTH_ROOT}/src/froth_fmt.c
    ${FROTH_ROOT}/src/froth_ffi.c
    ${FROTH_ROOT}/src/froth_snapshot.c
    ${FROTH_ROOT}/src/froth_snapshot_reader.c
    ${FROTH_ROOT}/src/froth_snapshot_writer.c
    ${FROTH_ROOT}/src/froth_crc32.c
    ${FROTH_ROOT}/src/froth_snapshot_prims.c
    ${FROTH_ROOT}/src/froth_transport.c
    ${FROTH_ROOT}/src/froth_link.c
    ${FROTH_ROOT}/src/froth_console.c
)

idf_component_register(
    SRCS
        "main.c"
        ${FROTH_KERNEL_SRCS}
        "${FROTH_ROOT}/platforms/esp-idf/platform.c"
        "${FROTH_ROOT}/boards/${FROTH_BOARD}/ffi.c"
    INCLUDE_DIRS
        "${FROTH_ROOT}/src"
        "${FROTH_ROOT}/boards/${FROTH_BOARD}"
    REQUIRES driver nvs_flash
)

# Froth compile-time configuration
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FROTH_CELL_SIZE_BITS=32
    FROTH_DS_CAPACITY=256
    FROTH_RS_CAPACITY=256
    FROTH_CS_CAPACITY=256
    FROTH_HEAP_SIZE=4096
    FROTH_SLOT_TABLE_SIZE=128
    FROTH_LINE_BUFFER_SIZE=1024
    FROTH_MAX_PERM_SIZE=8
    FROTH_VERSION="0.1.0"
    FROTH_BOARD_NAME="${FROTH_BOARD}"
    FROTH_HAS_SNAPSHOTS=1
    FROTH_SNAPSHOT_BLOCK_SIZE=2048
    FROTH_HAS_LIVE=1
    FROTH_STRING_MAX_LEN=256
)

# Embed stdlib as C header
set(FROTH_LIB_CORE_H "${CMAKE_BINARY_DIR}/froth_lib_core.h")
add_custom_command(
    OUTPUT ${FROTH_LIB_CORE_H}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${FROTH_ROOT}/src/lib/core.froth
        -DOUTPUT=${FROTH_LIB_CORE_H}
        -DVARNAME="froth_lib_core"
        -P ${FROTH_ROOT}/cmake/embed_froth.cmake
    DEPENDS ${FROTH_ROOT}/src/lib/core.froth
    COMMENT "Embedding core.froth as C data..."
)
add_custom_target(froth_lib_core DEPENDS ${FROTH_LIB_CORE_H})
add_dependencies(${COMPONENT_LIB} froth_lib_core)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
