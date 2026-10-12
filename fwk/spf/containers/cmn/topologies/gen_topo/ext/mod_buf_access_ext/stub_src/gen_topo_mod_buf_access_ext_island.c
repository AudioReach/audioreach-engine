/**
 * \file gen_topo_mod_buf_access_ext.c
 * \brief
 *  This file contains utility functions for INTF_EXTN_MODULE_BUFFER_ACCESS
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_osal_error.h"
#include "gen_topo.h"

/////////////// Function definitions ////////////////

ar_result_t gen_topo_clear_and_replace_capi_input_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_topo_clear_and_replace_capi_output_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_topo_handle_assign_buffers_capi_input_buffer_extn_module(gen_topo_t *topo_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_topo_clear_and_replace_capi_input_buffers_util(gen_topo_t            *topo_ptr,
                                                               gen_topo_input_port_t *in_port_ptr,
                                                               bool_t                 force_drop_data)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_topo_clear_and_replace_capi_output_buffers_util(gen_topo_t             *topo_ptr,
                                                                gen_topo_output_port_t *out_port_ptr,
                                                                bool_t                  force_drop_data)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_topo_query_n_assign_capi_input_port_util(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr)
{
   return AR_EUNSUPPORTED;
}
