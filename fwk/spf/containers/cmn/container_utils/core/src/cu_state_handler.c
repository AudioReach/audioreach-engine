/**
 * \file cu_state_handler.c
 * \brief
 *     This file contains container utility functions for handling graph state.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "cu_i.h"
#include "apm.h"
#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"

/*==============================================================================
 * Local Declarations
 * =============================================================================*/
static ar_result_t cu_apply_downgraded_port_states(cu_base_t *cu_ptr);

static ar_result_t cu_update_sg_port_states(cu_base_t *me_ptr, gu_sg_t *sg_ptr, bool_t is_skip_ctrl_ports);

/**
 * Port state update is a three step process.
 *    1. First pass,Port states are downgraded based on sub graph state.
 *    2. Boundary port states are propagated in the container and port states
 *       are downgraded again based on propagated states. After this step,
 *       each port state in container are downgraded based on (self SG state, connected SG state, propagated state).
 *    3. Apply the downgraded state on the container.
 * Step 1:
 * Goes through all external and internal input and output ports in the
 * subgraph and updates their states to be the downgraded state
 * of the current port's state and its connected port's state. Logically,
 * if a source port is stopped but a destination port is started, the
 * destination port will never receive data so it is essentially also stopped.
 * Conversely, if a source port is started but the destination port is stopped,
 * the destination port will drop all data so we have the same behavior as if
 * the source port is stopped.
 *
 * Step 2:
 * Port state is also downgraded based on propagated state. States are propagated from the
 * boundary modules. A module can be termed Boundary module if,
 *   1. it has external ports.
 *   2. A dangling module. (No connected in/out ports but not source/sink module.)
 *
 * Step 3:
 * Apply the downgraded state obtained from (self sg, connected sg, propagated states) on the container.
 *
 * called after graph management cmds
 * and also after RT/FTRT propagation
 */

ar_result_t cu_update_all_sg_port_states(cu_base_t *me_ptr, bool_t is_skip_ctrl_ports)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   VERIFY(result, me_ptr);

   CU_MSG(me_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "updating all sg port state.");

   // Downgrade port states based on the (self SG, connected SG) states.
   // This is the first downgraded state.
   for (gu_sg_list_t *all_sgs_list_ptr = me_ptr->gu_ptr->sg_list_ptr; (NULL != all_sgs_list_ptr);
        LIST_ADVANCE(all_sgs_list_ptr))
   {
      result |= cu_update_sg_port_states(me_ptr, all_sgs_list_ptr->sg_ptr, is_skip_ctrl_ports);
   }

   // Propagates boundary module's port state, and downgrades each port state with the propagated
   // state.
   if (me_ptr->topo_vtbl_ptr->propagate_boundary_modules_port_state)
   {
      result |= me_ptr->topo_vtbl_ptr->propagate_boundary_modules_port_state(me_ptr->topo_ptr);
   }

   // Handle necessary container specific functionality for the latest downgraded state.
   // Iterates through all modules in the container and applies the downgraded port state through container call back.
   // downgraded state is applied on all the ports irrespective of any change in the state. Its upto container,
   // to check and prevent redundant state application.
   result |= cu_apply_downgraded_port_states(me_ptr);

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t cu_update_sg_port_states(cu_base_t *me_ptr, gu_sg_t *sg_ptr, bool_t is_skip_ctrl_ports)
{
   ar_result_t result = AR_EOK;

   topo_port_state_t self_port_state = topo_sg_state_to_port_state(me_ptr->topo_vtbl_ptr->get_sg_state(sg_ptr));
   topo_port_state_t ds_downgraded_state;
   topo_port_state_t downgraded_state;

   for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gu_module_t *module_ptr = (gu_module_t *)module_list_ptr->module_ptr;

      /**
       * state propagation in control path is only from DS to US.
       * For US to DS, port state is not propagated in control path. It happens through EOS in data path
       * which changes the data flow state of the port.
       */
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gu_output_port_t *out_port_ptr = (gu_output_port_t *)out_port_list_ptr->op_port_ptr;

         ds_downgraded_state = TOPO_PORT_STATE_INVALID;

         if (out_port_ptr->ext_out_port_ptr)
         {
            uint8_t           *gu_ext_out_port_ptr = (uint8_t *)out_port_ptr->ext_out_port_ptr;
            cu_ext_out_port_t *ext_out_port_ptr =
               (cu_ext_out_port_t *)(gu_ext_out_port_ptr + me_ptr->ext_out_port_cu_offset);

            /** Since shared mem EP ports are internally created, their connected and propagated sg
             *  state must also be set internally based on SG state*/
            if ((MODULE_ID_RD_SHARED_MEM_EP == module_ptr->module_id)
                /* || (MODULE_ID_RD_SHARED_MEM_CLIENT == module_ptr->module_id)*/)
            {
               ext_out_port_ptr->connected_port_state  = self_port_state;
               ext_out_port_ptr->propagated_port_state = self_port_state;
            }

            ds_downgraded_state =
               cu_evaluate_n_update_ext_out_ds_downgraded_port_state(me_ptr, out_port_ptr->ext_out_port_ptr);
         }
         else // intra-container ports
         {
            // Handles inter-SG connections within the container.
            if (out_port_ptr->conn_in_port_ptr &&
                (out_port_ptr->conn_in_port_ptr->cmn.module_ptr->sg_ptr->id != sg_ptr->id))
            {
               ds_downgraded_state = topo_sg_state_to_port_state(
                  me_ptr->topo_vtbl_ptr->get_sg_state(out_port_ptr->conn_in_port_ptr->cmn.module_ptr->sg_ptr));
            }
            else if (!out_port_ptr->conn_in_port_ptr) // dangling port, about to get closed
            {
               ds_downgraded_state = TOPO_PORT_STATE_STOPPED;
            }
            else
            {
               ds_downgraded_state = self_port_state;
            }
         }

         /* Note: There can be an output port which blocks the propagation which means
          * it doesn't want downstream state to affect its own state.
          * At this point only external ports are downgraded which will be
          * reverted to self_state if they want to block the propagation. This happens in
          * gen_topo_propagate_boundary_modules_port_state.
          */
         downgraded_state = tu_get_downgraded_state(self_port_state, ds_downgraded_state);

         if (TOPO_PORT_STATE_INVALID != downgraded_state)
         {
            me_ptr->topo_vtbl_ptr->set_port_property(me_ptr->topo_ptr,
                                                     TOPO_DATA_OUTPUT_PORT_TYPE,
                                                     PORT_PROPERTY_TOPO_STATE,
                                                     out_port_ptr,
                                                     downgraded_state);
         }
      }

      // update IPC port states
      if (module_ptr->ipc_output_port_list_ptr)
      {
         result |= cu_ipc_output_ports_update_sg_state(me_ptr, module_ptr, self_port_state);
      }

      /**
       * For input ports we just set self state. there's no downgrade wrt ext-in or US SG.
       * However, state prop can still happen from DS to US (later in the flow in cu_apply_downgraded_port_states)
       */
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gu_input_port_t *in_port_ptr = (gu_input_port_t *)in_port_list_ptr->ip_port_ptr;

         /** Since shared mem EP ports are internally created, their connected and propagated sg
          *  state must also be set internally based on SG state*/
         if (MODULE_ID_WR_SHARED_MEM_EP == module_ptr->module_id && in_port_ptr->ext_in_port_ptr)
         {
            uint8_t          *gu_ext_in_port_ptr = (uint8_t *)in_port_ptr->ext_in_port_ptr;
            cu_ext_in_port_t *ext_in_port_ptr =
               (cu_ext_in_port_t *)(gu_ext_in_port_ptr + me_ptr->ext_in_port_cu_offset);

            ext_in_port_ptr->connected_port_state = self_port_state;
         }

         me_ptr->topo_vtbl_ptr->set_port_property(me_ptr->topo_ptr,
                                                  TOPO_DATA_INPUT_PORT_TYPE,
                                                  PORT_PROPERTY_TOPO_STATE,
                                                  in_port_ptr,
                                                  self_port_state);
      }

      if (module_ptr->ipc_input_port_list_ptr)
      {
         result |= cu_ipc_input_ports_update_sg_state(me_ptr, module_ptr, self_port_state);
      }

      if (is_skip_ctrl_ports)
      {
         continue;
      }

      for (gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr; (NULL != ctrl_port_list_ptr);
           LIST_ADVANCE(ctrl_port_list_ptr))
      {
         gu_ctrl_port_t *  ctrl_port_ptr = (gu_ctrl_port_t *)ctrl_port_list_ptr->ctrl_port_ptr;
         topo_port_state_t actual_ctrl_port_state;
         me_ptr->topo_vtbl_ptr->get_port_property(me_ptr->topo_ptr,
                                                  TOPO_CONTROL_PORT_TYPE,
                                                  PORT_PROPERTY_TOPO_STATE,
                                                  (void *)ctrl_port_ptr,
                                                  (uint32_t *)&actual_ctrl_port_state);

         ds_downgraded_state = TOPO_PORT_STATE_INVALID;

         if (ctrl_port_ptr->ext_ctrl_port_ptr)
         {
            uint8_t *           gu_ext_ctrl_port_ptr = (uint8_t *)ctrl_port_ptr->ext_ctrl_port_ptr;
            cu_ext_ctrl_port_t *ext_ctrl_port_ptr =
               (cu_ext_ctrl_port_t *)(gu_ext_ctrl_port_ptr + me_ptr->ext_ctrl_port_cu_offset);

            ds_downgraded_state = ext_ctrl_port_ptr->connected_port_state;
         }
         else
         {
            // Handles inter-SG connections within the container.
            if (ctrl_port_ptr->peer_ctrl_port_ptr &&
                (ctrl_port_ptr->peer_ctrl_port_ptr->module_ptr->sg_ptr->id != sg_ptr->id))
            {
               ds_downgraded_state = topo_sg_state_to_port_state(
                  me_ptr->topo_vtbl_ptr->get_sg_state(ctrl_port_ptr->peer_ctrl_port_ptr->module_ptr->sg_ptr));
            }
            else
            {
               ds_downgraded_state = self_port_state;
            }
         }

         // Set the port state to the downgraded state. If the downgraded state is same as the current state,
         // don't do anything.
         downgraded_state = tu_get_downgraded_state(self_port_state, ds_downgraded_state);
         if ((TOPO_PORT_STATE_INVALID != downgraded_state) && (actual_ctrl_port_state != downgraded_state))
         {
            result = cu_set_downgraded_state_on_ctrl_port(me_ptr, ctrl_port_ptr, downgraded_state);
         }
      }
   }

   return result;
}

