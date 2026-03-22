#include "froth_link.h"
#include "froth_console.h"
#include "froth_evaluator.h"
#include "froth_primitives.h"
#include "froth_slot_table.h"
#include "froth_transport.h"
#include "froth_vm.h"
#include <stdio.h>
#include <string.h>

/* ── Payload builder helpers ─────────────────────────────────────── */

typedef struct {
  uint8_t *buf;
  uint16_t cap;
  uint16_t pos;
} payload_writer_t;

static froth_error_t pw_u8(payload_writer_t *pw, uint8_t v) {
  if (pw->pos + 1 > pw->cap)
    return FROTH_ERROR_LINK_OVERFLOW;
  pw->buf[pw->pos++] = v;
  return FROTH_OK;
}

static froth_error_t pw_u16(payload_writer_t *pw, uint16_t v) {
  if (pw->pos + 2 > pw->cap)
    return FROTH_ERROR_LINK_OVERFLOW;
  pw->buf[pw->pos++] = v & 0xFF;
  pw->buf[pw->pos++] = (v >> 8) & 0xFF;
  return FROTH_OK;
}

static froth_error_t pw_u32(payload_writer_t *pw, uint32_t v) {
  if (pw->pos + 4 > pw->cap)
    return FROTH_ERROR_LINK_OVERFLOW;
  pw->buf[pw->pos++] = v & 0xFF;
  pw->buf[pw->pos++] = (v >> 8) & 0xFF;
  pw->buf[pw->pos++] = (v >> 16) & 0xFF;
  pw->buf[pw->pos++] = (v >> 24) & 0xFF;
  return FROTH_OK;
}

static froth_error_t pw_str(payload_writer_t *pw, const char *s) {
  uint16_t len = (uint16_t)strlen(s);
  FROTH_TRY(pw_u16(pw, len));
  if (pw->pos + len > pw->cap)
    return FROTH_ERROR_LINK_OVERFLOW;
  memcpy(pw->buf + pw->pos, s, len);
  pw->pos += len;
  return FROTH_OK;
}

/* ── Stack formatting ─────────────────────────────────────────────── */

/* Format the data stack into buf as "[1 2 3]". Returns bytes written
   (not including the null terminator). Truncates if buf is too small. */
static int format_stack(froth_vm_t *vm, char *buf, int cap) {
  int pos = 0;
  froth_cell_u_t depth = froth_stack_depth(&vm->ds);

  if (pos < cap)
    buf[pos++] = '[';

  for (froth_cell_u_t i = 0; i < depth; i++) {
    if (i > 0 && pos < cap)
      buf[pos++] = ' ';

    froth_cell_t cell = vm->ds.data[i];
    froth_cell_t tag = FROTH_CELL_GET_TAG(cell);
    froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);
    int wrote = 0;

    switch (tag) {
    case FROTH_NUMBER:
      wrote = snprintf(buf + pos, cap - pos, "%" FROTH_CELL_FORMAT, payload);
      break;
    case FROTH_SLOT: {
      const char *name;
      if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK)
        wrote = snprintf(buf + pos, cap - pos, "<s:%s>", name);
      else
        wrote = snprintf(buf + pos, cap - pos, "<s:%d>", (int)payload);
      break;
    }
    case FROTH_QUOTE:
      wrote = snprintf(buf + pos, cap - pos, "<q>");
      break;
    case FROTH_PATTERN:
      wrote = snprintf(buf + pos, cap - pos, "<p>");
      break;
    case FROTH_BSTRING:
      wrote = snprintf(buf + pos, cap - pos, "<str>");
      break;
    default:
      wrote = snprintf(buf + pos, cap - pos, "<%d>", (int)tag);
      break;
    }
    if (wrote > 0 && pos + wrote < cap)
      pos += wrote;
    else if (wrote > 0) {
      pos = cap - 3;
      break;
    } /* room for ] \0 */
  }

  if (pos < 0)
    pos = 0;
  if (pos < cap - 1)
    buf[pos++] = ']';
  buf[pos] = '\0';

  return pos;
}

/* ── Response buffer (shared across handlers) ────────────────────── */

static uint8_t resp_buf[FROTH_LINK_MAX_PAYLOAD];

/* ── HELLO ───────────────────────────────────────────────────────── */

