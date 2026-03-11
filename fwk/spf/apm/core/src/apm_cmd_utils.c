/**
 * \file apm_cmd_utils.c
 *
 * \brief
 *     This file contains utility functions for APM command handling
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
#include "apm_graph_utils.h"
#include "posal_intrinsics.h"
#include "spf_list_utils.h"
#include "apm_data_path_utils.h"
#include "apm_cmd_utils.h"
#include "apm_memmap_api.h"
#include "spf_macros.h"
#include "spf_svc_utils.h"
#include "posal_err_fatal.h"
#include "apm_proxy_vcpm_utils.h"
#include "apm_gpr_cmd_handler.h"

/****************************************************************************
 * Function Definitions
 ****************************************************************************/
uint32_t apm_get_cmd_opcode_from_msg_payload(spf_msg_t *msg_ptr)
{
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      cmd_opcode;

   if (SPF_MSG_CMD_GPR == msg_ptr->msg_opcode)
   {
      /** Get GPR packet pointer */
      gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

      /** Get the GPR command opcode */
      cmd_opcode = gpr_pkt_ptr->opcode;
   }
   else /** GK MSG */
   {
      cmd_opcode = msg_ptr->msg_opcode;
   }

   return cmd_opcode;
}

static ar_result_t apm_allocate_cmd_ctrl_obj(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;
   uint32_t        cmd_slot_idx;
   uint32_t        cmd_opcode;

   /** Get GPR packet pointer */
   cmd_opcode = apm_get_cmd_opcode_from_msg_payload(msg_ptr);

   /** Check if all the slots in the command obj list are
    *  occupied.
    *  This condition should not hit as the APM cmd Q is removed
    *  from the wait mask once all the cmd obj slots are
    *  occupied. */
   if (APM_CMD_LIST_FULL_MASK == apm_info_ptr->active_cmd_mask)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_set_cmd_ctrl(), cmd obj list is full, cmd_opcode[0x%lX]", cmd_opcode);

      return AR_EFAILED;
   }

   /** Find the next available slot in the command list */
   cmd_slot_idx = s32_ct1_s32(apm_info_ptr->active_cmd_mask);

   /** Set this bit in the active command mask */
   APM_SET_BIT(&apm_info_ptr->active_cmd_mask, cmd_slot_idx);

   /** If the command list is full remove the CmdQ from the wait mask.
    *  Start listening to cmdQ again as soon as at least one of the slot becomes free */
   if (APM_CMD_LIST_FULL_MASK == apm_info_ptr->active_cmd_mask)
   {
      apm_info_ptr->curr_wait_mask &= ~(APM_CMD_Q_MASK);
   }

   /** Get the pointer to command control object corresponding to
    *  available slot */
   cmd_ctrl_ptr = &apm_info_ptr->cmd_ctrl_list[cmd_slot_idx];

   /** Save the list index in cmd obj */
   cmd_ctrl_ptr->list_idx = cmd_slot_idx;

   /** Save the current cmd ctrl under process */
   apm_info_ptr->curr_cmd_ctrl_ptr = cmd_ctrl_ptr;

   /** Cache the GK message in command control */
   memscpy(&cmd_ctrl_ptr->cmd_msg, sizeof(spf_msg_t), msg_ptr, sizeof(spf_msg_t));

   /** Save GPR command opcode */
   cmd_ctrl_ptr->cmd_opcode = cmd_opcode;

   /** Set the command pending flag */
   cmd_ctrl_ptr->cmd_pending = TRUE;

   /** Init the command status */
   cmd_ctrl_ptr->cmd_status = AR_EOK;

   /** Configure the command sequencer corresponding to current
    *  command process  */
   apm_set_cmd_seq_func(apm_info_ptr);

   AR_MSG(DBG_MED_PRIO,
          "apm_set_cmd_ctrl(), assigned cmd_list_idx[%lu], cmd_opcode[0x%lX]",
          cmd_ctrl_ptr->list_idx,
          cmd_opcode);

   return result;
}

ar_result_t apm_clear_graph_mgmt_cmd_info(apm_t *apm_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   /** Clear the cached sub-graph obj list processsed as part of
    *  graph management command */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr)
   {
      spf_list_delete_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr, TRUE);
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs = 0;
   }

   /** Clear the cached sub-graph obj list processed as part of
    *  the link close command */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr)
   {
      spf_list_delete_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr, TRUE);
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cont_port_hdl_sg = 0;
   }

   /**Pending proxy book keeping is cleaned up only after the command processing is finished, either successful or
    * aborted due to some error. This condition will hit for the abort scenario. */
   if ((apm_cmd_ctrl_ptr->rsp_ctrl.active_proxy_mgr_list_ptr ||
        apm_cmd_ctrl_ptr->rsp_ctrl.inactive_proxy_mgr_list_ptr) &&
       (!apm_cmd_ctrl_ptr->cmd_pending))
   {
      AR_MSG(DBG_MED_PRIO, "apm_clear_graph_mgmt_cmd_info: clearing active/inactive VCPM proxy mgr");

      apm_proxy_util_clear_vcpm_active_or_inactive_proxy(apm_info_ptr, apm_cmd_ctrl_ptr);
   }

   /** Clear the container and sub-graph processing info */
   apm_gm_cmd_reset_sg_cont_list_proc_info(apm_cmd_ctrl_ptr);

   return result;
}

