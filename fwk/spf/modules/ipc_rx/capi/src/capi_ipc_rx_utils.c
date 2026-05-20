/*==============================================================================
@file capi_ipc_rx_utils.c
@brief This file implements utility functions for the IPC RX CAPI module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
/* clang-format off */
/* No Edit History for automated refactoring. */
/* clang-format on */

/*------------------------------------------------------------------------
 * Include Files
 * -----------------------------------------------------------------------*/
#include "capi_ipc_rx_utils.h"
#include "capi_fwk_extns_ipc_port_handler.h"
#include "spf_svc_utils.h"
#include "wr_sh_mem_ep_api.h" // For shared memory endpoint API types and functions

/*------------------------------------------------------------------------
 * Static Function Definitions
 * -----------------------------------------------------------------------*/


static capi_err_t capi_ipc_raise_data_link_info_event(uint32_t                             miid,
                                                      capi_event_callback_info_t          *cb_info_ptr,
                                                      fwk_extn_event_ipc_data_link_info_t *link_info_ptr);
/**
 * \brief Converts a generic PCM interleaving type to a CAPI interleaving type.
 *
 * This function maps platform-specific PCM interleaving types (e.g., PCM_INTERLEAVED,
 * PCM_DEINTERLEAVED_PACKED, PCM_DEINTERLEAVED_UNPACKED) to their corresponding
 * CAPI standard interleaving types. If an unsupported value is encountered,
 * CAPI_INTERLEAVED is returned by default to ensure a valid return.
 *
 * \param[in] pcm_interleaving Generic PCM interleaving type from the platform.
 *
 * \return Corresponding CAPI interleaving type.
 */
static inline capi_interleaving_t generic_interleaved_to_capi_interleaved(uint16_t pcm_interleaving)
{
   capi_interleaving_t capi_interleaving = CAPI_INTERLEAVED; /* Default to CAPI_INTERLEAVED */

   switch (pcm_interleaving)
   {
      case PCM_INTERLEAVED:
      {
         capi_interleaving = CAPI_INTERLEAVED;
         break;
      }
      case PCM_DEINTERLEAVED_PACKED:
      {
         capi_interleaving = CAPI_DEINTERLEAVED_PACKED;
         break;
      }
      case PCM_DEINTERLEAVED_UNPACKED:
      {
         capi_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;
         break;
      }
      default:
      {
         /* Unsupported value, default CAPI_INTERLEAVED is already set. */
         IPC_RX_MSG(MIID_UNKNOWN,
                    DBG_ERROR_PRIO,
                    "Error: Unsupported PCM interleaving type: %u. Defaulting to CAPI_INTERLEAVED.",
                    pcm_interleaving);
         break;
      }
   }
   return capi_interleaving;
}

/*------------------------------------------------------------------------
 * Function Definitions
 * -----------------------------------------------------------------------*/

/**
 * \brief Generates and sends a GPR acknowledgement packet.
 *
 * This function handles sending acknowledgements for various GPR commands,
 * including EOS (End-Of-Stream) and data buffer done messages. It constructs
 * the appropriate GPR response packet and sends it back to the source,
 * then frees the original received packet.
 *
 * \param[in] me_ptr         Pointer to the CAPI IPC_RX instance (used for module instance ID logging).
 *                           Can be NULL if `packet_ptr` is NULL.
 * \param[in] packet_ptr     The received GPR packet that requires an ACK. This packet will be freed. Must not be NULL.
 * \param[in] status         Status of the operation being acknowledged (AR_EOK for success, or an error code).
 * \param[in] ack_payload_ptr Optional. Pointer to the payload to be included in the ACK packet. Can be NULL.
 * \param[in] size           Size of the `ack_payload_ptr` in bytes. Must be 0 if `ack_payload_ptr` is NULL.
 * \param[in] ack_opcode     Optional. The opcode for the ACK packet. If 0, a basic command end is performed.
 *
 * \return AR_EOK on successful sending of ACK and freeing of the packet.
 *         Returns AR_EBADPARAM if `packet_ptr` is NULL.
 *         Returns other `AR_DID_FAIL` codes if `__gpr_cmd_alloc_send` or `__gpr_cmd_end_command` fails.
 */
ar_result_t ipc_rx_gpr_generate_ack(capi_ipc_rx_t *me_ptr,
                                    gpr_packet_t  *packet_ptr,
                                    ar_result_t    status,
                                    void          *ack_payload_ptr,
                                    uint32_t       size,
                                    uint32_t       ack_opcode)
{
   ar_result_t result = AR_EOK; /* Initialize ar_result_t */
   /* Module instance ID */
   uint32_t miid = (NULL != me_ptr) ? me_ptr->miid : MIID_UNKNOWN; /* Module instance ID */

   if (NULL == packet_ptr)
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: GPR ACK failed: received NULL packet pointer.");
      return AR_EBADPARAM;
   }

   switch (packet_ptr->opcode) /* Handle different GPR packet opcodes */
   {
      case DATA_CMD_WR_SH_MEM_EP_EOS:
      {
         IPC_RX_MSG(miid, DBG_HIGH_PRIO, "INFO: Sending DATA_CMD_RSP_WR_SH_MEM_EP_EOS_RENDERED to client.");

         gpr_cmd_alloc_send_t args;
         args.src_domain_id = packet_ptr->dst_domain_id;
         args.dst_domain_id = packet_ptr->src_domain_id;
         args.src_port      = packet_ptr->dst_port;
         args.dst_port      = packet_ptr->src_port;
         args.token         = packet_ptr->token;
         args.opcode        = DATA_CMD_RSP_WR_SH_MEM_EP_EOS_RENDERED;
         args.payload       = NULL;
         args.payload_size  = 0;
         args.client_data   = 0;

         result = __gpr_cmd_alloc_send(&args); /* Attempt to allocate and send the ACK */
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to send EOS rendered ACK: %lu.", result);
         }

         __gpr_cmd_free(packet_ptr); /* Always free the original packet after processing */
         break;
      }

      case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
      {
         IPC_RX_MSG(miid,
                    DBG_LOW_PRIO,
                    "INFO: Port=0x%x: status 0x%lx, ACK opcode 0x%x, Payload Addr=0x%p, Size=%lu, Token=0x%lX.",
                    packet_ptr->dst_port,
                    (uint32_t)status,
                    ack_opcode,
                    (uintptr_t)ack_payload_ptr,
                    size,
                    packet_ptr->token);

         gpr_cmd_alloc_send_t args;
         args.src_domain_id = packet_ptr->dst_domain_id;
         args.dst_domain_id = packet_ptr->src_domain_id;
         args.src_port      = packet_ptr->dst_port;
         args.dst_port      = packet_ptr->src_port;
         args.token         = packet_ptr->token;
         args.opcode        = ack_opcode;
         args.payload       = ack_payload_ptr;
         args.payload_size  = size;
         args.client_data   = 0;

         result = __gpr_cmd_alloc_send(&args); /* Attempt to allocate and send the ACK */
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to send data buffer done ACK: %lu.", result);
         }

         __gpr_cmd_free(packet_ptr); /* Always free the original packet after processing */
         break;
      }

      default:
      {
         if (ack_payload_ptr && ack_opcode) /* For other opcodes, either send a custom ACK or a basic command end */
         {
            IPC_RX_MSG(miid,
                       DBG_LOW_PRIO,
                       "INFO: Port=0x%x: status 0x%lx, Custom ACK: 0x%lx, Payload Addr=0x%p, Size=%lu.",
                       packet_ptr->dst_port,
                       (uint32_t)status,
                       ack_opcode,
                       (uintptr_t)ack_payload_ptr,
                       size);

            gpr_cmd_alloc_send_t args;
            args.src_domain_id = packet_ptr->dst_domain_id;
            args.dst_domain_id = packet_ptr->src_domain_id;
            args.src_port      = packet_ptr->dst_port;
            args.dst_port      = packet_ptr->src_port;
            args.token         = packet_ptr->token;
            args.opcode        = ack_opcode;
            args.payload       = ack_payload_ptr;
            args.payload_size  = size;
            args.client_data   = 0;

            result = __gpr_cmd_alloc_send(&args); /* Attempt to allocate and send the ACK */
            if (AR_DID_FAIL(result))
            {
               IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to send custom ACK: %lu.", result);
            }

            __gpr_cmd_free(packet_ptr); /* Always free the original packet after processing */
         }
         else
         {
            IPC_RX_MSG(miid,
                       DBG_LOW_PRIO,
                       "INFO: Port=0x%x: status 0x%lx, Basic ACK: 0x%lx, Payload Addr=0x%p, Size=%lu. Ending command.",
                       packet_ptr->dst_port,
                       (uint32_t)status,
                       packet_ptr->opcode,
                       (uintptr_t)ack_payload_ptr,
                       size);
            result = __gpr_cmd_end_command(packet_ptr, status); /* End the command without explicit ACK payload */
            if (AR_DID_FAIL(result))
            {
               IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to end command: %lu.", result);
            }
         }
         break;
      }
   }

   if (AR_DID_FAIL(result))
   {
      IPC_RX_MSG(miid,
                 DBG_ERROR_PRIO,
                 "Error: Port=0x%x: Failed to send ACK, final result 0x%lx.",
                 packet_ptr->dst_port,
                 (uint32_t)result);
   }

   return result;
}

/**
 * \brief Parses input PCM media format received from a GPR client.
 *
 * This function extracts relevant PCM media format information from the
 * received GPR media format payload and populates the CAPI media format structure.
 * It performs validation checks on payload size and data formats.
 *
 * \param[in] me_ptr            Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] media_fmt_ptr     Pointer to the `media_format_t` structure received from GPR. Must not be NULL.
 * \param[in] is_data_path      Flag indicating if the media format came over the data path. (Currently not used)
 * \param[out] input_media_fmt_ptr Pointer to the CAPI media format structure to populate. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         Returns AR_EBADPARAM if any input pointer is NULL, payload size is invalid,
 *         or channel mapping size is incorrect.
 *         Returns AR_EUNSUPPORTED if `fmt_id` is not PCM or `data_format` is unsupported.
 */
