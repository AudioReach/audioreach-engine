/**
 * \file pt_cntr.c
 * \brief
 *      Continer stub functions.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "pt_cntr.h"

const cu_cntr_vtable_t pt_cntr_cntr_funcs = {};
const topo_to_cntr_vtable_t topo_to_pt_cntr_vtable = {};

// Pass thru container will be enabled only if USES_PASS_THRU_CONTAINER macro is enabled in the chip specific build
// configuration file.
bool_t is_pass_thru_container_supported()
{
   return FALSE;
}

ar_result_t pt_cntr_stm_fwk_extn_handle_enable(pt_cntr_t *me_ptr, gu_module_list_t *stm_mod_list_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_stm_fwk_extn_handle_disable(pt_cntr_t *me_ptr, gu_module_list_t *mod_list_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_update_module_process_list(pt_cntr_t *me_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_handle_module_buffer_access_event(gen_topo_t        *topo_ptr,
                                                       gen_topo_module_t *mod_ptr,
                                                       capi_event_info_t *event_info_ptr)
{
   return AR_EUNSUPPORTED;
}

/** ----------FRONT End data path utilities -------- **/
ar_result_t pt_cntr_signal_trigger(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_assign_port_buffers(pt_cntr_t *me_ptr)
{

   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_destroy_modules_resources(pt_cntr_t *me_ptr, bool_t b_destroy_all_modules)
{
   return AR_EUNSUPPORTED;
}

capi_err_t  pt_cntr_capi_event_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr)
{
   return CAPI_EUNSUPPORTED;
}

ar_result_t pt_cntr_validate_media_fmt_thresh(pt_cntr_t *me_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_validate_topo_at_open(pt_cntr_t *me_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t pt_cntr_init_data_ports_post_async_create_finish(gen_topo_t *topo_ptr)
{
   return AR_EUNSUPPORTED;
}