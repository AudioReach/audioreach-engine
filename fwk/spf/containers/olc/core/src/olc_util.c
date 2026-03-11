/**
 * \file olc_util.c
 * \brief
 *     This file contains utility functions for OLC
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_i.h"

/* =======================================================================
   Macro definitions
========================================================================== */
/* Minimum stack Size required by the OLC container*/
// TODO: to profile these and tune
#ifdef PROD_SPECIFIC_MAX_CH
static const uint32_t OLC_PROCESS_STACK_SIZE = 3072;// additional requirement based on profiling
#else
static const uint32_t OLC_PROCESS_STACK_SIZE = 2048;
#endif
static const uint32_t OLC_BASE_STACK_SIZE    = 4096;

/* =======================================================================
Static Function Declarations.
========================================================================== */

/* =======================================================================
Static Function Definitions
========================================================================== */
/**
 * me_ptr - OLC instance pointer
 * Stack size - input: Stack size decided by IPC module and container requirements.
 *            - output: Stack size decided based on comparing client given size, capi given
 *              size, etc.
 */
ar_result_t olc_get_thread_stack_size(olc_t *me_ptr, uint32_t *stack_size)
{
   *stack_size = MAX(me_ptr->cu.configured_stack_size, *stack_size);
   *stack_size = MAX(OLC_BASE_STACK_SIZE, *stack_size);
   *stack_size += OLC_PROCESS_STACK_SIZE;

   // Check this after adding the OLC_PROCESS_STACK_SIZE to the stack_size
   // to prevent multiple addition during relaunch
   *stack_size = MAX(me_ptr->cu.actual_stack_size, *stack_size);

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "Stack sizes: Configured %lu, actual %lu, final %lu",
           me_ptr->cu.configured_stack_size,
           me_ptr->cu.actual_stack_size,
           *stack_size);

   return AR_EOK;
}

/**
 * Get the thread priority that the olc should be running at. Thread priority is primarily
 * based on the operating frame size since this determines the deadline period while processing
 * data.
 */
ar_result_t olc_get_set_thread_priority(olc_t *me_ptr, int32_t *priority_ptr, bool_t should_set)
{
   ar_result_t         result       = AR_EOK;
   bool_t              is_real_time = FALSE;
   posal_thread_prio_t curr_prio    = posal_thread_prio_get();
   posal_thread_prio_t new_prio     = curr_prio, new_prio1;


   if (!me_ptr->cu.flags.is_cntr_started)
   {
      new_prio = posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID);
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Since no sgs are started returning floor thread priority.");
   }
   else
   {
      if ((FRAME_SIZE_DONT_CARE == me_ptr->cu.cntr_proc_duration) || (0 == me_ptr->cu.cntr_proc_duration))
      {
         new_prio = posal_thread_prio_get();
      }
      else
      {
         is_real_time = cu_is_realtime(&me_ptr->cu);
         if (!is_real_time)
         {
            new_prio = posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID);
         }
         else // rt
         {
            prio_query_t query_tbl;
            query_tbl.frame_duration_us = me_ptr->cu.cntr_proc_duration;
            query_tbl.static_req_id     = SPF_THREAD_DYN_ID;
            query_tbl.is_interrupt_trig = FALSE;
            result                      = posal_thread_calc_prio(&query_tbl, &new_prio);
            if (AR_DID_FAIL(result))
            {
               OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Failed to get OLC thread priority");
               return result;
            }
         }
      }
   }

   new_prio1 = new_prio;
   /**
    * If container prio is configured, then it is used independent of whether container is started, or
    * running commands during data processing or if it's FTRT or if its frame size is not known or
    * if a module changes container priority.
    */
   if (APM_CONT_PRIO_IGNORE != me_ptr->cu.configured_thread_prio)
   {
      new_prio = me_ptr->cu.configured_thread_prio;
   }

   if (curr_prio != new_prio)
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              SPF_LOG_PREFIX "OLC thread priority %d (larger is higher prio), period_us %lu us, frame len %lu us, proc "
              "duration %lu us, is real time %u, cntr started %u",
              new_prio,
              me_ptr->cu.period_us,
              me_ptr->cu.cntr_frame_len.frame_len_us,
              me_ptr->cu.cntr_proc_duration,
              is_real_time,
              me_ptr->cu.flags.is_cntr_started);

      if (new_prio1 != new_prio)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning: thread priority: configured %d prio overrides internal logic %d", me_ptr->cu.configured_thread_prio, new_prio1);
      }

      SET_IF_NOT_NULL(priority_ptr, new_prio);

      if (should_set)
      {
         posal_thread_prio_t prio = (posal_thread_prio_t)new_prio;
         posal_thread_set_prio(prio);
      }
   }

   return AR_EOK;
}

