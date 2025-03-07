/**
 * \file olc_rsp_handler.c
 * \brief
 *     This file contains olc functions for response handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_driver.h"
#include "olc_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */
/**
 * Error case handling of graph open. Destroys relevant subgraphs and acks back.
 */
void olc_handle_failure_at_graph_open(olc_t *                   me_ptr,
                                      spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                      ar_result_t               result,
                                      bool_t                    sg_open_state)
{
   /** destroy subgraphs one by one */
   if (open_cmd_ptr)
   {
      for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
      {
         gu_sg_t *sg_ptr = gu_find_subgraph(me_ptr->cu.gu_ptr, open_cmd_ptr->sg_cfg_list_pptr[i]->sub_graph_id);

         if (!sg_ptr)
         {
            continue;
         }
         else
         {
            sg_ptr->gu_status = GU_STATUS_CLOSING;
         }

         // olc_destroy_buffers_and_queues(me_ptr, &cmd_mgmt.c);
         // todo: might need a closer look
         // todo : we need to send close to satellite graphs opened
         gen_topo_destroy_modules(&me_ptr->topo, FALSE /*b_destroy_all_modules*/);

         // if this container has a port connected to another SG, then the port alone might be destroyed in this cmd.
         // Also, very important to call gen_topo_destroy_modules before cu_deinit_external_ports.
         // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
         // deinit.
         cu_deinit_external_ports(&me_ptr->cu,
                                  FALSE /*b_ignore_ports_from_sg_close*/,
                                  FALSE /*force_deinit_all_ports*/);

         gu_destroy_graph(&me_ptr->topo.gu, FALSE /*b_destroy_everything*/);
      }
   }

   spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
}

/**
 * Graph open handling after relaunching the thread. The main reason this is
 * needed (compared to all handling before relaunching the thread) is that
 * calibration might have new stack size requirements.
 */
static ar_result_t olc_handle_rest_of_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *                   me_ptr                = (olc_t *)base_ptr;
   spf_msg_cmd_graph_open_t *open_cmd_ptr          = NULL;
   bool_t                    pm_already_registered = posal_power_mgr_is_registered(me_ptr->cu.pm_info.pm_handle_ptr);

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   cu_reset_handle_rest(base_ptr);

   // in success case, ack to APM is sent from this func.
   TRY(result, gu_respond_to_graph_open(&me_ptr->topo.gu, &me_ptr->cu.cmd_msg, me_ptr->cu.heap_id));

   cu_register_with_pm(&me_ptr->cu, FALSE /* is_duty_cycling_allowed */);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      if (pm_already_registered)
      {
         cu_deregister_with_pm(&me_ptr->cu);
      }
      olc_handle_failure_at_graph_open(me_ptr, open_cmd_ptr, result, TRUE);
   }

   posal_memory_free(me_ptr->olc_core_graph_open_cmd_ptr);

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_OPEN: Done executing graph open command. "
           "current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return result;
}

ar_result_t olc_graph_set_get_packed_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info_ptr, void * sat_rsp_pkt_ptr)
{
   ar_result_t result     = AR_EOK;
   ar_result_t rsp_result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   olc_t *me_ptr = (olc_t *)base_ptr;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "GRAPH_MGMT: Executing graph set_get_cfg command."
           " current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   gpr_packet_t *packet_ptr           = (gpr_packet_t *)rsp_info_ptr->cmd_msg->payload_ptr;
   gpr_packet_t *get_cfg_resp_pkt_ptr = (gpr_packet_t *)sat_rsp_pkt_ptr;

   if (AR_EOK == rsp_info_ptr->rsp_result)
   {
      TRY(result,
          sgm_set_get_packed_cfg_rsp_update(&me_ptr->spgm_info,
                                            rsp_info_ptr->token,
                                            rsp_info_ptr->opcode,
                                            sat_rsp_pkt_ptr));
   }
   else
   {
      result = rsp_info_ptr->rsp_result;
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "GRAPH_MGMT:Done executing set_packed_cfg. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   if (!((get_cfg_resp_pkt_ptr) && (APM_CMD_RSP_GET_CFG == get_cfg_resp_pkt_ptr->opcode) && (AR_EOK == result)))
   {
      __gpr_cmd_end_command(packet_ptr, (result));
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "GRAPH_MGMT:Done executing get_packed_cfg. "
              "free packet result=0x%lx.",
              result);
      __gpr_cmd_free(packet_ptr);
   }

   return rsp_result;
}