static bool_t cu_is_atleast_one_sg_started(cu_base_t *me_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = me_ptr->gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = (gu_sg_t *)sg_list_ptr->sg_ptr;
      if (TOPO_SG_STATE_STARTED == me_ptr->topo_vtbl_ptr->get_sg_state(sg_ptr))
      {
         return TRUE;
      }
   }
   return FALSE;
}

static void cu_update_ext_connected_in_port_state(cu_base_t        *me_ptr,
                                                  gu_ext_in_port_t *gu_ext_in_port_ptr,
                                                  topo_port_state_t state)
{
   if (TOPO_PORT_STATE_INVALID != state)
   {
      cu_ext_in_port_t *ext_in_port_ptr =
         (cu_ext_in_port_t *)((uint8_t *)gu_ext_in_port_ptr + me_ptr->ext_in_port_cu_offset);
      ext_in_port_ptr->connected_port_state = state;
   }
}

static void cu_update_ext_connected_out_port_state(cu_base_t         *me_ptr,
                                                   gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                   topo_port_state_t  state)
{
   if (TOPO_PORT_STATE_INVALID != state)
   {
      cu_ext_out_port_t *ext_out_port_ptr =
         (cu_ext_out_port_t *)((uint8_t *)gu_ext_out_port_ptr + me_ptr->ext_out_port_cu_offset);
      ext_out_port_ptr->connected_port_state = state;
   }
}

static void cu_update_ext_connected_ctrl_port_state(cu_base_t          *me_ptr,
                                                    gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr,
                                                    topo_port_state_t   state)
{
   if (TOPO_PORT_STATE_INVALID != state)
   {
      cu_ext_ctrl_port_t *ext_ctrl_port_ptr =
         (cu_ext_ctrl_port_t *)((uint8_t *)gu_ext_ctrl_port_ptr + me_ptr->ext_ctrl_port_cu_offset);
      ext_ctrl_port_ptr->connected_port_state = state;
   }
}

/**
 * Parses the graph management command (START, PREPARE, STOP, etc.)
 * to operate on only external ports and subgraphs specified by the
 * command. After operating, updates subgraph and port states. Pass in
 * the state the subgraph should move to after the operation, for operations
 * that don't correspond to states (example close op and stop state).
 *
 *
 * Close handling (US = upstream, DS = downstream)
      a. APM issues Stop [port-handles, SG-list] if not already stopped.
         Stop is a series operation: US [port-handles, SG-list] to DS [port-handles, SG-list]

      b. APM issues Disconnect [port-handles] in parallel
         i.   Cannot free-up memory as US may still access to send media fmt
         ii.  Return buffers.
         iii. Set peer handle = NULL.
         iv.  This handles are still valid. So control cmds may be pushed from peer until it also is disconnected [e.g.
               in stop state prepare cmd could come in other SG]
         v.   Ack to APM
         vi.  Why disconnect & not direct close: if we close, then memory is freed. But other port still has reference
 to
              the memory. And control cmd can be sent in both directions (US to DS and DS to US).  Having this
 intermediate step
              ensures memory is not freed until each entity removes reference to the other.
      c. APM issues close [port-handles, SG-list] in parallel.
         i.   Port memory is freed.
 *
 */