static ar_result_t apm_update_inter_sg_conn_list(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_sg_edge_node_ptr;
   apm_cont_port_connect_info_t *port_conn_info_ptr;

   /** Get the pointer to the list of inter sub-graph edges */
   curr_sg_edge_node_ptr = apm_info_ptr->graph_info.sub_graph_conn_list_ptr;

   /** Iterate over the list of these edge */
   while (curr_sg_edge_node_ptr)
   {
      /** Get the pointer to edge object */
      port_conn_info_ptr = (apm_cont_port_connect_info_t *)curr_sg_edge_node_ptr->obj_ptr;

      /** If the peer sub-graph matches with sub-graph being
       *  closed, mark it as NULL in the edge */
      if (port_conn_info_ptr->peer_sg_obj_ptr == sub_graph_node_ptr)
      {
         port_conn_info_ptr->peer_sg_obj_ptr = NULL;
      }

      /** Advance to next node in the list */
      curr_sg_edge_node_ptr = curr_sg_edge_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_clear_pspc_cont_mod_list(apm_t *              apm_info_ptr,
                                         apm_container_t *    container_node_ptr,
                                         apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                         spf_list_node_t *    sub_graph_list_ptr)
{
   ar_result_t             result                 = AR_EOK;
   apm_pspc_module_list_t *pspc_mod_list_node_ptr = NULL;
   spf_list_node_t *       curr_mod_node_ptr;
   apm_module_t *          module_node_ptr;
   apm_sub_graph_t *       sub_graph_node_ptr;
   spf_list_node_t *       curr_sg_node_ptr;
   apm_ext_utils_t *       ext_utils_ptr;
   apm_cmd_ctrl_t *        apm_cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   /** Get the pointer to APM ext utils vtbl ptr  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;
   /** Get the pointer to start of the sub-graph list */
   curr_sg_node_ptr = sub_graph_list_ptr;

   /** Iterate over the sub-graph list */
   while (curr_sg_node_ptr)
   {
      /** Get the sub-graph list node obj pointer */
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_sg_node_ptr->obj_ptr;

      /** Get the list of module for this container and sub-graph
       *  pair */
      if (AR_EOK != (result = apm_db_get_module_list_node(container_node_ptr->pspc_module_list_node_ptr,
                                                          sub_graph_node_ptr->sub_graph_id,
                                                          container_node_ptr->container_id,
                                                          &pspc_mod_list_node_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Failed to get module list for SG_ID[0x%lX], CONT_ID[0x%lX]",
                sub_graph_node_ptr->sub_graph_id,
                container_node_ptr->container_id);

         return result;
      }

      /** Check if the module list exist for this SG_ID and CONT ID
       *  pair, if not, then continue */
      if (!pspc_mod_list_node_ptr)
      {
         /** Advance to next sub-graph node */
         curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

         continue;
      }

      /** Get the pointer to per-sg-per-cont module list */
      curr_mod_node_ptr = pspc_mod_list_node_ptr->module_list_ptr;

      /** Iterate over the list of modules present in this
       *  sub-graph-container pair */
      while (curr_mod_node_ptr)
      {
         /** Get the handle to module node */
         module_node_ptr = (apm_module_t *)curr_mod_node_ptr->obj_ptr;

         /** Clear up the input and output port connections for this
          *  module as it is getting closed. Also update the upstream
          *  and downstream peer output and input connection list
          *  accordingly */
         if (ext_utils_ptr->data_path_vtbl_ptr &&
             ext_utils_ptr->data_path_vtbl_ptr->apm_clear_module_data_port_conn_fptr)
         {
            ext_utils_ptr->data_path_vtbl_ptr->apm_clear_module_data_port_conn_fptr(apm_info_ptr, module_node_ptr);
         }

         /** Advance to next node in the module's list  */
         curr_mod_node_ptr = curr_mod_node_ptr->next_ptr;

      } /** While (sub-graph's module list )*/

      /** Remove this module's list from host container */
      if (container_node_ptr->pspc_module_list_node_ptr)
      {
         apm_db_remove_node_from_list(&container_node_ptr->pspc_module_list_node_ptr,
                                      pspc_mod_list_node_ptr,
                                      &container_node_ptr->num_pspc_module_lists);
      }

      /** Get the pointer to per-sg-per-cont module list */
      curr_mod_node_ptr = pspc_mod_list_node_ptr->module_list_ptr;

      /** Iterate over the list of modules present in this
       *  sub-graph-container pair */
      while (curr_mod_node_ptr)
      {
         /** Get the handle to module node */
         module_node_ptr = (apm_module_t *)curr_mod_node_ptr->obj_ptr;

         /** Remove this module from the APM global module list */
         if (AR_EOK != (result = apm_db_remove_obj_from_list(apm_info_ptr->graph_info.module_list_ptr,
                                                             module_node_ptr,
                                                             module_node_ptr->instance_id,
                                                             APM_OBJ_TYPE_MODULE,
                                                             &apm_info_ptr->graph_info.num_modules)))
         {
            return result;
         }

         /** Delete this node from the PSPC list and free up module
          *  object memory. Function call below also increments the
          *  list pointer */
         spf_list_delete_node_and_free_obj(&curr_mod_node_ptr, &pspc_mod_list_node_ptr->module_list_ptr, TRUE);
      }

      /** Free up the PSPC module list node */
      posal_memory_free(pspc_mod_list_node_ptr);

      /** Remove this sub-graph from overlapping container's
       *  sub-graph list */
      if (container_node_ptr->sub_graph_list_ptr)
      {
         apm_db_remove_node_from_list(&container_node_ptr->sub_graph_list_ptr,
                                      sub_graph_node_ptr,
                                      &container_node_ptr->num_sub_graphs);
      }

      /** Remove this container from the overlapping sub-graph's
       *  container list  */
      if (sub_graph_node_ptr->container_list_ptr)
      {
         apm_db_remove_node_from_list(&sub_graph_node_ptr->container_list_ptr,
                                      container_node_ptr,
                                      &sub_graph_node_ptr->num_containers);
      }

      /** Check if the container list for this sub-graph is emtpy.
       *  If so, free this sub-graph node */
      if (!sub_graph_node_ptr->num_containers)
      {
         /** Remove this sub-graph from all the overlapping container
          *  graphs */
         apm_remove_sg_from_cont_graph(&apm_info_ptr->graph_info, sub_graph_node_ptr);

         /** Update all the inter sub-graph edges to remove this
          *  sub-graph as peer */
         if (apm_info_ptr->graph_info.num_sub_graph_conn)
         {
            apm_update_inter_sg_conn_list(apm_info_ptr, sub_graph_node_ptr);
         }

         /** Remove this sub-graph from the APM global sub-graph list */
         apm_db_remove_node_from_list(&apm_info_ptr->graph_info.sub_graph_list_ptr,
                                      sub_graph_node_ptr,
                                      &apm_info_ptr->graph_info.num_sub_graphs);
         /** Remove this closed sub-graph from any of the deferred
          *  proxy command */
         if (ext_utils_ptr->parallel_cmd_utils_vtbl_ptr &&
             ext_utils_ptr->parallel_cmd_utils_vtbl_ptr->apm_update_deferred_gm_cmd_fptr)
         {
            ext_utils_ptr->parallel_cmd_utils_vtbl_ptr->apm_update_deferred_gm_cmd_fptr(apm_info_ptr,
                                                                                        sub_graph_node_ptr);
         }

         /** Remove this sub-graph from the current command ctrl processed SG list */
         apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.processed_sg_list_ptr,
                                      sub_graph_node_ptr,
                                      &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.num_processed_sg);

         if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
             apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_clear_graph_mgmt_context_fptr)
         {
            apm_info_ptr->ext_utils.multi_client_vtbl_ptr
               ->apm_multi_client_clear_graph_mgmt_context_fptr(apm_info_ptr, sub_graph_node_ptr);
         }

         AR_MSG(DBG_HIGH_PRIO, SPF_LOG_PREFIX "GRAPH_CLOSE: Destroyed SG_ID[0x%lX]", sub_graph_node_ptr->sub_graph_id);

         /** Free up the memory of this sub-graph node object */
         posal_memory_free(sub_graph_node_ptr);
      }

      /** Advance to next sub-graph node */
      curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

   } /** End of while (curr_sg_node_ptr) */

   return result;
}

static ar_result_t apm_clear_graph_open_cmd_info(apm_t *apm_info_ptr)
{
   apm_graph_open_cmd_ctrl_t *graph_open_cmd_ctrl_ptr;

   /** Get the pointer to graph open command control   */
   graph_open_cmd_ctrl_ptr = &apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl;

   /** For GRAPH OPEN command, clear any global sub-graph list
    *  if present */
   if (apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr)
   {
      spf_list_delete_list(&graph_open_cmd_ctrl_ptr->sg_id_list_ptr, TRUE /* pool_used */);
      graph_open_cmd_ctrl_ptr->num_sub_graphs = 0;
   }

   if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
       apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_clear_graph_open_cmd_info_fptr)
   {
      apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_clear_graph_open_cmd_info_fptr(apm_info_ptr);
   }

   for (uint32_t list_idx = 0; list_idx < LINK_TYPE_MAX; list_idx++)
   {
      if (graph_open_cmd_ctrl_ptr->data_ctrl_link_list[list_idx].num_nodes)
      {
         spf_list_delete_list(&graph_open_cmd_ctrl_ptr->data_ctrl_link_list[list_idx].list_ptr, TRUE /* pool_used */);

         graph_open_cmd_ctrl_ptr->data_ctrl_link_list[list_idx].num_nodes = 0;
      }
   }

   return AR_EOK;
}