/**
 * Handling of the control path graph set_ get configuration command.
 */
ar_result_t olc_graph_set_get_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   olc_t *                           me_ptr          = (olc_t *)base_ptr;
   spf_msg_cmd_param_data_cfg_t *    set_get_cfg_ptr = NULL;
   spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr    = NULL;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_MGMT: Executing graph set_get_cfg command."
           " current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   cmd_extn_ptr = (spgm_set_get_cfg_cmd_extn_info_t *)rsp_info->cmd_extn_info.extn_payload_ptr;

   if (0 == rsp_info->sec_opcode)
   {
      VERIFY(result,
             ((CFG_FOR_SATELLITE_ONLY == cmd_extn_ptr->cfg_destn_type) |
              (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)));

      if (NULL != cmd_extn_ptr->sat_cfg_cmd_ptr)
      {
         set_get_cfg_ptr = cmd_extn_ptr->sat_cfg_cmd_ptr;
         result = sgm_set_get_cfg_rsp_update(&me_ptr->spgm_info, set_get_cfg_ptr, rsp_info->token);
		 result |= rsp_info->rsp_result;
         cmd_extn_ptr->accu_result |= result;
         cmd_extn_ptr->pending_resp_counter--;
      }
      else
      {
         // error fatal
      }
   }
   else
   {
      VERIFY(result,
             ((CFG_FOR_CONTAINER_ONLY == cmd_extn_ptr->cfg_destn_type) |
              (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)));

      set_get_cfg_ptr = cmd_extn_ptr->cntr_cfg_cmd_ptr;
      if (AR_EOK == rsp_info->rsp_result)
      {
         result = sgm_cntr_set_cfg_rsp_update(&me_ptr->spgm_info, rsp_info->token);
      }
      else
      {
         result = rsp_info->rsp_result;
      }
      cmd_extn_ptr->accu_result |= result;
      cmd_extn_ptr->pending_resp_counter--;
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_MGMT::Done executing set_get_cfg. current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if ((cmd_extn_ptr) && (FALSE == cmd_extn_ptr->cmd_ack_done) && (AR_EOK != cmd_extn_ptr->accu_result))
   {
      result                     = spf_msg_ack_msg(rsp_info->cmd_msg, cmd_extn_ptr->accu_result);
      cmd_extn_ptr->cmd_ack_done = TRUE;
      me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   }

   if ((cmd_extn_ptr) && (0 == cmd_extn_ptr->pending_resp_counter))
   {
      if (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)
      {
         if (cmd_extn_ptr->cntr_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->cntr_cfg_cmd_ptr);
         }
         if (cmd_extn_ptr->sat_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->sat_cfg_cmd_ptr);
         }
      }
      if (FALSE == cmd_extn_ptr->cmd_ack_done)
      {
         result                     = spf_msg_ack_msg(rsp_info->cmd_msg, cmd_extn_ptr->accu_result);
         cmd_extn_ptr->cmd_ack_done = TRUE;
         me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
      }
      posal_memory_free(cmd_extn_ptr);
   }

   return result;
}

/**
 * Handling of the control path graph open command.
 */