void olc_set_thread_priority(olc_t *me_ptr, uint32_t period_in_us)
{
   posal_thread_prio_t priority = 0;
   olc_get_set_thread_priority(me_ptr, &priority, TRUE);
   posal_thread_set_prio(priority);
}

ar_result_t olc_ext_in_port_int_reset(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{

   ar_result_t result = AR_EOK;

   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   ext_in_port_ptr->flags.eof                 = FALSE;
   ext_in_port_ptr->flags.flushing_eos        = FALSE;
   ext_in_port_ptr->flags.input_discontinuity = FALSE;
   ext_in_port_ptr->flags.pending_mf          = FALSE;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "External input reset Module 0x%lX, Port 0x%lx",
           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
           in_port_ptr->gu.cmn.id);

   return result;
}

ar_result_t olc_ext_in_port_reset(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   spf_list_merge_lists((spf_list_node_t **)&in_port_ptr->common.sdata.metadata_list_ptr,
                        (spf_list_node_t **)&ext_in_port_ptr->md_list_ptr);

   gen_topo_reset_input_port(&me_ptr->topo, in_port_ptr);

   olc_ext_in_port_int_reset(me_ptr, ext_in_port_ptr);

   return result;
}

static ar_result_t olc_ext_out_port_int_reset(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = FALSE;

   if (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      olc_return_back_out_buf(me_ptr, ext_out_port_ptr);
   }

   ext_out_port_ptr->buf.data_ptr        = NULL;
   ext_out_port_ptr->buf.actual_data_len = 0;
   ext_out_port_ptr->buf.max_data_len    = 0;

   // memset(&ext_out_port_ptr->next_out_buf_ts, 0, sizeof(ext_out_port_ptr->next_out_buf_ts)); // OLC_CA

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "External output reset Module 0x%lX, Port 0x%lx",
           out_port_ptr->gu.cmn.module_ptr->module_instance_id,
           out_port_ptr->gu.cmn.id);

   return result;
}

ar_result_t olc_ext_out_port_basic_reset(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   topo_basic_reset_output_port(&me_ptr->topo, out_port_ptr, TRUE);

   olc_ext_out_port_int_reset(me_ptr, ext_out_port_ptr);

   return result;
}

ar_result_t olc_ext_out_port_reset(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   // copy EOS & other data to the internal port such that gen_topo_reset_output_port frees up EoS. This is done for
   // code-reuse only. Below method of merging is used because ext out port has old metadata compared to the one in
   // out_port_ptr.
   spf_list_merge_lists((spf_list_node_t **)&ext_out_port_ptr->md_list_ptr,
                        (spf_list_node_t **)&out_port_ptr->common.sdata.metadata_list_ptr);

   out_port_ptr->common.sdata.metadata_list_ptr = ext_out_port_ptr->md_list_ptr;
   ext_out_port_ptr->md_list_ptr                = NULL;
   gen_topo_reset_output_port(&me_ptr->topo, out_port_ptr);

   olc_ext_out_port_int_reset(me_ptr, ext_out_port_ptr);

   return result;
}


// Topo to cntr call back to handle propagation at external output port.
// If the propagated property is is_upstrm_rt, cmd is sent to downstream cntr.