ar_result_t apm_clear_graph_open_cfg(apm_t *apm_info_ptr)
{
   ar_result_t          result = AR_EOK;
   apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr;
   spf_list_node_t *    curr_node_ptr;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the pointer to current APM command control pointer */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to pending list of containers for this
    *  command */
   curr_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate through the list of pending list of containers
    *  for the current command control. */
   while (curr_node_ptr)
   {
      /** Get the pointer to the container object */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Get the command control object for current container */
      apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      /** Clear the PSPC module list for current cont ID and list of
       *  overlapping sub-graph ID's */
      apm_clear_pspc_cont_mod_list(apm_info_ptr,
                                   container_node_ptr,
                                   cont_cmd_ctrl_ptr,
                                   apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr);

      /** Advance to next container node */
      curr_node_ptr = curr_node_ptr->next_ptr;

   } /** While (pending container list) */

   return result;
}

ar_result_t apm_validate_module_instance_pair(apm_graph_info_t *graph_info_ptr,
                                              uint32_t          peer_1_module_iid,
                                              uint32_t          peer_2_module_iid,
                                              apm_module_t **   module_node_pptr,
                                              bool_t            dangling_link_allowed)
{
   ar_result_t result                   = AR_EOK;
   uint32_t    module_iid_list[2]       = { peer_1_module_iid, peer_2_module_iid };
   bool_t      atleast_one_peer_present = FALSE;

   if (!graph_info_ptr || !module_node_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_validate_module_instance_pair(): Invalid i/p args, graph_info_ptr[0x%lX], module_node_pptr[0x%lX]",
             graph_info_ptr,
             module_node_pptr);

      return AR_EFAILED;
   }

   /** Check if the SRC/PEER1 and DSTN/PEER2 module IDs exists in
    *  the graph data base. Client may provide dangling
    *  data/control link where either of the src/peer1 or
    *  dstn/peer2 module has not been opened. Such connection is
    *  possible where 2 separate use cases run concurrently with
    *  a data/control link established between them. Graph open
    *  command ignores such incomplete link. If both the
    *  src/peer1 and dstn/peer2 modules are not present, then it is
    *  flagged as an error and command processing is aborted. */

   for (uint32_t arr_idx = 0; arr_idx < 2; arr_idx++)
   {
      apm_db_get_module_node(graph_info_ptr, module_iid_list[arr_idx], &module_node_pptr[arr_idx], APM_DB_OBJ_QUERY);

      /** Check if the module node exists, don't error out yet if
       *  this module instance is not present */
      if (!module_node_pptr[arr_idx])
      {
         AR_MSG(DBG_HIGH_PRIO,
                "apm_validate_module_instance_pair() ::WARNING:: Module node: [M_IID]:[0x%lX] not opened yet",
                module_iid_list[arr_idx]);

         /** Check if the dangling link allowed for this validation */
         if (!dangling_link_allowed)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_validate_module_instance_pair(): Dangling link not allowed, module node: [M_IID]:[0x%lX] not "
                   "opened yet",
                   module_iid_list[arr_idx]);

            return AR_EBADPARAM;
         }
      }
      else /** Module is present */
      {
         /** Set the flag to indicate if atleast 1 peer in the
          *  data/control link is present */
         atleast_one_peer_present = TRUE;
      }

   } /** End of for loop */

   /** Execution falls through here if dangling link is allowed.
    *  Check if at least 1 peer is present, else return error
    * to the caller. */
   if (!atleast_one_peer_present)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_validate_module_instance_pair(): Both peer modules in the link are not present");

      result = AR_EFAILED;
   }

   return result;
}

