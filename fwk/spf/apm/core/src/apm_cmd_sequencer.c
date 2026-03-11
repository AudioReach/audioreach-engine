/**
 * \file apm_cmd_sequencer.c
 *
 * \brief
 *     This file contains sequencer utils for executing all the APM command operations
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_msg_utils.h"
#include "apm_proxy_utils.h"
#include "apm_graph_utils.h"
#include "apm_cmd_utils.h"
#include "apm_memmap_api.h"
#include "apm_ext_cmn.h"
#include "apm_proxy_vcpm_utils.h"

/* =======================================================================
**                          Function Definitions
** ======================================================================= */

void apm_end_cmd_op_sequencer(apm_cmd_ctrl_t *cmd_ctrl_ptr, apm_op_seq_t *curr_op_seq_ptr)
{
   /** Set the curr seq obj to completed   */
   curr_op_seq_ptr->op_idx = APM_CMN_CMD_OP_COMPLETED;

   /** Clear the cmd op pending status   */
   curr_op_seq_ptr->curr_cmd_op_pending = FALSE;

   /** Restore the curr seq obj to primary   */
   cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr = cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr;
}

void apm_clear_curr_cmd_op_pending_status(apm_op_seq_t *op_seq_ptr)
{
   /** Clear the curr op pending flag  */
   op_seq_ptr->curr_cmd_op_pending = FALSE;

   /** Clear the seq index for this op  */
   op_seq_ptr->curr_seq_idx = APM_CMD_SEQ_IDX_INVALID;

   /** Set the status to EOK   */
   op_seq_ptr->status = AR_EOK;
}

void apm_init_cmd_seq_info(apm_cmd_ctrl_t *         cmd_ctrl_ptr,
                           apm_cmd_seq_entry_func_t seq_fptr,
                           apm_op_seq_t *           prim_op_seq_ptr)
{
   cmd_ctrl_ptr->cmd_seq.cmd_seq_entry_fptr = seq_fptr;
   cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr     = prim_op_seq_ptr;
   cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr    = prim_op_seq_ptr;

   /** Init error handler command op */
   cmd_ctrl_ptr->cmd_seq.err_hdlr_seq.op_idx = APM_CMN_CMD_OP_HANDLE_FAILURE;
}

static ar_result_t apm_check_if_peer_suspend_reqd(apm_t *apm_info_ptr, bool_t *peer_suspend_reqd_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_gm_cmd_sg_node_ptr;
   apm_sub_graph_t *             curr_gm_cmd_sg_obj_ptr;
   spf_list_node_t *             curr_port_conn_node_ptr;
   apm_cont_port_connect_info_t *curr_port_conn_obj_ptr;
   apm_cmd_ctrl_t *              cmd_ctrl_ptr;

   /** Get the pointer to current command control obj   */
   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Peer suspend handlng needs to be done only for prepare/start
    *  commands */
   if ((cmd_ctrl_ptr->cmd_opcode != APM_CMD_GRAPH_PREPARE) && (cmd_ctrl_ptr->cmd_opcode != APM_CMD_GRAPH_START) &&
       (cmd_ctrl_ptr->cmd_opcode != SPF_MSG_CMD_PROXY_GRAPH_PREPARE) &&
       (cmd_ctrl_ptr->cmd_opcode != SPF_MSG_CMD_PROXY_GRAPH_START))
   {
      return AR_EOK;
   }

   /** Get the pointer to the list of data/control links across
    *  differnet sub-graphs */
   curr_port_conn_node_ptr = apm_info_ptr->graph_info.sub_graph_conn_list_ptr;

   /** Validate if regular sub-graph list is non-empty   */
   if (!cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_check_if_peer_suspend_reqd(): Reg SG list empty "
             "cmd_opcode[0x%08lX]",
             cmd_ctrl_ptr->cmd_opcode);

      return AR_EFAILED;
   }

   /** Init the return bool   */
   *peer_suspend_reqd_ptr = FALSE;

   /** Iterte over the list of data/control links across
    *  sub-graphs */
   while (curr_port_conn_node_ptr)
   {
      /** Get the pointer to current data/control link obj   */
      curr_port_conn_obj_ptr = (apm_cont_port_connect_info_t *)curr_port_conn_node_ptr->obj_ptr;

      /** Get the pointer to the list of regular sub-graphs being
       *  processed */
      curr_gm_cmd_sg_node_ptr = cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;

      /** Iterate over the list of regular sub-graphs   */
      while (curr_gm_cmd_sg_node_ptr)
      {
         curr_gm_cmd_sg_obj_ptr = (apm_sub_graph_t *)curr_gm_cmd_sg_node_ptr->obj_ptr;

         /** For data and control links across different sub-graphs,
          *  check if for the new link open the self port's sub-graph id,
          *  it's peer sub-graph is in suspended state and new self
          *  port propagated state is STOPPED as it has been newly
          *  opened  */

         if ((curr_gm_cmd_sg_obj_ptr->sub_graph_id == curr_port_conn_obj_ptr->self_sg_obj_ptr->sub_graph_id) &&
             (APM_SG_STATE_STOPPED == curr_port_conn_obj_ptr->peer_sg_propagated_state) &&
             (APM_SG_STATE_SUSPENDED == curr_port_conn_obj_ptr->peer_sg_obj_ptr->state))
         {
            AR_MSG(DBG_MED_PRIO,
                   "apm_check_if_peer_suspend_reqd(): Peer suspend required for peer SG_ID[0x%lX], "
                   "cmd_opcode[0x%08lX]",
                   curr_port_conn_obj_ptr->peer_sg_obj_ptr->sub_graph_id,
                   cmd_ctrl_ptr->cmd_opcode);

            *peer_suspend_reqd_ptr = TRUE;

            return AR_EOK;
         }

         /** Advance to next node in the list   */
         curr_gm_cmd_sg_node_ptr = curr_gm_cmd_sg_node_ptr->next_ptr;

      } /** End of while (reg sub-graph list) */

      /** Advance to next node in the list   */
      curr_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;

   } /** End of while(port conn list) */

   return result;
}

