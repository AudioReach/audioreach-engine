/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx_utils.cpp
 *
 * C source file to implement the Audio Post Processor Interface for
 * IPC_TX Module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * =========================================================================*/

#include "capi_ipc_tx_utils.h"
#include "capi_types.h"
#include "posal_memory.h"
#include "wr_sh_mem_ep_api.h"

// static bool_t ipc_tx_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr);

static capi_err_t capi_ipc_raise_data_link_info_event(uint32_t                             miid,
                                                      capi_event_callback_info_t          *cb_info_ptr,
                                                      fwk_extn_event_ipc_data_link_info_t *link_info_ptr);

static capi_err_t capi_ipc_tx_send_wr_buffer(capi_ipc_tx_t      *me_ptr,
                                             uint32_t            buffer_index,
                                             uint32_t            actual_data_filled,
                                             uint32_t            meta_data_size,
                                             uint64_t            timestamp,
                                             capi_stream_flags_t flags,
                                             bool_t              is_prebuffer);

static bool_t is_valid_size(size_t actual, size_t max)
{
   return (actual > 0 && actual <= max);
}

// todo: need to review the supported media format
static bool_t ipc_tx_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr)
{
   if ((CAPI_FIXED_POINT != format_ptr->header.format_header.data_format) &&
       (CAPI_FLOATING_POINT != format_ptr->header.format_header.data_format) &&
       (CAPI_RAW_COMPRESSED != format_ptr->header.format_header.data_format))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI IPC_TX: unsupported data format %lu",
             (uint32_t)format_ptr->header.format_header.data_format);
      return FALSE;
   }

   if ((CAPI_FIXED_POINT == format_ptr->header.format_header.data_format) ||
       (CAPI_FLOATING_POINT == format_ptr->header.format_header.data_format))
   {
      if ((16 != format_ptr->format.bits_per_sample) && (32 != format_ptr->format.bits_per_sample))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPI IPC_TX: only supports 16 and 32 bit data. Received %lu.",
                format_ptr->format.bits_per_sample);
         return FALSE;
      }

      if ((CAPI_DEINTERLEAVED_UNPACKED_V2 != format_ptr->format.data_interleaving) &&
          (CAPI_INTERLEAVED != format_ptr->format.data_interleaving))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI IPC_TX : Unsupported data type.");
         return FALSE;
      }

      if (!format_ptr->format.data_is_signed)
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI IPC_TX: Unsigned data not supported.");
         return FALSE;
      }

      if ((format_ptr->format.num_channels == 0) || (format_ptr->format.num_channels > CAPI_MAX_CHANNELS_V2))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPIv2 IPC_TX: Only upto %lu channels supported."
                "Received %lu.",
                CAPI_MAX_CHANNELS_V2,
                format_ptr->format.num_channels);
         return FALSE;
      }
   }

   return TRUE;
}

// /* =========================================================================
//  * FUNCTION : capi_ipc_tx_raise_process_event
//  * DESCRIPTION: Function to send the output media format using the
//  *              callback function
//  * =========================================================================*/
// capi_err_t capi_ipc_tx_raise_process_event(capi_ipc_tx_t *me_ptr)
// {
//    capi_err_t capi_result = CAPI_EOK;
//    uint32_t   enable      = TRUE;

//    capi_result = capi_cmn_update_process_check_event(&me_ptr->cb_info, enable);
//    if (CAPI_EOK == capi_result)
//    {
//       return capi_result;
//    }

//    return capi_result;
// }

/* =========================================================================
 * FUNCTION : capi_ipc_tx_update_event_states
 * DESCRIPTION: function to get KPPS numbers
 * =========================================================================*/
static uint32_t capi_ipc_tx_get_kpps(capi_ipc_tx_t *me_ptr)
{
   uint32_t kpps = 0;

   if ((me_ptr->inp_media_fmt.format.sampling_rate != CAPI_DATA_FORMAT_INVALID_VAL) &&
       (me_ptr->inp_media_fmt.format.num_channels != CAPI_DATA_FORMAT_INVALID_VAL))
   {
      // scale KPPS based on num channels and sampling rate //TBD
      kpps = CAPI_IPC_TX_KPPS * me_ptr->inp_media_fmt.format.num_channels *
             (me_ptr->inp_media_fmt.format.sampling_rate / 8000);
   }
   return kpps;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_raise_event
 * DESCRIPTION: Function to raise various events of the ipc_tx module
 * =========================================================================*/
capi_err_t capi_ipc_tx_raise_event(capi_ipc_tx_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   kpps        = 0;
   uint32_t   bw          = 0;
   IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "Enter capi_ipc_tx_raise_event");
   if (NULL == me_ptr->cb_info.event_cb)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Event callback is not set. Unable to raise events!");
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   kpps = capi_ipc_tx_get_kpps(me_ptr);
   bw   = CAPI_IPC_TX_BW;

   // capi_result |= capi_ipc_tx_raise_process_event(me_ptr);
   capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, kpps);
   capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->cb_info, bw, bw);

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: ipc_tx_process
  Processes an input buffer and packs into a GPR packet.
   // This function processes an input buffer and packs it into a GPR packet.
   // It takes in several parameters:
   //   - _pif: a pointer to the CAPI interface structure
   //   - input: an array of input stream data structures
   //   - output: an array of output stream data structures

   // we need to process the input buffer and pack it into a GPR packet.
   // This involves copying the data from the input buffer to the output buffer,

   shared data buffer is already allocated based on media format and container frame size.
   1. Check if metadata is present in the input data buffer
      a. Check if the shared buffer needs to be recreated based on the metadata size
         - Get the metadata size from the input data buffer
         - Check if the shared buffer needs to be recreated based on the metadata size
         - Allocate a new shared buffer with the updated metadata size
   2. Write the data to the shared buffer
      (copy data from input buffer to IPC buffer based on interleaving)
   3. Check if metadata has valid size. If yes, write metadata into the shared IPC buffer
   4. Fill in the data buffer structure
   5. Fill in the metadata buffer structure
   6. Fill in the packet header structure
   7. Allocate and Send the packet to the GPR
      - handle for failure, if needed

 * -----------------------------------------------------------------------*/