ar_result_t olc_graph_open_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   olc_t *                   me_ptr          = (olc_t *)base_ptr;
   uint32_t                  stack_size      = 0;
   posal_thread_prio_t       thread_priority = 0;
   char_t                    thread_name[POSAL_DEFAULT_NAME_LEN];
   bool_t                    thread_launched       = FALSE;
   spf_msg_cmd_graph_open_t *open_cmd_ptr          = NULL;
   gu_ext_in_port_list_t *   ext_in_port_list_ptr  = NULL;
   gu_ext_out_port_list_t *  ext_out_port_list_ptr = NULL;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_OPEN: Executing graph open command response."
           " current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   me_ptr->cu.cmd_msg = *rsp_info->cmd_msg;

   VERIFY(result, (AR_EOK == rsp_info->rsp_result));

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)rsp_info->cmd_msg->payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_open_t));

   open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   TRY(result, sgm_register_satellite_module_with_gpr(&me_ptr->spgm_info, &me_ptr->cu.spf_handle));

   TRY(result, sgm_register_olc_module_with_gpr(&me_ptr->spgm_info));

   /* Allocate memory for voice info structure, for voice call use cases*/
   TRY(result, cu_create_voice_info(&me_ptr->cu, open_cmd_ptr));

   TRY(result, cu_init_external_ports(&me_ptr->cu, ALIGN_8_BYTES(sizeof(olc_ext_ctrl_port_t))));

   // Check if RT and set frame size
   for (ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      ext_in_port_ptr->is_realtime_usecase                                = FALSE;
      ext_in_port_ptr->cu.icb_info.flags.is_default_single_buffering_mode = TRUE;

      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "External input port idx = %ld miid = 0x%lx is_realtime_usecase set to %ld",
              ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
              ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
              ext_in_port_ptr->is_realtime_usecase);
   }

   // Check sg properties and derive frame size. We can skip this if there aren't any sgs in the
   // open command.
   if (open_cmd_ptr->num_sub_graphs)
   {
      olc_check_sg_cfg_for_frame_size(me_ptr, open_cmd_ptr);
   }

   for (ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      ext_out_port_ptr->is_realtime_usecase                                = FALSE;
      ext_out_port_ptr->cu.icb_info.flags.is_default_single_buffering_mode = TRUE;

      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "External output port idx = %ld miid = 0x%lx is_realtime_usecase set to %ld",
              ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
              ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
              ext_out_port_ptr->is_realtime_usecase);
   }

   TRY(result,
       olc_prepare_to_launch_thread(me_ptr, &stack_size, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN));

   TRY(result, cu_check_launch_thread(&me_ptr->cu, stack_size, 0, thread_priority, thread_name, &thread_launched));

   if (thread_launched)
   {
      me_ptr->cu.handle_rest_fn      = olc_handle_rest_of_graph_open;
      me_ptr->cu.handle_rest_ctx_ptr = NULL;
      return AR_EOK;
   }

   return olc_handle_rest_of_graph_open(&me_ptr->cu, NULL /*ctx_ptr*/);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      olc_handle_failure_at_graph_open(me_ptr, open_cmd_ptr, result, TRUE);
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph prepare command.
 */
ar_result_t olc_graph_prepare_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)base_ptr;

   result = rsp_info->rsp_result;

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:Prepare Graph:Done executing prepare graph. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(rsp_info->cmd_msg, result);
}

/**
 * Handling of the control path graph start command.
 */
ar_result_t olc_graph_start_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr                = (olc_t *)base_ptr;
   uint32_t log_id                = me_ptr->topo.gu.log_id;
   bool_t   FORCE_AGGREGATE_FALSE = FALSE;

   result = rsp_info->rsp_result;

   VERIFY(result, AR_EOK == rsp_info->rsp_result);

   bool_t is_cntr_already_running   = me_ptr->cu.flags.is_cntr_started;
   me_ptr->cu.flags.is_cntr_started = TRUE;

   // if cntr was not already running, and now it started to run
   CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, sg_state_change);
   if (!is_cntr_already_running)
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, cntr_run_state_change);
   }

   olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);

   // OLC_CA

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }


   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:START:Done executing start command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(rsp_info->cmd_msg, result);
}