static ar_result_t apm_end_gpr_cmd(apm_t *apm_info_ptr)
{
   ar_result_t            result = AR_EOK;
   uint32_t               cmd_opcode;
   gpr_packet_t *         gpr_rsp_payload_ptr;
   gpr_packet_t *         gpr_cmd_payload_ptr;
   apm_cmd_rsp_get_cfg_t *get_cfg_rsp_ptr;
   apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr;
   ar_result_t            cmd_result;

   /** Get the pointer to current APM command object under
    *  process */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the overall command result */
   cmd_result = apm_cmd_ctrl_ptr->cmd_status;

   /** Release any pending message packet if no command response pending */
   if (!(apm_cmd_ctrl_ptr->cmd_pending) && (SPF_MSG_CMD_GPR == apm_cmd_ctrl_ptr->cmd_msg.msg_opcode))
   {
      /** Get the GPR command opcode  */
      cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

      gpr_cmd_payload_ptr = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

      if (AR_EOK == cmd_result)
      {
         AR_MSG(DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "APM RSP HDLR: GPR CMD with Opcode[0x%lX], SUCCESS, token:[0x%x]",
                cmd_opcode,
                gpr_cmd_payload_ptr->token);
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "APM RSP HDLR: GPR CMD with Opcode[0x%lX], FAILED, result: 0x%lX, token:[0x%x]",
                cmd_opcode,
                cmd_result,
                gpr_cmd_payload_ptr->token);
      }

      /** apm_cmd_ctrl_ptr->cmd_rsp_payload only defined for in-band get_cfg case */
      if ((APM_CMD_GET_CFG == cmd_opcode) && apm_cmd_ctrl_ptr->cmd_rsp_payload.payload_ptr)
      {
         /** Get the pointer to GPR response packet */
         gpr_rsp_payload_ptr = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_rsp_payload.payload_ptr;

         /** cmd_result = EOK indicates that there are no errors in APM command
          *  parsing and handling; the container/modules still return their error
          *  codes back to APM and it is cached separately in agg_rsp_status. */
         if (AR_EOK == cmd_result)
         {
            /** Get the pointer to GPR response payload */
            get_cfg_rsp_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, gpr_rsp_payload_ptr);

            /** Populate the overall command status */
            get_cfg_rsp_ptr->status = apm_cmd_ctrl_ptr->agg_rsp_status;

            /** Send the response payload to client */
            result = __gpr_cmd_async_send(gpr_rsp_payload_ptr);

            /** async_send failure, this is considered critical */
            if (AR_EOK != result)
            {
               /** Free up response payload, normally done
                *  at end of successful async_send routine */
               __gpr_cmd_free(gpr_rsp_payload_ptr);
               goto __bailout;
            }
            else /** Response sent successfully */
            {
               /** Free up the actual command payload */
               __gpr_cmd_free(gpr_cmd_payload_ptr);
            }
         }
         else /** Complete command failure */
         {
            /** For in-band GET_CFG command, free
             *  up the allocated GPR packet */
            __gpr_cmd_free(gpr_rsp_payload_ptr);

            /** End the GPR command with cmd_status.
             *  cmd_status contains critical error code */
            result = __gpr_cmd_end_command(gpr_cmd_payload_ptr, cmd_result);
         }
      }
      else /** Other commands (Not in-band GET_CFG command) */
      {
         /** Get command header pointer */
         apm_cmd_header_t *cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_cmd_payload_ptr);

         /** If out-of-band command, flush the memory for error code
          *  present in the module data header  in shared memory and
          *  for out of band get config commands */
         if (cmd_header_ptr->mem_map_handle && apm_cmd_ctrl_ptr->cmd_payload_ptr)
         {

            if (AR_EOK !=
                (result = posal_cache_flush_v2(&apm_cmd_ctrl_ptr->cmd_payload_ptr, cmd_header_ptr->payload_size)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Failed to flush the shmem after cmd completion,  cmd_opcode[0x%lX], result[%lu]",
                      cmd_opcode,
                      result);
            }

            posal_memorymap_shm_decr_refcount(apm_info_ptr->memory_map_client, cmd_header_ptr->mem_map_handle);

            cmd_result |= result;
         }

         /** If non-critical OOB GET_CFG, send APM_CMD_RSP */
         if ((APM_CMD_GET_CFG == cmd_opcode) && (AR_EOK == cmd_result))
         {
            gpr_packet_t *cmd_rsp_pkt = NULL;

            /** Allocate response packet */
            result = apm_allocate_cmd_rsp_payload(APM_MODULE_INSTANCE_ID,
                                                  gpr_cmd_payload_ptr,
                                                  &cmd_rsp_pkt,
                                                  APM_CMD_RSP_GET_CFG,
                                                  sizeof(apm_cmd_rsp_get_cfg_t));

            /** Allocation failure, this is considered critical */
            if (AR_EOK != result)
            {
               AR_MSG(DBG_ERROR_PRIO, "APM RSP HDLR: CMD_RSP allocation FAILED, ending cmd[0x%lX]", cmd_opcode);

               goto __bailout;
            }

            /** Get address of response payload */
            apm_cmd_rsp_get_cfg_t *cmd_rsp_pkt_payload = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, cmd_rsp_pkt);

            /** Respond with (status = rsp_status), not cmd_status.
             *  For GET_CFG, rsp_status contains aggregated
             *  error code */
            cmd_rsp_pkt_payload->status = apm_cmd_ctrl_ptr->agg_rsp_status;

            result = __gpr_cmd_async_send(cmd_rsp_pkt);

            /** async_send failure, this is considered critical */
            if (AR_EOK != result)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "APM RSP HDLR: async_send FAILED, freeing packet & ending cmd[0x%lX]",
                      cmd_opcode);

               /** Free up response payload, normally done
                *  at end of successful async_send routine */
               __gpr_cmd_free(cmd_rsp_pkt);
               goto __bailout;
            }
            else /** Response sent successfully */
            {
               /** Free up the actual command payload */
               __gpr_cmd_free(gpr_cmd_payload_ptr);
            }
         }
         else
         {
            /** In this else case for non-GET_CFG commands
             *  and out-of-band GET_CFG critical failures,
             *  send IBASIC_RSP instead of APM_CMD_RSP,
             *  cmd_status contains proper error code */
            result = __gpr_cmd_end_command(gpr_cmd_payload_ptr, cmd_result);
         }
      } /** End of if-else */

      /** Deallocate the command handler resources   */
      apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_cmd_ctrl_ptr);
   }

   return result;

__bailout:
   /** Bailout routine for critical failures that occur
    *  in this function; other APM-level critical failures
    *  are caught prior to this. cmd_result is replaced with
    *  result as result contains the error code from wherever
    *  bailout occurred. */
   cmd_result = result;
   result     = __gpr_cmd_end_command(gpr_cmd_payload_ptr, cmd_result);
   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_cmd_ctrl_ptr);
   return result;
}

static ar_result_t apm_end_spf_msg_cmd(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   if (!(apm_info_ptr->curr_cmd_ctrl_ptr->cmd_pending))
   {
      if (AR_EOK == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_status)
      {
         AR_MSG(DBG_HIGH_PRIO,
                "apm_end_spf_msg_cmd(): SPF CMD with Opcode[0x%lX], SUCCESS",
                apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode);
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO,
                "apm_end_spf_msg_cmd(): SPF CMD with Opcode[0x%lX], FAILED, result: 0x%lX",
                apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode,
                apm_info_ptr->curr_cmd_ctrl_ptr->cmd_status);
      }
      spf_msg_ack_msg(&apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg, apm_info_ptr->curr_cmd_ctrl_ptr->cmd_status);

      /** Deallocate the command handler resources   */
      apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_info_ptr->curr_cmd_ctrl_ptr);
   }

   return result;
}