ar_result_t cu_handle_sg_mgmt_cmd(cu_base_t *me_ptr, uint32_t sg_ops, topo_sg_state_t sg_state)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_cntr_sub_graph_list_t *sg_list_ptr;
   spf_msg_cmd_graph_mgmt_t  *cmd_gmgmt_ptr;
   bool_t                     is_only_ctrl_port_cmd = TRUE;
   topo_port_state_t          st                    = tu_sg_op_to_port_state(sg_ops);

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;

   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));
   VERIFY(result,
          me_ptr->cntr_vtbl_ptr && me_ptr->cntr_vtbl_ptr->operate_on_ext_in_port &&
             me_ptr->cntr_vtbl_ptr->operate_on_ext_out_port && me_ptr->cntr_vtbl_ptr->operate_on_subgraph);

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;

   CU_MSG(me_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "cu_handle_sg_mgmt_cmd. num_ip_port_handle %lu, num_op_port_handle %lu, num_ctrl_port_handle %lu, "
          "num_data_links %lu "
          "num_sub_graph %lu",
          cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle,
          cmd_gmgmt_ptr->cntr_port_hdl_list.num_op_port_handle,
          cmd_gmgmt_ptr->cntr_port_hdl_list.num_ctrl_port_handle,
          cmd_gmgmt_ptr->cntr_port_hdl_list.num_data_links,
          sg_list_ptr->num_sub_graph);

   for (uint32_t i = 0; i < sg_list_ptr->num_sub_graph; i++)
   {
      uint32_t *sg_id_base_ptr = sg_list_ptr->sg_id_list_ptr;
      uint32_t  sg_id          = *(sg_id_base_ptr + i);

      is_only_ctrl_port_cmd = FALSE;

      gu_sg_t *sg_ptr = gu_find_subgraph(me_ptr->gu_ptr, sg_id);
      if (sg_ptr)
      {
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "cu_handle_sg_mgmt_cmd. sg_id (%08X)", sg_id);
         TRY(result, me_ptr->cntr_vtbl_ptr->operate_on_subgraph(me_ptr, sg_ops, sg_state, sg_ptr, sg_list_ptr));
      }
      else
      {
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "cu_handle_sg_mgmt_cmd. sg_id (%08X) not found", sg_id);
         // ignore this error. APM sends close if open had failed. in that case, we shouldn't error out.
         // if client sends close to unknown SG, then APM rejects. containers don't need to worry about that case.
      }
   }

   /**
    * operate on the input port handles - external connections
    *
    * no need to send cmd to peer containers. APM sequences all graph-mgmt commands
    */
   for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle; i++)
   {
      gu_ext_in_port_t *ext_in_port_ptr =
         (gu_ext_in_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.ip_port_handle_list_pptr[i];

      // if the subgraph of this external port already went through this SG_OPS then don't need to operate on it
      // separately.
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_in_port_ptr->sg_ptr->id))
      {
         is_only_ctrl_port_cmd = FALSE;

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "cu_handle_sg_mgmt_cmd. External input of module, port (0x%lx, 0x%lx)",
                ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->int_in_port_ptr->cmn.id);

         TRY(result, me_ptr->cntr_vtbl_ptr->operate_on_ext_in_port(me_ptr, sg_ops, &ext_in_port_ptr, FALSE));
      }
      cu_update_ext_connected_in_port_state(me_ptr, ext_in_port_ptr, st);
   }

   /**
    * operate on the output port handles - external connections
    *
    * no need to send cmd to peer containers. APM sequences all graph-mgmt commands
    */
   for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_op_port_handle; i++)
   {
      gu_ext_out_port_t *ext_out_port_ptr =
         (gu_ext_out_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.op_port_handle_list_pptr[i];

      // if the subgraph of this external port already went through this SG_OPS then don't need to operate on it
      // separately.
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_out_port_ptr->sg_ptr->id))
      {
         is_only_ctrl_port_cmd = FALSE;

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "cu_handle_sg_mgmt_cmd. External output of module, port (0x%lx, 0x%lx)",
                ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_out_port_ptr->int_out_port_ptr->cmn.id);

         TRY(result, me_ptr->cntr_vtbl_ptr->operate_on_ext_out_port(me_ptr, sg_ops, &ext_out_port_ptr, FALSE));
      }
      cu_update_ext_connected_out_port_state(me_ptr, ext_out_port_ptr, st);
   }

   {
      // 1. if there were some operation on subgraph then force updated the started sorted module list.
      // 2. operation on external data-port can also cause attached modules to get detached therefore update the started
      // sorted module list.
      bool_t b_force_update = (sg_list_ptr->num_sub_graph) ? TRUE : FALSE;
      TRY(result, me_ptr->topo_vtbl_ptr->check_update_started_sorted_module_list(me_ptr->topo_ptr, b_force_update));
   }
   /**
    * operate on the output port handles - external connections
    *
    * no need to send cmd to peer containers. APM sequences all graph-mgmt commands
    */
   for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_ctrl_port_handle; i++)
   {
      gu_ext_ctrl_port_t *ext_ctrl_port_ptr =
         (gu_ext_ctrl_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.ctrl_port_handle_list_pptr[i];

      // if the subgraph of this external port already went through this SG_OPS then don't need to operate on it
      // separately.
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_ctrl_port_ptr->sg_ptr->id))
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "cu_handle_sg_mgmt_cmd. External ctrl port of module, port (0x%lx, 0x%lx)",
                ext_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id,
                ext_ctrl_port_ptr->int_ctrl_port_ptr->id);

         TRY(result, me_ptr->cntr_vtbl_ptr->operate_on_ext_ctrl_port(me_ptr, sg_ops, &ext_ctrl_port_ptr, FALSE));
      }
      cu_update_ext_connected_ctrl_port_state(me_ptr, ext_ctrl_port_ptr, st);
   }

   if (TOPO_SG_OP_CLOSE & sg_ops)
   {
      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_data_links; i++)
      {
         apm_module_conn_cfg_t *data_link_ptr = cmd_gmgmt_ptr->cntr_port_hdl_list.data_link_list_pptr[i];

         is_only_ctrl_port_cmd = FALSE;

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX
                "cu_handle_sg_mgmt_cmd: Data link close SRC (inst-id, port-id): (0x%lx, 0x%lx), DST (inst-id, "
                "port-id): (0x%lx, 0x%lx)",
                data_link_ptr->src_mod_inst_id,
                data_link_ptr->src_mod_op_port_id,
                data_link_ptr->dst_mod_inst_id,
                data_link_ptr->dst_mod_ip_port_id);

         gu_output_port_t *src_out_port_ptr = NULL;
         gu_input_port_t * dst_in_port_ptr  = NULL;
         if (AR_EOK == gu_parse_data_link(me_ptr->gu_ptr, data_link_ptr, &src_out_port_ptr, &dst_in_port_ptr))
         {
            bool_t              SET_PORT_OP_TRUE = TRUE;
            topo_sg_operation_t req_ops[]        = { TOPO_SG_OP_STOP, TOPO_SG_OP_DISCONNECT, TOPO_SG_OP_CLOSE };
            for (uint32_t k = 0; k < SIZE_OF_ARRAY(req_ops); k++)
            {
               // call operate on internal in port first, which takes care of upstream attached module.
               TRY(result,
                   me_ptr->topo_vtbl_ptr->operate_on_int_in_port(me_ptr->topo_ptr,
                                                                 dst_in_port_ptr,
                                                                 sg_list_ptr,
                                                                 req_ops[k],
                                                                 SET_PORT_OP_TRUE));

               TRY(result,
                   me_ptr->topo_vtbl_ptr->operate_on_int_out_port(me_ptr->topo_ptr,
                                                                  src_out_port_ptr,
                                                                  sg_list_ptr,
                                                                  req_ops[k],
                                                                  SET_PORT_OP_TRUE));
            }
         }
      }

      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_ctrl_links; i++)
      {
         apm_module_ctrl_link_cfg_t *ctrl_link_ptr = cmd_gmgmt_ptr->cntr_port_hdl_list.ctrl_link_list_pptr[i];
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX
                "cu_handle_sg_mgmt_cmd: Ctrl link info Peer1 (inst-id, port-id): (0x%lx, 0x%lx), Peer2 (inst-id, "
                "port-id): (0x%lx, 0x%lx)",
                ctrl_link_ptr->peer_1_mod_iid,
                ctrl_link_ptr->peer_1_mod_ctrl_port_id,
                ctrl_link_ptr->peer_2_mod_iid,
                ctrl_link_ptr->peer_2_mod_ctrl_port_id);

         gu_ctrl_port_t *peer_1_ctrl_port_ptr = NULL;
         gu_ctrl_port_t *peer_2_ctrl_port_ptr = NULL;
         if (AR_EOK == gu_parse_ctrl_link(me_ptr->gu_ptr, ctrl_link_ptr, &peer_1_ctrl_port_ptr, &peer_2_ctrl_port_ptr))
         {
            bool_t              SET_PORT_OP_TRUE = TRUE;
            topo_sg_operation_t req_ops[]        = { TOPO_SG_OP_STOP, TOPO_SG_OP_DISCONNECT, TOPO_SG_OP_CLOSE };

            for (uint32_t k = 0; k < SIZE_OF_ARRAY(req_ops); k++)
            {
               TRY(result,
                   me_ptr->topo_vtbl_ptr->operate_on_int_ctrl_port(me_ptr->topo_ptr,
                                                                   peer_1_ctrl_port_ptr,
                                                                   sg_list_ptr,
                                                                   req_ops[k],
                                                                   SET_PORT_OP_TRUE));
               TRY(result,
                   me_ptr->topo_vtbl_ptr->operate_on_int_ctrl_port(me_ptr->topo_ptr,
                                                                   peer_2_ctrl_port_ptr,
                                                                   sg_list_ptr,
                                                                   req_ops[k],
                                                                   SET_PORT_OP_TRUE));
            }

            // Flush and destroy the internal buf queue.
            TRY(result, cu_deinit_internal_ctrl_port(me_ptr, peer_1_ctrl_port_ptr, FALSE /*b_skip_q_flush*/));
            TRY(result, cu_deinit_internal_ctrl_port(me_ptr, peer_2_ctrl_port_ptr, TRUE /*b_skip_q_flush*/));
         }
      }
   }

   if (!is_only_ctrl_port_cmd)
   {
      /* At prepare/stop/suspend, propagate port property such as RT/FTRT.
       * Upstream RT propagation should be done before cu_update_all_sg_port_states because for FTRT we cannot propagate
       * stop state.
       * Propagation across subgraph is done based on the sg_state. if self sg state state is stopped/suspended then
       * peer sg property is non real time. Propagation within container is not dependent on port state or data flow
       * state. Therefore this can be done before calling "cu_update_all_sg_port_states". Propagation across container
       * is dependent on port state (non real time if self external port is stopped/suspended). Therefore across
       * container propagation should be done after updating port states. (cu_update_all_sg_port_states)*/

      /* https://orbit/CR/3064615 Fix: updating upstream RT property in CLOSE context as well.
       * consider a case when an output port (upstream rt is true) moves from STOP -> OPEN -> CLOSE;
       * during OPEN when output port structure is created the rt flag will be false and if close happens immediately
       * after that then we won't propagate the stop state backwd and other internal ports will be updated to the
       * sg-state. To prevent this, we need to first update the rt flag on the output port which is being closed and
       * then propagate the state.
       */
      if (sg_ops & (TOPO_SG_OP_PREPARE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND | TOPO_SG_OP_CLOSE))
      {
         me_ptr->topo_vtbl_ptr->propagate_port_property(me_ptr->topo_ptr, PORT_PROPERTY_IS_UPSTREAM_RT);

         me_ptr->topo_vtbl_ptr->propagate_port_property(me_ptr->topo_ptr, PORT_PROPERTY_IS_DOWNSTREAM_RT);
      }
   }

   /**
    * Only after sg_ptr->state is updated, we can update port states for all SGs in the container.
    * (This is due to dependency of intra-container SGs).
    */
   if (TOPO_SG_STATE_INVALID != sg_state)
   {
      cu_update_all_sg_port_states(me_ptr, FALSE);

      // state prop and RT/FTRT is complete now. inform upstream or downstream such that propagation across container
      // happens
      // Even though it's possible to interleave RT/FTRT and state in one loop in cu_apply_downgraded_port_states
      // it's not done so. cu_apply_downgraded_port_states handled internal ports also.
      // Across container propagation should be done after updating port state.
      /* When upstream Stops, Peer-downstream starts propagating flushing EOS. At the same time upstream pushes property
       * as upstream Non-real time. This means that the flushing data is now non-real time always.
       */
      result |= cu_inform_downstream_about_upstream_property(me_ptr);
      result |= cu_inform_upstream_about_downstream_property(me_ptr);
   }

   if (me_ptr->cntr_vtbl_ptr->post_operate_on_ext_in_port)
   {
      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle; i++)
      {
         gu_ext_in_port_t *ext_in_port_ptr =
            (gu_ext_in_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.ip_port_handle_list_pptr[i];

         if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_in_port_ptr->sg_ptr->id))
         {
#if 0
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "post operate on ext in port. External input of module, port (0x%lx, 0x%lx)",
                ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->int_in_port_ptr->cmn.id);