froth_error_t froth_link_send_hello_res(froth_vm_t *vm, uint64_t session_id,
                                        uint16_t seq) {
  payload_writer_t pw = {resp_buf, sizeof(resp_buf), 0};

  FROTH_TRY(pw_u8(&pw, FROTH_CELL_SIZE_BITS));
  FROTH_TRY(pw_u16(&pw, FROTH_LINK_MAX_PAYLOAD));
  FROTH_TRY(pw_u32(&pw, FROTH_HEAP_SIZE));
  FROTH_TRY(pw_u32(&pw, vm->heap.pointer));
  FROTH_TRY(pw_u16(&pw, froth_slot_count()));
  FROTH_TRY(pw_u8(&pw, 0)); /* flags (reserved) */
  FROTH_TRY(pw_str(&pw, FROTH_VERSION));
  FROTH_TRY(pw_str(&pw, FROTH_BOARD_NAME));
  FROTH_TRY(pw_u8(&pw, 0)); /* capability_count */

  froth_console_flush_output();
  return froth_link_send_frame(session_id, FROTH_LINK_HELLO_RES, seq, resp_buf,
                               pw.pos);
}

static froth_error_t handle_hello(froth_vm_t *vm,
                                  const froth_link_header_t *header) {
  return froth_link_send_hello_res(vm, header->session_id, header->seq);
}

/* ── EVAL ────────────────────────────────────────────────────────── */

static froth_error_t handle_eval(froth_vm_t *vm,
                                 const froth_link_header_t *header,
                                 const uint8_t *payload) {
  if (header->payload_length < 3)
    return FROTH_ERROR_LINK_TOO_LARGE;

  /* uint8_t flags = payload[0]; */
  uint16_t source_len = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);

  if (3 + source_len > header->payload_length)
    return FROTH_ERROR_LINK_TOO_LARGE;

  /* Copy source into a null-terminated buffer on the stack */
  char source[FROTH_LINK_MAX_PAYLOAD];
  if (source_len >= sizeof(source))
    return FROTH_ERROR_LINK_TOO_LARGE;
  memcpy(source, payload + 3, source_len);
  source[source_len] = '\0';

  /* Evaluate */
  froth_cell_u_t ds_snap = vm->ds.pointer;
  froth_cell_u_t rs_snap = vm->rs.pointer;
  vm->last_error_slot = -1;

  froth_error_t eval_err = froth_evaluate_input(source, vm);

  /* Build EVAL_RES */
  payload_writer_t pw = {resp_buf, sizeof(resp_buf), 0};

  if (eval_err == FROTH_OK) {
    char stack_buf[128];
    format_stack(vm, stack_buf, sizeof(stack_buf));

    FROTH_TRY(pw_u8(&pw, 0));          /* status: success */
    FROTH_TRY(pw_u16(&pw, 0));         /* error_code */
    FROTH_TRY(pw_str(&pw, ""));        /* fault_word */
    FROTH_TRY(pw_str(&pw, stack_buf)); /* stack_repr */
  } else {
    froth_cell_t code =
        (eval_err == FROTH_ERROR_THROW) ? vm->thrown : (froth_cell_t)eval_err;

    FROTH_TRY(pw_u8(&pw, 1));               /* status: error */
    FROTH_TRY(pw_u16(&pw, (uint16_t)code)); /* error_code */

    /* fault word */
    const char *fault = "";
    if (vm->last_error_slot >= 0) {
      const char *name;
      if (froth_slot_get_name((froth_cell_u_t)vm->last_error_slot, &name) ==
          FROTH_OK)
        fault = name;
    }
    FROTH_TRY(pw_str(&pw, fault));
    FROTH_TRY(pw_str(&pw, "")); /* stack_repr (empty on error) */

    vm->ds.pointer = ds_snap;
    vm->rs.pointer = rs_snap;
  }

  froth_console_flush_output();
  return froth_link_send_frame(header->session_id, FROTH_LINK_EVAL_RES,
                               header->seq, resp_buf, pw.pos);
}

/* ── INFO ────────────────────────────────────────────────────────── */