static ar_result_t ipc_rx_parse_inp_pcm_media_fmt_from_gpr_client(capi_ipc_rx_t       *me_ptr,
                                                                  media_format_t      *media_fmt_ptr,
                                                                  bool_t               is_data_path,
                                                                  capi_media_fmt_v2_t *input_media_fmt_ptr)
{
   ar_result_t result = AR_EOK; /* Initialize ar_result_t */

   if (NULL == me_ptr || NULL == media_fmt_ptr || NULL == input_media_fmt_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer received in ipc_rx_parse_inp_pcm_media_fmt_from_gpr_client.");
      return AR_EBADPARAM;
   }

   /* Validate media format ID. Only PCM is supported. */
   if (MEDIA_FMT_ID_PCM != media_fmt_ptr->fmt_id)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Unsupported media format ID 0x%lX. Only PCM (0x%lX) is supported.",
                 media_fmt_ptr->fmt_id,
                 MEDIA_FMT_ID_PCM);
      return AR_EUNSUPPORTED;
   }

   /* Ensure payload size is sufficient for payload_media_fmt_pcm_t */
   if (media_fmt_ptr->payload_size < sizeof(payload_media_fmt_pcm_t))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Invalid PCM media format payload size: %lu, expected at least %lu.",
                 media_fmt_ptr->payload_size,
                 sizeof(payload_media_fmt_pcm_t));
      return AR_EBADPARAM;
   }
   payload_media_fmt_pcm_t *pcm_fmt_ptr = (payload_media_fmt_pcm_t *)(media_fmt_ptr + 1);

   input_media_fmt_ptr->format.bitstream_format = media_fmt_ptr->fmt_id;

   /* Handle data format (Fixed-point or Floating-point) */
   switch (media_fmt_ptr->data_format) /* Corrected: use media_fmt_ptr->data_format */
   {
      case DATA_FORMAT_FIXED_POINT:
      {
         input_media_fmt_ptr->header.format_header.data_format = CAPI_FIXED_POINT;
         input_media_fmt_ptr->format.q_factor                  = pcm_fmt_ptr->q_factor;
         break;
      }
      case DATA_FORMAT_FLOATING_POINT:
      {
         input_media_fmt_ptr->header.format_header.data_format = CAPI_FLOATING_POINT;
         input_media_fmt_ptr->format.q_factor = INVALID_VALUE; /* Q factor is not applicable for floating point */
         break;
      }

         /* ToDo: Add case for raw handling */

      default:
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Unsupported data format %d.",
                    media_fmt_ptr->data_format); /* Corrected: use media_fmt_ptr->data_format */
         return AR_EUNSUPPORTED;                 /* Return immediately on unsupported data format */
      }
   }

   /* Populate CAPI media format structure with PCM details */
   input_media_fmt_ptr->format.bits_per_sample   = pcm_fmt_ptr->bits_per_sample;
   input_media_fmt_ptr->format.sampling_rate     = pcm_fmt_ptr->sample_rate;
   input_media_fmt_ptr->format.num_channels      = pcm_fmt_ptr->num_channels;
   input_media_fmt_ptr->format.data_interleaving = generic_interleaved_to_capi_interleaved(pcm_fmt_ptr->interleaved);
   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "ss_dbg: pcm_fmt_ptr->interleaved %d input_media_fmt_ptr->format.data_interleaving %d.",
              pcm_fmt_ptr->interleaved,
              input_media_fmt_ptr->format.data_interleaving); /* Corrected: use media_fmt_ptr->data_format */

   /* Handle channel mapping */
   uint8_t *channel_mapping = (uint8_t *)(pcm_fmt_ptr + 1);
   for (uint32_t ch = 0; ch < input_media_fmt_ptr->format.num_channels; ch++)
   {
      input_media_fmt_ptr->format.channel_type[ch] = channel_mapping[ch];
   }

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Successfully parsed input PCM media format.");
   return result;
}

/**
 * \brief Handles input media format control commands received from GPR client.
 *
 * If a media format is received over the data path while pending data exists,
 * it is cached. Otherwise, it processes the new media format, updates internal
 * media format structures, and raises necessary events. It also reallocates
 * the internal buffer if the size changes to match the expected output buffer size.
 *
 * \param[in] me_ptr       Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] pkt_ptr      Pointer to the GPR packet containing the media format command. Must not be NULL.
 * \param[in] is_data_path Flag indicating if the command came over the data path.
 *
 * \return CAPI_EOK on success, or an error code if media format parsing or
 *         internal buffer allocation fails.
 *         CAPI_EBADPARAM if `me_ptr` or `pkt_ptr` is NULL, or media format payload is NULL.
 *         CAPI_EUNSUPPORTED if data format is unsupported.
 *         CAPI_ENOMEMORY if internal buffer allocation fails.
 *         Other CAPI error codes from internal helper functions.
 */
capi_err_t ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(capi_ipc_rx_t *me_ptr,
                                                                    gpr_packet_t  *pkt_ptr,
                                                                    bool_t         is_data_path)
{
   capi_err_t capi_result = CAPI_EOK;
   /* Initialize ar_result_t */
   ar_result_t result = AR_EOK;

   /* Validate input pointers */
   if (NULL == me_ptr || NULL == pkt_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer received in ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client.");
      return CAPI_EBADPARAM;
   }

   media_format_t *media_fmt_ptr = GPR_PKT_GET_PAYLOAD(media_format_t, pkt_ptr);
   if (NULL == media_fmt_ptr)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: NULL media format payload received from GPR packet.");
      return CAPI_EBADPARAM;
   }

   /* If media format is received over data path and there is still pending data,
    * mark it as pending and handle after current data is processed. */
   if (is_data_path && (me_ptr->ipc_buf_actual_data_len > 0 || me_ptr->int_buf_actual_data_len > 0))
   {
      me_ptr->is_pending_mf = TRUE;
      me_ptr->mf_packet_ptr = pkt_ptr;
      /* Indicate that the container should not request new buffers, as MF is pending */
      me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED;
      IPC_RX_MSG(me_ptr->miid,
                 DBG_MED_PRIO,
                 "INFO: Media format received over data path with pending data. Marked as pending. ipc_buf_len=%lu, "
                 "int_buf_len=%lu.",
                 me_ptr->ipc_buf_actual_data_len,
                 me_ptr->int_buf_actual_data_len);
      return CAPI_EOK; /* Return success as the packet is handled for deferred processing */
   }

   me_ptr->is_pending_mf = FALSE; /* Clear pending flag if not deferred */

   /* Clear existing media format info before parsing new one */
   memset(&me_ptr->input_media_fmt, 0, sizeof(capi_media_fmt_v2_t));
   memset(&me_ptr->output_media_fmt, 0, sizeof(capi_media_fmt_v2_t));

   /* Parse input media format based on its data format */
   switch (media_fmt_ptr->data_format)
   {
      case DATA_FORMAT_FIXED_POINT:
      case DATA_FORMAT_FLOATING_POINT:
      {
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Processing input media format (Fixed/Floating Point).");
         result = ipc_rx_parse_inp_pcm_media_fmt_from_gpr_client(me_ptr,
                                                                 media_fmt_ptr,
                                                                 is_data_path,
                                                                 &me_ptr->input_media_fmt);
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to parse input PCM media format: %lu.", result);
            return (capi_err_t)result; /* Propagate the specific AR_result as capi_err_t */
         }
         break;
      }
      default:
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Unsupported data format 0x%lX in media format command.",
                    media_fmt_ptr->data_format);
         return CAPI_EUNSUPPORTED;
      }
   }

   /* Copy input media format to output media format. Output MF might be tweaked later. */
   memscpy(&me_ptr->output_media_fmt,
           sizeof(me_ptr->output_media_fmt),
           &me_ptr->input_media_fmt,
           sizeof(capi_media_fmt_v2_t));

   /* If input is deinterleaved packed, output should be deinterleaved unpacked v2 */
   if (CAPI_CMN_IS_PCM_FORMAT(me_ptr->input_media_fmt.header.format_header.data_format) &&
       CAPI_DEINTERLEAVED_PACKED == me_ptr->input_media_fmt.format.data_interleaving)
   {
      me_ptr->output_media_fmt.format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;
   }

   /* Calculate expected output buffer size based on container duration and media format */

   /* ToDo Do this handling only for PCM.
    * Analyze raw handling. Might need to get rx duration in bytes */
   if(0 != me_ptr->ipc_rx_cntr_duration_us)
   {
      uint32_t exp_out_buf_size = me_ptr->input_media_fmt.format.num_channels *
                                  capi_cmn_us_to_bytes_per_ch(me_ptr->ipc_rx_cntr_duration_us,
                                                              me_ptr->input_media_fmt.format.sampling_rate,
                                                              me_ptr->input_media_fmt.format.bits_per_sample);

      /* Reallocate internal buffer if necessary (size changed or not yet allocated) */
      if ((0 != exp_out_buf_size) && (me_ptr->out_buf_size != exp_out_buf_size))
      {
         /* Free existing buffer if it was allocated */
         if (NULL != me_ptr->int_buf_ptr)
         {
            posal_memory_free(me_ptr->int_buf_ptr);
            me_ptr->int_buf_ptr = NULL; /* Ensure pointer is NULL after freeing */
         }

         /* Allocate new internal buffer */
         me_ptr->int_buf_ptr = (int8_t *)posal_memory_malloc(exp_out_buf_size, POSAL_HEAP_DEFAULT);

         if (NULL == me_ptr->int_buf_ptr)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to allocate internal buffer, requested bytes %lu.",
                       exp_out_buf_size);
            return CAPI_ENOMEMORY;
         }
         else
         {
            me_ptr->out_buf_size                 = exp_out_buf_size;
            me_ptr->int_buf_actual_data_len      = 0; /* Reset actual data length as it's a new buffer */
            me_ptr->int_buf_max_data_len         = exp_out_buf_size;
            me_ptr->int_buf_max_data_len_per_buf = topo_div_num(exp_out_buf_size, ipc_rx_get_num_bufs_from_mf(me_ptr));

            IPC_RX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "INFO: Internal buffer allocated successfully int_buf_max_data_len %lu "
                       "int_buf_max_data_len_per_buf %lu bytes ",
                       me_ptr->int_buf_max_data_len,
                       me_ptr->int_buf_max_data_len_per_buf);
         }
      }
   }

   /* Raise event for IPC RX status update (KPPS, BW, Process Check) */
   capi_result = capi_ipc_rx_raise_event(me_ptr);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Failed to raise IPC RX event after media format update: %lu.",
                 capi_result);
      return capi_result;
   }

   /* Raise event for output media format to the container */
   capi_result = capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->output_media_fmt, FALSE, 0);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Failed to raise output media format event to container: %lu.",
                 capi_result);
      return capi_result;
   }

   IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Raised output media format event.");

   /* Raise event for output media format to the container */
   capi_ipc_rx_check_n_enable_buffer_extn(me_ptr);

   /* Update trigger conditionally based on dfs
    * If dfs flowing, then BUF NEEDED
    * If dfs at gap, then BUF NEEDED OPTIONALLY
    * */
   if (IPC_RX_DFS_FLOWING == me_ptr->dfs)
   {
      me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
   }
   else
   {
      me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;
   }

   return CAPI_EOK; // Return success if all operations completed successfully
}

/**
 * \brief Frees the input data command and sends acknowledgment.
 *
 * This function handles the completion of an input data command,
 * constructs the appropriate response payload (including data and metadata
 * buffer information), and sends an ACK back to the GPR client. It also
 * invalidates cache lines and decrements reference counts for shared memory
 * buffers.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] packet_ptr Pointer to the GPR packet to be freed and acknowledged. Must not be NULL.
 * \param[in] status     Result status to be included in the acknowledgement.
 * \param[in] is_flush   Flag indicating if the operation is a flush (influences `md_status`).
 *
 * \return AR_EOK on success.
 *         Returns AR_EBADPARAM if `packet_ptr` is NULL or if `write_payload_ptr` is NULL.
 *         Returns other `AR_DID_FAIL` codes if ACK generation fails.
 *
 *
 */