ar_result_t olc_graph_set_persistent_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info_ptr)
{
   ar_result_t   result     = rsp_info_ptr->rsp_result;
   olc_t *       me_ptr     = (olc_t *)base_ptr;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)rsp_info_ptr->cmd_msg->payload_ptr;

   result = rsp_info_ptr->rsp_result;
   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:REGISTER/SHARED_REGISTER_CFG:Done executing persistent set cfg command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return __gpr_cmd_end_command(packet_ptr, result);
}

ar_result_t olc_graph_set_persistent_packed_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)base_ptr;
   spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr           = NULL;

   cmd_extn_ptr = (spgm_set_get_cfg_cmd_extn_info_t *)rsp_info_ptr->cmd_extn_info.extn_payload_ptr;

   if (0 == rsp_info_ptr->sec_opcode)
   {
      VERIFY(result,
             ((CFG_FOR_SATELLITE_ONLY == cmd_extn_ptr->cfg_destn_type) |
              (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)));

      if (NULL != cmd_extn_ptr->sat_cfg_cmd_ptr)
      {
         result |= rsp_info_ptr->rsp_result;
         cmd_extn_ptr->accu_result |= result;
         cmd_extn_ptr->pending_resp_counter--;
      }
   }

   result = rsp_info_ptr->rsp_result;
   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:REGISTER/SHARED_REGISTER_CFG:Done executing persistent set cfg command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if ((cmd_extn_ptr) && (FALSE == cmd_extn_ptr->cmd_ack_done) && (AR_EOK != cmd_extn_ptr->accu_result))
   {
      result                     = spf_msg_ack_msg(rsp_info_ptr->cmd_msg, cmd_extn_ptr->accu_result);
      cmd_extn_ptr->cmd_ack_done = TRUE;
      me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   }

   if ((cmd_extn_ptr) && (0 == cmd_extn_ptr->pending_resp_counter))
   {
      if (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)
      {
         if (cmd_extn_ptr->cntr_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->cntr_cfg_cmd_ptr);
         }
         if (cmd_extn_ptr->sat_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->sat_cfg_cmd_ptr);
         }
      }
      if (FALSE == cmd_extn_ptr->cmd_ack_done)
      {
         result                     = spf_msg_ack_msg(rsp_info_ptr->cmd_msg, cmd_extn_ptr->accu_result);
         cmd_extn_ptr->cmd_ack_done = TRUE;
         me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
      }
      posal_memory_free(cmd_extn_ptr);
   }

   return result;
}

ar_result_t olc_graph_event_reg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info_ptr)
{
   ar_result_t   result     = rsp_info_ptr->rsp_result;
   olc_t *       me_ptr     = (olc_t *)base_ptr;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)rsp_info_ptr->cmd_msg->payload_ptr;

   result = rsp_info_ptr->rsp_result;
   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:APM_CMD_REGISTER_MODULE_EVENTS:Done "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return __gpr_cmd_end_command(packet_ptr, result);
}

/**
 * Handling of the control path graph suspend command.
 */
ar_result_t olc_graph_suspend_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result                = AR_EOK;
   olc_t *     me_ptr                = (olc_t *)base_ptr;
   uint32_t    log_id                = me_ptr->topo.gu.log_id;
   bool_t      FORCE_AGGREGATE_FALSE = FALSE;

   result = rsp_info->rsp_result;

   olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);
   CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, sg_state_change);
   if (!me_ptr->cu.flags.is_cntr_started)
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, cntr_run_state_change);
   }

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:SUSPEND:Done Executing suspend command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(rsp_info->cmd_msg, result);
}

/**
 * Handling of the control path graph stop command.
 */
