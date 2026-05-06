function(frothy_runtime_sources out_product out_substrate out_support source_root
         board_sources_var)
  set(board_sources ${${board_sources_var}})

  set(product_sources
    ${source_root}/src/frothy_boot.c
    ${source_root}/src/frothy_base_image.c
    ${source_root}/src/frothy_control.c
    ${source_root}/src/frothy_shell.c
    ${source_root}/src/frothy_ffi.c
    ${source_root}/src/frothy_inspect.c
    ${source_root}/src/frothy_ir.c
    ${source_root}/src/frothy_parser.c
    ${source_root}/src/frothy_snapshot.c
    ${source_root}/src/frothy_snapshot_codec.c
    ${source_root}/src/frothy_value.c
    ${source_root}/src/frothy_eval.c
    ${board_sources}
  )

  set(substrate_sources
    ${source_root}/src/froth_vm.c
    ${source_root}/src/froth_heap.c
    ${source_root}/src/froth_cellspace.c
    ${source_root}/src/froth_slot_table.c
    ${source_root}/src/froth_crc32.c
  )

  set(support_sources
    ${source_root}/src/frothy_ffi.c
    ${board_sources}
  )

  set(${out_product} "${product_sources}" PARENT_SCOPE)
  set(${out_substrate} "${substrate_sources}" PARENT_SCOPE)
  set(${out_support} "${support_sources}" PARENT_SCOPE)
endfunction()
