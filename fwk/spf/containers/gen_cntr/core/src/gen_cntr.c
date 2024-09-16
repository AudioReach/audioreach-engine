/**
 * \file gen_cntr.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "spf_svc_utils.h"
#include "pt_cntr.h"

// maximum number of commands expected ever in command queue.
static const uint32_t MAX_CMD_Q_ELEMENTS = 128;

/* -----------------------------------------------------------------------
 ** Queue handler f
 ** ----------------------------------------------------------------------- */
typedef ar_result_t (*posal_cmd_q_handler_f)(gen_cntr_t *me_ptr);

extern const cu_msg_handler_t      gen_cntr_cmd_handler_table[];
extern const uint32_t              g_sizeof_gen_cntr_cmd_handler_table;
extern const cu_cntr_vtable_t      gen_cntr_cntr_funcs;
extern const topo_to_cntr_vtable_t topo_to_gen_cntr_vtable;

extern const cu_cntr_vtable_t      pt_cntr_cntr_funcs;
extern const topo_to_cntr_vtable_t topo_to_pt_cntr_vtable;

static ar_result_t gen_cntr_create_channel_and_queues(gen_cntr_t *me_ptr, POSAL_HEAP_ID peer_cntr_heap_id)
{
   ar_result_t result = AR_EOK;

   char_t cmdQ_name[POSAL_DEFAULT_NAME_LEN]; // command queue name

   snprintf(cmdQ_name, POSAL_DEFAULT_NAME_LEN, "%s%8lX", "CGC", me_ptr->topo.gu.log_id);

   if (AR_EOK != (result = posal_channel_create(&me_ptr->cu.channel_ptr, peer_cntr_heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "GEN_CNTR: Unable to create channel, result: %lu", result);
      return result;
   }

   void    *cmdq_mem_ptr       = GEN_CNTR_PUT_CMDQ_OFFSET(me_ptr);
   uint32_t ctrl_port_q_offset = GEN_CNTR_CTRL_PORT_Q_OFFSET;

   if (check_if_pass_thru_container(me_ptr))
   {
      cmdq_mem_ptr       = PT_CNTR_PUT_CMDQ_OFFSET(me_ptr);
      ctrl_port_q_offset = PT_CNTR_CTRL_PORT_Q_OFFSET;
   }

   cu_init_queue(&me_ptr->cu,
                 cmdQ_name,
                 MAX_CMD_Q_ELEMENTS,
                 GEN_CNTR_CMD_BIT_MASK,
                 cu_handle_cmd_queue,
                 me_ptr->cu.channel_ptr,
                 &me_ptr->cu.cmd_handle.cmd_q_ptr,
                 cmdq_mem_ptr,
                 me_ptr->cu.heap_id);

   // Assign command handle pointer to point to the correct memory.
   me_ptr->cu.spf_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;

   /* Set the available bit mask for gen_cntr
    *  Important Note: Carefully set the mask availability based on the command handling priority.
    *  When the multiple signals are set on the channel, commands are handled based on the set bit position.
    *  The higher the bit position higher the priority. Need to decide if the control takes priority over data or
    *  vice versa and set the GEN_CNTR_CMD_BIT_MASK. For example, in End point case data takes higher priority than .
    *  control.
    */
   me_ptr->cu.available_bit_mask =
      0xFFFFFFFF & (~(GEN_CNTR_SYNC_CMD_BIT_MASK | GEN_CNTR_CMD_BIT_MASK | GEN_CNTR_TIMER_BIT_MASK));

   /* Intialize control channel mask. always create because we don't store peer_cntr_heap_id. */
   if (AR_EOK != (result = posal_channel_create(&me_ptr->cu.ctrl_channel_ptr, peer_cntr_heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "GEN_CNTR: Unable to create ctrl channel, result: %lu", result);
      return result;
   }

   /* Intialize the available ctrl chan mask*/
   me_ptr->cu.available_ctrl_chan_mask = 0xFFFFFFFF;

   /*Intra container IMCL ctrl queue*/
   cu_create_cmn_int_ctrl_port_queue(&me_ptr->cu, ctrl_port_q_offset);

   return result;
}

/**
 * Prerequisite tasks for thread launching. Must call this function before
 * launching/relaunching thread.
 *
 * priority [out]: returns the new thread's priority.
 */
ar_result_t gen_cntr_prepare_to_launch_thread(gen_cntr_t *         me_ptr,
                                              posal_thread_prio_t *priority_ptr,
                                              char *               thread_name,
                                              uint32_t             name_length)
{
   if (check_if_pass_thru_container(me_ptr))
   {
      snprintf(thread_name, name_length, "PTC_%lX", me_ptr->cu.gu_ptr->container_instance_id);
   }
   else
   {
      snprintf(thread_name, name_length, "GC_%lX", me_ptr->cu.gu_ptr->container_instance_id);
   }

   // if current thread exists, inherit its priority
   // (if a thread with bumped up priority launches another thread then new thread must also be made high priority)
   // gen_cntr_get_set_thread_priority doesn't take care of bumping up prio automatically.
   if (me_ptr->cu.cmd_handle.thread_id)
   {
      *priority_ptr = posal_thread_prio_get2(me_ptr->cu.cmd_handle.thread_id);
   }
   else
   {
	  //Here original prio is 0 since it is dont care
      gen_cntr_get_set_thread_priority(me_ptr,
                                       priority_ptr,
                                       FALSE /*should_set*/,
                                       1 /*bump_up_factor*/,
                                       0 /*original_prio*/);
   }

   return AR_EOK;
}

ar_result_t gen_cntr_create(cntr_cmn_init_params_t *init_params_ptr, spf_handle_t **handle, uint32_t cntr_type)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_t *me_ptr                   = NULL;
   *handle                              = NULL;
   uint32_t             stack_size      = 0;
   uint32_t             root_stack_size = 0;
   posal_thread_prio_t  thread_priority = 0;
   char_t               thread_name[POSAL_DEFAULT_NAME_LEN];
   bool_t               thread_launched = FALSE;
   gen_topo_init_data_t topo_init;
   POSAL_HEAP_ID        my_heap_id, peer_heap_id;
   uint32_t             log_id = 0;


   if (AR_EOK != (result = cu_set_cntr_type_bits_in_log_id(cntr_type, &log_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "GEN_CNTR: Failed to set bits for container log id");
      return AR_EFAILED;
   }
   cu_set_bits(&log_id, (uint32_t)init_params_ptr->log_seq_id, LOG_ID_SEQUENCE_MASK, LOG_ID_SEQUENCE_SHIFT);

   // retrieve heap_id from the container properties
   if (AR_EOK != cu_parse_get_self_and_peer_heap_ids(init_params_ptr->container_cfg_ptr, &my_heap_id, &peer_heap_id))
   {
      AR_MSG(DBG_ERROR_PRIO, "GEN_CNTR: Error getting the heap ID in create");
      return AR_EFAILED;
   }

   my_heap_id   = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, my_heap_id);
   peer_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, peer_heap_id);

   uint32_t cntr_handle_size = GEN_CNTR_SIZE_W_2Q;

   if (APM_CONTAINER_TYPE_ID_PTC == cntr_type)
   {
      if (is_pass_thru_container_supported())
      {
         cntr_handle_size = PT_CNTR_SIZE_W_2Q;
      }
      else // if pass thru container type is not supported return failure
      {
         AR_MSG(DBG_ERROR_PRIO, "Pass thru create failed! container type 0x%lx not supported", cntr_type);
         return AR_EFAILED;
      }
   }

   // allocate instance struct
   // Cant use MALLOC_MEMSET here, since it throws error and catch dereferences me_ptr
   me_ptr = (gen_cntr_t *)posal_memory_malloc(cntr_handle_size, my_heap_id);

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "GEN_CNTR: Malloc failed from heap ID %u. Required Size %lu",
             cntr_handle_size,
             (uint32_t)my_heap_id);
      return AR_ENOMEMORY;
   }
   memset(me_ptr, 0, cntr_handle_size);

   /*Initialize gu_ptr, such that if gen_cntr_destroy is called due to some failure, we don't crash for this being null
    */
   me_ptr->cu.topo_ptr    = (void *)&me_ptr->topo;
   me_ptr->cu.gu_ptr      = &me_ptr->topo.gu;
   me_ptr->topo.gu.log_id = log_id;
   me_ptr->cu.cntr_type   = cntr_type;
   me_ptr->cu.configured_thread_prio                = APM_CONT_PRIO_IGNORE; // Assume configured priority, to be updated by tools
   me_ptr->cu.configured_sched_policy               = APM_CONT_SCHED_POLICY_IGNORE;
   me_ptr->cu.configured_core_affinity              = APM_CONT_CORE_AFFINITY_IGNORE;

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   posal_mutex_create(&me_ptr->cu.gu_ptr->critical_section_lock_, my_heap_id);
#endif

   TRY(result, cu_parse_container_cfg(&me_ptr->cu, init_params_ptr->container_cfg_ptr));

   /*Assign heap ID */
   me_ptr->cu.heap_id                               = my_heap_id;
   me_ptr->cu.cmd_handler_table_ptr                 = gen_cntr_cmd_handler_table;
   me_ptr->cu.cmd_handler_table_size                = g_sizeof_gen_cntr_cmd_handler_table;
   me_ptr->cu.pm_info.weighted_kpps_scale_factor_q4 = UNITY_Q4;

   /* Init the topo and setup cu pointers to topo and gu fields. */
   memset(&topo_init, 0, sizeof(topo_init));

   if (check_if_pass_thru_container(me_ptr))
   {
      topo_init.topo_to_cntr_vtble_ptr = &topo_to_pt_cntr_vtable;
      me_ptr->cu.cntr_vtbl_ptr         = &pt_cntr_cntr_funcs;
   }
   else
   {
      topo_init.topo_to_cntr_vtble_ptr = &topo_to_gen_cntr_vtable;
      me_ptr->cu.cntr_vtbl_ptr         = &gen_cntr_cntr_funcs;
   }

   TRY(result, gen_topo_init_topo(&me_ptr->topo, &topo_init, me_ptr->cu.heap_id));
   me_ptr->cu.topo_vtbl_ptr           = topo_init.topo_cu_vtbl_ptr;
   me_ptr->cu.topo_island_vtbl_ptr    = topo_init.topo_cu_island_vtbl_ptr;
   me_ptr->cu.ext_in_port_cu_offset   = offsetof(gen_cntr_ext_in_port_t, cu);
   me_ptr->cu.ext_out_port_cu_offset  = offsetof(gen_cntr_ext_out_port_t, cu);
   me_ptr->cu.ext_ctrl_port_cu_offset = offsetof(gen_cntr_ext_ctrl_port_t, cu);
   me_ptr->cu.int_ctrl_port_cu_offset = offsetof(gen_cntr_int_ctrl_port_t, cu);
   me_ptr->cu.module_cu_offset        = offsetof(gen_cntr_module_t, cu);

   // Initially all bit masks are available.
   // me_ptr->cu.available_bit_mask = 0xFFFFFFFF;

   TRY(result, gen_cntr_create_channel_and_queues(me_ptr, peer_heap_id));

   // Initialize the wait mask only with the CMD handler mask
   me_ptr->cu.curr_chan_mask = (GEN_CNTR_SYNC_CMD_BIT_MASK | GEN_CNTR_CMD_BIT_MASK);

   TRY(result, cu_init(&me_ptr->cu));

   me_ptr->cu.gu_ptr->num_parallel_paths = 0;
   TRY(result, gen_cntr_allocate_wait_mask_arr(me_ptr));

   TRY(result, gen_cntr_get_thread_stack_size(me_ptr, &stack_size, &root_stack_size));

   gen_cntr_prepare_to_launch_thread(me_ptr, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN);

   TRY(result,
       cu_check_launch_thread(&me_ptr->cu,
                              stack_size,
                              root_stack_size,
                              thread_priority,
                              thread_name,
                              &thread_launched));

   *handle = (spf_handle_t *)&me_ptr->cu.spf_handle;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "Created GEN_CNTR with container ID 0x%08lX, from heap_id 0x%lx, peer_heap_id = 0x%lx",
                me_ptr->cu.gu_ptr->container_instance_id,
                (uint32_t)my_heap_id,
                (uint32_t)peer_heap_id);

   /** Register with GPR */
   if (AR_EOK != (result = __gpr_cmd_register(me_ptr->cu.gu_ptr->container_instance_id, cu_gpr_callback, &me_ptr->cu.spf_handle)))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "GEN_CNTR Create: Failed to register with GPR, result: 0x%8x",
                   result);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Create failed");
      gen_cntr_destroy(&me_ptr->cu, NULL);
   }

   return result;
}