#endif

            TRY(result,
                me_ptr->cntr_vtbl_ptr->post_operate_on_ext_in_port(me_ptr, sg_ops, &ext_in_port_ptr, sg_list_ptr));
         }
      }
   }

   if (me_ptr->cntr_vtbl_ptr->post_operate_on_ext_out_port)
   {
      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_op_port_handle; i++)
      {
         gu_ext_out_port_t *ext_out_port_ptr =
            (gu_ext_out_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.op_port_handle_list_pptr[i];

         if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_out_port_ptr->sg_ptr->id))
         {
#if 0
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "post operate on ext out port. External output of module, port (0x%lx, 0x%lx)",
                ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_out_port_ptr->int_out_port_ptr->cmn.id);
#endif

            TRY(result,
                me_ptr->cntr_vtbl_ptr->post_operate_on_ext_out_port(me_ptr, sg_ops, &ext_out_port_ptr, sg_list_ptr));
         }
      }
   }

   /** post operations (mainly DFG/EOS handling) */
   if (me_ptr->cntr_vtbl_ptr->post_operate_on_subgraph)
   {
      for (uint32_t i = 0; i < sg_list_ptr->num_sub_graph; i++)
      {
         uint32_t *sg_id_base_ptr = sg_list_ptr->sg_id_list_ptr;
         uint32_t  sg_id          = *(sg_id_base_ptr + i);

         gu_sg_t *sg_ptr = gu_find_subgraph(me_ptr->gu_ptr, sg_id);
         if (sg_ptr)
         {
            // CU_MSG(me_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "post operate on sg. sg_id (%08X)", sg_id);
            TRY(result, me_ptr->cntr_vtbl_ptr->post_operate_on_subgraph(me_ptr, sg_ops, sg_state, sg_ptr, sg_list_ptr));
         }
         else
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "post operate on sg. sg_id (%08X) not found", sg_id);
            // ignore this error. APM sends close if open had failed. in that case, we shouldn't error out.
            // if client sends close to unknown SG, then APM rejects. containers don't need to worry about that case.
         }
      }
   }

   if (sg_ops & (TOPO_SG_OP_START | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND))
   {
      cu_event_flags_t fwk_event_flags = { .word = 0 };
      fwk_event_flags.sg_state_change  = TRUE;

      bool_t prev_cntr_run_state = me_ptr->flags.is_cntr_started;

      me_ptr->flags.is_cntr_started = cu_is_atleast_one_sg_started(me_ptr);

      fwk_event_flags.cntr_run_state_change = (prev_cntr_run_state != me_ptr->flags.is_cntr_started) ? TRUE : FALSE;

      CU_SET_FWK_EVENT_FLAGS(me_ptr, fwk_event_flags);
   }
   else if (sg_ops & TOPO_SG_OP_CLOSE)
   {
      // if an external port is closing or an SG is closing then boundary module can start acting as a source or a sink
      // module. Setting this flag will ensure proper resource voting and topo-trigger to continue data processing.
      CU_SET_ONE_FWK_EVENT_FLAG(me_ptr, port_state_change);
   }
   else if (!is_only_ctrl_port_cmd && (sg_ops & (TOPO_SG_OP_PREPARE)))
   {
      cu_handle_prepare(me_ptr, cmd_gmgmt_ptr);
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t cu_handle_sg_mgmt_cmd_async(cu_base_t *me_ptr, uint32_t sg_ops, topo_sg_state_t sg_state)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spf_cntr_sub_graph_list_t *sg_list_ptr;
   spf_msg_cmd_graph_mgmt_t  *cmd_gmgmt_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;

   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;

   if (me_ptr->cntr_vtbl_ptr->operate_on_subgraph_async)
   {
      for (uint32_t i = 0; i < sg_list_ptr->num_sub_graph; i++)
      {
         uint32_t *sg_id_base_ptr = sg_list_ptr->sg_id_list_ptr;
         uint32_t  sg_id          = *(sg_id_base_ptr + i);

         gu_sg_t *sg_ptr = gu_find_subgraph(me_ptr->gu_ptr, sg_id);
         if (sg_ptr)
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "cu_handle_sg_mgmt_cmd_async. sg_id (%08X)", sg_id);
            TRY(result,
                me_ptr->cntr_vtbl_ptr->operate_on_subgraph_async(me_ptr, sg_ops, sg_state, sg_ptr, sg_list_ptr));
         }
         else
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "cu_handle_sg_mgmt_cmd. sg_id (%08X) not found", sg_id);
            // ignore this error. APM sends close if open had failed. in that case, we shouldn't error out.
            // if client sends close to unknown SG, then APM rejects. containers don't need to worry about that case.
         }
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