ar_result_t ipc_rx_free_input_data_cmd(capi_ipc_rx_t *me_ptr,
                                       gpr_packet_t  *packet_ptr,
                                       ar_result_t    status,
                                       bool_t         is_flush,
                                       bool_t         is_ref_counted)
{
   ar_result_t result = AR_EOK;

   if (NULL == packet_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL packet_ptr received in ipc_rx_free_input_data_cmd.");
      return AR_EBADPARAM;
   }

   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t write_done_payload;
   data_cmd_wr_sh_mem_ep_data_buffer_v2_t         *write_payload_ptr =
      (data_cmd_wr_sh_mem_ep_data_buffer_v2_t *)GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   if (NULL == write_payload_ptr)
   {
      /* If payload is NULL, cannot retrieve original buffer info. Set status to bad param. */
      memset(&write_done_payload, 0, sizeof(write_done_payload));
      write_done_payload.data_status = (int32_t)AR_EBADPARAM;
      write_done_payload.md_status   = (int32_t)AR_EBADPARAM;
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Failed to get write payload from GPR packet. Cannot construct valid ACK payload.");
      /* Proceed to generate ACK with bad param status */
   }
   else
   {
      // Populate ACK payload with original buffer information and current status
      write_done_payload.data_buf_addr_lsw   = write_payload_ptr->data_buf_addr_lsw;
      write_done_payload.data_buf_addr_msw   = write_payload_ptr->data_buf_addr_msw;
      write_done_payload.data_mem_map_handle = write_payload_ptr->data_mem_map_handle;
      write_done_payload.data_status         = status; /* Data processing status from module */

      write_done_payload.md_buf_addr_lsw   = write_payload_ptr->md_buf_addr_lsw;
      write_done_payload.md_buf_addr_msw   = write_payload_ptr->md_buf_addr_msw;
      write_done_payload.md_mem_map_handle = write_payload_ptr->md_mem_map_handle;
    write_done_payload.md_status         = (is_flush) ? (uint32_t)AR_EOK : (uint32_t)me_ptr->md_buf_info.md_status; /* Metadata status, AR_EOK if flushing */

      /* Invalidate cache and decrement reference count for data buffer if mapped */
      if (NULL != me_ptr->ipc_buf_virtual_addr)
      {
         posal_cache_invalidate_v2(&me_ptr->ipc_buf_virtual_addr, me_ptr->ipc_buf_size);
      if (0 != write_payload_ptr->data_mem_map_handle && is_ref_counted)
         {
            ar_result_t dec_ref_res =
               posal_memorymap_shm_decr_refcount(apm_get_mem_map_client(), write_payload_ptr->data_mem_map_handle);
            if (AR_DID_FAIL(dec_ref_res))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to decrement ref count for data buffer handle 0x%lx, result: %lu.",
                          write_payload_ptr->data_mem_map_handle,
                          dec_ref_res);
               result |= dec_ref_res; /* Aggregate error */
            }
         }
      me_ptr->ipc_buf_virtual_addr = NULL;
      }

      /* Invalidate cache and decrement reference count for metadata buffer if mapped */
      if (NULL != me_ptr->md_buf_info.md_buf_virtual_addr)
      {
         posal_cache_invalidate_v2(&me_ptr->md_buf_info.md_buf_virtual_addr, me_ptr->md_buf_info.md_buf_size);
         me_ptr->md_buf_info.md_buf_virtual_addr = NULL;
      }
   }

   // destroy all the md stored in the current buffer handle
   result |= ipc_rx_destroy_all_md(me_ptr, &me_ptr->md_buf_info.md_list_ptr);
   memset(&me_ptr->md_buf_info, 0, sizeof(me_ptr->md_buf_info));

   /* Generate and send ACK for the received packet */
   ar_result_t ack_gen_res = ipc_rx_gpr_generate_ack(me_ptr,
                                                     packet_ptr,
                                                     status,
                                                     &write_done_payload,
                                                     sizeof(write_done_payload),
                                                     DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2);
   if (AR_DID_FAIL(ack_gen_res))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Failed to generate ACK for input data command: %lu.",
                 ack_gen_res);
      result |= ack_gen_res; /* Aggregate error */
   }

   /* Clear the IPC buffer packet pointer as it's now handled */
   me_ptr->ipc_buf_packet_ptr = NULL;

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Input data command free and ACK complete.");
   return result;
}

static void capi_ipc_rx_get_virtual_address_from_mem_handle(uint64_t *data_buf_virtual_addr,
                                                            uint64_t  mem_map_virtual_addr,
                                                            uint32_t  buf_addr_lsw,
                                                            uint32_t  buf_addr_msw,
                                                            uint32_t  buf_size,
                                                            uint32_t  miid)
{
   uint64_t phy_addr_64bits = ((uint64_t)buf_addr_msw << 32) | buf_addr_lsw;
   //TBD: should we chcek for out of bounds before assigning?
   *data_buf_virtual_addr   = mem_map_virtual_addr + phy_addr_64bits;
}

/**
 * \brief Sets up the GPR client for an input data buffer.
 *
 * This function validates the incoming GPR data packet, retrieves virtual
 * addresses for data and metadata buffers from shared memory handles,
 * updates module's internal state, and decides whether the module needs
 * more data or can proceed with processing. It performs alignment checks
 * and ensures buffer sizes are valid before mapping.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] packet_ptr Pointer to the GPR packet containing the data buffer info. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         Returns AR_EBADPARAM if `me_ptr` or `packet_ptr` is NULL, payload is NULL,
 *                              buffers are unaligned, or invalid buffer data units.
 *         Returns AR_EOK if both data and metadata buffer sizes are zero (no-op).
 *         Returns AR_EBADPARAM for invalid packet size for in-band metadata.
 *         Returns other `AR_DID_FAIL` codes for memory mapping issues or data copy failures.
 */