// note: in case of port state property apply_downgraded_state_on_output_port is used
ar_result_t olc_set_propagated_prop_on_ext_output(gen_topo_t *              topo_ptr,
                                                  gu_ext_out_port_t *       gu_out_port_ptr,
                                                  topo_port_property_type_t prop_type,
                                                  void *                    payload_ptr)
{
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);

   olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)gu_out_port_ptr;

   if (PORT_PROPERTY_IS_UPSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;

      if (*is_rt_ptr != ext_out_port_ptr->cu.icb_info.flags.is_real_time)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_HIGH_PRIO,
                 "Propagating to peer: from ext output (0x%lX,%lu) forward prop upstream-real-time, prop_value %u, "
                 "prev value %u",
                 gu_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                 gu_out_port_ptr->int_out_port_ptr->cmn.index,
                 *is_rt_ptr,
                 ext_out_port_ptr->cu.icb_info.flags.is_real_time);

         ext_out_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         // For ICB: forward prop from this containers input (or RT module) to output
         olc_recreate_ext_out_buffers((void *)&me_ptr->cu, gu_out_port_ptr);

         // downstream message is sent at the end
         // cu_inform_downstream_about_upstream_property
      }
   }

   return result;
}

// Handle property propagation from downstream
// Propagates the property update through the topology.
// note: in case of port state property apply_downgraded_state_on_input_port is used
ar_result_t olc_set_propagated_prop_on_ext_input(gen_topo_t *              topo_ptr,
                                                 gu_ext_in_port_t *        gu_in_port_ptr,
                                                 topo_port_property_type_t prop_type,
                                                 void *                    payload_ptr)
{
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);

   olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)gu_in_port_ptr;

   if (PORT_PROPERTY_IS_DOWNSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;
      if (*is_rt_ptr != ext_in_port_ptr->cu.icb_info.flags.is_real_time)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_HIGH_PRIO,
                 "Propagating to peer: from ext input (0x%lX,%lu) backward prop downstream-real-time, "
                 "prop_value=%u, prev value %u",
                 gu_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                 gu_in_port_ptr->int_in_port_ptr->cmn.id,
                 *is_rt_ptr,
                 ext_in_port_ptr->cu.icb_info.flags.is_real_time);

         ext_in_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         // ICB: here we only need to store the value to see diff.
         // upon receiving this message upstream will recreate ext buf

         // upstream message is sent later at the end
         // cu_inform_upstream_about_downstream_property
      }
   }

   return result;
}

/**
 * Static function to derive the container frame size
 */
static uint32_t olc_get_frame_size(olc_t *me_ptr, uint32_t sg_perf_mode)
{
   uint32_t frame_size_ms = FRAME_SIZE_5_MS;

   if (APM_SG_PERF_MODE_LOW_LATENCY == sg_perf_mode)
   {
      frame_size_ms = FRAME_SIZE_1_MS;
   }
   else if (APM_SG_PERF_MODE_LOW_POWER == sg_perf_mode)
   {
      frame_size_ms = FRAME_SIZE_5_MS;
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "CMD:GRAPH_OPEN:Unsupported perf mode %d using default frame size %d",
              sg_perf_mode,
              FRAME_SIZE_5_MS);
   }

   return frame_size_ms;
}

/**
 * Choose the frame size based on perf mode of all subgraphs.
 */
ar_result_t olc_check_sg_cfg_for_frame_size(olc_t *me_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result                    = AR_EOK;
   uint32_t    sg_perf_mode              = 0;
   gu_sg_t *   sg_ptr                    = NULL;
   uint32_t    new_configured_frame_size = 0;

   // Check perf mode for all subgraphs in given open cmd
   for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
   {
      apm_sub_graph_cfg_t *sg_cmd_ptr = open_cmd_ptr->sg_cfg_list_pptr[i];

      sg_ptr = gu_find_subgraph(&me_ptr->topo.gu, sg_cmd_ptr->sub_graph_id);
      VERIFY(result, sg_ptr);

      /* If 0 -> perf mode not been set before, if it is low power perf mode always pick it
      because it implies higher frame size */
      if ((0 == sg_perf_mode) || (APM_SG_PERF_MODE_LOW_LATENCY == sg_ptr->perf_mode))
      {
         sg_perf_mode = sg_ptr->perf_mode;
      }
   }

   new_configured_frame_size = olc_get_frame_size(me_ptr, sg_perf_mode) * 1000;

   if ((0 == me_ptr->configured_frame_size_us) ||
       ((0 < new_configured_frame_size) && (me_ptr->configured_frame_size_us > new_configured_frame_size)))
   {

      me_ptr->configured_frame_size_us = new_configured_frame_size;

      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_MED_PRIO,
              "CMD:GRAPH_OPEN: OLC configured frame size is now %ld us.",
              me_ptr->configured_frame_size_us);

      // Configured frame length changing could change the container frame length if the configured frame length
      // is larger than the aggregated threshold. Frame length aggregation and handling if the frame length
      // changes is therefore needed.
      olc_handle_port_data_thresh_change_event(me_ptr);
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return result;
}