ar_result_t apm_end_cmd(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_cmd_ctrl_t *cmd_ctrl_ptr;

   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   if (SPF_MSG_CMD_GPR == cmd_ctrl_ptr->cmd_msg.msg_opcode)
   {
      result = apm_end_gpr_cmd(apm_info_ptr);
   }
   else /** End GK message */
   {
      result = apm_end_spf_msg_cmd(apm_info_ptr);
   }

   /** At this point evaluate if any deferred command present
    *  and if it can be processed. */

   if (apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr &&
       apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr->apm_check_def_cmd_is_ready_to_process_fptr)
   {

      if (AR_EOK !=
          (result = apm_info_ptr->ext_utils.parallel_cmd_utils_vtbl_ptr->apm_check_def_cmd_is_ready_to_process_fptr(
              apm_info_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_end_cmd(), Deferred cmd proc check failed for cmd_opcode: 0x%8lX",
                cmd_ctrl_ptr->cmd_opcode);
      }
   }

   return result;
}

bool_t apm_gm_cmd_is_sg_id_present(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, uint32_t sg_id)
{
   apm_sub_graph_t *sg_obj_ptr;
   spf_list_node_t *curr_node_ptr;

   /** Get the pointer to list of sub-graph ID's */
   curr_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;

   /** iterate over the list of sub-graph ID's present in the
    *  graph management command, return TRUE if the queried
    *  sub-graph is present in the list, else return FALSE */
   while (curr_node_ptr)
   {
      sg_obj_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      /** Sub-graph ID match found, return */
      if (sg_id == sg_obj_ptr->sub_graph_id)
      {
         return TRUE;
      }

      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return FALSE;
}

static ar_result_t apm_validate_sg_state(apm_cmd_ctrl_t * cmd_ctrl_ptr,
                                         apm_sub_graph_t *sub_graph_node_ptr,
                                         uint32_t         cmd_opcode,
                                         bool_t           is_gm_cmd_sg_id)
{
   ar_result_t result = AR_EOK;

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         /** OPEN command check for the CLOSE handling during
          *  GRAPH OPEN failure. */
         /* is_gm_cmd_sg_id is checked here because for link open failure sg may not be in stopped state and we need to
          * add the SG to process list for clean-up*/
         if (cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status && (APM_SG_STATE_STOPPED != sub_graph_node_ptr->state) &&
             is_gm_cmd_sg_id)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] not in STOPPED state, current state: %lu, cmd_opcode: "
                   "0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   sub_graph_node_ptr->state,
                   cmd_opcode);

            result = AR_EALREADY;
         }
         else if (!cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status && APM_SG_STATE_STARTED != sub_graph_node_ptr->state)
         {
            /** Graph management during graph open is applied for any
             *  data/ctrl link open across started sub-graphs.  */
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] not in STARTED state, current state: %lu, cmd_opcode: "
                   "0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   sub_graph_node_ptr->state,
                   cmd_opcode);

            result = AR_ENOTREADY;
         }

         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      {
         /** PREPARE command can be executed only in STOPPED state. */
         if (APM_SG_STATE_STOPPED != sub_graph_node_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] not in STOPPED state, current state: %lu, cmd_opcode: "
                   "0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   sub_graph_node_ptr->state,
                   cmd_opcode);

            result = AR_EALREADY;
         }
         break;
      }
      case APM_CMD_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      {
         /** START command can be executed in STOPPED, PREPARED or
          *  SUSPENDED state */
         if (APM_SG_STATE_STARTED == sub_graph_node_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] already STARTED, cmd_opcode: 0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   cmd_opcode);

            result = AR_EALREADY;
         }
         break;
      }
      case APM_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      {
         /** STOP command can be executed in PREPARED or STARTED
          *  state */
         if (APM_SG_STATE_STOPPED == sub_graph_node_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] already STOPPED, current state: %lu, cmd_opcode: "
                   "0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   sub_graph_node_ptr->state,
                   cmd_opcode);

            result = AR_EALREADY;
         }

         break;
      }
      case APM_CMD_GRAPH_FLUSH:
      {
         /** FLUSH command can be executed only in STARTED state, otherwise redundant, ignore */
         if (APM_SG_STATE_STARTED != sub_graph_node_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] in state %lu, cmd_opcode: 0x%lx, ignoring flush",
                   sub_graph_node_ptr->state,
                   sub_graph_node_ptr->sub_graph_id,
                   cmd_opcode);

            result = AR_ENOTREADY;
         }
         break;
      }
      case APM_CMD_GRAPH_SUSPEND:
      {
         /** SUSPEND command can be executed only in STARTED state */
         if (APM_SG_STATE_STARTED != sub_graph_node_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_validate_sg_state(): SG_ID[0x%lX] not STARTED, current state: %lu, cmd_opcode: 0x%lx",
                   sub_graph_node_ptr->sub_graph_id,
                   sub_graph_node_ptr->state,
                   cmd_opcode);

            result = AR_ENOTREADY;
         }

         break;
      }
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case SPF_MSG_CMD_SET_CFG:
      {
         /** CLOSE command is allowed in All the sub-graph
          *  states */

         /** Close all could be called in SPF_MSG_CMD_SET_CFG context
          *  It is allowed in all the subgraph states */
         break;
      }
      default:
      {
         /** Execution should not reach here */
         AR_MSG(DBG_ERROR_PRIO,
                "apm_validate_sg_state(): Unexpected cmd_opcode: 0x%lx for SG_ID[0x%lX]",
                cmd_opcode,
                sub_graph_node_ptr->sub_graph_id);

         result = AR_EFAILED;
      }
   }

   return result;
}

ar_result_t apm_validate_sg_state_and_add_to_process_list(apm_cmd_ctrl_t * apm_cmd_ctrl_ptr,
                                                          apm_sub_graph_t *sub_graph_node_ptr,
                                                          bool_t           is_gm_cmd_sg_id)
{
   ar_result_t           result     = AR_EOK;
   uint32_t              cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;
   apm_sub_graph_state_t sg_list_state;
   spf_list_node_t **    list_pptr = NULL;
   uint32_t *            list_node_counter_ptr;

   /** Validate sub-graph state for this sub-graph command. The
    *  sub-graphs that are already transitioned to state as per
    *  current command or not ready yet to transition are
    *  skipped from the processing. */
   if (AR_EOK != (result = apm_validate_sg_state(apm_cmd_ctrl_ptr, sub_graph_node_ptr, cmd_opcode, is_gm_cmd_sg_id)))
   {
      /** If the sub-graph is not ready to transition to next state
       *  or has already transitioned, skip this from processing */
      if ((AR_EALREADY == result) || (AR_ENOTREADY == result))
      {
         AR_MSG(DBG_HIGH_PRIO,
                "GRAPH_MGMT: Skip processing SG_ID[0x%lX], SG state[0x%lx], cmd_opcode[0x%08lx]",
                sub_graph_node_ptr->sub_graph_id,
                sub_graph_node_ptr->state,
                cmd_opcode);

         /** Since this SG is skipped from processing, returning the
          *  error status as success to the caller */
         return AR_EOK;
      }
      else /** Any other failure */
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH_MGMT: SG_ID[0x%lX], incorrect SG_STATE[%lu], cmd_opcode[0x%08lx]",
                sub_graph_node_ptr->sub_graph_id,
                sub_graph_node_ptr->state,
                cmd_opcode);

         return result;
      }
   }

   /** If the current sub-graph ID is received directly as part of
    *  the graph management command, add it to regular sub-graph
    *  list to process */
   if (is_gm_cmd_sg_id)
   {
      list_pptr             = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;
      list_node_counter_ptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs;
   }
   else /** sub-graph ID is received indirectly as part of the link definition */
   {
      /** If the host sub-graph ID for this port connection is not
       *  present in the graph management command, cache this
       *  sub-graph ID */
      if (!apm_gm_cmd_is_sg_id_present(apm_cmd_ctrl_ptr, sub_graph_node_ptr->sub_graph_id))
      {
         list_pptr             = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;
         list_node_counter_ptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cont_port_hdl_sg;
      }
   }

   /** Cache this sub-graphs to the list of sub-graphs that need
    *  to be processed as part of the graph managment command */
   if (list_pptr)
   {
      /** Add this sub-graph node to the selected list */
      if (AR_EOK != (result = apm_db_search_and_add_node_to_list(list_pptr, sub_graph_node_ptr, list_node_counter_ptr)))

      {
         return result;
      }
   }

   /** Get the default sub-graph list state. */
   sg_list_state = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state;

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      {
         /** For START command, the expected current sub-graph state is
          *  PREPARED. However, start command could be issued to
          *  sub-graphs in STOPPED state as well. In this case, the state
          *  for the overall sub-graph list is set to STOPPED. The
          *  overall state is used by the container command sequencer to
          *  determine the lowest state from which to execute the
          *  overall START command sequence */

         sg_list_state = MIN(sub_graph_node_ptr->state, sg_list_state);

         break;
      }
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case SPF_MSG_CMD_SET_CFG:
      {
         /** For CLOSE command, the expected current sub-graph state is
          *  STOPPED. However, close command could be issued to
          *  sub-graphs in STARTED state as well. In this case, the state
          *  for the overall sub-graph list is set to STARTED.  The
          *  overall state is used by the container command sequencer to
          *  determine the highest state from which to execute the
          *  overall CLOSE command sequence */

         /* For SPF_MSG_CMD_SET_CFG, close all might be called in the satellite
          * side. This command can come when subgraphs are in STARTED state as well.
          * So we take the highest state */
         sg_list_state = MAX(sub_graph_node_ptr->state, sg_list_state);

         break;
      }
      default:
      {
         result = AR_EOK;
         break;
      }
   }

   /** Save the update sub-graph list state */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state = sg_list_state;

   AR_MSG(DBG_HIGH_PRIO,
          SPF_LOG_PREFIX
          "GRAPH_MGMT: SG_ID[0x%lX], sg state[%lu], added to process list, cmd_opcode[0x%08lx], is_gm_cmd_sg_id%u",
          sub_graph_node_ptr->sub_graph_id,
          sub_graph_node_ptr->state,
          cmd_opcode,
          is_gm_cmd_sg_id);

   return result;
}