ar_result_t gen_cntr_destroy(cu_base_t *base_ptr, void *temp)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   spf_msg_t cmd_msg = me_ptr->cu.cmd_msg;

   bool_t is_duty_cycling_allowed = cu_check_all_subgraphs_duty_cycling_allowed(&me_ptr->cu);
   bool_t dcm_already_registered  = me_ptr->cu.pm_info.flags.register_with_dcm;

   VERIFY(result, NULL != me_ptr);

   log_id = me_ptr->topo.gu.log_id;

   /** De-register with  GPR    */
   if (AR_EOK != (result = __gpr_cmd_deregister(me_ptr->cu.gu_ptr->container_instance_id)))
   {
      GEN_CNTR_MSG(log_id, DBG_ERROR_PRIO, "GEN_CNTR: Failed to de-register with GPR, result: %lu", result);
   }

   cu_destroy_voice_info(&me_ptr->cu);

   cu_destroy_offload_info(&me_ptr->cu);

   cu_delete_all_event_nodes(&me_ptr->cu.event_list_ptr);

   if ((is_duty_cycling_allowed) && (dcm_already_registered))
   {
      (void)cu_deregister_with_dcm(&me_ptr->cu);
   }

   (void)cu_deregister_with_pm(&me_ptr->cu);

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   // deinit thread pool, any pending commands will be drained
   cu_async_cmd_handle_deinit(&me_ptr->cu);
