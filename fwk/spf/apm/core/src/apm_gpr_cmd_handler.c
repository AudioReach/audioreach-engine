/**
 * \file apm_gpr_cmd_handler.c
 *
 * \brief
 *     This file contains GPR command handler for APM module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/

#include "apm_gpr_cmd_parser.h"
#include "apm_gpr_if.h"
#include "apm_memmap_api.h"
#include "spf_svc_calib.h"
#include "apm_cmd_utils.h"
#include "apm_cmd_sequencer.h"

/****************************************************************************
 * Function Implementations
 ****************************************************************************/

/* Process APM commands which contain a payload */
static ar_result_t apm_get_gpr_cmd_payload(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t       result = AR_EOK;
   gpr_packet_t     *gpr_pkt_ptr;
   uint32_t          cmd_opcode;
   uint32_t          byte_aligned_size;
   uint8_t          *cmd_payload_ptr;
   gpr_packet_t     *cur_gpr_pkt_ptr;
   gpr_packet_t     *gpr_cmd_rsp_ptr;
   apm_cmd_header_t *cmd_header_ptr;

   /** Get the GPR packet pointer */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR cmd opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   /** CLOSE_ALL command does not have any payload */
   if (APM_CMD_CLOSE_ALL == cmd_opcode)
   {
      apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_ptr = NULL;

      /** Cache the command payload size */
      apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size = 0;

      return AR_EOK;
   }

   /** Get command header pointer */
   cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);

   /** Executino falls through if the GPR command has any payload */

   /** Get the command payload pointer */
   cur_gpr_pkt_ptr = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** Get the pointer to response payload pointer. Payload will
    *  be allocated based upon command type, e.g. in-band
    *  GET_CFG */
   gpr_cmd_rsp_ptr = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_rsp_payload.payload_ptr;

   if (AR_EOK != (result = spf_svc_get_cmd_payload_addr(APM_MODULE_INSTANCE_ID,
                                                        cur_gpr_pkt_ptr,
                                                        &gpr_cmd_rsp_ptr,
                                                        &cmd_payload_ptr,
                                                        &byte_aligned_size,
                                                        &apm_info_ptr->curr_cmd_ctrl_ptr->cmd_rsp_payload,
                                                        apm_info_ptr->memory_map_client)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to get cmd payload addr, cmd_opcode[0x%lX], result[%lu]", cmd_opcode, result);
      return result;
   }

   if (AR_EOK !=
       (result = posal_memorymap_shm_incr_refcount(apm_info_ptr->memory_map_client, cmd_header_ptr->mem_map_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Failed to increment Ref Count for shared memory, cmd_opcode[0x%lx], result[%lu]",
             cmd_opcode,
             result);

      return result;
   }

   /** Cache the command payload address. This pointer is used
    *  while ending the command for flushing the shared memory
    *  in case of OOB commands */
   apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_ptr = (void *)cmd_payload_ptr;

   /** Cache the command payload size */
   apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size = cmd_header_ptr->payload_size;

   return result;
}

static ar_result_t apm_process_gpr_cmd(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK, local_result;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      cmd_opcode;
   bool_t        defer_cmd_proc = FALSE;

   /** Get the GPR packet pointer */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR cmd opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   /** Allocate command handler resources  */
   if (AR_EOK != (result = apm_allocate_cmd_hdlr_resources(apm_info_ptr, msg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_gpr_cmd_handler(), Failed to allocate rsc for cmd/msg opcode: 0x%8lX", cmd_opcode);

      /** End the GPR command with failure status */
      if (AR_EOK != (local_result = __gpr_cmd_end_command(gpr_pkt_ptr, result)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to end GPR cmd_opcode[0x%lX], result[%lu]", cmd_opcode, local_result);
      }

      return result;
   }

   /** Get the pointer to GPR command payload. Pointer is
    *  retreived based upon if the command is in-band or
    *  out-of-band. Also, some commands may not have any payload
    *  at all. In this case, this function does nothing. */
   if (AR_EOK != (result = apm_get_gpr_cmd_payload(apm_info_ptr, msg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gpr_cmd_handler(), Failed to get cmd payload ptr, cmd/msg opcode: 0x%8lX",
             cmd_opcode);

      goto __bailout_cmd_rsc_alloc_fail;
   }

   /** Parse command payload, if present */
   if (apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_ptr)
   {
      if (AR_EOK != (result = apm_parse_cmd_payload(apm_info_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gpr_cmd_handler(), Cmd parsing failed, cmd/msg opcode: 0x%8lX, result: 0x%lX",
                cmd_opcode,
                result);

         /** Update the command status */
         apm_info_ptr->curr_cmd_ctrl_ptr->cmd_status = result;

         /** Set up sequencer to begin with error handling */
         apm_cmd_seq_set_up_err_hdlr(apm_info_ptr->curr_cmd_ctrl_ptr);
      }
   }

   /** Now check if this command processing needs to be
    *  deferred */
   if (apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr &&
       apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr->apm_check_and_defer_cmd_processing_fptr)
   {

      if (AR_EOK != (result = apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr
                                 ->apm_check_and_defer_cmd_processing_fptr(apm_info_ptr, &defer_cmd_proc)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gpr_cmd_handler(), Deferred cmd proc check failed for cmd_opcode: 0x%8lX",
                cmd_opcode);

         /** Set up sequencer to begin with error handling */
         apm_cmd_seq_set_up_err_hdlr(apm_info_ptr->curr_cmd_ctrl_ptr);
      }
   }

   /** Check if the current command processing needs to be
    *  deferred, then return */
   if ((AR_EOK == result) && defer_cmd_proc)
   {
      return AR_EOK;
   }

   /** Execution falls through if command processing can
    *  continue further */

   /** Now invoke the command sequencer based upon the current
    *  command opcode */

   /** Call the command sequencer, corresponding to current
    *  opcode under process */
   if (AR_EOK != (result = apm_cmd_sequencer_cmn_entry(apm_info_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "CMD seq failed, cmd_opcode[0x%lX], result[%lu]", cmd_opcode, result);
   }

   return result;

__bailout_cmd_rsc_alloc_fail:

   /** End the command with failed status   */
   apm_cmd_ctrl_clear_cmd_pending_status(apm_info_ptr->curr_cmd_ctrl_ptr, result);

   /** End the GPR command with failure status */
   apm_end_cmd(apm_info_ptr);

   AR_MSG(DBG_ERROR_PRIO, "apm_process_cmd(): GPR cmd_opcode[0x%lX] FAILED, result[%lu]", cmd_opcode, result);

   return result;
}

ar_result_t apm_allocate_cmd_rsp_payload(uint32_t       log_id,
                                         gpr_packet_t  *gpr_pkt_ptr,
                                         gpr_packet_t **gpr_rsp_pkt_pptr,
                                         uint32_t       cmd_rsp_opcode,
                                         uint32_t       cmd_rsp_payload_size)
{
   ar_result_t         result = AR_EOK;
   gpr_cmd_alloc_ext_t args;

   /** Allocate GPR response packet  */
   args.opcode        = cmd_rsp_opcode;
   args.src_domain_id = gpr_pkt_ptr->dst_domain_id;
   args.dst_domain_id = gpr_pkt_ptr->src_domain_id;
   args.src_port      = gpr_pkt_ptr->dst_port;
   args.dst_port      = gpr_pkt_ptr->src_port;
   args.token         = gpr_pkt_ptr->token;
   args.payload_size  = cmd_rsp_payload_size;
   args.client_data   = 0;
   args.ret_packet    = gpr_rsp_pkt_pptr;

   if (AR_EOK != (result = __gpr_cmd_alloc_ext(&args)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_allocate_cmd_rsp_payload: %08lX: Failed to allocate rsp payload for cmd opcode[0x%lX] result: %lu",
             log_id,
             result);
   }

   return result;
}

static ar_result_t apm_get_fwk_status_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t                   result = AR_EOK;
   gpr_packet_t                 *gpr_pkt_ptr;
   gpr_packet_t                 *gpr_pkt_rsp_ptr;
   apm_cmd_rsp_get_spf_status_t *rsp_payload_ptr;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   if (AR_EOK != (result = apm_allocate_cmd_rsp_payload(APM_MODULE_INSTANCE_ID,
                                                        gpr_pkt_ptr,
                                                        &gpr_pkt_rsp_ptr,
                                                        APM_CMD_RSP_GET_SPF_STATE,
                                                        sizeof(apm_cmd_rsp_get_spf_status_t))))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_get_fwk_status_cmd_handler(), Failed to allocate response payload, result: 0x%lX",
             result);

      goto __bail_out_fwk_s_cmd_hdlr;
   }

   /** Get the payload pointer corresponding to GPR response
    *  allocated above */
   rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_spf_status_t, gpr_pkt_rsp_ptr);

   /** Update the framework state */
   rsp_payload_ptr->status = APM_SPF_STATE_READY;

   /** Send the response payload to client */
   if (AR_EOK != (result = __gpr_cmd_async_send((gpr_packet_t *)gpr_pkt_rsp_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_get_fwk_status_cmd_handler(), Failed to send allocated response payload to client, result: 0x%lX",
             result);

      /** Free up the allocated GPR response packet */
      __gpr_cmd_free(gpr_pkt_rsp_ptr);

      goto __bail_out_fwk_s_cmd_hdlr;
   }

   AR_MSG(DBG_HIGH_PRIO, "apm_get_fwk_status_cmd_handler(), **** spf is Up and Ready ****");

   /** Free up the original GPR command packet */
   __gpr_cmd_free(gpr_pkt_ptr);

   return result;

__bail_out_fwk_s_cmd_hdlr:

   /** End the GPR command */
   __gpr_cmd_end_command(gpr_pkt_ptr, result);

   return result;
}

static ar_result_t apm_gpr_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t      result = AR_EOK;
   gpr_packet_t    *gpr_pkt_ptr;
   uint32_t         cmd_opcode;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      case APM_CMD_SET_CFG:
      case APM_CMD_GET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case APM_CMD_GRAPH_SUSPEND:
      {
         result = apm_process_gpr_cmd(apm_info_ptr, msg_ptr);
         break;
      }
      default:
      {
         result = AR_EUNSUPPORTED;

         break;
      }

   } /** End of switch (cmd_opcode) */

   /** If un-supported opcode, end the GPR command   */
   if (AR_EUNSUPPORTED == result)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_gpr_cmd_handler(), Unsupported cmd/msg opcode: 0x%8lX", cmd_opcode);

      /** End the GPR command */
      __gpr_cmd_end_command(gpr_pkt_ptr, result);
   }

   return result;
}

ar_result_t apm_cmdq_gpr_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t      result = AR_EOK;
   gpr_packet_t    *gpr_pkt_ptr;
   uint32_t         cmd_opcode;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   switch (cmd_opcode)
   {
      case APM_CMD_SHARED_MEM_MAP_REGIONS:
      case APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS:
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      case APM_CMD_GLOBAL_SHARED_MEM_UNMAP_REGIONS:
      case APM_CMD_SHARED_MEM_MAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_REGION_ACCESS_INFO:
      {
         if (ext_utils_ptr->shmem_vtbl_ptr && ext_utils_ptr->shmem_vtbl_ptr->apm_shmem_cmd_handler_fptr)
         {
            result =
               ext_utils_ptr->shmem_vtbl_ptr->apm_shmem_cmd_handler_fptr(apm_info_ptr, msg_ptr);
         }
         else /** Shared mem operation is unsupported */
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_cmdq_gpr_cmd_handler(), Unsupported cmd/msg opcode: 0x%8lX, memmap client %lu",
                   cmd_opcode,
                   apm_info_ptr->memory_map_client);

            result = AR_EUNSUPPORTED;

            /** End the GPR command */
            __gpr_cmd_end_command(gpr_pkt_ptr, result);
         }

         break;
      }
      case APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS:
      case APM_CMD_RSP_SHARED_MEM_MAP_REGIONS:
      case APM_CMD_SHARED_SATELLITE_MEM_UNMAP_REGIONS:
      {
         if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_offload_shmem_cmd_handler_fptr)
         {
            result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_shmem_cmd_handler_fptr(apm_info_ptr, msg_ptr);
         }
         else
         {
            result = AR_EUNSUPPORTED;
         }
         break;
      }
      case APM_CMD_GET_SPF_STATE:
      {
         result = apm_get_fwk_status_cmd_handler(apm_info_ptr, msg_ptr);

         break;
      }
      default:
      {
         result = apm_gpr_cmd_handler(apm_info_ptr, msg_ptr);
         break;
      }
   } /** End of switch (cmd_opcode) */

   return result;
}