ar_result_t ipc_rx_input_data_buffer_set_up_gpr_client_v2(capi_ipc_rx_t *me_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;

   /* ToDo We can avoid these cache line checks if use case involves ipc tx.
    * Need these cache line checks if ipc rx acts as WR SHMEM EP. */

   data_cmd_wr_sh_mem_ep_data_buffer_v2_t *pDataPayload =
      GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   if (NULL == pDataPayload) /* Validate GPR payload */
   {
    IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Cannot get GPR data payload from packet. Returning with AR_EBADPARAM.");
      return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, FALSE);
   }
   if (0 != pDataPayload->data_mem_map_handle)
   {
      ar_result_t inc_ref_res =
         posal_memorymap_shm_incr_refcount(apm_get_mem_map_client(), pDataPayload->data_mem_map_handle);
      if (AR_DID_FAIL(inc_ref_res))
      {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                     "Error: Failed to increment ref count for data buffer handle 0x%lx, result: %lu.",
                     pDataPayload->data_mem_map_handle,
                     inc_ref_res);
         result |= inc_ref_res; /* Aggregate error */
      }
   }

   /* If both data and metadata buffer sizes are zero, there's nothing to process. */
   if (0 == pDataPayload->data_buf_size && 0 == pDataPayload->md_buf_size)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "INFO: Received input buffer with zero data and zero metadata size. Returning as no-op.");
    return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EOK, FALSE, TRUE);
   }

   /* Validate data buffer alignment */
   if (0 != (pDataPayload->data_buf_addr_lsw & (CACHE_ALIGNMENT - 1)))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Input data buffer address 0x%lx not %lu-byte aligned. Returning with AR_EBADPARAM.",
                 pDataPayload->data_buf_addr_lsw,
                 CACHE_ALIGNMENT);
      return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, TRUE);
   }

   /* Validate metadata buffer alignment if it's out-of-band */
   if (0 != pDataPayload->md_mem_map_handle)
   {
      if (0 != (pDataPayload->md_buf_addr_lsw & (CACHE_ALIGNMENT - 1)))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Input metadata buffer address 0x%lx not %lu-byte aligned. Returning with AR_EBADPARAM.",
                    pDataPayload->md_buf_addr_lsw,
                    CACHE_ALIGNMENT);
         return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, TRUE);
      }
   }
   else /* In-band metadata: validate packet size to ensure metadata is fully contained */
   {
      uint32_t pkt_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
      uint32_t expected_payload_size =
         sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t) + pDataPayload->md_buf_size + pDataPayload->data_buf_size;

      if (pkt_size < expected_payload_size)
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Input GPR packet size %lu less than required md_buf_size %lu  data_buf_size %lu total size "
                    "%lu for in-band metadata. Returning with "
                    "AR_EBADPARAM.",
                    pkt_size,
                    pDataPayload->md_buf_size,
                    pDataPayload->data_buf_size,
                    expected_payload_size);
         return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, TRUE);
      }
   }

   uint64_t data_buf_virtual_addr = 0;

   /* ToDo We can use ipc buf actual data len for this as well. */
   me_ptr->ipc_buf_size = pDataPayload->data_buf_size;

  uint32_t num_bufs = ipc_rx_get_num_bufs_from_mf(me_ptr);
  me_ptr->ipc_buf_size_per_buf    = topo_div_num(me_ptr->ipc_buf_size, num_bufs);

  /* For PCM/packetized data, ensure data buffer size is an integer multiple of channel unit size */
  if (CAPI_FIXED_POINT == me_ptr->input_media_fmt.header.format_header.data_format)
  {
     uint32_t unit_size = me_ptr->input_media_fmt.format.num_channels *
                          TOPO_BITS_TO_BYTES(me_ptr->input_media_fmt.format.bits_per_sample);
     if (0 == unit_size) /* Avoid division by zero */
     {
        IPC_RX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Error: Unit size is zero due to invalid media format (num_channels or bits_per_sample). "
                   "Returning with AR_EBADPARAM.");
        return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, TRUE);
     }

     if (0 != (pDataPayload->data_buf_size % unit_size))
     {
        IPC_RX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Error: Input data buffer size %lu is not an integer multiple of channel unit size %lu. "
                   "Returning with AR_EBADPARAM.",
                   pDataPayload->data_buf_size,
                   unit_size);
        return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, AR_EBADPARAM, FALSE, TRUE);
     }
  }

  if (me_ptr->data_mem_map_hdl_entry.mem_map_handle == pDataPayload->data_mem_map_handle)
  {
     capi_ipc_rx_get_virtual_address_from_mem_handle(&data_buf_virtual_addr,
                                                     me_ptr->data_mem_map_hdl_entry.mem_map_virtual_addr,
                                                     pDataPayload->data_buf_addr_lsw,
                                                     pDataPayload->data_buf_addr_msw,
                                                     pDataPayload->data_buf_size,
                                                     me_ptr->miid);
  }
  else
  {
     /* Get virtual address for shared memory handle */
     result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                  pDataPayload->data_mem_map_handle,
                                                                  0,
                                                                  0,
                                                                  (pDataPayload->data_buf_addr_lsw +
                                                                   pDataPayload->data_buf_size),
                                                                  FALSE, /* is_ref_counted: Increment ref count */
                                                                  &me_ptr->data_mem_map_hdl_entry.mem_map_virtual_addr);
     /* Get virtual address for data buffer from shared memory handle */
     result |= posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                   pDataPayload->data_mem_map_handle,
                                                                   pDataPayload->data_buf_addr_lsw,
                                                                   pDataPayload->data_buf_addr_msw,
                                                                   pDataPayload->data_buf_size,
                                                                   FALSE, /* is_ref_counted: Increment ref count */
                                                                   &data_buf_virtual_addr);
     if (AR_DID_FAIL(result))
     {
        IPC_RX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Error: Failed to get virtual address for data buffer (paddr: 0x%lx%lx), result: %lu. Returning "
                   "with error.",
                   pDataPayload->data_buf_addr_msw,
                   pDataPayload->data_buf_addr_lsw,
                   result);
        return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, result, FALSE, TRUE);
     }
     me_ptr->data_mem_map_hdl_entry.mem_map_handle = pDataPayload->data_mem_map_handle;
  }

  me_ptr->ipc_buf_virtual_addr    = (int8_t *)data_buf_virtual_addr;
  me_ptr->ipc_buf_actual_data_len = me_ptr->ipc_buf_size; /* Initially, all data is available */

  IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Received a WR V2 buffer of size %lu bytes.", me_ptr->ipc_buf_size);

  uint64_t md_buf_virtual_addr   = 0;
  uint32_t wr_shm_md_buffer_size = pDataPayload->md_buf_size;
  if (wr_shm_md_buffer_size > 0)
  {
     if (0 != pDataPayload->md_mem_map_handle) /* Out-of-band metadata */
     {
        if (me_ptr->md_mem_map_hdl_entry.mem_map_handle == pDataPayload->md_mem_map_handle)
        {
           capi_ipc_rx_get_virtual_address_from_mem_handle(&md_buf_virtual_addr,
                                                           me_ptr->md_mem_map_hdl_entry.mem_map_virtual_addr,
                                                           pDataPayload->md_buf_addr_lsw,
                                                           pDataPayload->md_buf_addr_msw,
                                                           wr_shm_md_buffer_size,
                                                           me_ptr->miid);
        }
        else
        {
           /* Get virtual address for shared memory handle */
           result =
              posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                  pDataPayload->md_mem_map_handle,
                                                                  0,
                                                                  0,
                                                                  (pDataPayload->data_buf_addr_lsw +
                                                                   pDataPayload->data_buf_size),
                                                                  FALSE, // is_ref_counted: Increment ref count
                                                                  &me_ptr->md_mem_map_hdl_entry.mem_map_virtual_addr);

           /* Get virtual address for metadata buffer from shared memory handle */
           result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                        pDataPayload->md_mem_map_handle,
                                                                        pDataPayload->md_buf_addr_lsw,
                                                                        pDataPayload->md_buf_addr_msw,
                                                                        wr_shm_md_buffer_size,
                                                                        FALSE, /* is_ref_counted: Increment ref count */
                                                                        &md_buf_virtual_addr);
           if (AR_DID_FAIL(result))
           {
              IPC_RX_MSG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "Error: Failed to get virtual address for metadata buffer (paddr: 0x%lx%lx), result: %lu. "
                         "Marking metadata status as bad param.",
                         pDataPayload->md_buf_addr_msw,
                         pDataPayload->md_buf_addr_lsw,
                         result);
              me_ptr->md_buf_info.md_status = AR_EBADPARAM; /* Mark metadata status as failed */
              return ipc_rx_free_input_data_cmd(me_ptr,
                                                packet_ptr,
                                                result,
                                                FALSE,
                                                TRUE); /* Free resources and return */
           }
           me_ptr->md_mem_map_hdl_entry.mem_map_handle = pDataPayload->md_mem_map_handle;
        }
        me_ptr->md_buf_info.md_buf_virtual_addr = (int8_t *)md_buf_virtual_addr;

        /* Invalidate the cache before reading from shared memory. */
        posal_cache_invalidate_v2(&me_ptr->md_buf_info.md_buf_virtual_addr, wr_shm_md_buffer_size);
     }
     else /* In-band metadata */
     {
        /* Metadata starts immediately after the data command payload */
        me_ptr->md_buf_info.md_buf_virtual_addr =
           (int8_t *)(pDataPayload) + sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t);
        /* Invalidate the cache if in-band metadata is present and needs to be read. */
        posal_cache_invalidate_v2(&me_ptr->md_buf_info.md_buf_virtual_addr, wr_shm_md_buffer_size);
     }
     me_ptr->md_buf_info.md_mem_map_handle = pDataPayload->md_mem_map_handle;
     me_ptr->md_buf_info.md_buf_size       = pDataPayload->md_buf_size;

     result = ipc_rx_populate_metadata_from_ipc_buffer(me_ptr, packet_ptr);
     if (AR_DID_FAIL(result))
     {
        IPC_RX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Error: Failed to get populate metadata from the buffer (paddr: 0x%lx%lx), result: %lu",
                   pDataPayload->md_buf_addr_msw,
                   pDataPayload->md_buf_addr_lsw,
                   result);
        me_ptr->md_buf_info.md_status = AR_EBADPARAM;                               /* Mark metadata status as failed */
        return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, result, FALSE, TRUE); /* Free resources and return */
     }
  }

  /* Invalidate the data cache before reading from shared memory. */
  if (NULL != me_ptr->ipc_buf_virtual_addr && me_ptr->ipc_buf_size > 0)
  {
     posal_cache_invalidate_v2(&me_ptr->ipc_buf_virtual_addr, me_ptr->ipc_buf_size);
  }

  /* Store the GPR packet pointer for later acknowledgement */
  me_ptr->ipc_buf_packet_ptr = packet_ptr;

  /* Update data flow state if it's the first data buffer received */
  if (IPC_RX_DFS_AT_GAP == me_ptr->dfs)
  {
     me_ptr->dfs = IPC_RX_DFS_FLOWING;
     IPC_RX_MSG(me_ptr->miid,
                DBG_LOW_PRIO,
                "INFO: Received first data buffer. Data flow state updated from AT_GAP to FLOWING.");
  }

  // Before copying new data/MD into internal buffer check if there is a flushing EOS then convert into non-flushing EOS
  // and drop if there is internal EOS
  if(me_ptr->int_buf_md_list_ptr)
  {
     result = ipc_rx_modify_md_in_internal_buf_when_new_data_arrives(me_ptr, &me_ptr->int_buf_md_list_ptr);
  }

  /* Handle data copying: copy from IPC buffer to internal buffer if needed
   * (e.g., if internal buffer has residual data, or IPC buffer size is smaller than output)
   *
   * ToDo We can avoid copying into internal buffer, if internal buffer actual data len
   * and ipc buf actual data len is greater than output buffer size.
   * Need to re-write the logic accordingly. */
  // if ((me_ptr->ipc_buf_size + me_ptr->int_buf_actual_data_len) < me_ptr->out_buf_size)
  if (me_ptr->ipc_buf_size < me_ptr->out_buf_size)
  {
     result = ipc_rx_copy_ipc_buffer_to_int_buffer(me_ptr);
     if (AR_DID_FAIL(result))
     {
        IPC_RX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Error: Failed to copy IPC buffer to internal buffer during setup: %lu. Returning with error.",
                   result);
        return ipc_rx_free_input_data_cmd(me_ptr, packet_ptr, result, FALSE, TRUE); /* Free resources and return */
     }

     /* If IPC buffer is now empty after copy, then ack the IPC buffer */
     if (0 == me_ptr->ipc_buf_actual_data_len)
     {
        ar_result_t free_res = ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
        if (AR_DID_FAIL(free_res))
        {
           IPC_RX_MSG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Error: Failed to free IPC buffer packet after full copy to internal buffer: %lu.",
                      free_res);
           result |= free_res; /* Aggregate error */
        }
     }
  }

    // if there is flushing EOS at IPC buffer or if there is a flushing EOS
    bool_t is_flushing_eos_present_in_ipc_buf =
       ipc_rx_check_if_there_is_a_flushing_eos(me_ptr, me_ptr->md_buf_info.md_list_ptr);
    if (is_flushing_eos_present_in_ipc_buf)
    {
       IPC_RX_MSG(me_ptr->miid,
                  DBG_LOW_PRIO,
                  "INFO: Found flushing EOS in me_ptr->md_buf_info.md_list_ptr 0x%p",
                  me_ptr->md_buf_info.md_list_ptr);
    }

    bool_t is_flushing_eos_present_in_int_buf =
       ipc_rx_check_if_there_is_a_flushing_eos(me_ptr, me_ptr->int_buf_md_list_ptr);
    if (is_flushing_eos_present_in_int_buf)
    {
       IPC_RX_MSG(me_ptr->miid,
                  DBG_LOW_PRIO,
                  "INFO: Found flushing EOS in int_buf_md_list_ptr 0x%p",
                  me_ptr->int_buf_md_list_ptr);
    }

    if (is_flushing_eos_present_in_ipc_buf && is_flushing_eos_present_in_int_buf)
    {
       // flushing EOS is expected only either at IPC buf or internal buffer not both at the same time. internal EOS
       // must have been
       // converted to non flushing at this point
       IPC_RX_MSG(me_ptr->miid,
                  DBG_ERROR_PRIO,
                  "INFO: Unexpected! Found flushing EOS in both me_ptr->md_buf_info.md_list_ptr 0x%p and "
                  "int_buf_md_list_ptr 0x%p",
                  me_ptr->md_buf_info.md_list_ptr,
                  me_ptr->int_buf_md_list_ptr);
       spf_svc_crash();
    }

    if (is_flushing_eos_present_in_ipc_buf || is_flushing_eos_present_in_int_buf)
    {
       me_ptr->dfs = IPC_RX_DFS_AT_GAP;
       IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Received Flushing EOS setting input data flow state to AT_GAP");
    }

    if ((me_ptr->int_buf_actual_data_len + me_ptr->ipc_buf_actual_data_len) < me_ptr->out_buf_size)
    {
       if (is_flushing_eos_present_in_ipc_buf || is_flushing_eos_present_in_int_buf)
       {
          /* IPC buffer size is sufficient and no pending internal data, trigger processing directly */
          me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY;
       }
       else
       {
          me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED; /* Needs more data to fill internal buffer */
       }
    }
    else
    {
       /* IPC buffer size is sufficient and no pending internal data, trigger processing directly */
       me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED;
    }

    IPC_RX_MSG(me_ptr->miid,
               DBG_LOW_PRIO,
               "INFO: IPC input data buffer setup complete with result: %lu. Input trigger info: %lu.",
               result,
               me_ptr->input_trigger_info);

    return result;
}

/**
 * \brief Calculates the KPPS (kilo-packets per second) for the IPC_RX module.
 *
 * This calculation is based on the input media format's number of channels
 * and sampling rate. A scaling factor `CAPI_IPC_RX_KPPS` is applied to derive
 * the final KPPS value. KPPS indicates the processing load of the module.
 *
 * \param[in] me_ptr Pointer to the CAPI IPC_RX instance. Must not be NULL.
 *
 * \return Calculated KPPS value. Returns 0 if `me_ptr` is NULL or if input
 *         media format's sampling rate or number of channels are invalid (e.g., zero or INVALID_VAL).
 */
static uint32_t capi_ipc_rx_get_kpps(capi_ipc_rx_t *me_ptr)
{
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_get_kpps.");
      return 0;
   }

   uint32_t kpps = 0; /* Initialize KPPS to 0 */
   /* Only calculate KPPS if media format parameters are valid */
   if (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->input_media_fmt.format.sampling_rate &&
       me_ptr->input_media_fmt.format.sampling_rate > 0 &&
       CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->input_media_fmt.format.num_channels &&
       me_ptr->input_media_fmt.format.num_channels > 0)
   {
      /* Scale KPPS based on number of channels and sampling rate relative to 8kHz */
      kpps = CAPI_IPC_RX_KPPS * me_ptr->input_media_fmt.format.num_channels *
             (me_ptr->input_media_fmt.format.sampling_rate / 8000);
   }
   else
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "INFO: Invalid media format for KPPS calculation. Sampling rate: %lu, Channels: %lu. Returning 0 "
                 "KPPS.",
                 me_ptr->input_media_fmt.format.sampling_rate,
                 me_ptr->input_media_fmt.format.num_channels);
   }
   return kpps;
}

