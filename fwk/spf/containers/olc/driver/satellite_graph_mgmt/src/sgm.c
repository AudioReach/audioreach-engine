/**
 * \file sgm.c
 * \brief
 *     This file contains the functions for Satellite Graph Management
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"
uint32_t  g_olc_cnt =0;

/* =======================================================================
Static Function Definitions
========================================================================== */
/* Utility function to remove a node from the list */
ar_result_t sgm_util_remove_node_from_list(spgm_info_t *     spgm_ptr,
                                           spf_list_node_t **list_head_pptr,
                                           void *            list_node_ptr,
                                           uint32_t *        node_cntr_ptr)
{
   ar_result_t result     = AR_EOK;
   bool_t      node_found = FALSE;

   /** Remove the node from the list */
   if (FALSE == (node_found = spf_list_find_delete_node(list_head_pptr, list_node_ptr, TRUE /*pool_used*/)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "remove node: target node not present in the list, result: 0x%lx", result);
      return AR_EOK;
   }

   /** Decrement the node counter */
   (*node_cntr_ptr)--;

   return result;
}

/* Utility function to add a node to the list */
ar_result_t sgm_util_add_node_to_list(spgm_info_t *     spgm_ptr,
                                      spf_list_node_t **list_head_pptr,
                                      void *            list_node_ptr,
                                      uint32_t *        node_cntr_ptr)
{
   ar_result_t result = AR_EOK;

   /** Add the node to the list */
   if (AR_EOK !=
       (result = spf_list_insert_tail(list_head_pptr, list_node_ptr, spgm_ptr->cu_ptr->heap_id, TRUE /* use_pool*/)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "Add node: failed to add node to the list, result: 0x%lx", result);
      return AR_EFAILED;
   }

   /** Increment the node counter */
   (*node_cntr_ptr)++;

   return result;
}

/*
 * function to init the event response queue.
 * All the event responses from the Satellite process domain will be
 * sent to this queue.
 */