capi_err_t capi_ipc_tx_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   POSAL_ASSERT(_pif);
   POSAL_ASSERT(input[0]);

   capi_err_t             capi_result     = CAPI_EOK;
   capi_ipc_tx_t         *me_ptr          = (capi_ipc_tx_t *)_pif;
   capi_stream_data_v2_t *in_stream_ptr   = (capi_stream_data_v2_t *)input[0];
   uint32_t               meta_data_size  = 0;
   uint32_t               curr_buff_index = me_ptr->sh_buf_info.curr_buff_index;

   bool_t is_data_present = FALSE;
   bool_t is_md_present   = FALSE;

   // continue only if we have recieved the buffer from the GPR
   if (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED != me_ptr->output_trigger_info)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "Returning from process as we have not received the buffer from GPR");
      return capi_result;
   }

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, " Enter in process - ");
#endif

   // Check if the shared buffer needs to be recreated based on the metadata size
   if (me_ptr->sh_buf_info.metadata_buff_size + me_ptr->sh_buf_info.data_buff_size !=
       me_ptr->sh_buf_info.shared_mem_buf_handle[curr_buff_index].shm_alloc_size)
   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_MED_PRIO,
                 "write data processing: recreate sh buffer as buffer size got updated."
                 "metadata_buff_size %lu, data_buff_size %lu, shared_mem_buf_handle[%lu].shm_alloc_size %lu",
                 me_ptr->sh_buf_info.metadata_buff_size,
                 me_ptr->sh_buf_info.data_buff_size,
                 curr_buff_index,
                 me_ptr->sh_buf_info.shared_mem_buf_handle[curr_buff_index].shm_alloc_size);
#endif
      me_ptr->sh_buf_info.shmem_alloc_size =
         ALIGN_128_BYTES(me_ptr->sh_buf_info.data_buff_size) + ALIGN_128_BYTES(me_ptr->sh_buf_info.metadata_buff_size);
      // Allocate a new shared buffer with the updated metadata size
      if (CAPI_EOK !=
          (capi_result =
              capi_ipc_tx_handle_buffer_reallocation(me_ptr,
                                                     me_ptr->sh_buf_info
                                                        .shmem_alloc_size))) // TBD: maybe we can directly call
                                                                             // capi_ipc_tx_handle_buffer_reallocation
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to manage shared memory WR buffer");
         return capi_result;
      }
   }

   if (TRUE == me_ptr->need_to_overrun)
   {
      // drop input MD and data.
      capi_result                 = capi_ipc_tx_drop_all_metadata(me_ptr, input[0]);
      me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
      me_ptr->need_to_overrun     = FALSE;

      // if module extension is enabled
      if (me_ptr->is_mod_buf_access_enabled)
      {
         if ((input[0]->buf_ptr[0].data_ptr != me_ptr->curr_shared_buf_ptr) ||
             (input[0]->buf_ptr[0].data_ptr != me_ptr->sh_buf_info.overrun_buffer_ptr))
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "write_data: MOD_BUF_ACCESS: Fwk returned an invalid buf ptr input[0]->buf_ptr[0].data_ptr = "
                       "0x%p != 0x%p != 0x%p",
                       input[0]->buf_ptr[0].data_ptr,
                       me_ptr->curr_shared_buf_ptr,
                       me_ptr->sh_buf_info.overrun_buffer_ptr);
            capi_cmn_crash();
         }
         else
         {
            me_ptr->curr_shared_buf_ptr = NULL;

            // marking input as consumed
            for (uint32_t i = 0; i < input[0]->bufs_num; i++)
            {
               input[0]->buf_ptr[i].data_ptr = NULL;
            }
         }
      }

      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "IPC output packet not present, dropping input data and overrun");
      return capi_result;
   }

   // Check if metadata is present in the input data
   if (in_stream_ptr->metadata_list_ptr)
   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, " Metadata is present ");
