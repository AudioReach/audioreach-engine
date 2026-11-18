/**
 * \file gen_topo_fwk_extn_utils.c
 * \brief
 *     This file implements the framework extension utility functions
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "apm_sub_graph_api.h"

/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* -----------------------------------------------------------------------
 ** Constant / Define Declarations
 ** ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 ** Function prototypes
 ** ----------------------------------------------------------------------- */

ar_result_t gen_topo_fmwk_extn_handle_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result        = AR_EOK;
   ar_result_t ignore_result = AR_EUNSUPPORTED;

   if (module_ptr->flags.need_trigger_policy_extn)
   {
      fwk_extn_param_id_trigger_policy_cb_fn_t handler;
      gen_topo_populate_trigger_policy_extn_vtable(module_ptr, &handler);
      result |= gen_topo_capi_set_param(topo_ptr->gu.log_id,
                                        module_ptr->capi_ptr,
                                        FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN,
                                        (int8_t *)&handler,
                                        sizeof(handler));
   }

   if (module_ptr->flags.need_global_shmem_extn)
   {
      gen_topo_init_global_sh_mem_extn(topo_ptr, module_ptr);
   }

   // other framework extensions are handled in create_module callback to fwk
   // supressing unsupported error because SYNC modules trigger policy is supported under fwk-extenstion code.
   return (result & (~ignore_result));
}

ar_result_t gen_topo_fmwk_extn_handle_at_deinit(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t gen_topo_fwk_ext_set_cntr_frame_dur_per_module(gen_topo_t        *topo_ptr,
                                                           gen_topo_module_t *module_ptr,
                                                           uint32_t           frame_len_us)
{
   fwk_extn_param_id_container_frame_duration_t delay_ops;
   delay_ops.duration_us = frame_len_us;

   ar_result_t err_code = gen_topo_capi_set_param(topo_ptr->gu.log_id,
                                                  module_ptr->capi_ptr,
                                                  FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION,
                                                  (int8_t *)&delay_ops,
                                                  sizeof(delay_ops));

   if ((err_code != AR_EOK) && (err_code != AR_EUNSUPPORTED))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: setting container frame duration failed",
               module_ptr->gu.module_instance_id);
      return err_code;
   }
   else
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "Module 0x%lX: setting container frame duration of %lu",
               module_ptr->gu.module_instance_id,
               frame_len_us);
   }

   return AR_EOK;
}

ar_result_t gen_topo_fwk_ext_set_cntr_frame_dur(gen_topo_t *topo_ptr, uint32_t frame_len_us)
{
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // if frame duration fwk extension is not supported return
         if (FALSE == module_ptr->flags.need_cntr_frame_dur_extn)
         {
            continue;
         }

         gen_topo_fwk_ext_set_cntr_frame_dur_per_module(topo_ptr, module_ptr, frame_len_us);
      }
   }
   return AR_EOK;
}