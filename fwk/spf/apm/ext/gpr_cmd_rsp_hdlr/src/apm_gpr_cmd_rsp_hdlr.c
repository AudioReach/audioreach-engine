/**
 * \file apm_gpr_cmd_rsp_hdlr.c
 *
 * \brief
 *     This file contains APM GPR Command Response Handler utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/

#include "apm_internal.h"
#include "apm_memmap_api.h"
#include "apm_cmd_utils.h"

/****************************************************************************
 * Function Definitions
 ****************************************************************************/
static ar_result_t apm_aggregate_gpr_rsp(apm_t *                  apm_info_ptr,
                                         apm_cmd_ctrl_t *         apm_cmd_ctrl_ptr,
                                         gpr_ibasic_rsp_result_t *gpr_rsp_payload_ptr)
{
   ar_result_t result = AR_EOK;

   apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status |= gpr_rsp_payload_ptr->status;
   apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_rcvd++;

   AR_MSG(DBG_LOW_PRIO,
          "APM: apm_aggregate_gpr_rsp: num_rsp_rcvd %lu, num_cmd_issued = %lu ",
          apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_rcvd,
          apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued);

   if (apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_rcvd == apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued)
   {
      if (apm_cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr)
      {
         /** Set the current seq up status   */
         apm_cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->status = apm_cmd_ctrl_ptr->cmd_status;
      }

      /** Clear the APM cmd response control */
      apm_clear_cmd_rsp_ctrl(&apm_cmd_ctrl_ptr->rsp_ctrl);

      apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = FALSE;
   }
   else
   {
      return AR_EPENDING;
   }

   return result;
}

static ar_result_t apm_gpr_basic_rsp_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t              result              = AR_EOK;
   gpr_packet_t *           gpr_pkt_ptr         = NULL;
   gpr_ibasic_rsp_result_t *gpr_rsp_payload_ptr = NULL;
   apm_ext_utils_t *        ext_utils_ptr       = NULL;
   apm_cmd_ctrl_t *         apm_cmd_ctrl_ptr    = NULL;
   uint32_t                 cmd_ctrl_list_idx   = 0;

   /** Get the pointer to GPR command payload */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the response payload pointer */
   gpr_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, gpr_pkt_ptr);

   cmd_ctrl_list_idx = gpr_pkt_ptr->token;
   if (cmd_ctrl_list_idx >= APM_NUM_MAX_PARALLEL_CMD)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "APM: apm_gpr_basic_rsp_handler: Unexpected Error: Can't recover the cmd ctx. Token %lu",
             cmd_ctrl_list_idx);
      result = __gpr_cmd_free(gpr_pkt_ptr);
      return (result | AR_EUNEXPECTED);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_gpr_basic_rsp_handler(): Received GPR basic response for opcode: 0x%lX",
          gpr_rsp_payload_ptr->opcode);

   apm_cmd_ctrl_ptr = apm_get_nth_cmd_ctrl_obj(apm_info_ptr, cmd_ctrl_list_idx);

   if (AR_EPENDING == (result = apm_aggregate_gpr_rsp(apm_info_ptr, apm_cmd_ctrl_ptr, gpr_rsp_payload_ptr)))
   {
      result |= __gpr_cmd_free(gpr_pkt_ptr);
	  // if a command response needs more detailed handling for every response,
	  // we would need to process the response and then free or cache the response and process & free later.
	  // the above handling is TODO for now and will be planned as needed for future commands.
	  return result;
   }

   switch (gpr_rsp_payload_ptr->opcode)
   {
      case APM_CMD_SHARED_MEM_MAP_REGIONS:
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      case APM_CMD_SET_CFG:
      case APM_CMD_CLOSE_ALL:
      case APM_CMD_SHARED_MEM_MAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_REGION_ACCESS_INFO:
      {
         if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_offload_basic_rsp_handler_fptr)
         {
            result |= ext_utils_ptr->offload_vtbl_ptr->apm_offload_basic_rsp_handler_fptr(apm_info_ptr,
                                                                                          apm_cmd_ctrl_ptr,
                                                                                          gpr_pkt_ptr);
         }
         else
         {
            result |= AR_EUNSUPPORTED;
         }

         break;
      }

      default:
      {
         result |= AR_EUNSUPPORTED;

         break;
      }
   }

   /** If response opcode is not supported   */
   if (AR_EUNSUPPORTED == result)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gpr_basic_rsp_handler(): Unsupported GPR cmd response opcode: 0x%lX",
             gpr_rsp_payload_ptr->opcode);
   }

   /** Update the current APM cmd corresponding to current
    *  response in process */
   apm_info_ptr->curr_cmd_ctrl_ptr = apm_cmd_ctrl_ptr;

   if ((NULL != apm_cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr) &&
       (!apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending))
   {
      /** Call the sequencer function to perform next action */
      apm_cmd_sequencer_cmn_entry(apm_info_ptr);
   }

   return result;
}

ar_result_t apm_rspq_gpr_cmd_rsp_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;

   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "apm_rspq_gpr_cmd_rsp_handler():"
          " Received GPR cmd response opcode: 0x%lX",
          gpr_pkt_ptr->opcode);

   switch (gpr_pkt_ptr->opcode)
   {
      case GPR_IBASIC_RSP_RESULT:
      {
         result = apm_gpr_basic_rsp_handler(apm_info_ptr, msg_ptr);

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_rspq_gpr_cmd_rsp_handler(): Unexpected GPR opcode: 0x%lX", gpr_pkt_ptr->opcode);

         result = AR_EUNEXPECTED;

         /** End the GPR command */
         __gpr_cmd_end_command(gpr_pkt_ptr, result);

         break;
      }

   } /** End of switch (gpr_pkt_ptr->opcode) */

   return AR_EOK;
}