ar_result_t apm_populate_graph_close_proc_seq(apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                              apm_sub_graph_state_t  sub_graph_list_state,
                                              apm_cont_msg_opcode_t *cont_msg_opcode_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    op_idx = 0;

   switch (sub_graph_list_state)
   {
      case APM_SG_STATE_STOPPED:
      case APM_SG_STATE_PREPARED:
      {
         if (APM_CMD_GRAPH_OPEN == apm_cmd_ctrl_ptr->cmd_opcode)
         {
            switch (apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status)
            {
               case APM_OPEN_CMD_STATE_OPEN_FAIL:
               {
                  cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_CLOSE;
                  break;
               }
               case APM_OPEN_CMD_STATE_CONNECT_FAIL:
               {
                  cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_DISCONNECT;
                  cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_CLOSE;

                  break;
               }
               case APM_OPEN_CMD_STATE_NORMAL:
               default:
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_graph_close_proc_seq(): Unexpected state[%lu] during graph open cmd err "
                         "handling",
                         apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status);

                  result = AR_EUNEXPECTED;

                  break;
               }
            }
         }
         else /** Regular graph management command */
         {
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_DISCONNECT;
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_CLOSE;
         }

         cont_msg_opcode_ptr->num_msg_opcode = op_idx;

         break;
      }
      case APM_SG_STATE_SUSPENDED:
      case APM_SG_STATE_STARTED:
      {
         cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_STOP;

         if (APM_GM_CMD_OP_PROCESS_REG_GRAPH == apm_cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx)
         {
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_DISCONNECT;
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_CLOSE;
         }

         cont_msg_opcode_ptr->num_msg_opcode = op_idx;

         break;
      }
      default:
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_graph_close_proc_seq(): Unexpected SG list state[%lu], cmd_opcode[0x%08lX]",
                sub_graph_list_state,
                apm_cmd_ctrl_ptr->cmd_opcode);

         result = AR_EFAILED;
         break;
      }

   } /** End of switch (sub_graph_list_state) */

   return result;
}

static ar_result_t apm_populate_graph_start_proc_seq(apm_t *                apm_info_ptr,
                                                     apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                                     apm_sub_graph_state_t  sub_graph_list_state,
                                                     apm_cont_msg_opcode_t *cont_msg_opcode_ptr,
                                                     bool_t                 peer_suspend_reqd)
{
   ar_result_t result = AR_EOK;
   uint32_t    op_idx = 0;

   switch (sub_graph_list_state)
   {
      case APM_SG_STATE_PREPARED:
      case APM_SG_STATE_SUSPENDED:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_START;
         break;
      }
      case APM_SG_STATE_STOPPED:
      {
         if (APM_GM_CMD_OP_PROCESS_REG_GRAPH == apm_cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx)
         {
            if (peer_suspend_reqd)
            {
               cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_SUSPEND;
            }

            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_PREPARE;
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_START;

            cont_msg_opcode_ptr->num_msg_opcode = op_idx;
         }
         else /** Proxy graph processing */
         {
            cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_PREPARE;
         }

         break;
      }
      default:
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_graph_start_proc_seq(): Unexpected SG list state[%lu], "
                "cmd_opcode[0x%08lX]",
                sub_graph_list_state,
                apm_cmd_ctrl_ptr->cmd_opcode);

         result = AR_EFAILED;
         break;
      }

   } /** End of switch (sub_graph_list_state) */

   return result;
}

static ar_result_t apm_populate_graph_mgmt_at_open_proc_seq(apm_t *                apm_info_ptr,
                                                            apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                                            apm_sub_graph_state_t  sub_graph_list_state,
                                                            apm_cont_msg_opcode_t *cont_msg_opcode_ptr,
                                                            bool_t                 peer_suspend_reqd)
{
   ar_result_t result = AR_EOK;

   switch (apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx)
   {
      case APM_OPEN_CMD_OP_ERR_HDLR:
      {
         result = apm_populate_graph_close_proc_seq(apm_cmd_ctrl_ptr, sub_graph_list_state, cont_msg_opcode_ptr);

         break;
      }
      case APM_OPEN_CMD_OP_HDL_LINK_START:
      {
         result = apm_populate_graph_start_proc_seq(apm_info_ptr,
                                                    apm_cmd_ctrl_ptr,
                                                    sub_graph_list_state,
                                                    cont_msg_opcode_ptr,
                                                    peer_suspend_reqd);
         break;
      }
      default:
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_graph_mgmt_at_open_proc_seq(): Unexpected cmd op idx[0x%lX], "
                "cmd_opcode[0x%08lX]",
                apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx,
                apm_cmd_ctrl_ptr->cmd_opcode);

         break;
      }
   }

   return result;
}

static ar_result_t apm_populate_cont_graph_proc_seq(apm_t *apm_info_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr;
   apm_sub_graph_state_t  sub_graph_list_state = APM_SG_STATE_INVALID;
   uint32_t               op_idx               = 0;
   bool_t                 peer_suspend_reqd    = FALSE;

   /** Get the pointer to current command control obj   */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM command control */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   sub_graph_list_state = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state;

   /** If the state is invalid that implies the sub-graph list
    *  parsing did not assign the list state properly. The
    *  commands needs to be aborted here. */
   if (APM_SG_STATE_INVALID == sub_graph_list_state)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_populate_cont_graph_proc_seq(): SG list state not configured yet[%lu], cmd_opcode[0x%08lX]",
             sub_graph_list_state,
             apm_cmd_ctrl_ptr->cmd_opcode);

      return AR_EFAILED;
   }

   /** Check if peer suspend required   */
   if (AR_EOK != (result = apm_check_if_peer_suspend_reqd(apm_info_ptr, &peer_suspend_reqd)))
   {
      return result;
   }

   switch (apm_cmd_ctrl_ptr->cmd_opcode)
   {
      case APM_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      {
         if ((APM_GM_CMD_OP_PROCESS_REG_GRAPH == apm_cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx) && peer_suspend_reqd)
         {
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_SUSPEND;
            cont_msg_opcode_ptr->opcode_list[op_idx++] = SPF_MSG_CMD_GRAPH_PREPARE;

            cont_msg_opcode_ptr->num_msg_opcode = op_idx;
         }
         else /** Proxy sub-graph handling or Peer suspend not required */
         {
            cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_PREPARE;
         }

         break;
      }
      case APM_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_STOP;
         break;
      }
      case APM_CMD_GRAPH_FLUSH:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_FLUSH;
         break;
      }
      case APM_CMD_GRAPH_SUSPEND:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_SUSPEND;
         break;
      }
      case APM_CMD_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      {
         result = apm_populate_graph_start_proc_seq(apm_info_ptr,
                                                    apm_cmd_ctrl_ptr,
                                                    sub_graph_list_state,
                                                    cont_msg_opcode_ptr,
                                                    peer_suspend_reqd);

         break;
      }
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      {
         result = apm_populate_graph_close_proc_seq(apm_cmd_ctrl_ptr, sub_graph_list_state, cont_msg_opcode_ptr);

         break;
      }
      case APM_CMD_GRAPH_OPEN:
      {
         result = apm_populate_graph_mgmt_at_open_proc_seq(apm_info_ptr,
                                                           apm_cmd_ctrl_ptr,
                                                           sub_graph_list_state,
                                                           cont_msg_opcode_ptr,
                                                           peer_suspend_reqd);

         break;
      }
      default:
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_cont_graph_proc_seq(): Unexpected cmd_opcode[0x%08lX]",
                apm_cmd_ctrl_ptr->cmd_opcode);

         break;
      }

   } /** End of switch (apm_cmd_ctrl_ptr->cmd_opcode) */

   return result;
}