#endif
      // Check if the shared buffer needs to be recreated based on the metadata size
      if (AR_EOK != (capi_result = capi_ipc_tx_check_recreate_buffer(me_ptr, input, &meta_data_size)))
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to check size and recreate buffer");
      }
   }
   is_data_present = is_valid_size(input[0]->buf_ptr->actual_data_len, me_ptr->sh_buf_info.data_buff_size);

   is_md_present = is_valid_size(meta_data_size, me_ptr->sh_buf_info.metadata_buff_size);

   // Set the current buffer pointer
   // todo_mdf: currently curr_buff is getting updated in multiple places it should be udpated in the as soon as
   // curr_buff_index get updated
   me_ptr->sh_buf_info.curr_buff = (uint8_t *)me_ptr->sh_buf_info.shared_mem_buf_handle[curr_buff_index].shm_mem_ptr;

   // Write the data to the shared buffer if it is present
   if (AR_EOK != (capi_result = capi_ipc_tx_write_data(me_ptr, input)))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to write data");
      return capi_result;
   }

   // Write the metadata to the shared buffer if it is present
   if (is_md_present)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, " Metadata is present ");

      // Write the metadata to the shared buffer
      if (AR_EOK != (capi_result = capi_ipc_tx_write_metadata(me_ptr, input)))
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to write metadata");
         return capi_result;
      }
   }

   // check if prebuffers are not sent, send them before sending regular data buffer.
   if (me_ptr->sh_buf_info.num_pending_prebuffers)
   {
      capi_stream_flags_t flags = input[0]->flags;
      flags.end_of_frame        = FALSE;
      flags.marker_eos          = FALSE;
      flags.erasure             = FALSE;

      // get the first prebuffer timestamp
      uint64_t timestamp =
         input[0]->timestamp - (me_ptr->frame_length_info.frame_dur_us * me_ptr->sh_buf_info.num_pending_prebuffers);

      // send prebuffers
      while (me_ptr->sh_buf_info.num_pending_prebuffers)
      {
         uint8_t prebuf_index =
            me_ptr->sh_buf_info.pending_prebuf_index_arr[me_ptr->sh_buf_info.num_pending_prebuffers - 1];

         if (prebuf_index < me_ptr->sh_buf_info.num_ipc_bufs_created)
         {
            capi_result |= capi_ipc_tx_send_wr_buffer(me_ptr,
                                                      prebuf_index,
                                                      me_ptr->sh_buf_info.frame_size_in_bytes,
                                                      0,
                                                      timestamp,
                                                      flags,
                                                      TRUE);
         }
         else
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "write_data: failed sending prebuffer invalid buf index %lu >= %lu",
                       prebuf_index,
                       me_ptr->sh_buf_info.num_ipc_bufs_created);
         }

         // reset the buffer index to invalid
         me_ptr->sh_buf_info.pending_prebuf_index_arr[me_ptr->sh_buf_info.num_pending_prebuffers - 1] = 0xFF;

         timestamp += me_ptr->frame_length_info.frame_dur_us;

         me_ptr->sh_buf_info.num_pending_prebuffers--;
      }

      me_ptr->sh_buf_info.is_prebuffers_sent = TRUE;
   }

   // send regular buffer
   capi_result |= capi_ipc_tx_send_wr_buffer(me_ptr,
                                             me_ptr->sh_buf_info.curr_buff_index,
                                             me_ptr->sh_buf_info.actual_data_filled,
                                             meta_data_size,
                                             input[0]->timestamp,
                                             input[0]->flags,
                                             FALSE);

   if (AR_DID_FAIL(capi_result))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sending data buffer from ipc to client failed with 0x%lx", capi_result);

      // if sending gpr data buffer has failed free all the pending trackind MD associated with the desitnation domain
      // id to avoid any memory leaks just as recovery
      spf_ipcmd_destroy_pending_tracked_md_info(me_ptr->miid, TRUE, me_ptr->ipc_tx_link_info.peer_proc_domain_id, TRUE);
      return capi_result;
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sent a data buffer from IPC to client");
      me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED; // set the shared ptr so that the contanier starts
                                                                     // listening to the ext output port queue
      // todo: we need to set this only when all the buffers are sent
   }

   // todo_mdf: shouldnt we reset me_ptr->sh_data_buf.actual_data_filled &&
   // me_ptr->sh_data_buf.ipc_sh_mem_info.mem_attr.offset

   return capi_result;
}

