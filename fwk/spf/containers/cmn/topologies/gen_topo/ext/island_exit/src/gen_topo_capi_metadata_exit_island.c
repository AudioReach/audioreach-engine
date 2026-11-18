/**
 * \file gen_topo_capi_metadata_exit_island.c
 * \brief
 *     Function to exit island and then call into metadata vtbl funcitons
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"

/**
 * These functions are exposed to modules through capiv2 interface extensions
 * All these vtbl functions call md lib functions after performing md valid checks and exiting island
 */
capi_err_t gen_topo_capi_exit_island_metadata_create(void *                 context_ptr,
                                                     module_cmn_md_list_t **md_list_pptr,
                                                     uint32_t               size,
                                                     capi_heap_id_t         c_heap_id,
                                                     bool_t                 is_out_band,
                                                     module_cmn_md_t **     md_pptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);

   TRY(result, gen_topo_capi_metadata_create(context_ptr, md_list_pptr, size, c_heap_id, is_out_band, md_pptr));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}


capi_err_t gen_topo_capi_exit_island_metadata_create_with_tracking(void *                    context_ptr,
                                                                   module_cmn_md_list_t **   md_list_pptr,
                                                                   uint32_t                  size,
                                                                   capi_heap_id_t            heap_id,
                                                                   uint32_t                  metadata_id,
                                                                   module_cmn_md_flags_t     flags,
                                                                   module_cmn_md_tracking_t *tracking_info_ptr,
                                                                   module_cmn_md_t **        md_pptr)
{
   capi_err_t result = CAPI_EOK;
   uint32_t log_id = 0;                 // SAFE default
   
   INIT_EXCEPTION_HANDLING
   
   // Check context_ptr for NULL before dereferencing it
   if ((NULL == md_list_pptr) || (NULL == md_pptr) || (NULL == context_ptr))
   {
      THROW(result, AR_EBADPARAM)
   }
   
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   log_id = topo_ptr->gu.log_id;        // cache before any TRY/THROW paths
   gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);

   TRY(result,
       gen_topo_capi_metadata_create_with_tracking(context_ptr,
                                                   md_list_pptr,
                                                   size,
                                                   heap_id,
                                                   metadata_id,
                                                   flags,
                                                   tracking_info_ptr,
                                                   md_pptr));

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
   }
   return result;
}


capi_err_t gen_topo_capi_exit_island_metadata_clone(void *                 context_ptr,
                                                    module_cmn_md_t *      md_ptr,
                                                    module_cmn_md_list_t **out_md_list_pptr,
                                                    capi_heap_id_t         c_heap_id)
{

   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   if (NULL == md_ptr)
   {
      return CAPI_EFAILED;
   }

   gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);

   TRY(result, gen_topo_capi_metadata_clone(context_ptr, md_ptr, out_md_list_pptr, c_heap_id));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

capi_err_t gen_topo_capi_exit_island_metadata_destroy(void *                 context_ptr,
                                                      module_cmn_md_list_t * md_list_ptr,
                                                      bool_t                 is_dropped,
                                                      module_cmn_md_list_t **head_pptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   if (NULL == md_list_ptr)
   {
      return result;
   }

   gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);

   TRY(result, gen_topo_capi_metadata_destroy(context_ptr, md_list_ptr, is_dropped, head_pptr, 0, FALSE));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }
   return result;
}

capi_err_t gen_topo_capi_exit_island_metadata_propagate(void *                      context_ptr,
                                                        capi_stream_data_v2_t *     input_stream_ptr,
                                                        capi_stream_data_v2_t *     output_stream_ptr,
                                                        module_cmn_md_list_t **     internal_md_list_pptr,
                                                        uint32_t                    algo_delay_us,
                                                        intf_extn_md_propagation_t *input_md_info_ptr,
                                                        intf_extn_md_propagation_t *output_md_info_ptr)
{

   capi_err_t result = CAPI_EOK;
   uint32_t log_id = 0;                 // SAFE default
   
   INIT_EXCEPTION_HANDLING
   
   // Check context_ptr for NULL before dereferencing it
   if ((NULL == input_stream_ptr) || (NULL == context_ptr) || (NULL == output_stream_ptr))
   {
      return CAPI_EBADPARAM;
   }
   
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   log_id = topo_ptr->gu.log_id;        // cache before any TRY/THROW paths
   gen_topo_vote_against_lpi_if_md_lib_in_nlpi(topo_ptr);

   TRY(result,
       gen_topo_capi_metadata_propagate(context_ptr,
                                        input_stream_ptr,
                                        output_stream_ptr,
                                        internal_md_list_pptr,
                                        algo_delay_us,
                                        input_md_info_ptr,
                                        output_md_info_ptr));

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
   }
   return result;
}

capi_err_t gen_topo_capi_exit_island_metadata_modify_at_data_flow_start(void *                 context_ptr,
                                                                        module_cmn_md_list_t * md_node_ptr,
                                                                        module_cmn_md_list_t **head_pptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   if (NULL == md_node_ptr)
   {
      return result;
   }

   gen_topo_vote_against_lpi_if_md_lib_in_nlpi(topo_ptr);

   TRY(result, gen_topo_capi_metadata_modify_at_data_flow_start(context_ptr, md_node_ptr, head_pptr));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }
   return result;
}