ar_result_t apm_populate_cont_graph_mgmt_cmd_seq(apm_t *apm_info_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr;

   /** Get the pointer to current cmd ctrl object  */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM command control */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   switch (apm_cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx)
   {
      case APM_GM_CMD_OP_PRE_PROCESS:
      case APM_GM_CMD_OP_DB_QUERY_SEND_INFO:
      {
         break;
      }
      case APM_GM_CMD_OP_PROCESS_REG_GRAPH:
      case APM_GM_CMD_OP_PROCESS_PROXY_GRAPH:
      {
         result = apm_populate_cont_graph_proc_seq(apm_info_ptr);

         break;
      }
      case APM_GM_CMD_OP_HDL_DATA_PATHS:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_SET_CFG;

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_cont_graph_mgmt_cmd_seq(): Unexpected gm cmd op idx[%lu], cmd_opcode[0x%08lX]",
                apm_cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx,
                apm_cmd_ctrl_ptr->cmd_opcode);

         result = AR_EFAILED;

         break;
      }

   } /** End of switch (apm_cmd_ctrl_ptr->cmd_seq.curr_gm_cmd_op_idx) */

   return result;
}

static ar_result_t apm_populate_cont_open_cmd_seq(apm_t *apm_info_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_ext_utils_t *      ext_utils_ptr;
   apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr;

   /** Get the pointer to current cmd ctrl obj   */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM command control */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   /** Get the ext utils pointer */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   switch (apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx)
   {
      case APM_OPEN_CMD_OP_SEND_CONT_OPEN_MSG:
      {
         cont_msg_opcode_ptr->num_msg_opcode = 2;
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GRAPH_OPEN;
         cont_msg_opcode_ptr->opcode_list[1] = SPF_MSG_CMD_GRAPH_CONNECT;

         break;
      }
      case APM_OPEN_CMD_OP_HDL_LINK_START:
      {
         result = apm_populate_cont_graph_mgmt_cmd_seq(apm_info_ptr);

         break;
      }
      case APM_OPEN_CMD_OP_HDL_DATA_PATHS:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_SET_CFG;

         break;
      }
      case APM_OPEN_CMD_OP_HDL_DB_QUERY_PREPROCESS:
      case APM_OPEN_CMD_OP_HDL_DB_QUERY_SEND_INFO:
      {
         break;
      }
      case APM_OPEN_CMD_OP_ERR_HDLR:
      {
         if (ext_utils_ptr->err_hdlr_vtbl_ptr &&
             ext_utils_ptr->err_hdlr_vtbl_ptr->apm_populate_cont_open_cmd_err_hdlr_seq_fptr)
         {
            result = ext_utils_ptr->err_hdlr_vtbl_ptr->apm_populate_cont_open_cmd_err_hdlr_seq_fptr(apm_info_ptr);
         }
         else
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "::WARNING:: apm_populate_cont_open_cmd_seq(): open cmd error handling not supported");

            result = AR_EUNSUPPORTED;
         }

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_cont_open_cmd_seq(): Unexpected op_idx[0x%lX], cmd_opcode[0x%08lX]",
                apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx,
                apm_cmd_ctrl_ptr->cmd_opcode);

         result = AR_EFAILED;
         break;
      }

   } /** End of switch (apm_cmd_ctrl_ptr->cmd_seq.curr_open_cmd_op_idx) */

   return result;
}

static ar_result_t apm_populate_spf_set_cfg_cmd_seq(apm_t *apm_info_ptr)
{
   ar_result_t     result           = AR_EOK;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr = NULL;

   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;

   /** Get the pointer to current command control obj   */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM command control */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   switch (apm_cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx)
   {
      case APM_SET_GET_CFG_CMD_OP_SEND_CONT_MSG:
      case APM_SET_GET_CFG_CMD_OP_SEND_PROXY_MGR_MSG:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_SET_CFG;
         break;
      }
      case APM_SET_GET_CFG_CMD_OP_CLOSE_ALL:
      {
         if (apm_cmd_ctrl_ptr->set_cfg_cmd_ctrl.is_close_all_needed)
         {
            apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
            if (ext_utils_ptr->close_all_vtbl_ptr &&
                ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_set_cfg_close_all_seq_fptr)
            {
               result = ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_set_cfg_close_all_seq_fptr(apm_info_ptr);
            }
         }
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_spf_set_cfg_cmd_seq(): Un-supported set get cmd operation index[%lu]",
                apm_cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx);
      }
   }

   return result;
}

