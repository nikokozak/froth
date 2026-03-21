#ifdef FROTH_HAS_SNAPSHOTS

#include "froth_crc32.h"
#include "froth_ffi.h"
#include "froth_primitives.h"
#include "froth_slot_table.h"
#include "froth_snapshot.h"
#include "froth_types.h"
#include "froth_vm.h"
#include "platform.h"
#include <string.h>

/* Static workspace for save/restore. Lives in BSS, not on the call stack.
 * ESP32 main task stack is ~3.5KB; the old stack-allocated tables used ~2.8KB.
 * This struct uses ~2.8KB of BSS on targets with FROTH_HAS_SNAPSHOTS. */
static froth_snapshot_workspace_t ws;

/* ---- save ---- ( -- ) */
static froth_error_t prim_save(froth_vm_t *vm) {
  froth_snapshot_buffer_t ram_snapshot = {.data = ws.ram_buffer, .position = 0};

  FROTH_TRY(froth_snapshot_save(vm, &ram_snapshot, &ws));
  uint8_t slot;
  uint32_t generation;
  FROTH_TRY(froth_snapshot_pick_inactive(&slot, &generation));

  if ((FROTH_SNAPSHOT_HEADER_SIZE + ram_snapshot.position) >
      FROTH_SNAPSHOT_BLOCK_SIZE) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  FROTH_TRY(platform_snapshot_write(slot, FROTH_SNAPSHOT_HEADER_SIZE,
                                    ram_snapshot.data, ram_snapshot.position));
  FROTH_TRY(froth_snapshot_build_header(ws.header, ram_snapshot.position,
                                        ram_snapshot.data, generation));
  FROTH_TRY(
      platform_snapshot_write(slot, 0, ws.header, FROTH_SNAPSHOT_HEADER_SIZE));

  return FROTH_OK;
}

/* ---- restore ---- ( -- ) */
static froth_error_t prim_restore(froth_vm_t *vm) {
  uint8_t slot;
  uint32_t generation;
  FROTH_TRY(froth_snapshot_pick_active(&slot, &generation));

  FROTH_TRY(
      platform_snapshot_read(slot, 0, ws.header, FROTH_SNAPSHOT_HEADER_SIZE));

  froth_snapshot_header_info_t info;
  FROTH_TRY(froth_snapshot_parse_header(ws.header, &info));

  if (info.payload_len > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  FROTH_TRY(platform_snapshot_read(slot, FROTH_SNAPSHOT_HEADER_SIZE,
                                   ws.ram_buffer, info.payload_len));

  uint32_t stored_crc =
      ((uint32_t)ws.header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET]) |
      ((uint32_t)ws.header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 1] << 8) |
      ((uint32_t)ws.header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 2] << 16) |
      ((uint32_t)ws.header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 3] << 24);
  if (froth_crc32(ws.ram_buffer, info.payload_len) != stored_crc) {
    return FROTH_ERROR_SNAPSHOT_BAD_CRC;
  }

  froth_snapshot_buffer_t ram_snapshot = {.data = ws.ram_buffer,
                                          .position = info.payload_len};
  return froth_snapshot_load(vm, &ram_snapshot, &ws);
}

/* ---- wipe ---- ( -- )
 *
 * 1. Erase slot A via platform
 * 2. Erase slot B via platform
 * 3. Clear overlay flags on all slots in the slot table
 * 4. Reset heap pointer to watermark (base-only state)
 */
static froth_error_t prim_wipe(froth_vm_t *vm) {
  FROTH_TRY(platform_snapshot_erase(0));
  FROTH_TRY(platform_snapshot_erase(1));
  // froth_slot_reset_overlay();
  // vm->heap.pointer = vm->watermark_heap_offset;
  FROTH_TRY(froth_prim_dangerous_reset(vm));
  return FROTH_OK;
}

/* FFI registration table */
FROTH_FFI(prim_save, "save", "( -- )", "persist overlay to snapshot storage");
FROTH_FFI(prim_restore, "restore", "( -- )",
          "restore overlay from snapshot storage");
FROTH_FFI(prim_wipe, "wipe", "( -- )",
          "erase snapshots and reset to base state");

const froth_ffi_entry_t froth_snapshot_prims[] = {
    FROTH_BIND(prim_save),
    FROTH_BIND(prim_restore),
    FROTH_BIND(prim_wipe),
    {0},
};

#endif /* FROTH_HAS_SNAPSHOTS */