/**
 * \brief Raises various events for the IPC_RX module, including KPPS and bandwidth.
 *
 * This function consolidates the raising of several common CAPI events:
 * process check, KPPS update, and bandwidth update events. It ensures the event
 * callback is set before attempting to raise any events.
 *
 * \param[in] me_ptr Pointer to the CAPI IPC_RX instance. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` is NULL.
 *         CAPI_EUNSUPPORTED if the event callback is not set.
 *         Other `CAPI_FAILED` codes if any of the individual event raising functions fail.
 */
capi_err_t capi_ipc_rx_raise_event(capi_ipc_rx_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_raise_event.");
      return CAPI_EBADPARAM;
   }

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Entering capi_ipc_rx_raise_event.");

   /* Check if the event callback is set before proceeding */
   if (NULL == me_ptr->cb_info.event_cb)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Event callback is not set. Unable to raise events!");
      return CAPI_EUNSUPPORTED;
   }

   uint32_t kpps = capi_ipc_rx_get_kpps(me_ptr);
   /* Bandwidth is a fixed constant for this module */
   uint32_t bw = CAPI_IPC_RX_BW;

   /* Update KPPS event */
   capi_result = capi_cmn_update_kpps_event(&me_ptr->cb_info, kpps);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to update KPPS event: %lu.", capi_result);
      return capi_result; /* Return on failure */
   }

   /* Update bandwidth event (data and code BW) */
   capi_result = capi_cmn_update_bandwidth_event(&me_ptr->cb_info, bw, bw);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to update bandwidth event: %lu.", capi_result);
      return capi_result; /* Return on failure */
   }

   /* ToDo Need to update delay equivalent to rx frame duration us (report only for pcm ) */

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: All events (KPPS, BW, Process) raised successfully.");
   return capi_result;
}

/**
 * \brief Main processing function for the IPC_RX CAPI module.
 *
 * This function is the primary entry point for data processing within the
 * CAPI module. It manages the flow of data from the IPC buffer to the module's
 * internal processing and then to the CAPI output buffer. It also handles
 * acknowledging the IPC data command and updating the module's trigger state
 * for subsequent processing iterations.
 *
 * \param[in] _pif     Pointer to the CAPI instance, cast to `capi_ipc_rx_t` internally. Must not be NULL.
 * \param[in] input[]  Array of pointers to input stream data (unused in this module, typically NULL).
 * \param[out] output[] Array of pointers to output stream data buffers to be filled. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `_pif` is NULL.
 *         Other `CAPI_FAILED` codes if data reading or ACK sending fails.
 *
 *
 *         ToDo: Lets keep rx process related utility functions in same file and declare as static.
 */
capi_err_t capi_ipc_rx_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;

   /* Assert critical input parameters for debugging in development builds.
    * In release builds, these assertions might be compiled out. */
   POSAL_ASSERT(_pif);
   POSAL_ASSERT(output && output[0]); /* Ensure output array and its first element are valid */

   /* Cast the generic CAPI instance pointer to the module-specific structure */
   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;

   /* Only proceed with processing if a buffer is available and ready for consumption */
   if ((FWK_EXTN_IPC_PORT_BUFFER_NEEDED | FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY) & me_ptr->input_trigger_info)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "INFO: No buffer ready for processing or waiting for buffer. Skipping process call.");
      return CAPI_EOK; /* Nothing to process yet, or waiting for buffer, return success */
   }

#ifdef DEBUG_IPC_RX
   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Entering process function.");
#endif

   /* Read data from IPC buffer (or internal buffer) to the CAPI output stream */
   capi_result |= capi_ipc_rx_read_data(me_ptr, output);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to read data in capi_ipc_rx_process: %lu.", capi_result);
      return capi_result; /* Propagate error */
   }

   if (FALSE ==
       check_if_in_buffer_range(me_ptr->curr_shared_buf_ptr, me_ptr->ipc_buf_virtual_addr, me_ptr->ipc_buf_size))
   {
      /* Free the input data command and send acknowledgement.
       * This is only done if the IPC buffer was fully consumed by capi_ipc_rx_read_data. */
      if (NULL != me_ptr->ipc_buf_packet_ptr && 0 == me_ptr->ipc_buf_actual_data_len)
      {
         capi_result = (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
         if (AR_DID_FAIL(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to send ACK for data buffer: %lu.", capi_result);
            return capi_result; /* Propagate error */
         }
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Sent ACK for data buffer after full consumption.");
      }
      else if (NULL != me_ptr->ipc_buf_packet_ptr && me_ptr->ipc_buf_actual_data_len > 0)
      {
         /* If IPC buffer still has data, keep it as BUFFER_NOT_NEEDED for next process call
          * No change to me_ptr->input_trigger_info here as it's already NOT_NEEDED */
         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "INFO: IPC buffer not fully consumed (%lu bytes remain). Will continue processing in next call.",
                    me_ptr->ipc_buf_actual_data_len);
      }
   }
#ifdef DEBUG_IPC_RX
   else
   {
      // print all the variable update above
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "INFO: IPC buffer cannot be acked yet since its shared with fwk buffer ptr 0x%p remaining data %lu",
                 me_ptr->ipc_buf_virtual_addr,
                 me_ptr->ipc_buf_actual_data_len);
   }
#endif

   // if output buffer is not filled, underrun
   int32_t bytes_to_underrun = (output[0]->buf_ptr[0].max_data_len - output[0]->buf_ptr[0].actual_data_len);
   if (bytes_to_underrun && (TRUE == me_ptr->need_to_underrun) && (IPC_RX_DFS_FLOWING == me_ptr->dfs))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "INFO: Input IPC buffer not present, underrun bytes_to_underrun: %lu",
                 bytes_to_underrun);

      uint32_t actual_data_len = output[0]->buf_ptr[0].actual_data_len;
      for (uint32_t i = 0; i < output[0]->bufs_num; i++)
      {
         memset(output[0]->buf_ptr[i].data_ptr + actual_data_len, 0, bytes_to_underrun);
      }
      output[0]->buf_ptr[0].actual_data_len = output[0]->buf_ptr[0].max_data_len;
      me_ptr->input_trigger_info            = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
   }
   me_ptr->need_to_underrun = FALSE;

#ifdef DEBUG_IPC_RX
  IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: capi_ipc_rx_process completed.");
#endif
  return capi_result;
}

/**
 * \brief Processes and retrieves properties from the IPC_RX CAPI module.
 *
 * This function handles various property queries, including basic module
 * properties (memory, stack size, in-place status, buffering requirements),
 * framework extensions needed, and custom properties related to IPC messaging
 * callbacks. It also handles `CAPI_INTERFACE_EXTENSIONS`.
 *
 * \param[in] me_ptr        Pointer to the CAPI IPC_RX instance (can be NULL for static properties
 *                          like `CAPI_INIT_MEMORY_REQUIREMENT` etc., where `me_ptr` is not needed).
 * \param[in,out] proplist_ptr Pointer to the property list to process and fill. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         Returns CAPI_EBADPARAM if `proplist_ptr` is NULL or if `me_ptr` is required but NULL.
 *         Returns CAPI_ENEEDMORE if payload size is insufficient for a property.
 *         Returns CAPI_EUNSUPPORTED if an unknown or unsupported property ID is queried.
 */
capi_err_t capi_ipc_rx_process_get_properties(capi_ipc_rx_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   miid        = (NULL != me_ptr) ? me_ptr->miid : MIID_UNKNOWN;

   if (NULL == proplist_ptr)
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: NULL proplist_ptr received in capi_ipc_rx_process_get_properties.");
      return CAPI_EBADPARAM;
   }

   /* Basic properties for module initialization */
   capi_basic_prop_t mod_prop;
   /* List of framework extensions supported by this module */
   uint32_t fwk_extn_ids[] = { FWK_EXTN_IPC_PORT_HANDLER, FWK_EXTN_CONTAINER_FRAME_DURATION };

   /* Memory required for instance structure */
   mod_prop.init_memory_req = ALIGN_8_BYTES(sizeof(capi_ipc_rx_t));
   /* Module's stack size requirement */
   mod_prop.stack_size       = IPC_RX_STACK_SIZE;
   mod_prop.num_fwk_extns    = sizeof(fwk_extn_ids) / sizeof(fwk_extn_ids[0]);
   mod_prop.fwk_extn_ids_arr = fwk_extn_ids;
   /* Module processes data in-place */
   mod_prop.is_inplace = FALSE;
   /* Module does not require data buffering by container */
   mod_prop.req_data_buffering = FALSE;
   /* Currently no metadata size reported */
   mod_prop.max_metadata_size = 0;

   /* Get common basic properties using the CAPI common utility */
   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Get common basic properties failed with result %lu.", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   /* Iterate through each property in the list and handle specific ones */
   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      /* Update miid for logging purposes for each property */
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
            /* These properties are already handled by capi_cmn_get_basic_properties.
             * No further action or logging needed here to avoid redundancy. */
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            /* Validate payload size for interface extensions list */
            if (payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t))
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: CAPI_INTERFACE_EXTENSIONS bad param size %lu, expected at least %lu.",
                          payload_ptr->max_data_len,
                          sizeof(capi_interface_extns_list_t));
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            /* Validate if provided buffer size is sufficient for all extension descriptions */
            if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                             (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: CAPI_INTERFACE_EXTENSIONS invalid param size %lu, not enough space for %lu "
                          "extensions.",
                          payload_ptr->max_data_len,
                          intf_ext_list->num_extensions);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            /* Iterate through each interface extension and mark support status */
            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t ext_idx = 0; ext_idx < intf_ext_list->num_extensions; ext_idx++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  case INTF_EXTN_METADATA:
                  {
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  }
                  case INTF_EXTN_DATA_PORT_OPERATION:
                  {
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  }
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }

                     miid = me_ptr->miid; /* Now safe to use me_ptr->miid */

                     IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Entering capi_ipc_rx_process_set_properties.");

                     /* Set common basic properties (heap ID, callback info) */
                     capi_result =
                        capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, TRUE);
                     if (CAPI_FAILED(capi_result))
                     {
                        IPC_RX_MSG(miid,
                                   DBG_ERROR_PRIO,
                                   "Error: Set basic properties failed with result %lu.",
                                   capi_result);
                        return capi_result;
                     }
               }
               IPC_RX_MSG(miid,
                          DBG_HIGH_PRIO,
                          "INFO: CAPI_INTERFACE_EXTENSIONS: interface extension ID = 0x%lx, is_supported = %d.",
                          curr_intf_extn_desc_ptr->id,
                          (int)curr_intf_extn_desc_ptr->is_supported);
               curr_intf_extn_desc_ptr++;
            }
            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            if (NULL == me_ptr)
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: Get property ID 0x%lx (CAPI_CUSTOM_PROPERTY) requested, but module is not allocated "
                          "(me_ptr is NULL).",
                          prop_array[i].id);
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
               break;
            }

            /* Validate payload size for custom property header */
            if (payload_ptr->max_data_len < sizeof(capi_custom_property_t))
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: CAPI_CUSTOM_PROPERTY bad param size %lu, expected at least %lu.",
                          payload_ptr->max_data_len,
                          sizeof(capi_custom_property_t));
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)payload_ptr->data_ptr;
            void                   *cust_payload_ptr = (void *)(cust_prop_ptr + 1);

            /* Handle specific custom properties */
            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO:
               {
                  /* Validate payload size for the IPC message callback info structure */
                  if (payload_ptr->max_data_len <
                      (sizeof(capi_custom_property_t) + sizeof(fwk_extn_prop_ipc_msg_callback_info_t)))
                  {
                     IPC_RX_MSG(miid,
                                DBG_ERROR_PRIO,
                                "Error: Insufficient get property size %lu for "
                                "FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO, expected %lu.",
                                payload_ptr->max_data_len,
                                (sizeof(capi_custom_property_t) + sizeof(fwk_extn_prop_ipc_msg_callback_info_t)));
                     return CAPI_ENEEDMORE;
                  }

                  /* Populate end point async signal callback info */
                  fwk_extn_prop_ipc_msg_callback_info_t *cb_info_ptr =
                     (fwk_extn_prop_ipc_msg_callback_info_t *)cust_payload_ptr;
                  cb_info_ptr->ext_port_trigger_shared_ptr =
                     &me_ptr->input_trigger_info; /* Pointer to shared trigger state */
                  cb_info_ptr->ctrl_msg_handler =
                     capi_ipc_rx_fwk_extn_ipc_port_ctrl_msg_handler; /* Control message handler */
                  cb_info_ptr->data_msg_handler =
                     capi_ipc_rx_fwk_extn_ipc_port_data_msg_handler; /* Data message handler */
                  cb_info_ptr->callback_handle_ptr = me_ptr;         /* Context for callbacks */

                  payload_ptr->actual_data_len =
                     sizeof(capi_custom_property_t) +
                     sizeof(fwk_extn_prop_ipc_msg_callback_info_t); /* Update actual length */
                  IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Populated FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO.");
                  break;
               }
               default:
               {
                  IPC_RX_MSG(miid,
                             DBG_ERROR_PRIO,
                             "Error: Get, unsupported custom secondary property ID 0x%lx.",
                             cust_prop_ptr->secondary_prop_id);
                  CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
                  break;
               }
            }
            break;
         }
         default:
         {
            IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Get, unsupported property ID 0x%lx.", prop_array[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      /* If an error occurred for this specific property, return immediately for robustness
       * (as common practice, some implementations might aggregate errors and return at the end) */
      if (CAPI_FAILED(capi_result))
      {
         IPC_RX_MSG(miid,
                    DBG_HIGH_PRIO,
                    "ERROR: Get property for ID 0x%lx failed with result 0x%lx. Aborting property processing.",
                    prop_array[i].id,
                    capi_result);
         return capi_result;
      }
   }
   IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: All properties processed successfully.");
   return capi_result;
}

