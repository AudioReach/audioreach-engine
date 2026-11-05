/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx_metadata_utils.cpp
 *
 * C source file to implement the metadata utilities for IPC_TX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

/**----------------------------------------------------------------------------
 ** Include Files
 ** -------------------------------------------------------------------------*/

#include "capi_ipc_tx_utils.h"
#include "module_cmn_metadata.h"

/**----------------------------------------------------------------------------
 ** Function Definitions
 ** -------------------------------------------------------------------------*/

/**
 * \brief Calculates the total size of metadata in the provided metadata list.
 * This function iterates through the metadata list, calculates the size of each metadata element (including the base
 * structure and metadata payload), and sums up the sizes to determine the total metadata size.
 *
 * \param me_ptr Pointer to the capi_ipc_tx_t structure.
 * \param md_list_ptr Pointer to the metadata list.
 * \param meta_data_size_ptr Pointer to the variable that will store the total metadata size.
 *
 * \return capi_err_t Result of the operation, which is either an error code or a success code.
 */
/* function to calculate the total metadata size of the current write buffer */
ar_result_t ipc_tx_get_write_meta_data_size(capi_ipc_tx_t        *me_ptr,
                                            module_cmn_md_list_t *md_list_ptr,
                                            uint32_t             *req_md_buf_size_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == md_list_ptr)
   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid, DBG_MED_PRIO, "MD: get_md_size, md_list is null, md_size is zero");
#endif
      return AR_EOK;
   }

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid, DBG_MED_PRIO, "MD: get md size");
#endif

   *req_md_buf_size_ptr                    = 0;
   uint32_t              write_md_buf_size = 0;
   module_cmn_md_list_t *node_ptr          = md_list_ptr;
   while (node_ptr)
   {
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      // metadata element size : base structure + metadata payload max size
      // assuming max_size is already 4 bytes aligned
      if (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE == md_ptr->metadata_flag.tracking_mode)
      {
         write_md_buf_size += (sizeof(metadata_header_t) + md_ptr->max_size);
      }
      else // if its tracking MD source domain information needs to be propagated.
      {
         // Need to add MD source info always when MD is propagated from TX
         write_md_buf_size += (sizeof(metadata_header_t) + md_ptr->max_size) +
                              ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t));
      }

#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_MED_PRIO,
                 "MD: md_ele_size is %lu, wr_md_buf_size %lu",
                 ((sizeof(metadata_header_t) + md_ptr->max_size)),
                 write_md_buf_size);
#endif

      node_ptr = node_ptr->next_ptr;
   }

   *req_md_buf_size_ptr = write_md_buf_size;

   if (write_md_buf_size > 0)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "MD: total md_size required %lu", write_md_buf_size);
   }

   return result;
}

/**
 * \brief Converts internal metadata flags to client metadata flags, mapping internal flags to corresponding client
 * flags. This function takes internal metadata flags as input, extracts relevant information, and sets the
 * corresponding bits in the client metadata flags. It handles flags such as client metadata, tracking mode, tracking
 * policy, buffer sample association, and needs propagation to client buffer.
 *
 * \param int_md_flags Internal metadata flags to be converted.
 * \param client_md_flags Pointer to the client metadata flags to be updated.
 */
// referenced from from gen_topo
void capi_ipc_tx_convert_int_md_flags_to_client_md_flag(module_cmn_md_flags_t int_md_flags, uint32_t *client_md_flags)
{
   bool_t temp_client_metadata               = ~int_md_flags.is_client_metadata;
   bool_t needs_propagation_to_client_buffer = ~int_md_flags.needs_propagation_to_client_buffer;
   tu_set_bits(client_md_flags,
               temp_client_metadata,
               MD_HEADER_FLAGS_BIT_MASK_CLIENT_INFO,
               MD_HEADER_FLAGS_SHIFT_CLIENT_INFO);

   tu_set_bits(client_md_flags,
               int_md_flags.tracking_mode,
               MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
               MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);

   tu_set_bits(client_md_flags,
               int_md_flags.tracking_policy,
               MD_HEADER_FLAGS_BIT_MASK_TRACKING_EVENT_POLICY,
               MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);

   tu_set_bits(client_md_flags,
               int_md_flags.buf_sample_association,
               MD_HEADER_FLAGS_BIT_MASK_ASSOCIATION,
               MD_HEADER_FLAGS_SHIFT_ASSOCIATION_FLAG);

   tu_set_bits(client_md_flags,
               needs_propagation_to_client_buffer,
               MD_HEADER_FLAGS_BIT_MASK_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER,
               MD_HEADER_FLAGS_SHIFT_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER_FLAG);
}

