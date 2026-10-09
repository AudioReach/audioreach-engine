/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_rx_metadata_utils.cpp
 *
 * C source file to implement the metadata utilities for IPC_RX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

/**----------------------------------------------------------------------------
 ** Include Files
 ** -------------------------------------------------------------------------*/

#include "capi_ipc_rx_utils.h"
#include "metadata_api.h"
#include "module_cmn_metadata.h"
#include "spf_inter_proc_md_utils.h"

/**----------------------------------------------------------------------------
 ** Function Definitions
 ** -------------------------------------------------------------------------*/

ar_result_t ipc_rx_destroy_all_md(capi_ipc_rx_t *me_ptr, module_cmn_md_list_t **md_list_pptr)
{
   module_cmn_md_list_t *md_list_ptr      = *md_list_pptr;
   module_cmn_md_list_t *next_md_list_ptr = NULL;

   // Add debug print at the beginning of the function
   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "ipc_rx_destroy_all_md: Starting metadata destruction, md_list_ptr=0x%lx",
              md_list_ptr);

   while (md_list_ptr)
   {
      module_cmn_md_t *md_ptr = (module_cmn_md_t *)md_list_ptr->obj_ptr;
      next_md_list_ptr        = md_list_ptr->next_ptr;
      if (md_ptr)
      {
         // domain and propagate it to the downstream proc domain. to decrement ref count for the current proc domain
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   md_list_ptr,
                                                   FALSE,
                                                   md_list_pptr);
      }

      md_list_ptr = next_md_list_ptr;
   }

   if (*md_list_pptr)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "unexpected: md_list_ptr 0x%lx is not NULL", *md_list_pptr);
      return AR_EFAILED;
   }

   return AR_EOK;
}

capi_err_t ipc_rx_prop_md_from_src_buf_to_dst_buf(capi_ipc_rx_t         *me_ptr,
                                                  capi_media_fmt_v2_t   *src_mf_ptr,
                                                  uint32_t               src_buf_len_per_ch_before_copy,
                                                  uint32_t               src_buf_len_per_ch_after_copy,
                                                  capi_stream_data_v2_t *src_stream_ptr,
                                                  uint32_t               dst_buf_len_per_ch_before_copy,
                                                  uint32_t               dst_buf_len_per_ch_after_copy,
                                                  capi_stream_data_v2_t *dst_stream_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (src_stream_ptr && dst_stream_ptr)
   {
      // Add debug print at the beginning of the function
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "ipc_rx_prop_md: BEFORE: src_md=0x%lx, dst_md=0x%lx, src_len=%u->%u, dst_len=%u->%u src_flags=0x%lx "
                 "dst_flags=0x%lx",
                 src_stream_ptr->metadata_list_ptr,
                 dst_stream_ptr->metadata_list_ptr,
                 src_buf_len_per_ch_before_copy,
                 src_buf_len_per_ch_after_copy,
                 dst_buf_len_per_ch_before_copy,
                 dst_buf_len_per_ch_after_copy,
                 src_stream_ptr->flags.word,
                 dst_stream_ptr->flags.word);

      module_cmn_md_list_t *internal_md_list_dummy = NULL; // internal md list NULL because algo delay is 0

      intf_extn_md_propagation_t input_md_info;
      memset(&input_md_info, 0, sizeof(input_md_info));
      input_md_info.df              = src_mf_ptr->header.format_header.data_format;
      input_md_info.bits_per_sample = src_mf_ptr->format.bits_per_sample;
      input_md_info.sample_rate     = src_mf_ptr->format.sampling_rate;

      input_md_info.initial_len_per_ch_in_bytes = src_buf_len_per_ch_before_copy;

      // data consumed from input per ch
      input_md_info.len_per_ch_in_bytes = (src_buf_len_per_ch_before_copy - src_buf_len_per_ch_after_copy);

      intf_extn_md_propagation_t output_md_info;
      memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
      output_md_info.initial_len_per_ch_in_bytes = dst_buf_len_per_ch_before_copy;

      // data produced at output per ch
      output_md_info.len_per_ch_in_bytes = dst_buf_len_per_ch_after_copy - dst_buf_len_per_ch_before_copy;

      result = me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                           src_stream_ptr,
                                                           dst_stream_ptr,
                                                           &internal_md_list_dummy, // internal_list_pptr,
                                                           0,                       // algo_delay_us
                                                           &input_md_info,
                                                           &output_md_info);

      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "ipc_rx_prop_md: BEFORE: src_md=0x%lx, dst_md=0x%lx, src_len=%u->%u, dst_len=%u->%u src_flags=0x%lx "
                 "dst_flags=0x%lx",
                 src_stream_ptr->metadata_list_ptr,
                 dst_stream_ptr->metadata_list_ptr,
                 src_buf_len_per_ch_before_copy,
                 src_buf_len_per_ch_after_copy,
                 dst_buf_len_per_ch_before_copy,
                 dst_buf_len_per_ch_after_copy,
                 src_stream_ptr->flags.word,
                 dst_stream_ptr->flags.word);
   }
   else
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "ipc_rx_prop_md: src_stream_ptr?%lu, dst_stream_ptr?%lu, src_len=%u->%u, dst_len=%u->%u "
                 "src_flags=0x%lx "
                 "dst_flags=0x%lx",
                 (NULL == src_stream_ptr),
                 (NULL == dst_stream_ptr),
                 src_buf_len_per_ch_before_copy,
                 src_buf_len_per_ch_after_copy,
                 dst_buf_len_per_ch_before_copy,
                 dst_buf_len_per_ch_after_copy,
                 src_stream_ptr->flags.word,
                 dst_stream_ptr->flags.word);
      result = AR_EFAILED;
   }

   return result;
}