ar_result_t apm_validate_and_cache_link_for_state_mgmt(apm_t *                    apm_info_ptr,
                                                       apm_graph_mgmt_cmd_ctrl_t *gm_cmd_ctrl_ptr,
                                                       apm_module_t **            module_node_list_pptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    cmd_opcode;

   enum
   {
      PEER_1 = 0,
      PEER_2 = 1
   };

   /** Get the current comand opcode being processed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /** Get the pointer to graph management command control
    *  object */
   gm_cmd_ctrl_ptr = &apm_info_ptr->curr_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl;

   /** If the link within a single sub-graph is being closed,
    *  print warning and skip processing this link */
   if ((module_node_list_pptr[PEER_1]->host_sub_graph_ptr->sub_graph_id) ==
       (module_node_list_pptr[PEER_2]->host_sub_graph_ptr->sub_graph_id))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "apm_gm_cmd_validate_link_state() :: WARNING :: ctrl/data link within sub_graph[0x%lx] being "
             "closed/started, "
             "skipping",
             module_node_list_pptr[PEER_1]->host_sub_graph_ptr->sub_graph_id);

      return AR_ECONTINUE;
   }

   /** Set the default sub-graph list state, if it has not been
    *  set previously. Need to pick the higher state between
    *  thhe 2 host sub-graphs for this link during CLOSE. */
   if ((APM_CMD_GRAPH_CLOSE == cmd_opcode) &&
       (APM_SG_STATE_INVALID == gm_cmd_ctrl_ptr->sg_proc_info.curr_sg_list_state))
   {
      gm_cmd_ctrl_ptr->sg_proc_info.curr_sg_list_state = MAX(module_node_list_pptr[PEER_1]->host_sub_graph_ptr->state,
                                                             module_node_list_pptr[PEER_2]->host_sub_graph_ptr->state);
   }
   else if (APM_CMD_GRAPH_OPEN == cmd_opcode)
   {
      gm_cmd_ctrl_ptr->sg_proc_info.curr_sg_list_state = APM_SG_STATE_STOPPED;
   }

   /** Validate if the host sub-graph state for the data link
    *  ports being closed in the correct state to execute the
    *  Graph Close command. */
   for (uint32_t idx = 0; idx < 2; idx++)
   {
      if (AR_EOK !=
          (result = apm_validate_sg_state_and_add_to_process_list(apm_info_ptr->curr_cmd_ctrl_ptr,
                                                                  module_node_list_pptr[idx]->host_sub_graph_ptr,
                                                                  FALSE /** Non GRAPH MGMT SG-ID*/)))
      {
         return result;
      }
   }

   AR_MSG(DBG_MED_PRIO,
          "apm_gm_cmd_validate_link_state(): SG list state[%lu], cmd_opcode[0x%lX]",
          apm_info_ptr->curr_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state,
          cmd_opcode);

   return result;
}

apm_sub_graph_state_t apm_gm_cmd_get_default_sg_list_state(uint32_t cmd_opcode)
{
   apm_sub_graph_state_t sg_list_state;

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      case APM_CMD_GRAPH_OPEN:
      case SPF_MSG_CMD_SET_CFG:
      {
         /** For the GRAPH PREPARE, CLOSE/CLOSE_ALL commands, the
          *  sub-graph should be in the STOPPED state.
          *
          *  For GRAPH OPEN, there are 2 cases where sub-graph management
          *  will be invoked. First is if data/ctrl links are opened
          *  across started sub-graphs in which case the opened link also
          *  need to be started. Second, for any erorrs during open
          *  handling. Sub-graph list is closed. In either case, the
          *  default sub-graph state is assumed to be STOPPED
          *
          *  For SPF_MSG_CMD_SET_CFG, close call could be called for
          *  satellite apm. In this case we make similar assumption to
          *  graphp state as close all, however, this command can
          *  come when graph is in open state as well */
         sg_list_state = APM_SG_STATE_STOPPED;
         break;
      }
      case APM_CMD_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      {
         /** For the GRAPH START command, the sub-graph
          *  should be in the PREPARED state */
         sg_list_state = APM_SG_STATE_PREPARED;
         break;
      }
      case APM_CMD_GRAPH_SUSPEND:
      case APM_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      {
         /** For the GRAPH STOP/SUSPEND command, the sub-graph
          *  should be in the STARTED state */
         sg_list_state = APM_SG_STATE_STARTED;
         break;
      }
      case APM_CMD_GRAPH_FLUSH:
      {
         /** List state is don't care for flush  */
         sg_list_state = APM_SG_STATE_STARTED;
         break;
      }
      default:
      {
         /** Execution should not reach here */

         sg_list_state = APM_SG_STATE_INVALID;

         AR_MSG(DBG_ERROR_PRIO, "apm_gm_cmd_get_default_sg_list_state(): Unexpected cmd_opcode: 0x%lx ", cmd_opcode);

         break;
      }
   }

   return sg_list_state;
}