/**
 * \brief Writes metadata from the input buffer to the shared buffer, handling tracking mode and updating metadata
 * headers accordingly. This function iterates through the metadata list, copies the metadata to the shared buffer, and
 * updates the metadata headers with the correct metadata ID, offset, and payload size. It also handles the tracking
 * mode by adding the metadata node to the tracking list or destroying it, and updates the token in the metadata header.
 *
 * \param me_ptr Pointer to the capi_ipc_tx_t structure.
 * \param input Pointer to the capi_stream_data_t structure.
 *
 * \return capi_err_t Result of the operation, which is either an error code or a success code.
 */
capi_err_t capi_ipc_tx_write_metadata(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input[])
{

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "MD: write_metadata: called  me_ptr->sh_buf_info.metadata_buff_size %lu",
              me_ptr->sh_buf_info.metadata_buff_size);
#endif

   uint32_t num_md_cnt = 0;

   capi_err_t             capi_result   = CAPI_EOK;
   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[0];

   module_cmn_md_list_t  *next_ptr            = NULL;
   metadata_header_t     *md_data_header_ptr  = NULL;
   module_cmn_md_list_t **md_list_pptr        = &in_stream_ptr->metadata_list_ptr;
   uint8_t               *src_md_payload_ptr  = NULL;
   uint8_t               *dest_md_payload_ptr = NULL;

   uint32_t curr_buff_index = me_ptr->sh_buf_info.curr_buff_index;

   uint8_t *temp_md_buf_ptr = (uint8_t *)me_ptr->sh_buf_info.shared_mem_buf_handle[curr_buff_index].shm_mem_ptr +
                              me_ptr->sh_buf_info.data_buff_size;

   uint32_t total_md_buf_size_available = me_ptr->sh_buf_info.metadata_buff_size;

   if (NULL == in_stream_ptr->metadata_list_ptr)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "MD: Write metadata: node ptr is NULL");
      return CAPI_EBADPARAM;
   }

   module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;                    // metadata object pointer
      md_data_header_ptr      = (metadata_header_t *)temp_md_buf_ptr; // metadata data header pointer

      if (total_md_buf_size_available < ((sizeof(metadata_header_t) + md_ptr->max_size)))
      {
         capi_result = CAPI_EBADPARAM;
         break;
      }
      module_cmn_md_flags_t metadata_flag = md_ptr->metadata_flag;

      md_data_header_ptr->metadata_id = md_ptr->metadata_id;
      md_data_header_ptr->offset      = md_ptr->offset;

      // initialize the token,
      // when added to inter proc md tracker list, token will be updated if current domain is
      // originating domain. Token will be updated to a unique handle to handle render events
      // from the satellite proc domains.
      md_data_header_ptr->token_lsw = (md_ptr->tracking_ptr) ? md_ptr->tracking_ptr->token_lsw : 0;
      md_data_header_ptr->token_msw = (md_ptr->tracking_ptr) ? md_ptr->tracking_ptr->token_msw : 0;

      // at the proc domain boundary the external client is MD is converted to internal client MD.
      // and propagated to remote/peer proc domains.
      // Only in the originating/source proc domain will create a tracker as external client MD, and
      // render it based on the events from the peer proc domains.
      metadata_flag.is_client_metadata = MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD;
      capi_ipc_tx_convert_int_md_flags_to_client_md_flag(metadata_flag, &md_data_header_ptr->flags);

      uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;
      if (is_out_band)
      {
         src_md_payload_ptr = (uint8_t *)md_ptr->metadata_ptr;
      }
      else
      {
         src_md_payload_ptr = (uint8_t *)&(md_ptr->metadata_buf);
      }

      dest_md_payload_ptr = (uint8_t *)(md_data_header_ptr + 1);

      md_data_header_ptr->payload_size = memscpy(dest_md_payload_ptr,
                                                 total_md_buf_size_available - sizeof(metadata_header_t),
                                                 src_md_payload_ptr,
                                                  md_ptr->max_size);

      // align by 4 needs to be done for both Rx/Tx MD handling
      total_md_buf_size_available -= ALIGN_4_BYTES(sizeof(metadata_header_t) + md_data_header_ptr->payload_size);
      temp_md_buf_ptr += ALIGN_4_BYTES(sizeof(metadata_header_t) + md_data_header_ptr->payload_size);
      num_md_cnt++;

      // #ifdef DEBUG_IPC_TX
      uint32_t *payload = (uint32_t *)(md_data_header_ptr + 1);
      IPC_TX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "MD: Enter in process - metadata cnt: %lu metadata_id= 0x%lx, offset= 0x%lx, token_lsw= 0x%x, "
                 "token_msw=0x%x, payload_size= %lu  paylod[0]=0x%lx paylod[1]=0x%lx",
                 num_md_cnt,
                 md_data_header_ptr->metadata_id,
                 md_data_header_ptr->offset,
                 md_data_header_ptr->token_lsw,
                 md_data_header_ptr->token_msw,
                 md_data_header_ptr->payload_size,
                 payload[0],
                 payload[1]);
      // #endif

      // if EOS flushing MD is received need to send prebuffers again on the next data flow start, hence
      // reset the prebuffer sent flag to FALSE
      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         // Exit island here since we need to do a mem free operation which is in nlpi
         module_cmn_md_eos_t *eos_metadata_ptr = md_ptr->metadata_flag.is_out_of_band
                                                    ? (module_cmn_md_eos_t *)md_ptr->metadata_ptr
                                                    : (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);

         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "MD: Received EOS, it its flushing?%lu reset the prebuffer flag %lu",
                    eos_metadata_ptr->flags.is_flushing_eos,
                    me_ptr->sh_buf_info.is_prebuffers_sent);

         // if flushing EOS is received need to resend the prebuffer flags on the next data flow start.
         if (eos_metadata_ptr->flags.is_flushing_eos)
         {
            me_ptr->sh_buf_info.is_prebuffers_sent = FALSE;
         }
      }

      // -------TRACKING METADATA PART---------------
      // If the tracking mode is disable, we can safely destroy the metadata at the IPC tx input
      if (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE == md_ptr->metadata_flag.tracking_mode)
      {
         // capi_ipc_tx_destroy_metadata(md_ptr);

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "MD: Write metadata Tracking mode disabled");
#endif
         if (!me_ptr->metadata_handler.context_ptr)
         {
            IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "MD: metadata context ptr is NULL");
            capi_result = CAPI_EBADPARAM;
            return capi_result;
         }
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   TRUE /*dropped*/,
                                                   &in_stream_ptr->metadata_list_ptr);

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "MD: Write metadata destroy done");
#endif
      }
      /** check if the metadata originated in another domain or orginated in the current domain and propagated back from
       * peer DSP.*/
      else if (spf_ipcmd_is_md_already_being_tracked(me_ptr->miid, md_ptr))
      {
         // update the extension info
         metadata_header_extn_t *md_hdr_extn_ptr = (metadata_header_extn_t *)temp_md_buf_ptr;
         md_hdr_extn_ptr->metadata_extn_param_id = PARAM_ID_MD_EXTN_MD_ORIGIN_CFG;
         md_hdr_extn_ptr->payload_size           = sizeof(param_id_md_extn_md_origin_cfg_t);

         param_id_md_extn_md_origin_cfg_t *md_origin_cfg_ptr =
            (param_id_md_extn_md_origin_cfg_t *)(md_hdr_extn_ptr + 1);

         // retain the originating proc domain from the current tacking ptr payload.
         md_origin_cfg_ptr->domain_id = md_ptr->tracking_ptr->src_domain_id;

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "MD: is being already tracked, decrement ref count to IPMD tracker in originating domain");
#endif
         // IPC TX is propagating the MD to downstream proc domain, and we can consider MD is consumed in the scope of
         // current IPX TX's proc domain. But this is not truly a render/drop event, hence we raise this event to
         // indicate the orginiator domain to only decrement ref count and not to handle any render events.
         spf_ipcmd_raise_event_to_update_ref_count(me_ptr->miid,
                                                   md_ptr->tracking_ptr,
                                                   md_data_header_ptr->metadata_id,
                                                   FALSE /*need to incr ref count*/);

         // here MD is just getting destroyed in the current proc domain context and not getting dropped, hence override
         // the tracking mode to avoid raising drop event to orginating proc domain.
         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "MD: Overwriting tracking mode to MODULE_CMN_MD_TRACKING_CONFIG_DISABLE to avoid raising tracking "
                    "event for drop. ");

         // destroy MD since its already tracked but skip raising the tracking/render event.
         md_ptr->metadata_flag.tracking_mode = MODULE_CMN_MD_TRACKING_CONFIG_DISABLE;
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   TRUE /*dropped*/,
                                                   &in_stream_ptr->metadata_list_ptr);

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "MD: Write metadata destroy done");
#endif
         temp_md_buf_ptr += ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t));
         total_md_buf_size_available -=
            (ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t)));

         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "MD: Write metadata - metadata cnt: %lu metadata_id= 0x%lx, offset= 0x%lx, token_lsw= 0x%x, "
                    "token_msw=0x%x, payload_size= %lu, flags= %lu already being tracked, just propagating.",
                    num_md_cnt,
                    md_data_header_ptr->metadata_id,
                    md_data_header_ptr->offset,
                    md_data_header_ptr->token_lsw,
                    md_data_header_ptr->token_msw,
                    md_data_header_ptr->payload_size,
                    md_data_header_ptr->flags);
      }
      else // MD orginiated in the current domain and needs to be propagated downstream to the peer domain
      {
         IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "MD: Write metadata Tracking mode enabled");

         /* copy the required MD info into the tracker utility and destory the rest of the MD node, so that MD is
          * considered freed from the cntr context.*/

         // If the tracking mode is enable, we need to keep the metadata
         // in the IPC tx input
         // this will also ensure that the MD payload being propagated will have the tracking mode set to
         // both render/drop evnets and policy will be set to EACH.
         ar_result_t res = spf_add_md_to_inter_proc_md_tracker(me_ptr->miid,
                                                               me_ptr->metadata_handler.context_ptr,
                                                               node_ptr,
                                                               md_list_pptr,
                                                               md_data_header_ptr,
                                                               (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);

         if (AR_EOK != res)
         {
            // failed to cache the MD node, disabling tracking in the propatation
            // disable the tracking mode in md header flag

#ifdef DEBUG_IPC_TX
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "MD: Write metadata - metadata cnt: %lu metadata_id= 0x%lx, offset= 0x%lx, token_lsw= 0x%x, "
                       "token_msw=0x%x, payload_size= %lu, flags= %lu ERROR: creating ipc md tracker failed",
                       num_md_cnt,
                       md_data_header_ptr->metadata_id,
                       md_data_header_ptr->offset,
                       md_data_header_ptr->token_lsw,
                       md_data_header_ptr->token_msw,
                       md_data_header_ptr->payload_size,
                       md_data_header_ptr->flags);
#endif

            capi_result = CAPI_EFAILED;
            me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                      node_ptr,
                                                      TRUE /*dropped*/,
                                                      &in_stream_ptr->metadata_list_ptr);
         }

         // update the extension info
         metadata_header_extn_t *md_hdr_extn_ptr = (metadata_header_extn_t *)temp_md_buf_ptr;
         md_hdr_extn_ptr->metadata_extn_param_id = PARAM_ID_MD_EXTN_MD_ORIGIN_CFG;
         md_hdr_extn_ptr->payload_size           = sizeof(param_id_md_extn_md_origin_cfg_t);

         param_id_md_extn_md_origin_cfg_t *md_origin_cfg_ptr =
            (param_id_md_extn_md_origin_cfg_t *)(md_hdr_extn_ptr + 1);

         md_origin_cfg_ptr->domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;

         // align by 4 needs to be done for both Rx/Tx MD handling
         temp_md_buf_ptr += ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t));
         total_md_buf_size_available -=
            (ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t)));

         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "MD: Write metadata - metadata cnt: %lu metadata_id= 0x%lx, offset= 0x%lx, token_lsw= 0x%x, "
                    "token_msw=0x%x, payload_size= %lu, flags= %lu added to MD tracker",
                    num_md_cnt,
                    md_data_header_ptr->metadata_id,
                    md_data_header_ptr->offset,
                    md_data_header_ptr->token_lsw,
                    md_data_header_ptr->token_msw,
                    md_data_header_ptr->payload_size,
                    md_data_header_ptr->flags);
      }
      node_ptr = next_ptr;
   }

   // always clear flush-eos flag since we propagated to output //todo: review once
   in_stream_ptr->flags.marker_eos  = FALSE;
   in_stream_ptr->metadata_list_ptr = NULL;

   return capi_result;
}

capi_err_t capi_ipc_tx_drop_all_metadata(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input)
{
   capi_err_t             capi_result   = CAPI_EOK;
   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input;

   for (module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr; node_ptr;)
   {
      bool_t                IS_DROPPED_TRUE = TRUE;
      module_cmn_md_list_t *next_ptr        = node_ptr->next_ptr;

      if (me_ptr->metadata_handler.metadata_destroy)
      {
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   IS_DROPPED_TRUE,
                                                   &in_stream_ptr->metadata_list_ptr);
      }
      else
      {
         return CAPI_EFAILED;
      }
      node_ptr = next_ptr;
   }

   // EOF/EOS will be dropped from the module.
   in_stream_ptr->flags.end_of_frame = FALSE;
   in_stream_ptr->flags.marker_eos   = FALSE;

   return capi_result;
}