static capi_err_t capi_ipc_tx_send_wr_buffer(capi_ipc_tx_t      *me_ptr,
                                             uint32_t            buffer_index,
                                             uint32_t            actual_data_filled,
                                             uint32_t            meta_data_size,
                                             uint64_t            timestamp,
                                             capi_stream_flags_t flags,
                                             bool_t              is_prebuffer)
{
   capi_err_t result = CAPI_EOK;
   // Fill in the data buffer structure
   data_cmd_wr_sh_mem_ep_data_buffer_v2_t wr_data_buf;
   memset(&wr_data_buf, 0, sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t));

   wr_data_buf.data_buf_addr_lsw = me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset;
   wr_data_buf.data_buf_addr_msw = 0;
   wr_data_buf.data_buf_size =
      me_ptr->sh_buf_info.actual_data_filled; // depends on intr or deintr //total samples * wordsize // buffer size
                                              // calculated for sh buffer
   wr_data_buf.timestamp_lsw = (uint32_t)timestamp;
   wr_data_buf.timestamp_msw = (uint32_t)(timestamp >> 32);
   wr_data_buf.flags         = flags.is_timestamp_valid << WR_SH_MEM_EP_SHIFT_TIMESTAMP_VALID_FLAG;
   wr_data_buf.flags |= flags.ts_continue << WR_SH_MEM_EP_SHIFT_TS_CONTINUE_FLAG;
   wr_data_buf.flags |= flags.end_of_frame << WR_SH_MEM_EP_SHIFT_EOF_FLAG;

   wr_data_buf.data_mem_map_handle = me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.sat_handle;

   // Fill in the metadata buffer structure
   wr_data_buf.md_buf_addr_lsw =
      me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset + me_ptr->sh_buf_info.data_buff_size;
   wr_data_buf.md_buf_addr_msw = 0;
   wr_data_buf.md_buf_size     = meta_data_size;

   if (me_ptr->sh_buf_info.metadata_buff_size)
   {
      wr_data_buf.md_mem_map_handle = me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.sat_handle;
   }
   else
   {
      wr_data_buf.md_mem_map_handle = 0;
   }
   me_ptr->base_token++;
   gpr_cmd_alloc_send_t args;
   // Fill in the GPR packet header structure
   args.src_domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;
   args.dst_domain_id = me_ptr->ipc_tx_link_info.peer_proc_domain_id;
   args.src_port      = me_ptr->ipc_tx_link_info.self_module_iid;
   args.dst_port      = me_ptr->ipc_tx_link_info.peer_module_iid;
   args.token         = me_ptr->base_token;
   args.opcode        = DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2;
   args.payload       = &wr_data_buf;
   args.payload_size  = sizeof(wr_data_buf);
   args.client_data   = 0;

   // Allocate and Send the packet
   result = __gpr_cmd_alloc_send(&args);

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "wr_data_buf.data_buf_addr_lsw 0x%lx, wr_data_buf.data_buf_addr_msw 0x%lx, "
              "wr_data_buf.data_buf_size %ld, wr_data_buf.data_mem_map_handle 0x%lx args.payload 0x%lx "
              "args.payload_size %ld wr_data_buf.timestamp_lsw %ld wr_data_buf.timestamp_msw %ld",
              wr_data_buf.data_buf_addr_lsw,
              wr_data_buf.data_buf_addr_msw,
              wr_data_buf.data_buf_size,
              wr_data_buf.data_mem_map_handle,
              args.payload,
              args.payload_size,
              wr_data_buf.timestamp_lsw,
              wr_data_buf.timestamp_msw);

   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "wr_data_buf.flags 0x%lx, args.token : 0x%lx is_prebuffer? %lu",
              wr_data_buf.flags,
              args.token,
              is_prebuffer);

   if (wr_data_buf.md_buf_size)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "wr_data_buf.md_mem_map_handle 0x%lx, wr_data_buf.md_buf_addr_msw 0x%lx "
                 "wr_data_buf.md_buf_addr_lsw  0x%lx wr_data_buf.md_buf_size  0x%lu",
                 wr_data_buf.md_mem_map_handle,
                 wr_data_buf.md_buf_addr_msw,
                 wr_data_buf.md_buf_addr_lsw,
                 wr_data_buf.md_buf_size);
   }

#endif
   return result;
}

capi_err_t capi_ipc_tx_process_get_properties(capi_ipc_tx_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_basic_prop_t mod_prop;
   //  Extensions a) frame duration to calculate buffer size. b)ipc port handler to to handle the data cmd ACK in the
   //  module
   uint32_t fwk_extn_ids[]     = { FWK_EXTN_IPC_PORT_HANDLER, FWK_EXTN_CONTAINER_FRAME_DURATION };
   mod_prop.init_memory_req    = ALIGN_8_BYTES(sizeof(capi_ipc_tx_t));
   mod_prop.stack_size         = IPC_TX_STACK_SIZE;
   mod_prop.num_fwk_extns      = sizeof(fwk_extn_ids) / sizeof(fwk_extn_ids[0]);
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = FALSE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;

   uint32_t miid = me_ptr ? me_ptr->miid : MIID_UNKNOWN;

   capi_result |= capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Get common basic properties failed with result %lu", capi_result);
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      miid                    = me_ptr ? me_ptr->miid : MIID_UNKNOWN;
      capi_buf_t *payload_ptr = &prop_array[i].payload;

      switch (prop_array[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         case CAPI_IS_ELEMENTARY:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_interface_extns_list_t))
            {
               capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
               if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                                (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
               {
                  IPC_TX_MSG(miid,
                             DBG_ERROR_PRIO,
                             "capi_test_module: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
                             payload_ptr->max_data_len);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               }
               else
               {
                  capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
                     (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

                  for (uint32_t i = 0; i < intf_ext_list->num_extensions; i++)
                  {
                     switch (curr_intf_extn_desc_ptr->id)
                     {
                        case INTF_EXTN_METADATA:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_MODULE_BUFFER_ACCESS:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_DATA_PORT_OPERATION:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE; // TODO UPDATE TO TRUE
                           break;
                        }
                        default:
                        {
                           curr_intf_extn_desc_ptr->is_supported = FALSE;
                           break;
                        }
                     }
                     IPC_TX_MSG(miid,
                                DBG_HIGH_PRIO,
                                "CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = %d",
                                curr_intf_extn_desc_ptr->id,
                                (int)curr_intf_extn_desc_ptr->is_supported);
                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               IPC_TX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "CAPI_INTERFACE_EXTENSIONS bad param size %lu",
                          payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            if (NULL == me_ptr)
            {
               IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Get property id 0x%lx, module is not allocated", prop_array[i].id);
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
               break;
            }
            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)payload_ptr->data_ptr;
            void                   *cust_payload_ptr = (void *)(cust_prop_ptr + 1);

            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO:
               {
                  if (payload_ptr->max_data_len <
                      sizeof(capi_custom_property_t) + sizeof(fwk_extn_prop_ipc_msg_callback_info_t))
                  {
                     IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Insufficient get property size %lu", payload_ptr->max_data_len);
                     return CAPI_EBADPARAM;
                  }

                  // populate IPC msg callback info
                  fwk_extn_prop_ipc_msg_callback_info_t *cb_info_ptr =
                     (fwk_extn_prop_ipc_msg_callback_info_t *)cust_payload_ptr;
                  cb_info_ptr->ext_port_trigger_shared_ptr = &me_ptr->output_trigger_info;
                  cb_info_ptr->ctrl_msg_handler            = capi_ipc_tx_fwk_extn_ipc_port_ctrl_msg_handler;
                  cb_info_ptr->data_msg_handler            = capi_ipc_tx_fwk_extn_ipc_port_data_msg_handler;
                  cb_info_ptr->callback_handle_ptr         = me_ptr;

                  payload_ptr->actual_data_len =
                     sizeof(capi_custom_property_t) + sizeof(fwk_extn_prop_ipc_msg_callback_info_t);

                  break;
               }
            }
            break;
         }
         default:
         {
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }

      if (CAPI_FAILED(capi_result))
      {
         IPC_TX_MSG(miid, DBG_HIGH_PRIO, "Get property for %#x failed with opcode %lu", prop_array[i].id, capi_result);
      }
   }
   return capi_result;
}