static ar_result_t apm_populate_cont_cmd_seq(apm_t *apm_info_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr;

   /** Get current command control obj pointer   */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM command control */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   /** Clear the seq struct */
   memset(cont_msg_opcode_ptr, 0, sizeof(apm_cont_msg_opcode_t));

   /** Reset the current command index being processed */
   cont_msg_opcode_ptr->curr_opcode_idx = 0;

   /** Set the default num opcode */
   cont_msg_opcode_ptr->num_msg_opcode = 1;

   switch (apm_cmd_ctrl_ptr->cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         result = apm_populate_cont_open_cmd_seq(apm_info_ptr);
         break;
      }
      case APM_CMD_SET_CFG:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_SET_CFG;
         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      {
         result = apm_populate_spf_set_cfg_cmd_seq(apm_info_ptr);
         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      {
         break;
      }
      case APM_CMD_GET_CFG:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_GET_CFG;
         break;
      }
      case SPF_EVT_TO_APM_FOR_PATH_DELAY:
      {
         cont_msg_opcode_ptr->num_msg_opcode = 2;

         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_SET_CFG;
         cont_msg_opcode_ptr->opcode_list[1] = SPF_MSG_CMD_SET_CFG;

         break;
      }
      case APM_CMD_REGISTER_CFG:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_REGISTER_CFG;
         break;
      }
      case APM_CMD_DEREGISTER_CFG:
      {
         cont_msg_opcode_ptr->opcode_list[0] = SPF_MSG_CMD_DEREGISTER_CFG;
         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_GRAPH_SUSPEND:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      case APM_CMD_CLOSE_ALL:
      {
         result = apm_populate_cont_graph_mgmt_cmd_seq(apm_info_ptr);

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_cont_cmd_seq(): Unexpected cmd_opcode[0x%08lX]",
                apm_cmd_ctrl_ptr->cmd_opcode);

         result = AR_EFAILED;
         break;
      }
   } /** End of switch (cmd_opcode)*/

   return result;
}

void apm_abort_cmd_op_sequencer(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   /** This case should hit only in case of any corruptions, in
    *  which case the command should be ended immediately.  */

   /** Update the failure status only if not set previously.
    *  Default value is EOK.  */
   if (AR_EOK == cmd_ctrl_ptr->cmd_status)
   {
      cmd_ctrl_ptr->cmd_status = AR_EFAILED;
   }

   /** Set the command operation to complete so that this
    *  command can be ended*/
   cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx = APM_CMN_CMD_OP_COMPLETED;

   return;
}

static bool_t apm_check_curr_cmd_op_is_pending(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   /** Set the default status to pending   */
   bool_t op_status = TRUE;

   /** Update sub-graph state, if applicable based upon the
    *  current message opcode */
   /** Incremenent container msg opcode index in the list */
   apm_cont_msg_one_iter_completed(cmd_ctrl_ptr);

   /** If the container message sequence is done or any
    *  intemediate failure occured, clear the
    *  current operation pending flag */
   if ((apm_check_cont_msg_seq_done(cmd_ctrl_ptr)) || (AR_EOK != cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->status))
   {
      AR_MSG(DBG_MED_PRIO,
             "apm_check_curr_cmd_op_is_pending(): Done processing msg seq for op_idx[0x%lX], "
             "cmd_opcode[0x%08lx], status[0x%lX]",
             cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->op_idx,
             cmd_ctrl_ptr->cmd_opcode,
             cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->status);

      /** Clear the curr op pending status   */
      op_status = FALSE;

      /** Clear the pending container list, if non-empty.
       *  This will also release the cached container resposne
       *  messages */
      if (cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr)
      {
         /** Clear the list of pending containers */
         apm_clear_container_list(cmd_ctrl_ptr,
                                  &cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                                  &cmd_ctrl_ptr->rsp_ctrl.num_pending_container,
                                  APM_CONT_CACHED_CFG_RELEASE,
                                  APM_CONT_CMD_PARAMS_RELEASE,
                                  APM_PENDING_CONT_LIST);
      }
   }

   return op_status;
}

ar_result_t apm_cmd_cmn_sequencer(apm_t *apm_info_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;
   apm_op_seq_t *  curr_op_seq_ptr;

   /** Get the pointer to current command control object */
   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   curr_op_seq_ptr = cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr;

   /** Init cmd op seq idx, if not already initialized  */
   apm_init_next_cmd_op_seq_idx(curr_op_seq_ptr, APM_SEQ_SET_UP_CONT_MSG_SEQ);

   switch (curr_op_seq_ptr->curr_seq_idx)
   {
      case APM_SEQ_SET_UP_CONT_MSG_SEQ:
      {
         /** Populate container msg opcode sequence as per command
          *  opcode and also sub-graph state if APM command is for
          *  graph management.
          *  If the opcode list is already configured, skip this step */

         if (AR_EOK != (result = apm_populate_cont_cmd_seq(apm_info_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_cmd_cmn_sequencer(): Failed to populate container cmd sequence, "
                   "cmd_opcode[0x%08lx]",
                   cmd_ctrl_ptr->cmd_opcode);

            return result;
         }

         /** Set the next sequence */
         curr_op_seq_ptr->curr_seq_idx = APM_SEQ_SEND_MSG_TO_CONTAINERS;

         break;
      }
      case APM_SEQ_SEND_MSG_TO_CONTAINERS:
      {
         /** Keep sending message to containers until the list is
          *  exhausted */
         result = apm_send_msg_to_containers(&apm_info_ptr->handle, cmd_ctrl_ptr);

         /** Set next sequence */
         curr_op_seq_ptr->curr_seq_idx = APM_SEQ_CONT_SEND_MSG_COMPLETED;

         break;
      }
      case APM_SEQ_CONT_SEND_MSG_COMPLETED:
      {
         /** Keep sending message to containers until all the opcodes
          *  have been processed. The function above clears the
          *  current operation pending flag which stops the looping
          *  logic. */

         if (apm_check_curr_cmd_op_is_pending(cmd_ctrl_ptr))
         {
            /** Set the default next sequence */
            curr_op_seq_ptr->curr_seq_idx = APM_SEQ_SEND_MSG_TO_CONTAINERS;
         }
         else /** Current operation completed */
         {
            /** Cache the current operation status   */
            result = cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->status;

            /** Clear the current op pending status   */
            apm_clear_curr_cmd_op_pending_status(cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr);

            AR_MSG(DBG_HIGH_PRIO,
                   "apm_cmd_cmn_sequencer(): Completed op_idx[0x%lX], op_status[%lu], cmd_opcode[0x%08lx]",
                   curr_op_seq_ptr->op_idx,
                   result,
                   cmd_ctrl_ptr->cmd_opcode);
         }

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_cmn_sequencer(): Unsupported cmd seq_idx[%lu], op_idx[0x%lX], cmd_opcode[0x%08lx]",
                curr_op_seq_ptr->curr_seq_idx,
                curr_op_seq_ptr->op_idx,
                cmd_ctrl_ptr->cmd_opcode);

         break;
      }

   } /** End of switch (cmd_ctrl_ptr->cmd_seq.curr_seq_idx) */

   return result;
}