/**
 * \brief Processes and sets properties for the IPC_RX CAPI module.
 *
 * This function handles various property settings, including common CAPI
 * properties like heap ID and callback info, port number information,
 * module instance ID, algorithmic reset, and framework extension parameters.
 * It iterates through the provided property list and applies each property,
 * performing necessary validations and actions.
 *
 * \param[in,out] me_ptr        Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] proplist_ptr Pointer to the property list containing properties to set. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         Returns CAPI_EBADPARAM if `me_ptr` or `proplist_ptr` is NULL.
 *         Returns CAPI_ENEEDMORE if payload size is insufficient for a property.
 *         Returns CAPI_EUNSUPPORTED if an unknown or unsupported property ID is encountered.
 *         Returns other `CAPI_FAILED` codes if underlying operations (e.g., flush) fail.
 */
capi_err_t capi_ipc_rx_process_set_properties(capi_ipc_rx_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   miid        = MIID_UNKNOWN; /* Initialize for potential early exit logging */

   if (NULL == me_ptr || NULL == proplist_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL pointer received in capi_ipc_rx_process_set_properties.");
      return CAPI_EBADPARAM;
   }
   miid = me_ptr->miid; /* Now safe to use me_ptr->miid */

   IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Entering capi_ipc_rx_process_set_properties.");

   /* Set common basic properties (heap ID, callback info) */
   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, TRUE);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Set basic properties failed with result %lu.", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   /* Iterate through each property in the list and handle specific ones */
   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Processing property ID: 0x%lx.", prop_array[i].id);

      capi_buf_t *payload_ptr = &(prop_array[i].payload);

      switch (prop_array[i].id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_HEAP_ID:
         case CAPI_CUSTOM_INIT_DATA:
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            /* These properties are already handled by capi_cmn_set_basic_properties.
             * No further action or logging needed here to avoid redundancy. */
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            /* Validate payload size for port number information */
            if (payload_ptr->actual_data_len < sizeof(capi_port_num_info_t))
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: Set property ID 0x%lx, Bad param size %lu, expected at least %lu.",
                          prop_array[i].id,
                          payload_ptr->actual_data_len,
                          sizeof(capi_port_num_info_t));
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break; /* Exit switch, loop continues */
            }

            capi_port_num_info_t *num_port_info = (capi_port_num_info_t *)payload_ptr->data_ptr;

            /* Verify number of max input/output ports supported by the module. */
            if (num_port_info->num_input_ports > IPC_RX_MAX_INPUT_PORTS ||
                num_port_info->num_output_ports > IPC_RX_MAX_OUTPUT_PORTS)
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: Incorrect number of input (%lu) or output (%lu) ports. Max supported input: %lu, "
                          "output: %lu.",
                          num_port_info->num_input_ports,
                          num_port_info->num_output_ports,
                          IPC_RX_MAX_INPUT_PORTS,
                          IPC_RX_MAX_OUTPUT_PORTS);
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM); /* This is a configuration error */
               break;                                       /* Exit switch, loop continues */
            }

            me_ptr->num_port_info = *num_port_info; /* Copy the port info to module instance */

            IPC_RX_MSG(miid,
                       DBG_MED_PRIO,
                       "INFO: Number of input ports: %lu, Number of output ports: %lu.",
                       me_ptr->num_port_info.num_input_ports,
                       me_ptr->num_port_info.num_output_ports);

            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            /* Validate payload size for module instance ID */
            if (payload_ptr->actual_data_len < sizeof(capi_module_instance_id_t))
            {
               IPC_RX_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Error: Set property ID 0x%lx, Bad param size %lu, expected at least %lu.",
                          prop_array[i].id,
                          payload_ptr->actual_data_len,
                          sizeof(capi_module_instance_id_t));
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break; /* Exit switch, loop continues */
            }
            capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
            me_ptr->miid                        = data_ptr->module_instance_id; /* Set module instance ID */
            IPC_RX_MSG(miid,
                       DBG_LOW_PRIO,
                       "INFO: Module ID: 0x%08lX, Instance ID: 0x%08lX.",
                       data_ptr->module_id,
                       me_ptr->miid);
            break;
         } /* CAPI_MODULE_INSTANCE_ID */

         case CAPI_ALGORITHMIC_RESET:
         {
            IPC_RX_MSG(miid,
                       DBG_LOW_PRIO,
                       "INFO: Received algorithmic reset for instance 0x%lx. Flushing internal state.",
                       me_ptr->miid);
            /* Call flush handling. Cast result from ar_result_t to capi_err_t. */
            capi_result = (capi_err_t)ipc_rx_flush_handling(me_ptr, NULL, TRUE);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Algorithmic reset flush handling failed: %lu.", capi_result);
            }
            break;
         }
         default:
         {
            IPC_RX_MSG(miid,
                       DBG_ERROR_PRIO,
                       "Error: Set, unsupported property ID 0x%lx. Marking as unsupported.",
                       prop_array[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break; /* Exit switch, loop continues */
         }
      }

      /* If a critical error (not just unsupported) occurred for this specific property,
       * propagate it and return immediately. Otherwise, continue processing other properties. */
      if (CAPI_FAILED(capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         IPC_RX_MSG(miid,
                    DBG_HIGH_PRIO,
                    "ERROR: Set property for ID 0x%lx failed with critical result 0x%lx. Aborting property processing.",
                    prop_array[i].id,
                    capi_result);
         return capi_result;
      }
   }
   IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: All properties processed successfully.");
   return capi_result;
}

/**
 * \brief Processes and sets a specific parameter for the IPC_RX CAPI module.
 *
 * This function handles setting parameters such as IPC data link information,
 * metadata handler, container frame duration, and data port operations. It
 * performs validation checks on payload sizes and, for container frame duration,
 * also sends a `PARAM_ID_DOWNSTREAM_FRAME_LENGTH` GPR command to the peer.
 *
 * \param[in] _pif          Pointer to the CAPI instance. Must not be NULL.
 * \param[in] param_id      ID of the parameter to set.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL if not port-specific). (Not directly used
 * here) \param[in] params_ptr    Pointer to the CAPI buffer containing the parameter data. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         Returns CAPI_EBADPARAM for NULL `_pif` or `params_ptr`.
 *         Returns CAPI_ENEEDMORE for insufficient payload size.
 *         Returns CAPI_EUNSUPPORTED for unknown parameter IDs.
 *         Returns other `CAPI_FAILED` codes if underlying GPR command sending or data port handling fails.
 */