capi_err_t capi_ipc_tx_process_set_properties(capi_ipc_tx_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == me_ptr)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Set common property received null ptr");
      return CAPI_EBADPARAM;
   }

   capi_result |= capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, TRUE);
   uint32_t miid = me_ptr ? me_ptr->miid : MIID_UNKNOWN;
   if (CAPI_EOK != capi_result)
   {
      IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Set basic properties failed with result %lu", capi_result);
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;
   uint32_t     i          = 0;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, "Enter prop_array[%d].id %lu", i, prop_array[i].id);

      capi_buf_t *payload_ptr = &(prop_array[i].payload);
      miid                    = me_ptr ? me_ptr->miid : MIID_UNKNOWN;

      switch (prop_array[i].id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_HEAP_ID:
         case CAPI_CUSTOM_INIT_DATA:
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            break;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            // increment session ID
            if (prop_array[i].port_info.is_valid && prop_array[i].port_info.is_input_port)
            {
               me_ptr->logging_info.session_id++;
               IPC_TX_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "Updated session ID 0x%lx due to algo reset",
                          me_ptr->logging_info.session_id);
            }

            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_port_num_info_t))
            {
               IPC_TX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Set property id 0x%lx, Bad param size %lu",
                          prop_array[i].id,
                          payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_port_num_info_t *num_port_info = (capi_port_num_info_t *)payload_ptr->data_ptr;

            /*Verify number of max input/output ports supported by the module
              In some case, number of ports is less than or equal to Max in/out ports.
              This check can be changed if module can support <= max.
             */
            if ((num_port_info->num_input_ports != IPC_TX_MAX_INPUT_PORTS) ||
                (num_port_info->num_output_ports != IPC_TX_MAX_OUTPUT_PORTS))
            {
               IPC_TX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "incorrect number of input (%lu) or output (%lu) ports",
                          num_port_info->num_input_ports,
                          num_port_info->num_output_ports);
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
               break;
            }

            me_ptr->num_port_info = *num_port_info;
#ifdef DEBUG_IPC_TX
            IPC_TX_MSG(me_ptr->miid,
                       DBG_MED_PRIO,
                       "num_input_ports: %lu, num_output_ports: %lu",
                       me_ptr->num_port_info.num_input_ports,
                       me_ptr->num_port_info.num_output_ports);