ar_result_t apm_cmd_graph_open_sequencer(apm_t *apm_info_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_cmd_ctrl_t * cmd_ctrl_ptr;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer current cmd ctrl object */
   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM ext utils vtbl ptr  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   AR_MSG(DBG_HIGH_PRIO,
          "apm_cmd_graph_open_sequencer(): Executing curr_op_idx[0x%lX], curr_seq_idx[%lu], cmd_opcode[0x%lX]",
          cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx,
          cmd_ctrl_ptr->cmd_seq.graph_open_seq.curr_seq_idx,
          cmd_ctrl_ptr->cmd_opcode);

   /** Switch to handler function corresponding to current
    *  operation sequencer  */
   switch (cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx)
   {
      case APM_OPEN_CMD_OP_SEND_CONT_OPEN_MSG:
      {
         result = apm_cmd_cmn_sequencer(apm_info_ptr);

         break;
      }
      case APM_OPEN_CMD_OP_HDL_DATA_PATHS:
      {
         if (ext_utils_ptr->data_path_vtbl_ptr && ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_cfg_cmn_seqncer_fptr)
         {
            result = ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_cfg_cmn_seqncer_fptr(apm_info_ptr,
                                                                                           TRUE /** Data path create*/);
         }

         break;
      }
      case APM_OPEN_CMD_OP_PROXY_MGR_OPEN:
      case APM_OPEN_CMD_PROXY_MGR_PREPROCESS:
      case APM_OPEN_CMD_OP_PROXY_MGR_CFG:
      {
         result = apm_proxy_graph_open_cmn_sequencer(apm_info_ptr);

         break;
      }
      case APM_OPEN_CMD_OP_HDL_LINK_OPEN_INFO:
      case APM_OPEN_CMD_OP_HDL_LINK_START:
      {
         if (ext_utils_ptr->runtime_link_hdlr_vtbl_ptr &&
             ext_utils_ptr->runtime_link_hdlr_vtbl_ptr->apm_graph_open_handle_link_start_fptr)
         {
            result = ext_utils_ptr->runtime_link_hdlr_vtbl_ptr->apm_graph_open_handle_link_start_fptr(apm_info_ptr);
         }

         break;
      }
      case APM_OPEN_CMD_OP_HDL_DB_QUERY_PREPROCESS:
      case APM_OPEN_CMD_OP_HDL_DB_QUERY_SEND_INFO:
      {
         if ((ext_utils_ptr->db_query_vtbl_ptr) &&
             (NULL != ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_handle_graph_open_fptr))
         {
            result = ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_handle_graph_open_fptr(apm_info_ptr);
         }
         break;
      }
      case APM_OPEN_CMD_OP_ERR_HDLR:
      {
         if (ext_utils_ptr->err_hdlr_vtbl_ptr &&
             ext_utils_ptr->err_hdlr_vtbl_ptr->apm_cmd_graph_open_err_hdlr_sequencer_fptr)
         {
            result = ext_utils_ptr->err_hdlr_vtbl_ptr->apm_cmd_graph_open_err_hdlr_sequencer_fptr(apm_info_ptr);

            /** Check if error handler is done, or further errors occurred
             *  during error handling ,then proceed to end the
             *  open command */
            if ((APM_CMN_CMD_OP_COMPLETED == cmd_ctrl_ptr->cmd_seq.err_hdlr_seq.op_idx) || (AR_EOK != result))
            {
               /** Set the op index to end the command   */
               cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx = APM_OPEN_CMD_OP_COMPLETED;

               /** Set result = EOK so that further error handing can be
                *  aborted */
               result = AR_EOK;
            }

            /** If the err handler sequencer is on going, op status
             *  will be pending. If the err handler is done, then need to next
             *  op in the error handler. Pending flag need to be set to
             *  true so that sequencer can return back to the set op
             *  index */

            cmd_ctrl_ptr->cmd_seq.graph_open_seq.curr_cmd_op_pending = TRUE;
         }

         break;
      }
      case APM_OPEN_CMD_OP_COMPLETED:
      {
         apm_end_cmd_op_sequencer(cmd_ctrl_ptr, &cmd_ctrl_ptr->cmd_seq.graph_open_seq);

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_graph_open_sequencer(): Un-supported cmd operation index[0x%lX]",
                cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx);

         /** Abort command sequencer   */
         apm_abort_cmd_op_sequencer(cmd_ctrl_ptr);

         break;
      }

   } /** End of switch() */

   return result;
}

static void apm_gm_cmd_cont_msg_seq_determine_next_step(apm_t *apm_info_ptr, apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   apm_op_seq_t *         curr_op_seq_ptr;
   apm_cached_cont_list_t cont_list_type;
   uint32_t               num_active_cont_list = 0;

   cont_list_type = apm_get_curr_active_cont_list_type(cmd_ctrl_ptr);

   /** Get the pointer to current op seq object in process   */
   curr_op_seq_ptr = &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq;

   if (cont_list_type < APM_CONT_LIST_MAX)
   {
      for (uint32_t list_idx = 0; list_idx < APM_CONT_LIST_MAX; list_idx++)
      {
         if (cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[list_idx].list_ptr)
         {
            num_active_cont_list++;
         }
      }

      /** If only one list is active and traversal is not done,
       *  for current message opcode, then return */
      if (APM_CONT_LIST_TRAV_STARTED ==
          cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[cont_list_type])
      {
         curr_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPARE_FOR_NEXT_CONT_MSG;

         return;
      }
      else if (num_active_cont_list > 1) /** Current list traversal done */
      {
         /**  Check if there are more active list present */

         for (uint32_t idx = (cont_list_type + 1); idx < APM_CONT_LIST_MAX; idx++)
         {
            if (cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[idx].list_ptr &&
                (APM_CONT_LIST_TRAV_STOPPED ==
                 cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[idx]))
            {
               cont_list_type = idx;

               curr_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPARE_FOR_NEXT_CONT_MSG;

               return;
            }
         }
      }
   } /** End of if (list_type_idx < APM_CONT_LIST_MAX) */

   /** Increment the container message opcode index. Update
    *  sub-graph state based upon current message opcode */
   apm_cont_msg_one_iter_completed(cmd_ctrl_ptr);

   /** if reached the end of opcode list, then complete the
    * current operation */
   if (!apm_check_cont_msg_seq_done(cmd_ctrl_ptr))
   {
      if (cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state)
      {
         curr_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPROC_GRAPH_MGMT_MSG;

         /** Clear the mixed state flag */
         cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = FALSE;
      }
      else /** Not mixed state */
      {
         curr_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPARE_FOR_NEXT_CONT_MSG;
      }
   }
   else /** Message sequence is done.  */
   {
      AR_MSG(DBG_MED_PRIO,
             "apm_gm_cmd_cont_msg_seq_determine_next_step(): Done processing msg seq for op_idx[%lu], "
             "cmd_opcode[0x%08lx]",
             curr_op_seq_ptr->op_idx,
             cmd_ctrl_ptr->cmd_opcode);

      if (APM_GM_CMD_OP_PROCESS_REG_GRAPH == curr_op_seq_ptr->op_idx)
      {
         /** Clear current operation pending flag  */
         curr_op_seq_ptr->curr_cmd_op_pending = FALSE;
      }

      curr_op_seq_ptr->curr_seq_idx = APM_SEQ_REG_SG_PROC_COMPLETED;

      /** Clear the command info */
      apm_clear_graph_mgmt_cmd_info(apm_info_ptr, cmd_ctrl_ptr);
   }

   return;
}