bool_t olc_is_stm(cu_base_t *base_ptr)
{
   olc_t *me_ptr = (olc_t *)base_ptr;
   if (me_ptr->topo.flags.is_signal_triggered)
   {
      return TRUE;
   }
   return FALSE;
}

/*
 * Handles downgraded port START/STOP if necessary and updates state in topo layer.
 * This dowgraded state is based on,
 *  1. Sub graph state 2. Peer sub graph state 3. Propagated state.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t olc_apply_downgraded_state_on_input_port(cu_base_t *       cu_ptr,
                                                     gu_input_port_t * gu_in_port_ptr,
                                                     topo_port_state_t downgraded_state)
{
   ar_result_t            result          = AR_EOK;
   olc_t *                me_ptr          = (olc_t *)cu_ptr;
   olc_ext_in_port_t *    ext_in_port_ptr = NULL;
   gen_topo_input_port_t *in_port_ptr     = NULL;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != gu_in_port_ptr));

   // If its an external port apply the downgraded state on ext port.
   // NOTE: as an optimization we can apply the port state only if there is a change.
   if (gu_in_port_ptr->ext_in_port_ptr)
   {
      ext_in_port_ptr = (olc_ext_in_port_t *)gu_in_port_ptr->ext_in_port_ptr;

      // Input port can be flushed only if the connected peer port state is STOPPED. Else it may result in infinite
      // loop. Ideally upstream is stopped first and then downstream to avoid infinite loops.
      //
      // But, due to state propagation the external input can be stopped if ext output stop in the container is
      // propagated backwards. In this case the upstream peer container is still in started state and input should
      // not be flushed to prevent infinite loop.
      // Message to upstream is sent in cu_inform_upstream_about_downstream_property
      if ((ext_in_port_ptr->cu.connected_port_state == TOPO_PORT_STATE_STOPPED) &&
          (((TOPO_PORT_STATE_STOPPED) == downgraded_state) || (TOPO_PORT_STATE_SUSPENDED == downgraded_state)))
      {
         olc_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */, FALSE, FALSE);
      }

      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state)
      {
         olc_ext_in_port_reset(me_ptr, ext_in_port_ptr);
      }

      // stop listening to input if port is suspended/stopped
      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state || (TOPO_PORT_STATE_SUSPENDED) == downgraded_state)
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
         if (ext_in_port_ptr->wdp_ctrl_cfg_ptr)
         {
            cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sat_rw_bit_mask);
         }
      }
   } // end of ext port handling

   // Reset input port, if stopped
   //if (TOPO_PORT_STATE_STOPPED == downgraded_state)
   //{
   //   gen_topo_reset_input_port(&me_ptr->topo, (void *)gu_in_port_ptr);
   //}

   // Apply port state on the internal port.
   in_port_ptr = (gen_topo_input_port_t *)gu_in_port_ptr;

   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &in_port_ptr->common.last_issued_opcode,
                                                      TRUE, // is_input
                                                      in_port_ptr->gu.cmn.index,
                                                      in_port_ptr->gu.cmn.id);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/*
 * Handles port START/STOP if necessary and updates state in topo layer.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t olc_apply_downgraded_state_on_output_port(cu_base_t *       cu_ptr,
                                                      gu_output_port_t *gu_out_port_ptr,
                                                      topo_port_state_t downgraded_state)
{
   ar_result_t             result           = AR_EOK;
   olc_t *                 me_ptr           = (olc_t *)cu_ptr;
   olc_ext_out_port_t *    ext_out_port_ptr = NULL;
   gen_topo_output_port_t *out_port_ptr     = NULL;
   INIT_EXCEPTION_HANDLING
   VERIFY(result, (NULL != gu_out_port_ptr));

   // If its an external port apply the downgraded state on ext output port.
   // NOTE: as an optimization we can apply the port state only if there is a change.
   if (gu_out_port_ptr->ext_out_port_ptr)
   {
      ext_out_port_ptr = (olc_ext_out_port_t *)gu_out_port_ptr->ext_out_port_ptr;

      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state || (TOPO_PORT_STATE_SUSPENDED) == downgraded_state)
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
         if (ext_out_port_ptr->rdp_ctrl_cfg_ptr)
         {
            cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sat_rw_bit_mask);
         }
      }

      if ((ext_out_port_ptr->cu.connected_port_state == TOPO_PORT_STATE_STOPPED) &&
          (TOPO_PORT_STATE_STOPPED == downgraded_state))
      {
         olc_flush_output_data_queue(me_ptr, ext_out_port_ptr, FALSE);
      }

      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state)
      {
         (void)olc_ext_out_port_reset(me_ptr, ext_out_port_ptr);
      }

   } // end of ext port handling

   // Apply port state on the internal port.
   out_port_ptr = (gen_topo_output_port_t *)gu_out_port_ptr;

   //if (TOPO_PORT_STATE_STOPPED == downgraded_state)
   //{
   //   gen_topo_reset_output_port(&me_ptr->topo, (void *)gu_out_port_ptr);
   //}

   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &out_port_ptr->common.last_issued_opcode,
                                                      FALSE, // is_input
                                                      out_port_ptr->gu.cmn.index,
                                                      out_port_ptr->gu.cmn.id);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * this is callback from topo
 */