ar_result_t olc_graph_stop_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result                = AR_EOK;
   olc_t *     me_ptr                = (olc_t *)base_ptr;
   uint32_t    log_id                = me_ptr->topo.gu.log_id;
   bool_t      FORCE_AGGREGATE_FALSE = FALSE;

   result = rsp_info->rsp_result;

   olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);

   // TODO: No need to mark this event if the SG is stopped already. for other cntrs flags
   // are set in cu_state_handler.c need to check if that applies to OLC as well
   CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, sg_state_change);
   if (!me_ptr->cu.flags.is_cntr_started)
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, cntr_run_state_change);
   }

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:STOP:Done Executing stop command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(rsp_info->cmd_msg, result);
}

/**
 * Handling of the control path graph flush command.
 */
ar_result_t olc_graph_flush_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)base_ptr;

   result = rsp_info->rsp_result;
   olc_post_operate_flush(base_ptr, rsp_info->cmd_msg);
   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:FLUSH:Done Executing flush command. current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(rsp_info->cmd_msg, result);
}

/**
 * Handling of the control path graph close command.
 */
ar_result_t olc_graph_close_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                result                = AR_EOK;
   olc_t *                    me_ptr                = (olc_t *)base_ptr;
   bool_t                     ret_terminated        = FALSE;
   uint32_t                   log_id                = me_ptr->topo.gu.log_id;
   spf_msg_t                  cmd_msg               = *(rsp_info->cmd_msg);
   uint32_t                   sub_graph_id          = 0;
   bool_t                     FORCE_AGGREGATE_FALSE = FALSE;
   spf_cntr_sub_graph_list_t *sg_list_ptr           = NULL;
   spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr         = NULL;
   spf_msg_header_t *         header_ptr            = NULL;

   header_ptr = (spf_msg_header_t *)rsp_info->cmd_msg->payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;

   sg_list_ptr = &cmd_gmgmt_ptr->sg_id_list;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CLOSE:Executing close Command response, "
           "current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   // module destroy happens only with SG destroy
   gen_topo_destroy_modules(&me_ptr->topo, FALSE /*b_destroy_all_modules*/);

   // If this container has a port connected to another SG, then the port alone might be destroyed in this cmd.
   // Also, very important to call gen_topo_destroy_modules before cu_deinit_external_ports.
   // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
   // deinit.
   cu_deinit_external_ports(&me_ptr->cu, FALSE /*b_ignore_ports_from_sg_close*/, FALSE /*force_deinit_all_ports*/);

   gu_destroy_graph(me_ptr->cu.gu_ptr, FALSE /*b_destroy_everything*/);

   TRY(result, olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));                              // OLC_CA
   TRY(result, olc_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, FORCE_AGGREGATE_FALSE, NULL, NULL)); // OLC_CA

   for (uint32_t sg_arr_index = 0; sg_arr_index < sg_list_ptr->num_sub_graph; sg_arr_index++)
   {
      sub_graph_id = sg_list_ptr->sg_id_list_ptr[sg_arr_index];
      sgm_deregister_satellite_module_with_gpr(&me_ptr->spgm_info, sub_graph_id);
      sgm_deregister_olc_module_with_gpr(&me_ptr->spgm_info, sub_graph_id);
   }

   me_ptr->cu.curr_chan_mask |= (OLC_CMD_BIT_MASK);
   // check if any subgraph is pending, if not, destroy this container
   if (!me_ptr->cu.gu_ptr->num_subgraphs)
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "As number of subgraphs is zero, "
              "destroying this container");

      olc_destroy(me_ptr);
      me_ptr         = NULL;
      ret_terminated = TRUE;
   }

   // Catch here so we don't print an error on AR_ETERMINATED.
   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (ret_terminated && AR_EOK == result)
   {
      result = AR_ETERMINATED;
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CLOSE:Done Executing close command. current channel mask=0x%x. result=0x%lx.",
           me_ptr ? me_ptr->cu.curr_chan_mask : 0,
           result);

   spf_msg_ack_msg(&cmd_msg, result); // don't overwrite result as it might be AR_ETERMINATED

   return result;
}
