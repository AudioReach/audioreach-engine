#ifndef _APM_SET_GET_CFG_H__
#define _APM_SET_GET_CFG_H__

/**
 * \file apm_set_get_cfg_utils.h
 *
 * \brief
 *     This file contains function declaration for APM framework parameters Set get Config utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_i.h"
#include "apm_cntr_debug_if.h"
#include "apm_debug_info.h"
#include "apm_debug_info_cfg.h"
#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/**------------------------------------------------------------------------------
 *  Structure Definition
 *----------------------------------------------------------------------------*/

typedef struct apm_set_get_cfg_utils_vtable
{
   ar_result_t (*apm_parse_fwk_get_cfg_params_fptr)(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

   ar_result_t (*apm_parse_fwk_set_cfg_params_fptr)(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

   ar_result_t (*apm_populate_cntr_param_payload_size_fptr)(uint32_t                    container_id,
                                                            apm_cont_set_get_cfg_hdr_t *set_get_cfg_hdr_ptr,
                                                            apm_module_param_data_t *   param_data_ptr);
   ar_result_t (*apm_compute_cntr_msg_payload_size_fptr)(uint32_t param_id, uint32_t *cntr_msg_payload_size_ptr);

} apm_set_get_cfg_utils_vtable_t;

typedef struct apm_cont_port_media_fmt_cfg_t apm_cont_port_media_fmt_cfg_t;

/**
 * This structure is used for caching the port media format pointer
 * to be sent to the host container
 */

typedef struct apm_port_mf_cfg_t
{
   uint32_t enable;

}apm_port_mf_cfg_t;

typedef struct apm_offload_voice_session_info_t
{
   apm_cont_set_get_cfg_hdr_t header;
   /**< Header for caching the container param */
   
   uint32_t sg_id;
   /**< Subgraph ID.*/

   uint32_t kpps_sf;
   /**< kpps scale factor to be applied on offload path.*/
   uint32_t bw_sf;
   /**< bw scale factor to be applied on offload path.*/
}apm_offload_voice_session_info_t;

typedef struct apm_cont_aggregate_payload_cfg_t
{
   apm_cont_set_get_cfg_hdr_t header;
   /**< Header for caching the container param */

   apm_port_mf_cfg_t  cntr_payload;
   /** Pointer to enable/disable */
}apm_cont_aggregate_payload_cfg_t;

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

ar_result_t apm_set_get_cfg_utils_init(apm_t *apm_info_ptr);

static inline void apm_update_cont_param_set_cfg_msg_hdr(uint32_t                 container_id,
                                                         uint32_t                 param_id,
                                                         uint32_t                 param_size,
                                                         apm_module_param_data_t *param_data_ptr)
{
   param_data_ptr->module_instance_id = container_id;
   param_data_ptr->param_id           = param_id;
   param_data_ptr->param_size         = param_size;
   param_data_ptr->error_code         = AR_EOK;

   return;
}

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_SET_GET_CFG_H__ */