ar_result_t olc_destroy_module(gen_topo_t *       topo_ptr,
                               gen_topo_module_t *module_ptr,
                               bool_t             reset_capi_dependent_dont_destroy)
{
   ar_result_t   result         = AR_EOK;
   olc_module_t *olc_module_ptr = (olc_module_t *)module_ptr;

   // for all modules delete cu event list
   cu_delete_all_event_nodes(&olc_module_ptr->cu.event_list_ptr);

   return result;
}

/**
 * this is callback from topo
 */
ar_result_t olc_create_module(gen_topo_t *           topo_ptr,
                              gen_topo_module_t *    module_ptr,
                              gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *me_ptr = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);

   if (AMDB_MODULE_TYPE_FRAMEWORK == module_ptr->gu.module_type)
   {
      switch (module_ptr->gu.module_id)
      {
         case MODULE_ID_WR_SHARED_MEM_CLIENT:
         {
            VERIFY(result, 1 == module_ptr->gu.max_input_ports);
            // VERIFY(result, 1 == module_ptr->gu.max_output_ports);

            if (1 == module_ptr->gu.num_input_ports && 1 == module_ptr->gu.num_output_ports)
            {
               /* Make sure that the only in port of this module is also an external port.*/
               VERIFY(result, NULL != module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr);

               module_ptr->flags.inplace         = TRUE;
               module_ptr->flags.dynamic_inplace = TRUE;
               gen_topo_input_port_t *input_port_ptr =
                  (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
               input_port_ptr->common.flags.port_has_threshold = FALSE;
               // requires data buffering is FALSE by default
            }

            // For the RD/WR Client modules, the GPR callback registration is done by the container
            break;
         }
         case MODULE_ID_RD_SHARED_MEM_CLIENT:
         {
            VERIFY(result, 1 == module_ptr->gu.max_output_ports);
            // VERIFY(result, 1 == module_ptr->gu.max_input_ports);

            if (1 == module_ptr->gu.num_input_ports && 1 == module_ptr->gu.num_output_ports)
            {
               // Make sure that the only out port of this module is also an external port.
               VERIFY(result, NULL != module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr);

               module_ptr->flags.inplace         = TRUE;
               module_ptr->flags.dynamic_inplace = TRUE;
               gen_topo_output_port_t *output_port_ptr =
                  (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
               output_port_ptr->common.flags.port_has_threshold = FALSE;
               // requires data buffering is FALSE by default
            }
            // For the RD/WR Client modules, the GPR callback registration is done by the container
            break;
         }
         default:
         {
            OLC_MSG(me_ptr->topo.gu.log_id,
                    DBG_HIGH_PRIO,
                    "Unknown fwk module id 0x%lX",
                    module_ptr->gu.module_instance_id);
            THROW(result, AR_EFAILED);
         }
      }
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t olc_handle_fwk_events(olc_t *me_ptr)
{
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   // no reconciliation needed for OLC
   CU_FWK_EVENT_HANDLER_CONTEXT
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT

   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do_reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do_reconcile*/);

   if ((0 == fwk_event_flag_ptr->word) && (0 == capi_event_flag_ptr->word))
   {
      return AR_EOK;
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "Handling fwk events: fwk events 0x%lX, capi events 0x%lX",
           fwk_event_flag_ptr->word,
           capi_event_flag_ptr->word);

   if (capi_event_flag_ptr->port_thresh || capi_event_flag_ptr->media_fmt_event)
   {
      // me_ptr->cu.topo_vtbl_ptr->propagate_media_fmt(&me_ptr->topo, FALSE /*is_data_path*/);  //OLC_CA
      olc_update_cntr_kpps_bw(me_ptr, FALSE /*force_aggregate*/);
      me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(&me_ptr->cu);
   }

   olc_perf_vote(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);

   if (me_ptr->cu.flags.is_cntr_started)
   {
      if (fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->proc_dur_change ||
          capi_event_flag_ptr->algo_delay_event)
      {
         olc_cu_update_path_delay(&me_ptr->cu, CU_PATH_ID_ALL_PATHS);
      }
   }

   capi_event_flag_ptr->word         = 0;
   fwk_event_flag_ptr->word          = 0;

   return AR_EOK;
}

ar_result_t olc_handle_ext_in_data_flow_begin(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   bool_t                 prev_dfs    = in_port_ptr->common.data_flow_state;
   gen_topo_handle_data_flow_begin(&me_ptr->topo, &in_port_ptr->common, &in_port_ptr->gu.cmn);

   if ((prev_dfs != in_port_ptr->common.data_flow_state) &&
       (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->common.data_flow_state))
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, dfs_change);
      olc_handle_fwk_events(me_ptr);
   }

   return result;
}