bool_t cu_is_disconnect_ext_in_port_needed(cu_base_t *base_ptr, gu_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t       result = AR_EOK;
   cu_base_t *       me_ptr = base_ptr;
   topo_port_state_t state;
   INIT_EXCEPTION_HANDLING

   me_ptr->topo_vtbl_ptr->get_port_property(me_ptr->topo_ptr,
                                            TOPO_DATA_INPUT_PORT_TYPE,
                                            PORT_PROPERTY_TOPO_STATE,
                                            (void *)ext_in_port_ptr->int_in_port_ptr,
                                            (uint32_t *)&state);

   // ensure either upstream stop or self stop before disconnecting. APM must stop ports before disconnecting.
   // if US SG is stopped, then connected_port_state stop, but if self SG is stopped, then topo_port_state_t is stop.
   //
   // difference b/w output port and input port is: in input side, US stop doesn't
   // necessarily change the input port state (as state is propagated in data path & other conditions may not be
   // met).
   // if self is stopped, then connected_port_state is not updated. hence need to check both.
   // As long as one is stopped, it's ok to disconnect.
   cu_ext_in_port_t *cu_ext_in_port_ptr =
      (cu_ext_in_port_t *)(((uint8_t *)ext_in_port_ptr) + me_ptr->ext_in_port_cu_offset);

   VERIFY(result,
          (cu_ext_in_port_ptr->connected_port_state != TOPO_PORT_STATE_STARTED) || (state != TOPO_PORT_STATE_STARTED));

   if (NULL != ext_in_port_ptr->upstream_handle.spf_handle_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "US Disconnection from (mod-inst-id, port-id): (0x%lx, 0x%lx)",
             ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
             ext_in_port_ptr->int_in_port_ptr->cmn.id);
      return TRUE;
   }
   else
   {
      // if SG is also getting in the cmd, then this is not an error (assume we are in graph cmd processing context)
      spf_msg_header_t *         header_ptr    = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;
      spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
      spf_cntr_sub_graph_list_t *sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_in_port_ptr->sg_ptr->id))
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CMD : Connection doesn't exist for (mod-inst-id, port-id): (0x%lx, 0x%lx)",
                ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->int_in_port_ptr->cmn.id);
      }
      return FALSE;
   }
   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "cu_is_disconnect_ext_in_port_needed failed result = %d",
             result);
   }
   return FALSE;
}

bool_t cu_is_disconnect_ext_out_port_needed(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t       result = AR_EOK;
   cu_base_t *       me_ptr = base_ptr;
   topo_port_state_t state;
   INIT_EXCEPTION_HANDLING

   me_ptr->topo_vtbl_ptr->get_port_property(me_ptr->topo_ptr,
                                            TOPO_DATA_OUTPUT_PORT_TYPE,
                                            PORT_PROPERTY_TOPO_STATE,
                                            (void *)ext_out_port_ptr->int_out_port_ptr,
                                            (uint32_t *)&state);

   // see comments in in-port loop
   cu_ext_out_port_t *cu_ext_out_port_ptr =
      (cu_ext_out_port_t *)(((uint8_t *)ext_out_port_ptr) + me_ptr->ext_out_port_cu_offset);

   VERIFY(result,
          ((cu_ext_out_port_ptr->connected_port_state != TOPO_PORT_STATE_STARTED) ||
           (state != TOPO_PORT_STATE_STARTED)));

   if (NULL != ext_out_port_ptr->downstream_handle.spf_handle_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "DS Disconnection from (mod-inst-id, port-id): (0x%lx, 0x%lx) ",
             ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             ext_out_port_ptr->int_out_port_ptr->cmn.id);

      return TRUE;
   }
   else
   {
      // if SG is also getting in the cmd, then this is not an error (assume we are in graph cmd processing context)
      spf_msg_header_t *         header_ptr    = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;
      spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
      spf_cntr_sub_graph_list_t *sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_out_port_ptr->sg_ptr->id))
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CMD : Connection doesn't exist for (mod-inst-id, port-id): (0x%lx, 0x%lx)",
                ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_out_port_ptr->int_out_port_ptr->cmn.id);
      }
      return FALSE;
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "cu_is_disconnect_ext_out_port_needed failed result = %d",
             result);
   }
   return FALSE;
}

