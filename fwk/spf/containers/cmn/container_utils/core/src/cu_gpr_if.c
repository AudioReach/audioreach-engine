/**
 * \file cu_gpr_if.c
 *
 * \brief
 *     GPR callbacks.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "container_utils.h"

/*==============================================================================
   Global Defines
==============================================================================*/
#include "cu_i.h"
#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_ep_ext_api.h"
#include "rd_sh_mem_ep_ext_api.h"

uint32_t cu_gpr_callback(gpr_packet_t *packet, void *callback_data)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spf_handle_t *handle_ptr = NULL;

   AR_MSG(DBG_LOW_PRIO,
         SPF_LOG_PREFIX "GPR callback from src 0x%lX to dst 0x%lX, opcode 0x%lX, token 0x%lX",
          packet->src_port,
          packet->dst_port,
          packet->opcode,
          packet->token);

   spf_msg_t msg;
   uint32_t  thread_id = 0;
   msg.payload_ptr     = packet;
   msg.msg_opcode      = SPF_MSG_CMD_GPR;

   // No need of mutex here. If a container destroys a module, it will first
   // deregister from GPR/ Until this function finishes, GPR must block
   // deregister.
   handle_ptr = (spf_handle_t *)callback_data;

   /*Validate handles and queue pointers */
   VERIFY(result, (handle_ptr && handle_ptr->cmd_handle_ptr && handle_ptr->cmd_handle_ptr->cmd_q_ptr));

   thread_id = (uint32_t)posal_thread_get_tid_v2(handle_ptr->cmd_handle_ptr->thread_id);

   switch (cu_get_bits(packet->opcode, AR_GUID_TYPE_MASK, AR_GUID_TYPE_SHIFT))
   {
      case AR_GUID_TYPE_CONTROL_CMD:
      {
         /** control commands */
         TRY(result,
             (ar_result_t)posal_queue_push_back(handle_ptr->cmd_handle_ptr->cmd_q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_DATA_CMD:
      {
         VERIFY(result, (NULL != handle_ptr->q_ptr));
         /** Data commands */
         TRY(result, (ar_result_t)posal_queue_push_back(handle_ptr->q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_CONTROL_EVENT:
      {
         switch (packet->opcode)
         {
            case IMCL_INTER_PROC_TRIGGER_MSG_GPR:
            case IMCL_INTER_PROC_POLLING_MSG_GPR:
            case IMCL_INTER_PROC_PEER_STATE_UPDATE:
            case EVENT_ID_MODULE_CMN_METADATA_CUSTOM_TRACKING_EVENT:
               // for polling message, first push to cmd Q. from there, we'll open the message and push to the ctrl port
               // Q
               // which will be polled at process boundary
               // peer state update will be handled differently from the cmd handler
               {
                  TRY(result,
                      (ar_result_t)posal_queue_push_back(handle_ptr->cmd_handle_ptr->cmd_q_ptr,
                                                         (posal_queue_element_t *)&msg));
                  break;
               }
            default:
            {
               AR_MSG(DBG_ERROR_PRIO, "CNTR TID: 0x%lx Unsupported Ctrl Event 0x%lX", thread_id, packet->opcode);
               THROW(result, AR_EFAILED);
               break;
            }
         }
         break;
      }
      case AR_GUID_TYPE_CONTROL_CMD_RSP:
      {
         AR_MSG(DBG_ERROR_PRIO, "CNTR TID: 0x%lx received response GUID 0x%lX. This is unexpected!", thread_id, packet->opcode);

         //response command should just be freed. calling __gpr_cmd_end_command will result into sending another response back.
         //If a module is sending a GPR command to somewhere then its ack can come back to the container.
         __gpr_cmd_free(packet);
         return AR_EFAILED;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "CNTR TID: 0x%lx Improper GUID 0x%lX", thread_id, packet->opcode);

         THROW(result, AR_EFAILED);
      }
   }

   CATCH(result, "CNTR TID ID: 0x%lx ", thread_id)
   {
      __gpr_cmd_end_command(packet, result);
   }

   return result;
}

ar_result_t cu_gpr_free_pkt(uint32_t gpr_handle, gpr_packet_t *pkt_ptr)
{
   return __gpr_cmd_free(pkt_ptr);
}

ar_result_t cu_gpr_generate_client_event(uint8_t  src_domain_id,
                                         uint8_t  dst_domain_id,
                                         uint32_t src_port,
                                         uint32_t dst_port,
                                         uint32_t token,
                                         uint32_t event_opcode,
                                         void *   event_payload_ptr,
                                         uint32_t payload_size)
{

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = dst_domain_id;
   args.dst_domain_id = src_domain_id;
   args.src_port      = dst_port;
   args.dst_port      = src_port;
   args.token         = token;
   args.opcode        = event_opcode; // DATA_CMD_RSP_WR_SH_MEM_EP_EOS_RENDERED,
   args.payload       = NULL;
   args.payload_size  = 0;
   args.client_data   = 0;
   ar_result_t result = __gpr_cmd_alloc_send(&args);

   return result;
}

/** ACK function for call back */
ar_result_t cu_gpr_generate_ack(cu_base_t *   me_ptr,
                                gpr_packet_t *packet_ptr, /// This is the received packet that requires ACK.
                                ar_result_t   status,
                                void *        ack_payload_ptr, /// payload that is required in ACK, specified by caller
                                uint32_t      size,            /// payload size.
                                uint32_t      ack_opcode       /// Optonal The opcode for ACK.
                                )
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = packet_ptr->opcode;
   switch (opcode)
   {
      case DATA_CMD_WR_SH_MEM_EP_EOS:
      {

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "CNTR sending DATA_CMD_RSP_WR_SH_MEM_EP_EOS_RENDERED to the client.");

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
         __gpr_cmd_alloc_send(&args);

         __gpr_cmd_free(packet_ptr);
      }
      break;

      // For a lot of commands, the ack is basically the inverse
      // the packet_ptr with slight change
      case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                SPF_LOG_PREFIX "CNTR:Port=0x%x: status 0x%lx, DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2 0x%8x , "
                               "[0x%p,%lu], Token %lX",
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
         args.client_data = result = __gpr_cmd_alloc_send(&args);

         __gpr_cmd_free(packet_ptr);
         break;
      }

      case DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2:
         if (ack_payload_ptr == NULL || size <= 0)
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "CNTR:Port=0x%x: Error: DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2 ACK requires a payload, "
                   "[0x%p,%lu] ",
                   packet_ptr->dst_port,
                   (uintptr_t)ack_payload_ptr,
                   size);
            result = AR_EFAILED;
         }
         else
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   SPF_LOG_PREFIX "CNTR:Port=0x%x: status 0x%lx, DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2 0x%8x "
                                  "[0x%p,%lu], Token %lX ",
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
            result             = __gpr_cmd_alloc_send(&args);

            __gpr_cmd_free(packet_ptr);
         }
         break;

      default:
         if (ack_payload_ptr && ack_opcode)
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "CNTR:Port=0x%x: status 0x%lx, Ack 0x%lx , [0x%p,%lu] ",
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
            result             = __gpr_cmd_alloc_send(&args);

            __gpr_cmd_free(packet_ptr);
         }
         else
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "CNTR:Port=0x%x: status 0x%lx, Basic Ack: 0x%lx, [0x%p,%lu] ",
                   packet_ptr->dst_port,
                   (uint32_t)status,
                   packet_ptr->opcode,
                   (uintptr_t)ack_payload_ptr,
                   size);
            result = __gpr_cmd_end_command(packet_ptr, status);
         }

         break;
   }

   if (AR_DID_FAIL(result))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "CNTR:Port=0x%x: fail to send ack 0x%lx",
             packet_ptr->dst_port,
             (uint32_t)result);
   }

   return result;
}