ar_result_t apm_gm_cmd_cfg_sg_list_to_process(apm_t *apm_info_ptr)
{
   ar_result_t           result = AR_EOK;
   apm_sub_graph_t *     sub_graph_node_ptr;
   apm_cmd_ctrl_t *      apm_cmd_ctrl_ptr;
   apm_sub_graph_state_t sg_list_state;
   uint32_t              cmd_opcode;
   spf_list_node_t *     curr_node_ptr;
   spf_list_node_t *     cont_port_hdl_sg_list_ptr = NULL;
   uint32_t              num_sg_list               = 0;

   typedef struct sg_list_info
   {
      bool_t           direct_cmd_list;
      spf_list_node_t *list_ptr;
   } sg_list_info;

   sg_list_info gm_cmd_sg_list[2] = { { 0 } };

   /** Get the pointer to current APM command control object */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the currnt opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** Set the default state for the list of sub-graphs being
    *  processed under the current graph management command. Get
    *  the expected default state for the sub-graph for the
    *  current command. */
   if (APM_SG_STATE_INVALID == (sg_list_state = apm_gm_cmd_get_default_sg_list_state(cmd_opcode)))
   {
      return AR_EFAILED;
   }

   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr)
   {
      gm_cmd_sg_list[num_sg_list].list_ptr          = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;
      gm_cmd_sg_list[num_sg_list++].direct_cmd_list = TRUE;
   }

   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr)
   {
      /** Cache the cont port handle sg list and num nodes   */
      gm_cmd_sg_list[num_sg_list].list_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;
      gm_cmd_sg_list[num_sg_list++].direct_cmd_list = FALSE;

      /** Cache the list ptr, num nodes   */
      cont_port_hdl_sg_list_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;
      /** Clear the corresponding book keeping variables. This gets
       *  re-populated after sg-state validation below. */
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr = NULL;
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cont_port_hdl_sg      = 0;
   }

   /** Set the default sub-graph list state */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state = sg_list_state;

   for (uint32_t list_idx = 0; list_idx < num_sg_list; list_idx++)
   {
      /** Get the pointer to list of regular sub-graphs  */
      curr_node_ptr = gm_cmd_sg_list[list_idx].list_ptr;

      /** Iterate over the sub-graph list */
      while (curr_node_ptr)
      {
         sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

         /** Validate the sub-graph state and add it to the process
          *  list */
         if (AR_EOK !=
             (result = apm_validate_sg_state_and_add_to_process_list(apm_cmd_ctrl_ptr,
                                                                     sub_graph_node_ptr,
                                                                     gm_cmd_sg_list[list_idx]
                                                                        .direct_cmd_list /** GM cmd sub-graph ID*/)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_gm_cmd_cfg_sg_list_to_process(): Failed to add SG_ID[0x%lX] to proc list, cmd_opcode[0x%lX]",
                   sub_graph_node_ptr->sub_graph_id,
                   cmd_opcode);

            return result;
         }

         /** Advance to next node in the list  */
         curr_node_ptr = curr_node_ptr->next_ptr;

      } /** End of for loop() */
   }

   /** Get the updated sub-graph list state update sub-graph
    *  state validation */
   sg_list_state = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state;

   /** Clear the temp list of cont port handle sub-graphs, if
    *  present */
   if (cont_port_hdl_sg_list_ptr)
   {
      spf_list_delete_list(&cont_port_hdl_sg_list_ptr, TRUE);
   }

   AR_MSG(DBG_MED_PRIO,
          "apm_gm_cmd_cfg_sg_list_to_process(): SG list state[%lu], cmd_opcode[0x%lX]",
          sg_list_state,
          cmd_opcode);

   return result;
}

ar_result_t apm_gm_cmd_validate_sg_list(apm_t *             apm_info_ptr,
                                        uint32_t            num_sub_graphs,
                                        apm_sub_graph_id_t *sg_id_list_ptr,
                                        uint32_t            cmd_opcode)
{
   ar_result_t      result = AR_EOK;
   apm_sub_graph_t *sub_graph_node_ptr;
   apm_cmd_ctrl_t * apm_cmd_ctrl_ptr;
   bool_t           seq_entry_reqd = TRUE;
   bool_t           found_sg_node  = FALSE;

   if (!num_sub_graphs || !sg_id_list_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "GRAPH_MGMT: Invalid I/p arg, num_sub_graphs: %lu, sg_id_list_ptr: 0x%lX, cmd_opcode: 0x%lx ",
             num_sub_graphs,
             sg_id_list_ptr,
             cmd_opcode);

      return AR_EFAILED;
   }

   /** Get the pointer to current APM command control object */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Iterate over the sub-graph list */
   for (uint32_t arr_idx = 0; arr_idx < num_sub_graphs; arr_idx++)
   {

      /** Get the sub-graph node from graph DB, For close command we can proceed if one of the SG is not present*/
      if ((AR_EOK != (result = apm_db_get_sub_graph_node(&apm_info_ptr->graph_info,
                                                         sg_id_list_ptr[arr_idx].sub_graph_id,
                                                         &sub_graph_node_ptr,
                                                         APM_DB_OBJ_QUERY))) &&
          (APM_CMD_GRAPH_CLOSE != cmd_opcode))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_validate_sg_list(): SG_ID: 0x%lX does not exist, cmd_opcode: 0x%lx",
                sg_id_list_ptr[arr_idx].sub_graph_id,
                cmd_opcode);

         return result;
      }

      /* For close command we can proceed if one of the SG is not present */
      if ((!sub_graph_node_ptr) && (APM_CMD_GRAPH_CLOSE != cmd_opcode))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_validate_sg_list(): SG_ID: 0x%lX does not exist, cmd_opcode: 0x%lx, sg_node_ptr is NULL",
                sg_id_list_ptr[arr_idx].sub_graph_id,
                cmd_opcode);

         return AR_EFAILED;
      }

      /**
       * Below checks and aggregates the sub graph management states from each client and decides to put it to
       * reg_sg_list_ptr or not. reg_sg_list_ptr is used by APM cmn sequencer to process further.(Default handling).
       * allow_sequencer is TRUE when it's decided to allowed to be further processing by APM cmn sequencer, else FALSE.
       */
      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_check_sg_state_and_update_send_to_seqncr_fptr)
      {
         if (AR_EOK != apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                          ->apm_multi_client_check_sg_state_and_update_send_to_seqncr_fptr(apm_info_ptr,
                                                                                           sub_graph_node_ptr,
                                                                                           cmd_opcode,
                                                                                           &seq_entry_reqd))
         {
            AR_MSG(DBG_ERROR_PRIO, "[multi-client]: Failure checking apm_check_sg_state_and_update_send_to_seqncr");
            return AR_EFAILED;
         }
      }

      /** Add this sub-graph node to the selected list */
      if (sub_graph_node_ptr)
      {
         if (seq_entry_reqd)
         {
         if (AR_EOK !=
             (result =
                 apm_db_search_and_add_node_to_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr,
                                                    sub_graph_node_ptr,
                                                       &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list
                                                           .num_reg_sub_graphs)))

         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_gm_cmd_validate_sg_list(): Failed to add node SG_ID: 0x%lX to list, cmd_opcode: 0x%lx",
                   sg_id_list_ptr[arr_idx].sub_graph_id,
                   cmd_opcode);

            return result;
         }
      }
         else
         {
            found_sg_node = TRUE; // This is false by default, so single client case should work as current behaviour
         }
      }

   } /** End of for loop() */

   /* if none of the sub-graphs listed in the close command are found, then return failure to the client.*/
   if ((!apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs) && (APM_CMD_GRAPH_CLOSE == cmd_opcode) &&
       (!found_sg_node))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gm_cmd_validate_sg_list(): num_reg_sub_graphs = %d, cmd_opcode: 0x%lx",
             apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs,
             cmd_opcode);
      return AR_EFAILED;
   }

   return result;
}