ar_result_t apm_cmd_graph_mgmt_cmn_sequencer(apm_t *apm_info_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;
   uint32_t        cmd_opcode;
   apm_op_seq_t *  gm_op_seq_ptr;

   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the current command opcode under process */
   cmd_opcode = cmd_ctrl_ptr->cmd_opcode;

   /** Get the gm op seq pointer   */
   gm_op_seq_ptr = &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq;

   /** If the list of regular sub-graphs is empty, then nothing
    *  needs to be done. This is if only proxy managed
    *  sub-graphs are sent the gm command */
   if (!cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr &&
       !cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr)
   {
      AR_MSG(DBG_MED_PRIO,
             "apm_cmd_graph_mgmt_cmn_sequencer(): No SG/ports/links to process, cmd_opcode[0x%08lx]",
             cmd_opcode);

      /** End current operation   */
      apm_clear_curr_cmd_op_pending_status(gm_op_seq_ptr);

      return AR_EOK;
   }

   /** Init the cmd op sequence for the first entry to the
    *  sequencer routine */
   apm_init_next_cmd_op_seq_idx(gm_op_seq_ptr, APM_SEQ_VALIDATE_SG_LIST);

   switch (gm_op_seq_ptr->curr_seq_idx)
   {
      case APM_SEQ_VALIDATE_SG_LIST:
      {
         /** Validate sub-graph ID list being processed. This call
          *  populates the list of sub-graphs to be processed and also
          *  sets the default state for the overall sub-graph list */
         if (AR_EOK != (result = apm_gm_cmd_cfg_sg_list_to_process(apm_info_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO, "GRAPH_MGMT: SG_ID list validation failed, cmd_opcode[0x%08lx]", cmd_opcode);
         }

         /** Set the next sequence */
         gm_op_seq_ptr->curr_seq_idx = APM_SEQ_SET_UP_CONT_MSG_SEQ;

         break;
      }
      case APM_SEQ_SET_UP_CONT_MSG_SEQ:
      {
         if (AR_EOK != (result = apm_populate_cont_cmd_seq(apm_info_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_send_msg_to_containers(): Failed to populate container cmd sequence, cmd_opcode[0x%08lx]",
                   cmd_opcode);
         }

         /** Set the next sequence */
         gm_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPROC_GRAPH_MGMT_MSG;

         break;
      }
      case APM_SEQ_PREPROC_GRAPH_MGMT_MSG:
      {
         /** Identify the first disjoint container graph to start command
          *  processing container list traversal structures */

         /** Filter out the sub-graph and port level info for each of
          *  the operable containers based upon the current state */

         /** Check if the sub-graph list being processed is of mixed
          *  state */
         if (AR_EOK != (result = apm_gm_cmd_pre_processing(cmd_ctrl_ptr, &apm_info_ptr->graph_info)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_set_up_cont_msg_proc(): Graph mgmt cmd pre-process step failed, cmd_opcode[0x%08lx]",
                   cmd_opcode);
         }

         /** Set the next sequence */
         gm_op_seq_ptr->curr_seq_idx = APM_SEQ_PREPARE_FOR_NEXT_CONT_MSG;

         break;
      }
      case APM_SEQ_PREPARE_FOR_NEXT_CONT_MSG:
      {
         /** Populate the list of containers to process based upon the
          *  cached container list and current GK msg opcode to be
          *  processed  */
         if (AR_EOK != (result = apm_gm_cmd_prepare_for_next_cont_msg(&apm_info_ptr->graph_info, cmd_ctrl_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_set_up_cont_msg_proc(): Failed to start processing container list, cmd_opcode[0x%08lx]",
                   cmd_opcode);
         }

         /** Set the next sequence */
         gm_op_seq_ptr->curr_seq_idx = APM_SEQ_SEND_MSG_TO_CONTAINERS;

         break;
      }
      case APM_SEQ_SEND_MSG_TO_CONTAINERS:
      {
         /** Send the message to list of pending containers */
         result = apm_send_msg_to_containers(&apm_info_ptr->handle, cmd_ctrl_ptr);

         /** Set the next sequence */
         gm_op_seq_ptr->curr_seq_idx = APM_SEQ_CONT_SEND_MSG_COMPLETED;

         break;
      }
      case APM_SEQ_CONT_SEND_MSG_COMPLETED:
      {
         apm_gm_cmd_cont_msg_seq_determine_next_step(apm_info_ptr, cmd_ctrl_ptr);

         break;
      }
      case APM_SEQ_REG_SG_PROC_COMPLETED:
      {
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_graph_mgmt_sequencer(): Un-supported cmd seq_idx[%lu], op_index[%lu]",
                gm_op_seq_ptr->curr_seq_idx,
                gm_op_seq_ptr->op_idx);

         result = AR_EFAILED;

         break;
      }

   } /** End of switch() */

   return result;
}

ar_result_t apm_cmd_graph_mgmt_sequencer(apm_t *apm_info_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_cmd_ctrl_t * cmd_ctrl_ptr;
   apm_ext_utils_t *ext_utils_ptr;
   apm_op_seq_t *   gm_op_seq_ptr;

   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM ext utils vtbl ptr  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the graph management sequencer pointer */
   gm_op_seq_ptr = &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq;

   /** Assign the parent seq ptr for the first time, so we can assign
       the parent seq ptr back to current when graph mgnt seq finishes */
   if (&cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq != cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr)
   {
      cmd_ctrl_ptr->cmd_seq.parent_op_seq_ptr = cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr;
   }

   /** Set the curr op seq obj under process   */
   cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr = &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq;

   AR_MSG(DBG_HIGH_PRIO,
          "apm_cmd_graph_mgmt_sequencer(): Executing curr_op_idx[0x%lX], curr_seq_idx[%lu], cmd_opcode[0x%lX]",
          cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.op_idx,
          cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq.curr_seq_idx,
          cmd_ctrl_ptr->cmd_opcode);

   switch (gm_op_seq_ptr->op_idx)
   {
      case APM_GM_CMD_OP_PRE_PROCESS:
      case APM_GM_CMD_OP_DB_QUERY_SEND_INFO:
      {
         // In case we are cleaning up a failed graph open we don't need to close the handles, they are already sent
         if ((ext_utils_ptr->db_query_vtbl_ptr) &&
             (NULL != ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_handle_graph_close_fptr) &&
             (APM_OPEN_CMD_OP_ERR_HDLR != cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx))
         {
            result = ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_handle_graph_close_fptr(apm_info_ptr);
         }
         break;
      }
      case APM_GM_CMD_OP_PROCESS_REG_GRAPH:
      {
         result = apm_cmd_graph_mgmt_cmn_sequencer(apm_info_ptr);

         break;
      }
      case APM_GM_CMD_OP_PROCESS_PROXY_GRAPH:
      {
         /** If there are any sub-graphs present managed via proxy
          *  manager, send the graph management command to them. Proxy
          *  manager are only expected to  provide permission for
          *  given command opcode if they are in the right state to be
          *  processed. Proxy manager returns back the response with
          *  the list of sub-graphs that can be operated upon. This
          *  sub-graph list can be empty as well. */

         result = apm_proxy_graph_mgmt_sequencer(apm_info_ptr);

         break;
      }
      case APM_GM_CMD_OP_CMN_HDLR:
      {
         /** Update container graph list if any of the graph shape
          *  changed */
         if ((APM_CMD_GRAPH_OPEN == cmd_ctrl_ptr->cmd_opcode) || (APM_CMD_GRAPH_CLOSE == cmd_ctrl_ptr->cmd_opcode))
         {
            /** Container graphs need to be resorted for the deleted
             *  containers, while processing GRAPH CLOSE command */
            result = apm_update_cont_graph_list(&apm_info_ptr->graph_info);

            /** For close command, any failure should be ignored for
             *  cleaning up data path info as next step */
            if ((AR_EOK != result) && (APM_CMD_GRAPH_CLOSE == cmd_ctrl_ptr->cmd_opcode))
            {
               /** Cache the current cmd status */
               cmd_ctrl_ptr->cmd_status = result;

               /** Reset the result  */
               result = AR_EOK;
            }
         }

         break;
      }
      case APM_GM_CMD_OP_HDL_DATA_PATHS:
      {
         if (ext_utils_ptr->data_path_vtbl_ptr && ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_cfg_cmn_seqncer_fptr)
         {
            result =
               ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_cfg_cmn_seqncer_fptr(apm_info_ptr,
                                                                                     FALSE /** Data path destroy*/);
         }

         break;
      }
      case APM_GM_CMD_OP_HANDLE_FAILURE:
      case APM_GM_CMD_OP_COMPLETED:
      {
         apm_end_cmd_op_sequencer(cmd_ctrl_ptr, &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq);

         /** reset the current op sequence ptr to parent seq ptr */
         cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr = cmd_ctrl_ptr->cmd_seq.parent_op_seq_ptr;

         cmd_ctrl_ptr->cmd_seq.parent_op_seq_ptr = NULL;

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_graph_mgmt_sequencer(): Un-supported cmd operation index[%lu]",
                gm_op_seq_ptr->op_idx);

         /** Abort command sequencer   */
         apm_abort_cmd_op_sequencer(cmd_ctrl_ptr);

         break;
      }

   } /** End of switch() */

   return result;
}

ar_result_t apm_cmd_set_get_cfg_sequencer(apm_t *apm_info_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;

   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr = &cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq;
   AR_MSG(DBG_HIGH_PRIO,
          "apm_cmd_set_get_cfg_sequencer(): Executing curr_op_idx[0x%lX], curr_seq_idx[%lu], cmd_opcode[0x%lX]",
          cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->op_idx,
          cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->curr_seq_idx,
          cmd_ctrl_ptr->cmd_opcode);

   switch (cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx)
   {
      case APM_SET_GET_CFG_CMD_OP_SEND_CONT_MSG:
      {
         result = apm_cmd_cmn_sequencer(apm_info_ptr);

         break;
      }
      case APM_SET_GET_CFG_CMD_OP_SEND_PROXY_MGR_MSG:
      {
         result = apm_proxy_set_cfg_sequencer(apm_info_ptr);

         break;
      }
      case APM_SET_GET_CFG_CMD_OP_HDL_DBG_INFO:
      {
         apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
         if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_debug_info_cfg_hdlr_fptr)
         {
            result = ext_utils_ptr->offload_vtbl_ptr->apm_debug_info_cfg_hdlr_fptr(apm_info_ptr);
         }
         break;
      }
      case APM_SET_GET_CFG_CMD_OP_CLOSE_ALL:
      {
         if (SPF_MSG_CMD_SET_CFG == cmd_ctrl_ptr->cmd_opcode && cmd_ctrl_ptr->set_cfg_cmd_ctrl.is_close_all_needed)
         {
            apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
            if (ext_utils_ptr->close_all_vtbl_ptr && ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_close_all_seqncer_fptr)
            {
               result = ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_close_all_seqncer_fptr(apm_info_ptr);
               /** Check if Close all needs to handled during set param and handle it until it is done */
               if ((APM_CMN_CMD_OP_COMPLETED == cmd_ctrl_ptr->cmd_seq.close_all_seq.op_idx) || (AR_EOK != result))
               {
                  /** Don't update set_get_cfg_cmd_seq.op_idx to APM_SET_GET_CFG_CMD_OP_COMPLETED
                   * Since the curr_op_seq_ptr will updated to  set_get_cfg_cmd_seq when close all
                   * completes, if we update, op_idx will incremented to unsupported value */

                  /** Set the status to false since we want to navigated to next op_idx*/
                  cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.curr_cmd_op_pending = FALSE;
               }
               else
               {
                  cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.curr_cmd_op_pending = TRUE;
               }
            }
         }
         break;
      }
      case APM_SET_GET_CFG_CMD_OP_HANDLE_FAILURE:
      case APM_SET_GET_CFG_CMD_OP_COMPLETED:
      {
         apm_end_cmd_op_sequencer(cmd_ctrl_ptr, &cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq);

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_set_get_cfg_sequencer(): Un-supported cmd operation index[%lu]",
                cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx);

         /** Abort command sequencer   */
         apm_abort_cmd_op_sequencer(cmd_ctrl_ptr);

         break;
      }

   } /** End of switch() */

   return result;
}