static ar_result_t sgm_init_event_queue(cu_base_t *cu_ptr, spgm_info_t *spgm_ptr, uint32_t offset)
{
   ar_result_t result = AR_EOK;
   char        sgm_event_q_name[POSAL_DEFAULT_NAME_LEN]; // event queue name
   uint32_t    num_elements = SGM_MAX_EVNT_Q_ELEMENTS;
   uint32_t    bit_mask     = SGM_EVENTQ_BIT_MASK;
   uint32_t    log_id       = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   VERIFY(result, (NULL != cu_ptr));
   log_id = spgm_ptr->sgm_id.cont_id;

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: init event queue with bit mask input 0x%08lx ", bit_mask);
#endif

   snprintf(sgm_event_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "EVT", "SGM", spgm_ptr->sgm_id.log_id);

   if (AR_EOK != (result = cu_init_queue(cu_ptr,
                                           sgm_event_q_name,
                                           num_elements,
                                           bit_mask,
                                           sgm_event_queue_handler,
                                           cu_ptr->channel_ptr,
                                           &spgm_ptr->evnt_q_ptr,
                                           CU_PTR_PUT_OFFSET(cu_ptr, offset),
                                           cu_ptr->heap_id)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "init: failed to init event queue with bit mask input 0x%08lx ",
                  bit_mask);
      return result;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: init event queue done ");
#endif

   return result;
}

/*
 * function to init the command response queue.
 * All the command responses from the Satellite process domain would be
 * sent to this queue.
 */
static ar_result_t sgm_init_rsp_queue(cu_base_t *cu_ptr, spgm_info_t *spgm_ptr, uint32_t offset)
{
   ar_result_t result = AR_EOK;
   char        sgm_rsp_q_name[POSAL_DEFAULT_NAME_LEN]; // response queue name
   uint32_t    num_elements = SGM_MAX_RSP_Q_ELEMENTS;
   uint32_t    bit_mask     = SGM_RSPQ_BIT_MASK;
   uint32_t    log_id       = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   VERIFY(result, (NULL != cu_ptr));

   log_id = spgm_ptr->sgm_id.cont_id;

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: init response queue with bit mask input 0x%08lx ", bit_mask);
#endif

   snprintf(sgm_rsp_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "RSP", "SGM", spgm_ptr->sgm_id.log_id);

   if (AR_EOK != (result = cu_init_queue(cu_ptr,
                                           sgm_rsp_q_name,
                                           num_elements,
                                           bit_mask,
                                           sgm_cmd_rsp_handler,
                                           cu_ptr->channel_ptr,
                                           &spgm_ptr->rsp_q_ptr,
                                           CU_PTR_PUT_OFFSET(cu_ptr, offset),
                                           cu_ptr->heap_id)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "init: failed to init response queue with bit mask input 0x%08lx",
                  bit_mask);
      return result;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: init response queue done ");
#endif

   return result;
}

/*
 * function to initialize the satellite graph management driver
 * Registers the container with GPR
 * Creates the command response and Event response Queues
 */
ar_result_t sgm_init(spgm_info_t *spgm_ptr, cu_base_t *cu_ptr, void *rsp_vtbl_ptr, uint32_t queues_start_offset)

{
   ar_result_t result         = AR_EOK;
   int32_t     gpr_result     = AR_EOK;
   uint32_t    host_domain_id = APM_PROC_DOMAIN_ID_INVALID;
   uint32_t    log_id         = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   VERIFY(result, (NULL != cu_ptr));
   VERIFY(result, (NULL != rsp_vtbl_ptr));

   spgm_ptr->sgm_id.cont_id = cu_ptr->gu_ptr->container_instance_id;
   spgm_ptr->sgm_id.log_id  = cu_ptr->gu_ptr->log_id;
   log_id                   = spgm_ptr->sgm_id.cont_id;

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: sgm driver create");
#endif

   // Get the Host domain ID
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   spgm_ptr->sgm_id.master_pd = host_domain_id;

   // Setting the sat_pd to invalid value. (default initialization)
   spgm_ptr->sgm_id.sat_pd = APM_PROC_DOMAIN_ID_INVALID;
   spgm_ptr->cu_ptr        = cu_ptr;

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: register the container with GPR");
#endif
   /** Register with OLC container with GPR */
   if (AR_EOK != (gpr_result = __gpr_cmd_register(spgm_ptr->sgm_id.cont_id, sgm_gpr_callback, &spgm_ptr->spf_handle)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "init: failed to register with GPR, result: 0x%8x", gpr_result);
      return AR_EFAILED;
   }

   /** create the event response queue. The event response queue has higher priority as control command
    * comes to this queue*/
   TRY(result, sgm_init_event_queue(cu_ptr, spgm_ptr, queues_start_offset));

   /** create the command response queue */
   TRY(result, sgm_init_rsp_queue(cu_ptr, spgm_ptr, queues_start_offset + posal_queue_get_size()));

   // update the available bitmask and curr_mask
   cu_ptr->available_bit_mask &= (~(SGM_RSPQ_BIT_MASK | SGM_EVENTQ_BIT_MASK));
   cu_ptr->curr_chan_mask |= ((SGM_RSPQ_BIT_MASK | SGM_EVENTQ_BIT_MASK));

   // update the OLC response table function
   spgm_ptr->cmd_rsp_vtbl = (sgmc_rsp_h_vtable_t *)rsp_vtbl_ptr;

   // create dynamic token variable
   if (AR_DID_FAIL(result = posal_atomic_word_create(&spgm_ptr->token_instance, spgm_ptr->cu_ptr->heap_id)))
   {
      return result;
   }


   // set the base_token
   spgm_ptr->base_token = (uint32_t)(g_olc_cnt << 24);
   // set the dynamic token variable with start value
   posal_atomic_set(spgm_ptr->token_instance, 0);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "init: created sgm driver done, result %lu", result);

   return AR_EOK;
}

/*
 * function to de-initialize the satellite graph management driver
 * De-registers the container with GPR
 * destroy's the command response and Event response Queues
 * destroys multiple list created
 */
ar_result_t sgm_deinit(spgm_info_t *spgm_ptr)
{
   ar_result_t result      = AR_EOK;
   ar_result_t temp_result = AR_EOK;
   int32_t     gpr_result  = AR_EOK;
   uint32_t    log_id      = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_INIT_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: getting started");
#endif

   /** De-register the container with  GPR    */
   if (AR_EOK != (gpr_result = __gpr_cmd_deregister(spgm_ptr->sgm_id.cont_id)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "deinit: failed to de-register with GPR result: 0x%8x", gpr_result);
      // Should not return from here. set error flag
      result |= AR_EUNEXPECTED;
   }

   /** delete the event list  */
   if (AR_EOK != (temp_result = spf_list_delete_list_and_free_objs(&spgm_ptr->event_reg_list_ptr, TRUE)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: failed to satellite module list, result %lu ", result);
      // Should not return from here. set error flag
      result |= temp_result;
   }

   /** delete the OLC container module list  */
   if (AR_EOK != (temp_result = spf_list_delete_list_and_free_objs(&spgm_ptr->gu_graph_info.olc_module_list_ptr, TRUE)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: failed to olc module list, result %lu ", result);
      // Should not return from here. set error flag
      result |= temp_result;
   }

   /** delete the satellite container module list  */
   if (AR_EOK !=
       (temp_result = spf_list_delete_list_and_free_objs(&spgm_ptr->gu_graph_info.satellite_module_list_ptr, TRUE)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: failed to satellite module list, result %lu ", result);
      // Should not return from here. set error flag
      result |= temp_result;
   }

   /** delete the path delay register information */
   if (AR_EOK != (temp_result = sgm_path_delay_list_destroy(spgm_ptr, FALSE)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: failed to destroy path delay list, result %lu ", result);
      // Should not return from here. set error flag
      result |= temp_result;
   }

   /** deinit the command response Queue  */
   if (NULL != spgm_ptr->rsp_q_ptr)
   {
      spf_svc_deinit_cmd_queue(spgm_ptr->rsp_q_ptr);
      spgm_ptr->rsp_q_ptr = NULL;
   }

   /** deinit the event response Queue  */
   if (NULL != spgm_ptr->evnt_q_ptr)
   {
      spf_svc_deinit_cmd_queue(spgm_ptr->evnt_q_ptr);
      spgm_ptr->evnt_q_ptr = NULL;
   }

   posal_atomic_word_destroy(spgm_ptr->token_instance);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "deinit: completed, result %lu ", result);

   return result;
}