capi_err_t capi_ipc_rx_process_set_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   /* Initialize ar_result_t */
   ar_result_t result = AR_EOK;

   if (NULL == _pif || NULL == params_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: Set param received NULL pointer (_pif: 0x%p, params_ptr: 0x%p).",
                 _pif,
                 params_ptr);
      return CAPI_EBADPARAM;
   }
   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)(_pif);
   uint32_t       miid   = me_ptr->miid;

   switch (param_id)
   {
      /* IPC TX sends control path media format through set cfg */
      case PARAM_ID_MEDIA_FORMAT:
      {
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Processing PARAM_ID_MEDIA_FORMAT set param command.");

         // If data is currently flowing, a media format change is not supported
         if (IPC_RX_DFS_FLOWING == me_ptr->dfs)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Input media format control command received while data is flowing (IPC_RX_DFS_FLOWING). "
                       "Not supported.");
            result = AR_EUNSUPPORTED;
         }
         else
         {
            if (sizeof(media_format_t) > params_ptr->actual_data_len)
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Set PARAM_ID_MEDIA_FORMAT, Bad param size %lu",
                          params_ptr->actual_data_len);
               capi_result = CAPI_ENEEDMORE;
               break;
            }
            media_format_t *media_fmt_ptr = (media_format_t *)(params_ptr->data_ptr);
            // Handle the incoming media format from the GPR client
            // result = ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(me_ptr,
            //                                                                  pkt_ptr,
            //                                                                  FALSE /* data_cmd: This is a control
            //                                                                  command, not a data command */);
            me_ptr->is_pending_mf = FALSE; /* Clear pending flag if not deferred */

            /* Clear existing media format info before parsing new one */
            memset(&me_ptr->input_media_fmt, 0, sizeof(capi_media_fmt_v2_t));
            memset(&me_ptr->output_media_fmt, 0, sizeof(capi_media_fmt_v2_t));

            /* Parse input media format based on its data format */
            switch (media_fmt_ptr->data_format)
            {
               case DATA_FORMAT_FIXED_POINT:
               case DATA_FORMAT_FLOATING_POINT:
               {
                  IPC_RX_MSG(me_ptr->miid,
                             DBG_HIGH_PRIO,
                             "INFO: Processing input media format (Fixed/Floating Point).");
                  result = ipc_rx_parse_inp_pcm_media_fmt_from_gpr_client(me_ptr,
                                                                          media_fmt_ptr,
                                                                          FALSE,
                                                                          &me_ptr->input_media_fmt);
                  if (AR_DID_FAIL(result))
                  {
                     IPC_RX_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "Error: Failed to parse input PCM media format: %lu.",
                                result);
                     return (capi_err_t)result; /* Propagate the specific AR_result as capi_err_t */
                  }
                  break;
               }
               default:
               {
                  IPC_RX_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Error: Unsupported data format 0x%lX in media format command.",
                             media_fmt_ptr->data_format);
                  return CAPI_EUNSUPPORTED;
               }
            }

            /* Copy input media format to output media format. Output MF might be tweaked later. */
            memscpy(&me_ptr->output_media_fmt,
                    sizeof(me_ptr->output_media_fmt),
                    &me_ptr->input_media_fmt,
                    sizeof(capi_media_fmt_v2_t));

            /* If input is deinterleaved packed, output should be deinterleaved unpacked v2 */
            if (CAPI_CMN_IS_PCM_FORMAT(me_ptr->input_media_fmt.header.format_header.data_format) &&
                CAPI_DEINTERLEAVED_PACKED == me_ptr->input_media_fmt.format.data_interleaving)
            {
               me_ptr->output_media_fmt.format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;
            }

            /* Calculate expected output buffer size based on container duration and media format */
            if (0 != me_ptr->ipc_rx_cntr_duration_us)
            {
          uint32_t exp_out_buf_size =0;
          if(CAPI_CMN_IS_PCM_FORMAT(me_ptr->input_media_fmt.header.format_header.data_format))
          {
            exp_out_buf_size = me_ptr->input_media_fmt.format.num_channels *
                                           capi_cmn_us_to_bytes_per_ch(me_ptr->ipc_rx_cntr_duration_us,
                                                                       me_ptr->input_media_fmt.format.sampling_rate,
                                                                       me_ptr->input_media_fmt.format.bits_per_sample);
          }
          else
          {
            // todo_mdf: for raw compressed assuming 2K
            exp_out_buf_size = 2048;
          }

          /* Reallocate internal buffer if necessary (size changed or not yet allocated) */
          if ((0 != exp_out_buf_size) && (me_ptr->out_buf_size != exp_out_buf_size))
          {
             /* Free existing buffer if it was allocated */
             if (NULL != me_ptr->int_buf_ptr)
             {
                posal_memory_free(me_ptr->int_buf_ptr);
                me_ptr->int_buf_ptr = NULL; /* Ensure pointer is NULL after freeing */
             }

             /* Allocate new internal buffer */
             me_ptr->int_buf_ptr = (int8_t *)posal_memory_malloc(exp_out_buf_size, POSAL_HEAP_DEFAULT);

             if (NULL == me_ptr->int_buf_ptr)
             {
                IPC_RX_MSG(me_ptr->miid,
                           DBG_ERROR_PRIO,
                           "Error: Failed to allocate internal buffer, requested bytes %lu.",
                           exp_out_buf_size);
                return CAPI_ENOMEMORY;
             }
             else
             {
                me_ptr->out_buf_size            = exp_out_buf_size;
                me_ptr->int_buf_actual_data_len = 0; /* Reset actual data length as it's a new buffer */
                me_ptr->int_buf_max_data_len    = exp_out_buf_size;

                me_ptr->int_buf_max_data_len_per_buf =
                   topo_div_num(exp_out_buf_size, ipc_rx_get_num_bufs_from_mf(me_ptr));

                IPC_RX_MSG(me_ptr->miid,
                           DBG_HIGH_PRIO,
                           "INFO: Internal buffer allocated successfully int_buf_max_data_len %lu "
                           "int_buf_max_data_len_per_buf %lu bytes ",
                           me_ptr->int_buf_max_data_len,
                           me_ptr->int_buf_max_data_len_per_buf);
             }
          }
            }

            /* Raise event for IPC RX status update (KPPS, BW, Process Check) */
            capi_result = capi_ipc_rx_raise_event(me_ptr);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to raise IPC RX event after media format update: %lu.",
                          capi_result);
               return capi_result;
            }

            /* Raise event for output media format to the container */
            capi_result = capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->output_media_fmt, FALSE, 0);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to raise output media format event to container: %lu.",
                          capi_result);
               return capi_result;
            }

            IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Raised output media format event.");

            /* Update the input trigger info to BUFFER_NEEDED so that container triggers process call. */
            me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;

            /* Raise module buffer access extension enable/disable event to the container. */
            capi_ipc_rx_check_n_enable_buffer_extn(me_ptr);

            if (AR_DID_FAIL(result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to handle incoming media format from GPR client, result: %lu.",
                          result);
            }
         }
         break;
      }
      case PARAM_ID_IPC_DATA_LINK_INFO:
      {
         if ((sizeof(param_id_ipc_data_link_info_t) + sizeof(ipc_data_link_info_per_port_t)) >
             params_ptr->actual_data_len)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set PARAM_ID_IPC_DATA_LINK_INFO, Bad param size %lu",
                       params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_ipc_data_link_info_t *link_info_ptr = (param_id_ipc_data_link_info_t *)(params_ptr->data_ptr);

         // currently ipc modules support one port
         ipc_data_link_info_per_port_t *ipc_rx_link_info_ptr = (ipc_data_link_info_per_port_t *)(link_info_ptr + 1);

         me_ptr->ipc_rx_link_info.ipc_port_type       = ipc_rx_link_info_ptr->ipc_port_type;
         me_ptr->ipc_rx_link_info.self_port_id        = ipc_rx_link_info_ptr->self_port_id;
         me_ptr->ipc_rx_link_info.self_module_iid     = ipc_rx_link_info_ptr->self_module_iid;
         me_ptr->ipc_rx_link_info.self_proc_domain_id = ipc_rx_link_info_ptr->self_proc_domain_id;
         me_ptr->ipc_rx_link_info.peer_port_id        = ipc_rx_link_info_ptr->peer_port_id;
         me_ptr->ipc_rx_link_info.peer_module_iid     = ipc_rx_link_info_ptr->peer_module_iid;
         me_ptr->ipc_rx_link_info.peer_proc_domain_id = ipc_rx_link_info_ptr->peer_proc_domain_id;

         IPC_RX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Set PARAM_ID_IPC_DATA_LINK_INFO, ipc_port_type 0x%x self_port_id 0x%x self_module_iid "
                    "0x%x self_proc_domain_id 0x%x peer_port_id 0x%x peer_module_iid 0x%x, peer_proc_domain_id 0x%x",
                    me_ptr->ipc_rx_link_info.ipc_port_type,
                    me_ptr->ipc_rx_link_info.self_port_id,
                    me_ptr->ipc_rx_link_info.self_module_iid,
                    me_ptr->ipc_rx_link_info.self_proc_domain_id,
                    me_ptr->ipc_rx_link_info.peer_port_id,
                    me_ptr->ipc_rx_link_info.peer_module_iid,
                    me_ptr->ipc_rx_link_info.peer_proc_domain_id);

         // the payload for param_id_ipc_data_link_info_t is same as fwk_extn_event_ipc_data_link_info_t
         // hence just passing the same payload for the event.
         capi_result = capi_ipc_raise_data_link_info_event(me_ptr->miid,
                                                           &me_ptr->cb_info,
                                                           (fwk_extn_event_ipc_data_link_info_t *)ipc_rx_link_info_ptr);

         break;
      }

      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         IPC_RX_MSG(miid, DBG_HIGH_PRIO, "INFO: Set param INTF_EXTN_PARAM_ID_METADATA_HANDLER.");
         /* Validate payload size for metadata handler */
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            IPC_RX_MSG(miid,
                       DBG_ERROR_PRIO,
                       "Error: Param ID 0x%lx, Bad param size %lu, expected at least %lu.",
                       param_id,
                       params_ptr->actual_data_len,
                       sizeof(intf_extn_param_id_metadata_handler_t));
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }
         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr; /* Store the metadata handler */
         IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Metadata handler set successfully.");
         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         /* Validate payload size for container frame duration */
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            IPC_RX_MSG(miid,
                       DBG_ERROR_PRIO,
                       "Error: Param ID 0x%lx, Bad param size %lu, expected at least %lu.",
                       param_id,
                       params_ptr->actual_data_len,
                       sizeof(fwk_extn_param_id_container_frame_duration_t));
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }
         fwk_extn_param_id_container_frame_duration_t *fm_dur =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;
         me_ptr->ipc_rx_cntr_duration_us = fm_dur->duration_us; /* Store container frame duration */

         uint32_t exp_out_buf_size = 0;

         /* Calculate expected output buffer size based on container duration and media format */
         if ((0 != me_ptr->ipc_rx_cntr_duration_us) &&
             (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->input_media_fmt.format.sampling_rate) &&
             (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->input_media_fmt.format.num_channels) &&
             (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->input_media_fmt.format.bits_per_sample))
         {
            exp_out_buf_size = me_ptr->input_media_fmt.format.num_channels *
                               capi_cmn_us_to_bytes_per_ch(me_ptr->ipc_rx_cntr_duration_us,
                                                           me_ptr->input_media_fmt.format.sampling_rate,
                                                           me_ptr->input_media_fmt.format.bits_per_sample);

            /* Reallocate internal buffer if necessary (size changed or not yet allocated) */
            if ((0 != exp_out_buf_size) && (me_ptr->out_buf_size != exp_out_buf_size))
            {
               /* Free existing buffer if it was allocated */
               if (NULL != me_ptr->int_buf_ptr)
               {
                  posal_memory_free(me_ptr->int_buf_ptr);
                  me_ptr->int_buf_ptr = NULL; /* Ensure pointer is NULL after freeing */
               }

               /* Allocate new internal buffer */
               me_ptr->int_buf_ptr = (int8_t *)posal_memory_malloc(exp_out_buf_size, POSAL_HEAP_DEFAULT);

               if (NULL == me_ptr->int_buf_ptr)
               {
                  IPC_RX_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Error: Failed to allocate internal buffer, requested bytes %lu.",
                             exp_out_buf_size);
                  return CAPI_ENOMEMORY;
               }
               else
               {
                  me_ptr->out_buf_size            = exp_out_buf_size;
                  me_ptr->int_buf_actual_data_len = 0; /* Reset actual data length as it's a new buffer */
                  me_ptr->int_buf_max_data_len    = exp_out_buf_size;

                  me_ptr->int_buf_max_data_len_per_buf =
                     topo_div_num(exp_out_buf_size, ipc_rx_get_num_bufs_from_mf(me_ptr));

                  IPC_RX_MSG(me_ptr->miid,
                             DBG_HIGH_PRIO,
                             "INFO: Internal buffer allocated successfully int_buf_max_data_len %lu "
                             "int_buf_max_data_len_per_buf %lu bytes ",
                             me_ptr->int_buf_max_data_len,
                             me_ptr->int_buf_max_data_len_per_buf);
               }
            }
         }

         /* Prepare and send GPR command for downstream frame length to peer */
         param_id_downstream_frame_length_t ds_frame_length;
         ds_frame_length.peer_frame_len.frame_len_us = me_ptr->ipc_rx_cntr_duration_us;
         ds_frame_length.peer_frame_len.frame_len_bytes =
            exp_out_buf_size; /* ToDo: Ideally should use this for raw data. Updating this as temp wa */

         gpr_cmd_alloc_send_t args;
         args.src_domain_id = me_ptr->ipc_rx_link_info.self_proc_domain_id;
         args.dst_domain_id = me_ptr->ipc_rx_link_info.peer_proc_domain_id;
         args.src_port      = me_ptr->ipc_rx_link_info.self_module_iid;
         args.dst_port      = me_ptr->ipc_rx_link_info.peer_module_iid;
         args.token         = 0x7777; /* Arbitrary token for this message */
         args.opcode        = PARAM_ID_DOWNSTREAM_FRAME_LENGTH;
         args.payload       = &ds_frame_length;
         args.payload_size  = sizeof(ds_frame_length);
         args.client_data   = 0;

         // result = __gpr_cmd_alloc_send(&args); /* Send the GPR command */ //todo enable later when needed
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to send downstream frame length packet: %lu.", result);
            CAPI_SET_ERROR(capi_result, (capi_err_t)result); /* Convert AR_result to capi_err_t and propagate */
         }

         IPC_RX_MSG(miid,
                    DBG_LOW_PRIO,
                    "INFO: Received frame duration as %lu us and sent downstream frame length packet to IPC TX.",
                    me_ptr->ipc_rx_cntr_duration_us);

         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         /* Handle data port operations (OPEN, CLOSE, START, STOP) */
         capi_result = capi_ipc_rx_handle_intf_extn_data_port_operation(me_ptr, params_ptr);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Failed to handle data port operation: %lu.", capi_result);
         }
         break;
      }
      default:
      {
         IPC_RX_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Error: Set, unsupported parameter ID 0x%lx. Marking as unsupported.",
                    param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }
   IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Set parameter ID 0x%lx processed with result: %lu.", param_id, capi_result);
   return capi_result;
}