ar_result_t apm_cmd_seq_set_up_err_hdlr(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   /** Set the op index for handling failure   */
   cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx = APM_CMN_CMD_OP_HANDLE_FAILURE;

   /** Clear the op pending status   */
   apm_clear_curr_cmd_op_pending_status(cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr);

   return result;
}

ar_result_t apm_cmd_sequencer_cmn_entry(apm_t *apm_info_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;

   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "apm_cmd_sequencer_cmn_entry(): curr_op_idx[0x%lX], curr_seq_idx[%lu], cmd_opcode[0x%lX]",
          cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx,
          cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->curr_seq_idx,
          cmd_ctrl_ptr->cmd_opcode);

   /** This while loop continues to iterate until at least 1
    *  command/msg has been sent to the containers/modules/proxy
    *  managers or if any error has occurred during the command
    *  processing, in which the command is ended with clean up */
   while (!cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending)
   {
      /** Call the sequencer function corresponding to current
       *  command under process.  */
      if (AR_EOK != (result = cmd_ctrl_ptr->cmd_seq.cmd_seq_entry_fptr(apm_info_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cmd_sequencer_cmn_entry(): FAILED: curr_op_idx[0x%lX], curr_seq_idx[%lu], cmd_opcode[0x%lX]",
                cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx,
                cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->curr_seq_idx,
                cmd_ctrl_ptr->cmd_opcode);

         /** Aggregate the command failure status */
         cmd_ctrl_ptr->cmd_status |= result;

         /** Set the cmd op code for handling failure. Need to set it
          *  only if not set previously set and command is not
          *  completed */
         if ((cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx != APM_CMN_CMD_OP_COMPLETED) &&
             (cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx != APM_CMN_CMD_OP_HANDLE_FAILURE))
         {
            /** Set the op index for handling failure   */
            cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx = APM_CMN_CMD_OP_HANDLE_FAILURE;

            /** Clear the op pending status   */
            apm_clear_curr_cmd_op_pending_status(cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr);

            /** Continue to handle failure  */
            continue;
         }
      }

      /** If the sequencer operation is completed, end the command  */
      if (APM_CMN_CMD_OP_COMPLETED == cmd_ctrl_ptr->cmd_seq.pri_op_seq_ptr->op_idx)
      {
         /** Clear the command pending flag */
         cmd_ctrl_ptr->cmd_pending = FALSE;

         /** End the command */
         result = apm_end_cmd(apm_info_ptr);

         /** Break the while loop */
         break;
      }
      else /** Command op list not completed */
      {
         /** Increment operation index for next operation, if all
          *  operations are not completed */
         apm_incr_cmd_op_idx(cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr);
      }

   } /** End of while (!cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending) */

   return result;
}