#endif

   TRY(result, cu_deinit(&me_ptr->cu));

   // internal control ports have their own outgoing buffer queues. They need to be drained and destroyed when the
   // sg is closed
   cu_deinit_internal_ctrl_ports(&me_ptr->cu, TRUE /*b_destroy_all_ports*/);

   if (check_if_pass_thru_container(me_ptr))
   {
      result |= pt_cntr_destroy_modules_resources((pt_cntr_t *)me_ptr, TRUE /*b_destroy_all_modules*/);
   }

   gen_topo_destroy_modules(&me_ptr->topo, TRUE /*b_destroy_all_modules*/);

   // if this container has a port connected to another SG, then the port alone might be destroyed in this cmd.
   // Also, very important to call gen_topo_destroy_modules before cu_deinit_external_ports.
   // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
   // deinit.
   cu_deinit_external_ports(&me_ptr->cu, FALSE /*b_ignore_ports_from_sg_close*/, TRUE /*force_deinit_all_ports*/);

   gen_topo_destroy_topo(&me_ptr->topo);

   gu_destroy_graph(me_ptr->cu.gu_ptr, TRUE /*b_destroy_everything*/);

   /*Destroy the intra container IMCL Queue */
   cu_destroy_cmn_int_port_queue(&me_ptr->cu);

   if (me_ptr->cu.spf_handle.q_ptr)
   {
      posal_queue_deinit(me_ptr->cu.spf_handle.q_ptr);
      me_ptr->cu.spf_handle.q_ptr = NULL;
   }

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   posal_mutex_destroy(&me_ptr->cu.gu_ptr->critical_section_lock_);
#endif

   if (me_ptr->cu.cmd_handle.cmd_q_ptr)
   {
      spf_svc_deinit_cmd_queue(me_ptr->cu.cmd_handle.cmd_q_ptr);
      me_ptr->cu.cmd_handle.cmd_q_ptr = NULL;
      // shouldn't be made NULL as cntr_cmn_destroy uses this to get handle->thread_id
      // me_ptr->cu.spf_handle.cmd_handle_ptr = NULL;

      // if cmdQ is created then ch is also init'ed.
      posal_channel_destroy(&me_ptr->cu.channel_ptr);

      /** Destroy the control port channel */
      posal_channel_destroy(&me_ptr->cu.ctrl_channel_ptr);
   }

   MFREE_NULLIFY(me_ptr->wait_mask_arr);

   // If the thread is not launched, free up the me ptr as
   // APM cannot send destroy messsage without a thread context
   if (!me_ptr->cu.cmd_handle.thread_id)
   {
      MFREE_NULLIFY(me_ptr);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, log_id)
   {
      GEN_CNTR_MSG(log_id, DBG_HIGH_PRIO, "destroy failed");
   }

   if (cmd_msg.payload_ptr)
   {
      spf_msg_ack_msg(&cmd_msg, AR_ETERMINATED);
   }

   GEN_CNTR_MSG(log_id, DBG_HIGH_PRIO, "CMD:DESTROY:Done");

   return AR_ETERMINATED;
}