bool_t cu_is_disconnect_ext_ctrl_port_needed(cu_base_t *base_ptr, gu_ext_ctrl_port_t *ext_ctrl_port_ptr)
{
   cu_base_t *me_ptr = base_ptr;

   if ((NULL != ext_ctrl_port_ptr->peer_handle.spf_handle_ptr) || (0 != ext_ctrl_port_ptr->peer_domain_id))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "Ctrl link Disconnection from (mod-inst-id, ctrl-port-id): (0x%lx, 0x%lx) ",
             ext_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id,
             ext_ctrl_port_ptr->int_ctrl_port_ptr->id);

      return TRUE;
   }
   else
   {
      // if SG is also getting in the cmd, then this is not an error (assume we are in graph cmd processing context)
      spf_msg_header_t *         header_ptr    = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;
      spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
      spf_cntr_sub_graph_list_t *sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;
      if (!gu_is_sg_id_found_in_spf_array(sg_list_ptr, ext_ctrl_port_ptr->sg_ptr->id))
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CMD :Ctrl link doesn't exist for (mod-inst-id, ctrl-port-id): (0x%lx, 0x%lx)",
                ext_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id,
                ext_ctrl_port_ptr->int_ctrl_port_ptr->id);
      }
      return FALSE;
   }
   return FALSE;
}

static ar_result_t cu_init_external_output_port(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION

   cu_ext_out_port_t *cu_ext_out_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);

   if (GU_STATUS_NEW == ext_out_port_ptr->gu_status)
   {
      switch (ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_id)
      {
         case MODULE_ID_RD_SHARED_MEM_EP:
         {
            cu_offload_ds_propagation_init(base_ptr, cu_ext_out_port_ptr);
            break;
         }
         default: // for all other modules, it's considered peer cntr
         {
            cu_peer_cntr_ds_propagation_init(base_ptr, cu_ext_out_port_ptr);
            break;
         }
      }

      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      result |= base_ptr->cntr_vtbl_ptr->init_ext_out_port(base_ptr, ext_out_port_ptr);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

      // NEW handling is done, so reset the status.
      ext_out_port_ptr->gu_status = GU_STATUS_DEFAULT;
   }
   return result;
}

static ar_result_t cu_init_external_input_port(cu_base_t *base_ptr, gu_ext_in_port_t *ext_in_port_ptr)
{
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t       result = AR_EOK;
   cu_ext_in_port_t *cu_ext_in_port_ptr =
      (cu_ext_in_port_t *)((uint8_t *)ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

   if (GU_STATUS_NEW == ext_in_port_ptr->gu_status)
   {
      switch (ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_id)
      {
         case MODULE_ID_WR_SHARED_MEM_EP:
         {
            cu_offload_us_propagation_init(base_ptr, cu_ext_in_port_ptr);
            break;
         }
         default: // for all other modules, it's considered peer cntr
         {
            cu_peer_cntr_us_propagation_init(base_ptr, cu_ext_in_port_ptr);
            break;
         }
      }

      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      result |= base_ptr->cntr_vtbl_ptr->init_ext_in_port(base_ptr, ext_in_port_ptr);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

      // NEW handling is done, so reset the status.
      ext_in_port_ptr->gu_status = GU_STATUS_DEFAULT;
   }
   return result;
}
/**
 * Checks all external data & control port new statuses and handle any non-default status.
 * For now, external port statuses only ever get changed to new. In this case, we need to
 * initialize the external port. After handling a non-default status, we change the
 * status back to default.
 *
 * This gets called immediately after gu_create_graph. GU will create the base structs
 * and indicate which graph structs are new by updating their status fields. This function
 * is responsible for initializing inhereted struct fields.
 *
 * The framework layer of the container is only responsible for external port handling.
 * other gu struct handling is taken care of by the topo layer.
 *
 */
ar_result_t cu_init_external_ports(cu_base_t *base_ptr,
                                   uint32_t   ext_in_queue_offset,
                                   uint32_t   ext_out_queue_offset,
                                   uint32_t   ctrl_ptr_queue_offset)
{
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t result = AR_EOK;

   VERIFY(result,
          base_ptr->cntr_vtbl_ptr && base_ptr->cntr_vtbl_ptr->init_ext_in_port &&
             base_ptr->cntr_vtbl_ptr->init_ext_out_port && base_ptr->cntr_vtbl_ptr->init_ext_ctrl_port);

   uint32_t self_proc_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&self_proc_domain_id);

   gu_t *gu_ptr = get_gu_ptr_for_current_command_context(base_ptr->gu_ptr);

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = gu_ptr->ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *ext_out_port_ptr = ext_out_port_list_ptr->ext_out_port_ptr;
      result |= cu_init_external_output_port(base_ptr, ext_out_port_ptr);
   }

   for (gu_ext_out_port_list_t *ipc_ext_out_port_list_ptr = gu_ptr->ipc_ext_out_port_list_ptr;
        ipc_ext_out_port_list_ptr;
        LIST_ADVANCE(ipc_ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *ipc_ext_out_port_ptr = ipc_ext_out_port_list_ptr->ext_out_port_ptr;
      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      result |= cu_ipc_tx_init_ext_out_port(base_ptr, ipc_ext_out_port_ptr, ext_out_queue_offset);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
   }

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *ext_in_port_ptr = ext_in_port_list_ptr->ext_in_port_ptr;
      result |= cu_init_external_input_port(base_ptr, ext_in_port_ptr);
   }

   for (gu_ext_in_port_list_t *ipc_ext_in_port_list_ptr = gu_ptr->ipc_ext_in_port_list_ptr; ipc_ext_in_port_list_ptr;
        LIST_ADVANCE(ipc_ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *ipc_ext_in_port_ptr = ipc_ext_in_port_list_ptr->ext_in_port_ptr;
      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      result |= cu_ipc_rx_init_ext_in_port(base_ptr, ipc_ext_in_port_ptr, ext_in_queue_offset);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
   }

   for (gu_ext_ctrl_port_list_t *ext_ctrl_port_list_ptr = gu_ptr->ext_ctrl_port_list_ptr;
        (NULL != ext_ctrl_port_list_ptr);
        LIST_ADVANCE(ext_ctrl_port_list_ptr))
   {
      gu_ext_ctrl_port_t *ext_ctrl_port_ptr = ext_ctrl_port_list_ptr->ext_ctrl_port_ptr;

      if (GU_STATUS_NEW == ext_ctrl_port_ptr->gu_status)
      {
         SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
         result |= base_ptr->cntr_vtbl_ptr->init_ext_ctrl_port(base_ptr, ext_ctrl_port_ptr, ctrl_ptr_queue_offset);
         SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

         // NEW handling is done, so reset the status.
         ext_ctrl_port_ptr->gu_status = GU_STATUS_DEFAULT;
      }
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }
   return result;
}