ar_result_t ipc_rx_modify_md_in_internal_buf_when_new_data_arrives(capi_ipc_rx_t         *me_ptr,
                                                                   module_cmn_md_list_t **md_list_pptr)
{
   ar_result_t           result   = AR_EOK;
   module_cmn_md_list_t *node_ptr = *md_list_pptr;
   module_cmn_md_list_t *next_ptr = NULL;
   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      uint32_t md_id = md_ptr->metadata_id;

      // flushing eos will be converted to non flushing in the metadata_modify_at_data_flow_start() callback.
      if (MODULE_CMN_MD_ID_EOS == md_id)
      {
         module_cmn_md_eos_t *eos_ptr =
            (module_cmn_md_eos_t *)(md_ptr->metadata_flag.is_out_of_band ? md_ptr->metadata_ptr : md_ptr->metadata_buf);
         if (eos_ptr->flags.is_flushing_eos)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "ipc_rx_prop_md: Converting flushing EOS to non flushing EOS since new data arrived",
                       md_id,
                       eos_ptr->flags.is_flushing_eos);
         }
      }

      // 1. Internal EOS is dropped
      // 2. Flushing EOS is converted to non-flushing
      // 3. DFG is dropped at the input of the SPR itself.
      result = me_ptr->metadata_handler.metadata_modify_at_data_flow_start(me_ptr->metadata_handler.context_ptr,
                                                                           node_ptr,
                                                                           md_list_pptr);

      if (CAPI_FAILED(result))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "ipc_rx_prop_md: Failed to modify_md ID 0x%X with error %x",
                    md_id,
                    result);
      }

      node_ptr = next_ptr;
   }
   return result;
}

bool_t ipc_rx_check_if_there_is_a_flushing_eos(capi_ipc_rx_t *me_ptr, module_cmn_md_list_t *md_list_ptr)
{
   module_cmn_md_list_t *node_ptr = md_list_ptr;
   uint32_t              counter  = 0;
   while (node_ptr)
   {
      module_cmn_md_t *md_ptr = (module_cmn_md_t *)node_ptr->obj_ptr;

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         counter++;
         module_cmn_md_eos_t *eos_metadata_ptr = NULL;
         uint32_t             is_out_band      = md_ptr->metadata_flag.is_out_of_band;
         if (is_out_band)
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }

         if (MODULE_CMN_MD_EOS_FLUSHING == eos_metadata_ptr->flags.is_flushing_eos)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "MD: Found Flushing EOS, counter %lu in the md list 0x%p node ptr 0x%p is_internal_eos?%lu",
                       counter,
                       node_ptr,
                       md_ptr,
                       eos_metadata_ptr->flags.is_internal_eos);
         }
      }
      node_ptr = node_ptr->next_ptr;
   }

   if (counter)
   {
      return TRUE;
   }

   return FALSE;
}
