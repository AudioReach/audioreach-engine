/**
 * \file graph_utils_ipc_extn.c
 *
 * \brief
 *
 *     Graph utilities
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "graph_utils.h"

ar_result_t gu_insert_ipc_output_port(gu_t             *gu_ptr,
                                       gu_module_t      *module_ptr,
                                       gu_output_port_t *output_port_ptr,
                                       POSAL_HEAP_ID     heap_id)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gu_insert_ipc_input_port(gu_t            *gu_ptr,
                                      gu_module_t     *module_ptr,
                                      gu_input_port_t *input_port_ptr,
                                      POSAL_HEAP_ID    heap_id)
{
   return AR_EUNSUPPORTED;
}

bool_t gu_does_module_needs_ipc_port(gu_module_t *module_ptr, bool_t is_input_port)
{
   //TODO:add this in 1.0.21
   return FALSE;
}