static uint32_t olc_aggregate_ext_in_port_delay_util(olc_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
#if 0 //pending testing
   //gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)gu_ext_in_port_ptr->int_in_port_ptr;

   bool_t requires_data_buf = FALSE;
   if (in_port_ptr->nblc_end_ptr && (in_port_ptr->nblc_end_ptr != in_port_ptr))
   {
      requires_data_buf =
         ((gen_topo_module_t *)in_port_ptr->nblc_end_ptr->gu.cmn.module_ptr)->flags.requires_data_buf;
   }
   else
   {
      requires_data_buf = ((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr)->flags.requires_data_buf;
   }

   /**
    * If topo doesn't require buf, then we call process only after the input buf is filled. This adds delay.
    * We need to subtract this delay by upstream frame duration.
    */
   if (!requires_data_buf)
   {
      return topo_bytes_per_ch_to_us(in_port_ptr->common.max_buf_len, in_port_ptr->common.media_fmt_ptr, NULL);
   }
#endif
   return 0;
}

/**
 * Callback from CU to get additional container delays due to requires data buf etc
 */
uint32_t olc_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   olc_t *me_ptr = (olc_t *)base_ptr;
   return olc_aggregate_ext_in_port_delay_util(me_ptr, gu_ext_in_port_ptr);
}

/**
 * callback from topo layer to get container delays due to requires data buf etc
 */
uint32_t olc_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   olc_t *me_ptr = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);
   return cu_aggregate_ext_in_port_delay(&me_ptr->cu, gu_ext_in_port_ptr);
}

/**
 * callback from CU to get additional container delays.
 */
uint32_t olc_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   //olc_t *me_ptr = (olc_t *)base_ptr;
   return 0;
}

/**
 * callback from topo to get container delays
 */
uint32_t olc_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   olc_t *me_ptr = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);
   return cu_aggregate_ext_out_port_delay(&me_ptr->cu, gu_ext_out_port_ptr);
}

ar_result_t olc_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_proc_delay_ptr)
{
   *hw_acc_proc_delay_ptr = 0;
   return AR_EOK;
}