static void cu_deinit_ext_output_port(cu_base_t         *base_ptr,
                                      gu_ext_out_port_t *ext_out_port_ptr,
                                      bool_t             b_ignore_ports_from_sg_close,
                                      bool_t             force_deinit_all_ports)
{
   SPF_MANAGE_CRITICAL_SECTION
   bool_t b_deinit = force_deinit_all_ports;

   // port already deinited, skip
   if (!ext_out_port_ptr->int_out_port_ptr)
   {
      return;
   }

   if (!b_deinit &&
       (GU_STATUS_CLOSING == ext_out_port_ptr->gu_status || GU_STATUS_CLOSING == ext_out_port_ptr->sg_ptr->gu_status))
   {
      // If external port/SG is marked for the closing then deinit it.
      b_deinit = TRUE;

      // If subgraph is marked for the closing then deinit can be done later when subgraph is being destroyed.
      if (b_ignore_ports_from_sg_close && GU_STATUS_CLOSING == ext_out_port_ptr->sg_ptr->gu_status)
      {
         b_deinit = FALSE;
      }
   }

   if (b_deinit)
   {
      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      base_ptr->cntr_vtbl_ptr->deinit_ext_out_port(base_ptr, ext_out_port_ptr);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
   }
}

static void cu_deinit_ext_input_port(cu_base_t        *base_ptr,
                                     gu_ext_in_port_t *ext_in_port_ptr,
                                     bool_t            b_ignore_ports_from_sg_close,
                                     bool_t            force_deinit_all_ports)
{
   SPF_MANAGE_CRITICAL_SECTION
   bool_t b_deinit = force_deinit_all_ports;

   // port already deinited, skip
   if (!ext_in_port_ptr->int_in_port_ptr)
   {
      return;
   }

   if (!b_deinit &&
       (GU_STATUS_CLOSING == ext_in_port_ptr->gu_status || GU_STATUS_CLOSING == ext_in_port_ptr->sg_ptr->gu_status))
   {
      // If external port or SG is marked for the closing then deinit it.
      b_deinit = TRUE;

      // If subgraph is marked for the closing then deinit can be done later when subgraph is being destroyed.
      if (b_ignore_ports_from_sg_close && GU_STATUS_CLOSING == ext_in_port_ptr->sg_ptr->gu_status)
      {
         b_deinit = FALSE;
      }
   }

   if (b_deinit)
   {
      SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
      base_ptr->cntr_vtbl_ptr->deinit_ext_in_port(base_ptr, ext_in_port_ptr);
      SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
   }
}

/**
 * Deinit all external ports passed into the graph management command. This will get
 * called as part of close handling before calling gu_graph_destroy. gu_graph_destroy
 * will deallocate all graph struct memory, but for any deinitialization tasks related
 * to inherited structs, this function will take care of those tasks.
 *
 *
 * The framework layer of the container is only responsible for external port handling.
 * other gu struct handling is taken care of by the topo layer.
 */
void cu_deinit_external_ports(cu_base_t *base_ptr, bool_t b_ignore_ports_from_sg_close, bool_t force_deinit_all_ports)
{
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t result = AR_EOK;

   VERIFY(result,
          base_ptr->cntr_vtbl_ptr && base_ptr->cntr_vtbl_ptr->deinit_ext_in_port &&
             base_ptr->cntr_vtbl_ptr->deinit_ext_out_port);

   gu_t *gu_ptr = get_gu_ptr_for_current_command_context(base_ptr->gu_ptr);

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = gu_ptr->ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *ext_out_port_ptr = ext_out_port_list_ptr->ext_out_port_ptr;
      cu_deinit_ext_output_port(base_ptr, ext_out_port_ptr, b_ignore_ports_from_sg_close, force_deinit_all_ports);
   }

   for (gu_ext_out_port_list_t *ipc_ext_out_port_list_ptr = gu_ptr->ipc_ext_out_port_list_ptr;
        ipc_ext_out_port_list_ptr;
        LIST_ADVANCE(ipc_ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *ipc_ext_out_port_ptr = ipc_ext_out_port_list_ptr->ext_out_port_ptr;
      cu_deinit_ipc_tx_ext_out_port(base_ptr,
                                    ipc_ext_out_port_ptr,
                                    b_ignore_ports_from_sg_close,
                                    force_deinit_all_ports);
   }

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *ext_in_port_ptr = ext_in_port_list_ptr->ext_in_port_ptr;
      cu_deinit_ext_input_port(base_ptr, ext_in_port_ptr, b_ignore_ports_from_sg_close, force_deinit_all_ports);
   }

   for (gu_ext_in_port_list_t *ipc_ext_in_port_list_ptr = gu_ptr->ipc_ext_in_port_list_ptr; ipc_ext_in_port_list_ptr;
        LIST_ADVANCE(ipc_ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *ipc_ext_in_port_ptr = ipc_ext_in_port_list_ptr->ext_in_port_ptr;
      cu_deinit_ipc_rx_ext_in_port(base_ptr, ipc_ext_in_port_ptr, b_ignore_ports_from_sg_close, force_deinit_all_ports);
   }

   for (gu_ext_ctrl_port_list_t *ext_ctrl_port_list_ptr = gu_ptr->ext_ctrl_port_list_ptr;
        (NULL != ext_ctrl_port_list_ptr);
        LIST_ADVANCE(ext_ctrl_port_list_ptr))
   {
      gu_ext_ctrl_port_t *ext_ctrl_port_ptr = ext_ctrl_port_list_ptr->ext_ctrl_port_ptr;
      bool_t              b_deinit          = force_deinit_all_ports;

      // port already deinited, skip
      if (!ext_ctrl_port_ptr->int_ctrl_port_ptr)
      {
         continue;
      }

      if (!b_deinit && (GU_STATUS_CLOSING == ext_ctrl_port_ptr->gu_status ||
                        GU_STATUS_CLOSING == ext_ctrl_port_ptr->sg_ptr->gu_status))
      {
         // If external port or SG is marked for the closing then deinit it.
         b_deinit = TRUE;

         // If subgraph is marked for the closing then deinit can be done later when subgraph is being destroyed.
         if (b_ignore_ports_from_sg_close && GU_STATUS_CLOSING == ext_ctrl_port_ptr->sg_ptr->gu_status)
         {
            b_deinit = FALSE;
         }
      }

      if (b_deinit)
      {
         SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
         base_ptr->cntr_vtbl_ptr->deinit_ext_ctrl_port(base_ptr, ext_ctrl_port_ptr);
         SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
      }
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }
}

/**
 * Iterate through all modules in the container and apply the downgraded port state through container cb.
 * Container can handle external and internal port state changes in the call backs.
 * Eg: If its an external port container must update the channel masks and for internal ports container
 *     can set the port state on the capi module if needed.
 *
 * in case of RT/FTRT - we use set_propagated_prop_on_ext_output & set_propagated_prop_on_ext_input is used, while
 * propagating itself.
 * Reason for the difference: state/etc may propagate multiple times.
 */