#endif
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_media_fmt_v2_t))
            {
               IPC_TX_MSG(miid, DBG_HIGH_PRIO, "Received Input media format");

               // bool_t is_media_fmt_changed = FALSE;

               capi_media_fmt_v2_t *data_ptr   = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
               uint32_t             param_size = payload_ptr->actual_data_len;
               if (!ipc_tx_is_supported_media_type(data_ptr))
               {
                  CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
                  return capi_result;
               }
#ifdef DEBUG_IPC_TX
               IPC_TX_MSG(miid,
                          DBG_HIGH_PRIO,
                          "Received me_ptr->out_port_info[0].port_state %x, data_ptr->format.num_channels 0x%lx",
                          me_ptr->out_port_info[0].port_state,
                          data_ptr->format.num_channels);
#endif
               memscpy(&me_ptr->inp_media_fmt, sizeof(me_ptr->inp_media_fmt), data_ptr, sizeof(capi_media_fmt_v2_t));
               me_ptr->is_inp_media_fmt_received = TRUE;
               me_ptr->inp_media_fmt_size        = param_size;

               capi_result = capi_ipc_tx_update_media_fmt(me_ptr, me_ptr->inp_media_fmt, me_ptr->inp_media_fmt_size);
               if (capi_result != CAPI_EOK)
               {
                  IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Failed to update input media format");
                  return capi_result;
               }
               // raise event for kpps, bw, process event
               capi_result = capi_ipc_tx_raise_event(me_ptr);
               if (capi_result != CAPI_EOK)
               {
                  IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Failed to raise event");
                  return capi_result;
               }

               /* enable module buffer access extension if possible.*/
               capi_result = capi_ipc_tx_check_n_enable_buffer_access_extn(me_ptr);
            }
            else
            {
               IPC_TX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Set property id 0x%lx Bad param size %lu",
                          (uint32_t)prop_array[i].id,
                          payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                        = data_ptr->module_instance_id;
               IPC_TX_MSG(miid,
                          DBG_LOW_PRIO,
                          "This module-id 0x%08lX, instance-id 0x%08lX",
                          data_ptr->module_id,
                          me_ptr->miid);
            }
            else
            {
               IPC_TX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Set property id 0x%lx, Bad param size %lu",
                          prop_array[i].id,
                          payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         } // CAPI_MODULE_INSTANCE_ID
         default:
         {
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
      if (CAPI_FAILED(capi_result))
      {
         IPC_TX_MSG(miid, DBG_HIGH_PRIO, "Set property for %#x failed with opcode %lu", prop_array[i].id, capi_result);
      }
   }

   return capi_result;
}

capi_err_t capi_ipc_tx_process_set_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == params_ptr)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
   }
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)(_pif);

   switch (param_id)
   {
      case PARAM_ID_IPC_DATA_LOGGING_CONFIG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_ipc_data_logging_config_t))
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Capi IPC TX : Param id 0x%lx Bad param size %lu",
                       (uint32_t)param_id,
                       params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_ipc_data_logging_config_t *payload_ptr = (param_id_ipc_data_logging_config_t *)params_ptr->data_ptr;

         me_ptr->logging_info.cfg = *payload_ptr;
         if(me_ptr->logging_info.cfg.log_code)
         {
            IPC_TX_MSG(me_ptr->miid,
                        DBG_ERROR_PRIO,
                        "Capi IPC TX : Enabling data logging log_code 0x%lx",
                        me_ptr->logging_info.cfg.log_code);
         }

         break;
      }
      case FWK_EXTN_PARAM_ID_IPC_BUFFER_INFO:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_ipc_buffer_info_t))
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Capi IPC TX : Param id 0x%lx Bad param size %lu",
                       (uint32_t)param_id,
                       params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         fwk_extn_param_id_ipc_buffer_info_t *payload_ptr = (fwk_extn_param_id_ipc_buffer_info_t *)params_ptr->data_ptr;

         uint32_t old_num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_created;

         me_ptr->sh_buf_info.num_ipc_bufs_needed            = payload_ptr->num_reg_bufs + payload_ptr->num_reg_prebufs;
         me_ptr->sh_buf_info.num_ipc_prebufs_needed_to_send = payload_ptr->num_reg_prebufs;
         if (me_ptr->sh_buf_info.pending_prebuf_index_arr)
         {
            posal_memory_free(me_ptr->sh_buf_info.pending_prebuf_index_arr);
            me_ptr->sh_buf_info.pending_prebuf_index_arr = NULL;
         }

         me_ptr->sh_buf_info.num_pending_prebuffers = 0;
         if (me_ptr->sh_buf_info.num_ipc_prebufs_needed_to_send)
         {
            uint32_t alloc_size = sizeof(uint8_t) * me_ptr->sh_buf_info.num_ipc_prebufs_needed_to_send;
            me_ptr->sh_buf_info.pending_prebuf_index_arr =
               (uint8_t *)posal_memory_malloc(alloc_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
            if (NULL == me_ptr->sh_buf_info.pending_prebuf_index_arr)
            {
               IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed malloc mem for prebuffer indices");
               return capi_result;
            }
            memset(me_ptr->sh_buf_info.pending_prebuf_index_arr, 0xFF, alloc_size);
         }

         me_ptr->is_num_bufs_received = TRUE;

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "IPC buffer: ipc bufs count recieved: num_reg_bufs %lu num_reg_prebufs %lu",
                    payload_ptr->num_reg_bufs, payload_ptr->num_reg_prebufs);
