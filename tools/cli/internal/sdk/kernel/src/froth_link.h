#pragma once
#include "froth_transport.h"
#include "froth_types.h"

froth_error_t froth_link_dispatch(froth_vm_t *vm,
                                  const froth_link_header_t *header,
                                  const uint8_t *payload);