static ar_result_t cu_apply_downgraded_port_states(cu_base_t *cu_ptr)
{
   ar_result_t result = AR_EOK;

   // Iterate through all sgs.
   for (gu_sg_list_t *sg_list_ptr = cu_ptr->gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      topo_sg_state_t sg_state = cu_ptr->topo_vtbl_ptr->get_sg_state(sg_list_ptr->sg_ptr);

      // don't need to apply downgraded state to STOP/SUSPENDED SGs, it will be handled in "operate_on_modules" function
      if (TOPO_SG_STATE_STOPPED == sg_state || TOPO_SG_STATE_SUSPENDED == sg_state)
      {
         continue;
      }

      /** Iterate through all module's output ports and apply port states to attached modules */
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *module_ptr = module_list_ptr->module_ptr;

         // IPC ports will not have any attached modules hence skipping here.
         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->output_port_list_ptr; (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gu_output_port_t *out_port_ptr = out_port_list_ptr->op_port_ptr;

            // Set the downgraded state to the attached module
            if (out_port_ptr->attached_module_ptr)
            {
               topo_port_state_t downgraded_out_port_state;

               cu_ptr->topo_vtbl_ptr->get_port_property(cu_ptr->topo_ptr,
                                                        TOPO_DATA_OUTPUT_PORT_TYPE,
                                                        PORT_PROPERTY_TOPO_STATE,
                                                        (void *)out_port_ptr,
                                                        (uint32_t *)&downgraded_out_port_state);
               cu_ptr->topo_vtbl_ptr
                  ->set_port_property(cu_ptr->topo_ptr,
                                      TOPO_DATA_INPUT_PORT_TYPE,
                                      PORT_PROPERTY_TOPO_STATE,
                                      out_port_ptr->attached_module_ptr->input_port_list_ptr->ip_port_ptr,
                                      downgraded_out_port_state);

               cu_ptr->topo_vtbl_ptr
                  ->set_port_property(cu_ptr->topo_ptr,
                                      TOPO_DATA_OUTPUT_PORT_TYPE,
                                      PORT_PROPERTY_TOPO_STATE,
                                      out_port_ptr->attached_module_ptr->output_port_list_ptr->op_port_ptr,
                                      downgraded_out_port_state);
            }
         }

         // note that IPC outputs will not have any logging module at it output port
      }

      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *module_ptr = module_list_ptr->module_ptr;

         /** Iterate through module's regular input ports and ipc port ports, and apply port states. */
         gu_input_port_list_t *in_port_lists[] = { module_ptr->input_port_list_ptr,
                                                   module_ptr->ipc_input_port_list_ptr };

         for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
         {
            for (gu_input_port_list_t *in_port_list_ptr = in_port_lists[i]; (NULL != in_port_list_ptr);
                 LIST_ADVANCE(in_port_list_ptr))
            {
               gu_input_port_t *in_port_ptr = in_port_list_ptr->ip_port_ptr;

               topo_port_state_t downgraded_in_port_state;
               cu_ptr->topo_vtbl_ptr->get_port_property(cu_ptr->topo_ptr,
                                                        TOPO_DATA_INPUT_PORT_TYPE,
                                                        PORT_PROPERTY_TOPO_STATE,
                                                        (void *)in_port_ptr,
                                                        (uint32_t *)&downgraded_in_port_state);

               CU_MSG(cu_ptr->gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "downgraded state: (mod-inst-id, port-id): (0x%lx, 0x%lx) port_state: %lu ",
                      in_port_ptr->cmn.module_ptr->module_instance_id,
                      in_port_ptr->cmn.id,
                      downgraded_in_port_state);

               if (TOPO_PORT_STATE_INVALID != downgraded_in_port_state &&
                   (cu_ptr->cntr_vtbl_ptr->apply_downgraded_state_on_input_port))
               {
                  cu_ptr->cntr_vtbl_ptr->apply_downgraded_state_on_input_port(cu_ptr,
                                                                              in_port_ptr,
                                                                              downgraded_in_port_state);
               }
            }
         }

         /** Iterate through module's output ports and apply port states to modules */
         gu_output_port_list_t *out_port_lists[] = { module_ptr->output_port_list_ptr,
                                                     module_ptr->ipc_output_port_list_ptr };

         for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
         {
            for (gu_output_port_list_t *out_port_list_ptr = out_port_lists[i]; (NULL != out_port_list_ptr);
                 LIST_ADVANCE(out_port_list_ptr))
            {
               gu_output_port_t *out_port_ptr = out_port_list_ptr->op_port_ptr;

               topo_port_state_t downgraded_out_port_state;

               cu_ptr->topo_vtbl_ptr->get_port_property(cu_ptr->topo_ptr,
                                                        TOPO_DATA_OUTPUT_PORT_TYPE,
                                                        PORT_PROPERTY_TOPO_STATE,
                                                        (void *)out_port_ptr,
                                                        (uint32_t *)&downgraded_out_port_state);

               CU_MSG(cu_ptr->gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "downgraded state: (mod-inst-id, port-id): (0x%lx, 0x%lx) port_state: %lu ",
                      out_port_ptr->cmn.module_ptr->module_instance_id,
                      out_port_ptr->cmn.id,
                      downgraded_out_port_state);

               if (TOPO_PORT_STATE_INVALID != downgraded_out_port_state &&
                   (cu_ptr->cntr_vtbl_ptr->apply_downgraded_state_on_output_port))
               {
                  cu_ptr->cntr_vtbl_ptr->apply_downgraded_state_on_output_port(cu_ptr,
                                                                               out_port_ptr,
                                                                               downgraded_out_port_state);
               }
            }
         }
      }
   }

   return result;
}

topo_port_state_t cu_evaluate_n_update_ext_out_ds_downgraded_port_state(cu_base_t *        me_ptr,
                                                                        gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   uint8_t *          temp_ptr         = (uint8_t *)gu_ext_out_port_ptr;
   cu_ext_out_port_t *ext_out_port_ptr = (cu_ext_out_port_t *)(temp_ptr + me_ptr->ext_out_port_cu_offset);

   topo_port_state_t connected_port_state = TOPO_PORT_STATE_INVALID;

   /* External port's state is downgraded with connected port state,
    * Which is the state of the connected port in peer container.
    */
   if (TOPO_PORT_STATE_INVALID != ext_out_port_ptr->propagated_port_state)
   {
      // It is also downgraded with propagated state from the downstream.
      connected_port_state =
         tu_get_downgraded_state(ext_out_port_ptr->connected_port_state, ext_out_port_ptr->propagated_port_state);
   }
   else
   {
      connected_port_state = ext_out_port_ptr->connected_port_state;
   }

   ext_out_port_ptr->downgraded_port_state = connected_port_state;

   return connected_port_state;
}

ar_result_t cu_deinit_ext_port_queue(cu_base_t *base_ptr, spf_handle_t *hdl_ptr, uint32_t bit_mask)
{
   if (hdl_ptr->q_ptr)
   {
      /*Release mask only in Buffer driven mode*/
      cu_release_bit_in_bit_mask(base_ptr, bit_mask);

      // We can clear without checking input or output or control because only one bit is set in bit_mask.
      cu_clear_bits_in_x(&base_ptr->all_ext_in_mask, bit_mask);
      cu_clear_bits_in_x(&base_ptr->all_ext_out_mask, bit_mask);

      /*deinit the queue */
      posal_queue_deinit(hdl_ptr->q_ptr);
      hdl_ptr->q_ptr = NULL;
   }

   return AR_EOK;
}