#endif
         if ((old_num_bufs != me_ptr->sh_buf_info.num_ipc_bufs_needed) &&
             (me_ptr->is_inp_media_fmt_received)) // tbd: num bufs is coming in prepare state also when inpu media fmt
                                                  // is not rcvd
         {
            if (CAPI_EOK != (capi_result = capi_ipc_tx_manage_buffer(me_ptr,
                                                                     me_ptr->sh_buf_info.data_buff_size,
                                                                     me_ptr->sh_buf_info.metadata_buff_size)))
            {
               IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to manage shared memory WR buffer");
               return capi_result;
            }
         }
         break;
      }

      case PARAM_ID_IPC_DATA_LINK_INFO:
      {
         if ((sizeof(param_id_ipc_data_link_info_t) + sizeof(ipc_data_link_info_per_port_t)) >
             params_ptr->actual_data_len)
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set PARAM_ID_IPC_DATA_LINK_INFO, Bad param size %lu",
                       params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_ipc_data_link_info_t *link_info_ptr = (param_id_ipc_data_link_info_t *)(params_ptr->data_ptr);
         // currently ipc modules support one port
         ipc_data_link_info_per_port_t *ipc_tx_link_info_ptr = (ipc_data_link_info_per_port_t *)(link_info_ptr + 1);

         me_ptr->ipc_tx_link_info.ipc_port_type       = ipc_tx_link_info_ptr->ipc_port_type;
         me_ptr->ipc_tx_link_info.self_port_id        = ipc_tx_link_info_ptr->self_port_id;
         me_ptr->ipc_tx_link_info.self_module_iid     = ipc_tx_link_info_ptr->self_module_iid;
         me_ptr->ipc_tx_link_info.self_proc_domain_id = ipc_tx_link_info_ptr->self_proc_domain_id;
         me_ptr->ipc_tx_link_info.peer_port_id        = ipc_tx_link_info_ptr->peer_port_id;
         me_ptr->ipc_tx_link_info.peer_module_iid     = ipc_tx_link_info_ptr->peer_module_iid;
         me_ptr->ipc_tx_link_info.peer_proc_domain_id = ipc_tx_link_info_ptr->peer_proc_domain_id;
#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Set PARAM_ID_IPC_DATA_LINK_INFO, ipc_port_type 0x%x self_port_id 0x%x self_module_iid "
                    "0x%x self_proc_domain_id 0x%x peer_port_id 0x%x peer_module_iid 0x%x, peer_proc_domain_id 0x%x",
                    me_ptr->ipc_tx_link_info.ipc_port_type,
                    me_ptr->ipc_tx_link_info.self_port_id,
                    me_ptr->ipc_tx_link_info.self_module_iid,
                    me_ptr->ipc_tx_link_info.self_proc_domain_id,
                    me_ptr->ipc_tx_link_info.peer_port_id,
                    me_ptr->ipc_tx_link_info.peer_module_iid,
                    me_ptr->ipc_tx_link_info.peer_proc_domain_id);
#endif
         // the payload for param_id_ipc_data_link_info_t is same as fwk_extn_event_ipc_data_link_info_t
         // hence just passing the same payload for the event.
         capi_result = capi_ipc_raise_data_link_info_event(me_ptr->miid,
                                                           &me_ptr->cb_info,
                                                           (fwk_extn_event_ipc_data_link_info_t *)ipc_tx_link_info_ptr);

         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Set param INTF_EXTN_PARAM_ID_METADATA_HANDLER");
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Capi IPC TX : Param id 0x%lx Bad param size %lu",
                       (uint32_t)param_id,
                       params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            IPC_TX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set param id (0x%lx) Bad param size (%lu)",
                       (uint32_t)param_id,
                       params_ptr->max_data_len);
            return CAPI_ENEEDMORE;
         }

         fwk_extn_param_id_container_frame_duration_t *frame_len_param_ptr =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;
#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Received Set param of fwk container frame duration us = %ld",
                    frame_len_param_ptr->duration_us);
