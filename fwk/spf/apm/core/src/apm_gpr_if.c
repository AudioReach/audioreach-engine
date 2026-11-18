/**
 * \file apm_gpr_if.c
 *  
 * \brief
 *     This file contains APM GPR Handler Routines
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "posal.h"
#include "spf_utils.h"
#include "gpr_api_inline.h"
#include "spf_cmn_if.h"
#include "spf_inter_proc_md_utils.h"

/* =======================================================================
**                          Function Definitions
** ======================================================================= */

uint32_t apm_gpr_call_back_f(gpr_packet_t *gpr_pkt_ptr, void *cb_ctx_ptr)
{
   uint32_t      result = AR_EOK;
   spf_msg_t     msg;
   uint32_t      cmd_opcode;
   spf_handle_t *dst_handle_ptr = NULL;

   /* Validate GPR packet pointer */
   if (!gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM GPR CB: GPR pkt ptr is NULL");

      return AR_EFAILED;
   }

   /* Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   AR_MSG(DBG_HIGH_PRIO, "APM GPR CB, rcvd cmd opcode[0x%08lX], token:[0x%x] ", cmd_opcode, gpr_pkt_ptr->token);

   /* Validate GPR callback context pointer */
   if (!cb_ctx_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM GPR CB: CB ctxt ptr is NULL");
      goto __bailout;
   }

   /* Get the destination module handle */
   dst_handle_ptr = (spf_handle_t *)cb_ctx_ptr;

   /** Compose the GK message payload to be routed to
    *  destination module */
   msg.msg_opcode  = SPF_MSG_CMD_GPR;
   msg.payload_ptr = gpr_pkt_ptr;

   switch (gpr_pkt_ptr->opcode)
   {
      case GPR_IBASIC_RSP_RESULT:
      {
         if (AR_EOK !=
             (result = posal_queue_push_back((posal_queue_t *)dst_handle_ptr->q_ptr, (posal_queue_element_t *)&msg)))
         {
            AR_MSG(DBG_ERROR_PRIO, "APM GPR CB: Failed to push gpr rsp to APM rsp_q, result: %lu", result);

            goto __bailout;
         }

         break;
      }
      case EVENT_ID_IPC_METADATA_INCR_REF_COUNT:
      case EVENT_ID_IPC_METADATA_DECR_REF_COUNT:
      case EVENT_ID_MODULE_CMN_METADATA_CLONE_MD:
      case EVENT_ID_MODULE_CMN_METADATA_TRACKING_EVENT:
      {
         result = spf_ipmd_gpr_callback_handler(gpr_pkt_ptr);
         break;
      }
      default:
      {
         if (AR_EOK != (result = spf_msg_send_cmd(&msg, dst_handle_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO, "APM GPR CB: Failed to push gpr msg to APM cmd_q, result: %lu", result);

            goto __bailout;
         }
      }
      break;
   }

   return result;

__bailout:

   /* End the GPR command */
   __gpr_cmd_end_command(gpr_pkt_ptr, result);

   return AR_EOK;
}