static froth_error_t handle_info(froth_vm_t *vm,
                                 const froth_link_header_t *header) {
  payload_writer_t pw = {resp_buf, sizeof(resp_buf), 0};

  FROTH_TRY(pw_u32(&pw, FROTH_HEAP_SIZE));
  FROTH_TRY(pw_u32(&pw, vm->heap.pointer));

  /* overlay heap usage = current - boot watermark */
  uint32_t overlay_used = 0;
  if (vm->heap.pointer > vm->watermark_heap_offset)
    overlay_used = vm->heap.pointer - vm->watermark_heap_offset;
  FROTH_TRY(pw_u32(&pw, overlay_used));

  FROTH_TRY(pw_u16(&pw, froth_slot_count()));

  /* overlay slot count */
  uint16_t overlay_slots = 0;
  for (uint16_t i = 0; i < froth_slot_count(); i++) {
    if (froth_slot_is_overlay(i))
      overlay_slots++;
  }
  FROTH_TRY(pw_u16(&pw, overlay_slots));

  FROTH_TRY(pw_u8(&pw, 0)); /* flags */
  FROTH_TRY(pw_str(&pw, FROTH_VERSION));

  froth_console_flush_output();
  return froth_link_send_frame(header->session_id, FROTH_LINK_INFO_RES,
                               header->seq, resp_buf, pw.pos);
}

/* ── RESET ────────────────────────────────────────────────────────── */

static froth_error_t handle_reset(froth_vm_t *vm,
                                  const froth_link_header_t *header) {
  payload_writer_t pw = {resp_buf, sizeof(resp_buf), 0};

  /* Reset clears VM stacks, overlay slots, and heap back to watermark.
     The REPL line buffer is intentionally NOT cleared: the link and REPL
     own separate input streams. Any partially typed direct-mode input
     remains the user's intent regardless of host-triggered resets. */
  froth_error_t err = froth_prim_dangerous_reset(vm);
  uint32_t status = (err == FROTH_ERROR_RESET) ? 0 : (uint32_t)err;
  FROTH_TRY(pw_u32(&pw, status));

  FROTH_TRY(pw_u32(&pw, FROTH_HEAP_SIZE));
  FROTH_TRY(pw_u32(&pw, vm->heap.pointer));

  /* overlay heap usage = current - boot watermark */
  uint32_t overlay_used = 0;
  if (vm->heap.pointer > vm->watermark_heap_offset)
    overlay_used = vm->heap.pointer - vm->watermark_heap_offset;
  FROTH_TRY(pw_u32(&pw, overlay_used));

  FROTH_TRY(pw_u16(&pw, froth_slot_count()));

  /* overlay slot count */
  uint16_t overlay_slots = 0;
  for (uint16_t i = 0; i < froth_slot_count(); i++) {
    if (froth_slot_is_overlay(i))
      overlay_slots++;
  }
  FROTH_TRY(pw_u16(&pw, overlay_slots));

  FROTH_TRY(pw_u8(&pw, 0)); /* flags */
  FROTH_TRY(pw_str(&pw, FROTH_VERSION));

  froth_console_flush_output();
  return froth_link_send_frame(header->session_id, FROTH_LINK_RESET_RES,
                               header->seq, resp_buf, pw.pos);
}

/* ── ERROR response ──────────────────────────────────────────────── */

static froth_error_t send_error(const froth_link_header_t *header,
                                uint8_t category, const char *detail) {
  payload_writer_t pw = {resp_buf, sizeof(resp_buf), 0};
  FROTH_TRY(pw_u8(&pw, category));
  FROTH_TRY(pw_str(&pw, detail));
  froth_console_flush_output();
  return froth_link_send_frame(header->session_id, FROTH_LINK_ERROR,
                               header->seq, resp_buf, pw.pos);
}

/* ── Dispatch ────────────────────────────────────────────────────── */

froth_error_t froth_link_dispatch(froth_vm_t *vm,
                                  const froth_link_header_t *header,
                                  const uint8_t *payload) {
  switch (header->message_type) {
  case FROTH_LINK_HELLO_REQ:
    return handle_hello(vm, header);
  case FROTH_LINK_EVAL_REQ:
    return handle_eval(vm, header, payload);
  case FROTH_LINK_INFO_REQ:
    return handle_info(vm, header);
  case FROTH_LINK_RESET_REQ:
    return handle_reset(vm, header);
  default:
    return send_error(header, 0, "unknown message type");
  }
}