#ifdef USES_DEBUG_DEV_ENV
void gen_cntr_print_mem_req()
{
#if 0
   // not covered; path delay, trigger policy, metadata, temp mallocs for port operations,
   // sdata bufs is estimated worst case (as worst case ch count is used, not actual ch count per port).

   // queue elements is based on min elements 2 (CU_MIN_QUEUE_ELEMENTS_TO_PRE_ALLOC).
   uint32_t size_for_q = posal_queue_get_size() + 2 * sizeof(posal_queue_element_list_t)+16 /*sizeof(posal_qurt_mutex_t)*/;
   uint32_t size_for_channel = 24;
   printf(  "\nGeneric Container State Struct (topo, buf mgr)    %lu"
            "\nPer Subgraph                                      %lu"
            "\nPer Module                                        %lu"
            "\nFramework modules (WR/RD EP/Placeholder enc/dec)  %lu"
            "\nMetadata buffer (WR/RD EP)                        %lu"
            "\nPer input port                                    %lu"
            "\nPer ext input port (peer, WR)                     %lu"
            "\nPer output port                                   %lu"
            "\nPer ext output port (peer, RD)                    %lu"
            "\nPer control port                                  %lu"
            "\nPer ext control port                              %lu"
            "\nPer max input ports                               %lu"
            "\nPer max output ports                              %lu"
            "\nPer bypassed module                               %lu"
            "\nPer unique media format per container             %lu"
            "\nPer deinter buf per port (at least one per port)  %lu"
            "\nSize and number of topo buffers (TBD)                "
            "\nSize and number of external buffers (TBD)            "
            "\n",
         sizeof(gen_cntr_t) + 40 /*topo_buf_manager_t*/ + size_for_channel  + size_for_q /*cmdq */ +
            size_for_channel /* ctrl */ + size_for_q /*intra cntr q*/ +
            28 /* gp signal etc */  + 32 /* GPR? */ + 1024 /* min stack size */,
         sizeof(gen_topo_sg_t) + sizeof(spf_list_node_t),
         sizeof(gen_cntr_module_t) + 2 * sizeof(spf_list_node_t) /* sorted module, sg module */,
         sizeof(gen_cntr_fwk_module_t),
         sizeof(gen_cntr_md_buf_t),
         sizeof(gen_topo_input_port_t) + sizeof(spf_list_node_t),
         sizeof(gen_cntr_ext_in_port_t) + sizeof(gen_topo_ext_port_scratch_data_t) + size_for_q + sizeof(spf_list_node_t),
         sizeof(gen_topo_output_port_t) + sizeof(spf_list_node_t),
         sizeof(gen_cntr_ext_out_port_t) + sizeof(gen_topo_ext_port_scratch_data_t) + size_for_q + sizeof(spf_list_node_t),
         sizeof(gen_cntr_int_ctrl_port_t) + sizeof(spf_list_node_t),
         sizeof(gen_cntr_ext_ctrl_port_t) + 2 * size_for_q /* incoming, outgoing */ + sizeof(spf_list_node_t),
         sizeof(gen_topo_port_scratch_data_t) + sizeof(capi_stream_data_v2_t *),
         sizeof(gen_topo_port_scratch_data_t) + sizeof(capi_stream_data_v2_t *),
         sizeof(gen_topo_module_bypass_t),
         sizeof(topo_media_fmt_t) + sizeof(spf_list_node_t),
         sizeof(topo_buf_t)
         );
#endif
}
#endif