#endif
         // Set threshold according to new amount.
         // If media fmt already arrived, allocate buffers at new size.
         uint32_t frame_length_ms = frame_len_param_ptr->duration_us / 1000;
         if (me_ptr->frame_length_info.frame_dur_ms != frame_length_ms)
         {
            me_ptr->frame_length_info.frame_dur_ms          = frame_length_ms;
            me_ptr->frame_length_info.frame_dur_us          = frame_len_param_ptr->duration_us;
            me_ptr->frame_length_info.is_frame_len_received = TRUE; // tbd: do we need to reset it? in which case?

            // recalculate the data buffer size:
            //uint32_t prev_buf_len = me_ptr->sh_buf_info.data_buff_size;
            if ((TRUE == me_ptr->is_inp_media_fmt_received) &&
                (MEDIA_FMT_ID_PCM == me_ptr->inp_media_fmt.format.bitstream_format))
            {
               // todo_mdf: handle for Raw
               uint32_t unit_frame_size = CAPI_CMN_BITS_TO_BYTES(me_ptr->inp_media_fmt.format.bits_per_sample) *
                                          tu_get_unit_frame_size(me_ptr->inp_media_fmt.format.sampling_rate);

               me_ptr->sh_buf_info.frame_size_per_ch_in_bytes =
                  unit_frame_size * me_ptr->frame_length_info.frame_dur_ms;

               me_ptr->sh_buf_info.frame_size_in_bytes =
                  me_ptr->inp_media_fmt.format.num_channels * me_ptr->sh_buf_info.frame_size_per_ch_in_bytes;

               me_ptr->sh_buf_info.data_buff_size = ALIGN_128_BYTES(me_ptr->sh_buf_info.frame_size_in_bytes);
               // TBD: chcek this logic, we may need to use us
            }
            // do nothing for raw compressed as it has a fixed size.

            // Allocate the shared buffer with the default buffer count(2) if the media format is received for the first
            // time and have a valid size
            if (!me_ptr->is_num_bufs_received && me_ptr->sh_buf_info.data_buff_size)
            {
               capi_result |= capi_ipc_tx_manage_buffer(me_ptr,
                                                        me_ptr->sh_buf_info.data_buff_size,
                                                        me_ptr->sh_buf_info.metadata_buff_size);
               if (CAPI_EOK != capi_result)
               {
                  return capi_result;
               }
            }
            else
            {
#ifdef DEBUG_IPC_TX
               IPC_TX_MSG(me_ptr->miid,
                          DBG_LOW_PRIO,
                          "Did not allocate buffers with mf (nchs: %lu,bps: %lu, Sr: %lu ) is not set or frame "
                          "size %lu ms or buffers are already allocated",
                          me_ptr->inp_media_fmt.format.num_channels,
                          me_ptr->inp_media_fmt.format.bits_per_sample,
                          me_ptr->inp_media_fmt.format.sampling_rate,
                          me_ptr->frame_length_info.frame_dur_ms);
#endif
            }
         }
         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         capi_result = capi_ipc_tx_handle_intf_extn_data_port_operation(me_ptr, params_ptr);
         break;
      }

      default:
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set, unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }

         // /** Set param used by fwk to set the input/output threshold for the IPC modules. */
         // #define FWK_EXTN_PROPERTY_ID_IPC_FRAME_LENGTH_INFO  0x0A003BAD

         // /** unique port handle set by the fwk for each port ID along with link info. */
         // typedef struct fwk_extn_prop_ipc_frame_length_info_t fwk_extn_prop_ipc_frame_length_info_t;

         // /** @weakgroup fwk_extn_prop_ipc_frame_length_info_t
         // @{ */
         // struct fwk_extn_prop_ipc_frame_length_info_t
         // {
         //    bool_t   is_input;

         //    uint32_t port_id;

         //    uint32_t frame_duration_in_us;
         //    /** Containers aggregated PCM threshold duration in micro seconds. Applicable only for PCM data */

         //    uint32_t frame_length_in_bytes;
         //    /** If PCM format: its frame length in bytes for all the channels.
         //     *  If Raw compressed data: its just max per frame length in bytes.
         //     */
         // };
         // /** @} */ /* end_weakgroup fwk_extn_prop_ipc_frame_length_info_t */
         //       case FWK_EXTN_PROPERTY_ID_IPC_FRAME_LENGTH_INFO:
         //       {
         //          if (params_ptr->actual_data_len < sizeof(fwk_extn_prop_ipc_frame_length_info_t))
         //          {
         //             IPC_TX_MSG(me_ptr->miid,
         //                        DBG_ERROR_PRIO,
         //                        "Param id 0x%lx Bad param size %lu",
         //                        (uint32_t)param_id,
         //                        params_ptr->actual_data_len);
         //             capi_result |= CAPI_ENEEDMORE;
         //             break;
         //          }
         //          fwk_extn_prop_ipc_frame_length_info_t *fm_dur = (fwk_extn_prop_ipc_frame_length_info_t
         //          *)params_ptr->data_ptr;

         //          me_ptr->frame_length_info.frame_dur_us          = fm_dur->frame_duration_in_us;
         //          me_ptr->frame_length_info.frame_dur_ms          = fm_dur->frame_duration_in_us / NUM_US_PER_MS;
         //          me_ptr->frame_length_info.frame_len_bytes       = fm_dur->frame_length_in_bytes;
         //          me_ptr->frame_length_info.is_frame_len_received = TRUE;
         // #ifdef DEBUG_IPC_TX
         //          IPC_TX_MSG(me_ptr->miid,
         //                     DBG_LOW_PRIO,
         //                     "Frame duration of IPC TX configured to %lu us or %lu ms or bytes %lu",
         //                     fm_dur->frame_duration_in_us,
         //                     me_ptr->frame_length_info.frame_dur_ms,
         //                     fm_dur->frame_length_in_bytes);
         // #endif

         //          break;
         //       }
   }
   return capi_result;
}

capi_err_t capi_ipc_tx_process_get_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == params_ptr)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)_pif;

   switch (param_id)
   {
      default:
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Get unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }
   return capi_result;
}

static capi_err_t capi_ipc_raise_data_link_info_event(uint32_t                             miid,
                                                      capi_event_callback_info_t          *cb_info_ptr,
                                                      fwk_extn_event_ipc_data_link_info_t *link_info_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == cb_info_ptr->event_cb)
   {
      IPC_TX_MSG(miid, DBG_ERROR_PRIO, " Event callback is not set, Unable to ipc data link info event !");
      return CAPI_EBADPARAM;
   }

   capi_buf_t payload;
   payload.data_ptr        = (int8_t *)link_info_ptr;
   payload.actual_data_len = payload.max_data_len = sizeof(fwk_extn_event_ipc_data_link_info_t);

   result = capi_cmn_raise_data_to_dsp_svc_event(cb_info_ptr, FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO, &payload);

   if (CAPI_FAILED(result))
   {
      IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Failed to raise event FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO");
      return result;
   }
   else
   {
      IPC_TX_MSG(miid, DBG_HIGH_PRIO, "raised event FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO ");
   }

   return result;
}