ar_result_t apm_set_cmd_seq_func(apm_t *apm_info_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_cmd_ctrl_t * cmd_ctrl_ptr;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to current command control object */
   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to APM extended func vtable */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   switch (cmd_ctrl_ptr->cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         /** Pre-requisite for calling this sequencer is the first
          *  level command parsing is done and configuration to be
          *  sent to the containers is already cached in the impacted
          *  container objects */
         apm_init_cmd_seq_info(cmd_ctrl_ptr, apm_cmd_graph_open_sequencer, &cmd_ctrl_ptr->cmd_seq.graph_open_seq);

         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_SUSPEND:
      case APM_CMD_GRAPH_CLOSE:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      {
         /** Pre-requisite for calling this sequencer is the sub-graph
          *  ID list to be processed is configured in the commond
          *  control object book keeping */
         apm_init_cmd_seq_info(cmd_ctrl_ptr, apm_cmd_graph_mgmt_sequencer, &cmd_ctrl_ptr->cmd_seq.graph_mgmt_seq);

         break;
      }
      case APM_CMD_SET_CFG:
      case APM_CMD_GET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      case SPF_EVT_TO_APM_FOR_PATH_DELAY:
      case SPF_MSG_CMD_SET_CFG:
      case SPF_MSG_CMD_GET_CFG:
      {
         /** Pre-requisite for calling this sequencer is the first
          *  level command parsing is done and configuration to be
          *  sent to the containers is already cached in the impacted
          *  container objects */
         apm_init_cmd_seq_info(cmd_ctrl_ptr, apm_cmd_set_get_cfg_sequencer, &cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq);

         break;
      }
      case APM_CMD_CLOSE_ALL:
      {
         if (ext_utils_ptr->close_all_vtbl_ptr && ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_close_all_seqncer_fptr)
         {
            apm_init_cmd_seq_info(cmd_ctrl_ptr,
                                  ext_utils_ptr->close_all_vtbl_ptr->apm_cmd_close_all_seqncer_fptr,
                                  &cmd_ctrl_ptr->cmd_seq.close_all_seq);
         }

         break;
      }
      case APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS:
      case APM_CMD_RSP_SHARED_MEM_MAP_REGIONS:
      case APM_CMD_SHARED_SATELLITE_MEM_UNMAP_REGIONS:
      {
         /** Offload memory map handling is not the part of default
          *  sequencer and is handled separately. */
         break;
      }
      case APM_CMD_SHARED_MEM_MAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2:
      case APM_CMD_SHARED_MEM_REGION_ACCESS_INFO:
      {
         /** Shared memory handling V2 is not part of the default sequencer. */
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_set_cmd_seq_func(): Un-supported cmd_opcode[0x%lX]", cmd_ctrl_ptr->cmd_opcode);

         result = AR_EUNSUPPORTED;

         break;
      }

   } /** End of switch (cmd_ctrl_ptr->cmd_opcode) */

   return result;
}