ar_result_t apm_allocate_cmd_hdlr_resources(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t      result = AR_EOK;
   uint32_t         cmd_opcode;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to ext utlis vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Validate input arguments */
   if (!apm_info_ptr || !msg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_allocate_cmd_hdlr_resources(), Invalid ip arg, apm_info_ptr[0x%lX], msg_ptr[0x%lX]",
             apm_info_ptr,
             msg_ptr);

      return AR_EFAILED;
   }

   /** Get the current opcode under process */
   cmd_opcode = apm_get_cmd_opcode_from_msg_payload(msg_ptr);

   /** Insert Max vote asynchronously */
   if (ext_utils_ptr->pwr_mgr_vtbl_ptr && ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_vote_fptr)
   {
      if (AR_EOK != (result = ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_vote_fptr(apm_info_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_process_cmd(): Failed to vote for pwr mgr resources, cmd_opcode[0x%lX]",
                cmd_opcode);

         return result;
      }
   }

   /** Allocate the command control object for this command */
   if (AR_EOK != (result = apm_allocate_cmd_ctrl_obj(apm_info_ptr, msg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to get cmd ctrl obj, cmd_opcode[0x%lX], result[%lu]", cmd_opcode, result);

      /** Release the voted power resources in case of failure */
      if (ext_utils_ptr->pwr_mgr_vtbl_ptr && ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_devote_fptr)
      {
         ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_devote_fptr(apm_info_ptr);
      }
   }
   else /** ALlocation succesful */
   {
      /** Capture the start timestamp for this command execution */
      apm_info_ptr->curr_cmd_ctrl_ptr->cmd_start_ts_us = posal_timer_get_time();
   }

   return result;
}

static ar_result_t apm_deallocate_cmd_ctrl_obj(apm_t *apm_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    list_idx;
   uint32_t    cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   switch (apm_cmd_ctrl_ptr->cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         /** Clear any cached sub-graph configuration.  */
         apm_clear_graph_open_cmd_info(apm_info_ptr);

         if (AR_EOK != apm_cmd_ctrl_ptr->cmd_status)
         {
            /** Clear graph management info. This will get called if the
             *  graph open fails */
            apm_clear_graph_mgmt_cmd_info(apm_info_ptr, apm_cmd_ctrl_ptr);

            // If graph open failed clean up the remaining apm_db structures
            apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
            if ((ext_utils_ptr->db_query_vtbl_ptr) &&
                (NULL != ext_utils_ptr->db_query_vtbl_ptr->apm_db_free_cntr_and_sg_list_fptr))
            {
               ext_utils_ptr->db_query_vtbl_ptr->apm_db_free_cntr_and_sg_list_fptr();
            }
         }

         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case APM_CMD_GRAPH_SUSPEND:
      case APM_CMD_GRAPH_FLUSH:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      {
         apm_clear_graph_mgmt_cmd_info(apm_info_ptr, apm_cmd_ctrl_ptr);

         break;
      }
      case APM_CMD_GET_CFG:
      {
         /** Clear the list of cached get config PID payload list */
         if (apm_cmd_ctrl_ptr->get_cfg_cmd_ctrl.pid_payload_list_ptr)
         {
            spf_list_delete_list(&apm_cmd_ctrl_ptr->get_cfg_cmd_ctrl.pid_payload_list_ptr, TRUE);
         }

         break;
      }
      case SPF_EVT_TO_APM_FOR_PATH_DELAY:
      case SPF_MSG_CMD_SET_CFG:
      case SPF_MSG_CMD_GET_CFG:
      case APM_CMD_SET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      {
         /* For SPF_MSG_CMD_SET_CFG, close call could be called for
          * satellite apm. In this case we don't have to do any clean up */

         /* SPF_MSG_CMD_GET_CFG can be called for apm db query */

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_deallocate_cmd_ctrl_obj(), Unsupported cmd_opcode[0x%lX]", cmd_opcode);
         break;
      }

   } /** End of switch (apm_cmd_ctrl_ptr->cmd_opcode) */

   /** Check if the list of container pending command processing
    *  is present, then clear. This could happen in case of command
    *  failure and command processing has ended prematurely */
   if (apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr)
   {
      /** Clear the list of pending containers */
      apm_clear_container_list(apm_cmd_ctrl_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container,
                               APM_CONT_CACHED_CFG_RELEASE,
                               APM_CONT_CMD_PARAMS_RELEASE,
                               APM_PENDING_CONT_LIST);
   }

   /** Cache the freed list index */
   list_idx = apm_cmd_ctrl_ptr->list_idx;

   /** Get the current command opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** Add the command Q back to the wait mask.   */
   apm_info_ptr->curr_wait_mask |= APM_CMD_Q_MASK;

   /** Clear this bit in the active command mask */
   APM_CLR_BIT(&apm_info_ptr->active_cmd_mask, apm_cmd_ctrl_ptr->list_idx);

   /** Clear the command control */
   memset(apm_cmd_ctrl_ptr, 0, sizeof(apm_cmd_ctrl_t));

   /** Set the command msg opcode to invalid */
   apm_cmd_ctrl_ptr->cmd_msg.msg_opcode = AR_GUID_INVALID;

   /** Clear current command control pointer  */
   apm_info_ptr->curr_cmd_ctrl_ptr = NULL;

   AR_MSG(DBG_MED_PRIO,
          "apm_deallocate_cmd_ctrl_obj(), released cmd_list_idx[%lu], cmd_opcode[0x%lX]",
          list_idx,
          cmd_opcode);

   return result;
}

ar_result_t apm_deallocate_cmd_hdlr_resources(apm_t *apm_info_ptr, apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   ar_result_t      result = AR_EOK;
   uint32_t         cmd_exec_time_us;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to ext utlis vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Validate input arguments  */
   if (!apm_info_ptr || !cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_deallocate_cmd_hdlr_resources(): APM info ptr(0x%lX) and/or APM cmd ctrl ptr(0x%lX) is NULL",
             apm_info_ptr,
             cmd_ctrl_ptr);

      return AR_EFAILED;
   }

   /** At this point check if the overall command execution time
    *  exceeded the threshold */
   cmd_exec_time_us = (posal_timer_get_time() - cmd_ctrl_ptr->cmd_start_ts_us);

   AR_MSG(DBG_HIGH_PRIO,
          "apm_deallocate_cmd_hdlr_resources(): Cmd execution time[%lu us], opcode[0x%lX], result[0x%lX]",
          cmd_exec_time_us,
          cmd_ctrl_ptr->cmd_opcode,
          cmd_ctrl_ptr->cmd_status);

   /** If command execution time exceeds the threshold of 800
    *  microseconds, force crash */
   if (cmd_exec_time_us > SPF_EXTERNAL_CMD_EXEC_TIME_THRESHOLD_US)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "::Forced Crash:: Cmd exec time exceeded threshold[%lu us]",
             SPF_EXTERNAL_CMD_EXEC_TIME_THRESHOLD_US);

#if 0
      /** Force Crash  */
	  //Disable force crash
      posal_err_fatal("Forced Crash:: Cmd exec time exceeded threshold");
#endif
   }

   /** Insert delayed release with a delay of 20 ms
    *  Only if the counter reaches 0, then make the actual vote */
   if (ext_utils_ptr->pwr_mgr_vtbl_ptr && ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_devote_fptr)
   {
      ext_utils_ptr->pwr_mgr_vtbl_ptr->apm_pwr_mgr_devote_fptr(apm_info_ptr);
   }

   /** Clear the APM command message control  */
   apm_deallocate_cmd_ctrl_obj(apm_info_ptr, cmd_ctrl_ptr);

   return result;
}