/**
 * \brief Processes and retrieves a specific parameter from the IPC_RX CAPI module.
 *
 * This function currently serves as a placeholder for future `GET` parameter
 * implementations. It logs an error for any unsupported parameter ID queries,
 * as no GET parameters are currently implemented.
 *
 * \param[in] _pif          Pointer to the CAPI instance. Must not be NULL.
 * \param[in] param_id      ID of the parameter to get.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL). (Not directly used here)
 * \param[out] params_ptr   Pointer to the CAPI buffer to fill with the parameter data. Must not be NULL.
 *
 * \return CAPI_EOK on success (if a parameter were implemented and successfully retrieved).
 *         Returns CAPI_EBADPARAM for NULL `_pif` or `params_ptr`.
 *         Returns CAPI_EUNSUPPORTED for any unknown parameter IDs.
 */
capi_err_t capi_ipc_rx_process_get_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == params_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: Get param received NULL pointer (_pif: 0x%p, params_ptr: 0x%p).",
                 _pif,
                 params_ptr);
      return CAPI_EBADPARAM;
   }

   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;
   uint32_t       miid   = me_ptr->miid;

   /* Currently, no specific GET parameters are implemented for this module. */
   switch (param_id)
   {
      default:
      {
         IPC_RX_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Error: Get, unsupported parameter ID 0x%lx. Marking as unsupported.",
                    param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }
   IPC_RX_MSG(miid, DBG_LOW_PRIO, "INFO: Get parameter ID 0x%lx processed with result: %lu.", param_id, capi_result);
   return capi_result;
}

static capi_err_t capi_ipc_raise_data_link_info_event(uint32_t                             miid,
                                                      capi_event_callback_info_t          *cb_info_ptr,
                                                      fwk_extn_event_ipc_data_link_info_t *link_info_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == cb_info_ptr->event_cb)
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, " Event callback is not set, Unable to ipc data link info event !");
      return CAPI_EBADPARAM;
   }

   capi_buf_t payload;
   payload.data_ptr        = (int8_t *)link_info_ptr;
   payload.actual_data_len = payload.max_data_len = sizeof(fwk_extn_event_ipc_data_link_info_t);

   result = capi_cmn_raise_data_to_dsp_svc_event(cb_info_ptr, FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO, &payload);

   if (CAPI_FAILED(result))
   {
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Failed to raise event FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO");
      return result;
   }
   else
   {
      IPC_RX_MSG(miid, DBG_HIGH_PRIO, "raised event FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO ");
   }

   return result;
}

// Function to parse the meta-data from the IPC buffers and create meta-data nodes
// and add it to the input port context
ar_result_t ipc_rx_populate_metadata_from_ipc_buffer(capi_ipc_rx_t *me_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result     = AR_EOK;
   int8_t     *md_buf_ptr = me_ptr->md_buf_info.md_buf_virtual_addr;

   if ((NULL == md_buf_ptr) || (0 == me_ptr->md_buf_info.md_buf_size))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid md buffer, not populating metadata\n");
      return AR_EOK;
   }

   uint32_t num_md_ele_cnt        = 0;
   uint32_t md_buffer_read_offset = 0;
   while ((md_buffer_read_offset + sizeof(metadata_header_t)) <= me_ptr->md_buf_info.md_buf_size)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "md_buffer_read_offset %lu,  %lu <= %lu",
                 md_buffer_read_offset,
                 (md_buffer_read_offset + sizeof(metadata_header_t)),
                 me_ptr->md_buf_info.md_buf_size);

      metadata_header_t *md_data_header_ptr = (metadata_header_t *)(md_buf_ptr + md_buffer_read_offset);

      module_cmn_md_flags_t flags;
      memset(&flags, 0, sizeof(flags));

      spf_ipcmd_convert_client_md_flag_to_int_md_flags(me_ptr->miid, md_data_header_ptr->flags, &flags);

      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Found MD in Wr buffer, MD ID 0x%lx flags 0x%lX offset %lu token_lsw 0x%lx token_msw 0x%lx "
                 "payload_size %lu tracking_mode %lu",
                 md_data_header_ptr->metadata_id,
                 md_data_header_ptr->flags,
                 md_data_header_ptr->offset,
                 md_data_header_ptr->token_lsw,
                 md_data_header_ptr->token_msw,
                 md_data_header_ptr->payload_size,
                 flags.tracking_mode);

      // update the loop control elements
      md_buffer_read_offset += ALIGN_4_BYTES(sizeof(metadata_header_t) + md_data_header_ptr->payload_size);

      module_cmn_md_t *new_md_ptr = NULL;
      if (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE == flags.tracking_mode)
      {
         result |= me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                            &me_ptr->md_buf_info.md_list_ptr,
                                                            md_data_header_ptr->payload_size,
                                                            me_ptr->heap_info,
                                                            FALSE, /*is_out_band*/
                                                            &new_md_ptr);

         // this is non-tracking MD hence no need to create reference/tracking info.
      }
      else // received MD with tracking enabled.
      {
         metadata_header_extn_t *md_data_extn_hdr_ptr = (metadata_header_extn_t *)(md_buf_ptr + md_buffer_read_offset);

         if (PARAM_ID_MD_EXTN_MD_ORIGIN_CFG != md_data_extn_hdr_ptr->metadata_extn_param_id)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Received invalid MD extension param id (0x%lx) payload_size %lu",
                       md_data_extn_hdr_ptr->metadata_extn_param_id,
                       md_data_extn_hdr_ptr->payload_size);
            ipc_rx_destroy_all_md(me_ptr, &me_ptr->md_buf_info.md_list_ptr);
            break;
         }

         param_id_md_extn_md_origin_cfg_t *orgin_info_ptr =
            (param_id_md_extn_md_origin_cfg_t *)(md_data_extn_hdr_ptr + 1);

         // update the loop control elements
         md_buffer_read_offset +=
            ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t));

         module_cmn_md_tracking_t tracking_info;
         memset(&tracking_info, 0, sizeof(tracking_info));

         tracking_info.tracking_payload.flags.word = 0;

         // source and destination is considered as IPMD in the source proc domain for all the MD propagated across
         // process domains.
         tracking_info.tracking_payload.src_domain_id = orgin_info_ptr->domain_id;
         tracking_info.tracking_payload.src_port      = APM_MODULE_INSTANCE_ID;

         tracking_info.tracking_payload.dst_domain_id = orgin_info_ptr->domain_id;
         tracking_info.tracking_payload.dest_port     = APM_MODULE_INSTANCE_ID;

         tracking_info.heap_info.heap_id = me_ptr->heap_info.heap_id;

         tracking_info.tracking_payload.flags.enable_cloning_event = MODULE_CMN_MD_TRACKING_ENABLE_CLONING_EVENT;

         // note that this a unique token generated by the originating proc domain for handling tracking event sand should
         // not be overwritten
         tracking_info.tracking_payload.token_lsw = md_data_header_ptr->token_lsw;
         tracking_info.tracking_payload.token_msw = md_data_header_ptr->token_msw;

         // create metadata with tracking would create the node and add it to the list
         result |= me_ptr->metadata_handler.metadata_create_with_tracking(me_ptr->metadata_handler.context_ptr,
                                                                          &me_ptr->md_buf_info.md_list_ptr,
                                                                          md_data_header_ptr->payload_size,
                                                                          me_ptr->heap_info,
                                                                          md_data_header_ptr->metadata_id,
                                                                          flags,
                                                                          &tracking_info,
                                                                          &new_md_ptr);

         if (AR_EOK != result)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "failed to create metadata node while inserting metadata from "
                       "write client buffer, md payload size %lu, md_id (0x%lx)",
                       md_data_header_ptr->payload_size,
                       md_data_header_ptr->metadata_id);

            ipc_rx_destroy_all_md(me_ptr, &me_ptr->md_buf_info.md_list_ptr);
            break;
         }

         // incr ref count in the orginator proc domian to indicate that the current proc domain received the MD
         // and has a reference. further as this module get propagated topo metdata utils will take care of
         // sending cloning/render/drop events to src domain based on the tracking info.
         spf_ipcmd_raise_event_to_update_ref_count(me_ptr->miid,
                                                   new_md_ptr->tracking_ptr,
                                                   md_data_header_ptr->metadata_id,
                                                   TRUE /** increment ref count */);
      }

      new_md_ptr->metadata_id = md_data_header_ptr->metadata_id;

      // fill the offset
      new_md_ptr->offset = md_data_header_ptr->offset;

      // copy payload from IPC buffer MD to newly created MD ptr.
      uint32_t  is_out_band        = new_md_ptr->metadata_flag.is_out_of_band;
      uint32_t *new_md_payload_ptr = NULL;
      if (is_out_band)
      {
         new_md_payload_ptr = (uint32_t *)new_md_ptr->metadata_ptr;
      }
      else
      {
         new_md_payload_ptr = (uint32_t *)&(new_md_ptr->metadata_buf);
      }

      // copy the metadata payload
      new_md_ptr->actual_size = memscpy(new_md_payload_ptr,
                                        new_md_ptr->max_size,
                                        (int8_t *)(md_data_header_ptr + 1),
                                        md_data_header_ptr->payload_size);

      // #ifdef VERBOSE_DEBUGGING
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Updated metadata with offset received from client, MD ID (0x%lx) md offset %d flags (0x%lx) max_size "
                 "%lu, buf ptr %lx paylod[0]=0x%lx paylod[1]=0x%lx",
                 new_md_ptr->metadata_id,
                 new_md_ptr->offset,
                 (uint32_t)new_md_ptr->metadata_flag.word,
                 new_md_ptr->max_size,
                 new_md_ptr->metadata_buf,
                 (uint32_t)(*new_md_payload_ptr),
                 (uint32_t)(*(new_md_payload_ptr + 1)));
      // #endif
      num_md_ele_cnt++;
   }

   if (AR_EOK != result)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "failed to process the metadata from the input buffer ");
   }
   else
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_MED_PRIO,
                 "Metadata from the ipc input buffer inserted to temp metadata list. "
                 "md_buffer_size %lu num_md_element %lu",
                 me_ptr->md_buf_info.md_buf_size,
                 num_md_ele_cnt);
   }

   // if in

   return result;
}