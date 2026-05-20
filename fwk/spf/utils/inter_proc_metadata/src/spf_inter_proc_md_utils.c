/**
 * \file spf_ref_counter.c
 * \brief
 *     This file contains the implementation for spf message utility functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include "capi.h"
#include "capi_intf_extn_metadata.h"
#include "module_cmn_metadata.h"
#include "posal_mutex.h"
#include "spf_macros.h"
#include "spf_ref_counter.h"
#include "spf_inter_proc_md_utils.h"
#include "gen_topo.h"
#include "gen_topo_metadata.h"
#include "thin_topo_inline.h"
#include <stdint.h>
#include "gpr_api_inline.h"

///////////////////////////////////// MACROs and Global variables  /////////////////////////////////////

#define THREAD_PRIO (posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID))
#define THREAD_POOL_DEFAULT_STACK_SIZE (2048)
#define HEAP_ID_DEFAULT POSAL_HEAP_DEFAULT
#define SPF_IPCMD_LOG_ID 0xF00F

spf_ipcmd_tracker_util_t spf_ipcmd_global;

///////////////////////////////////// STATIC FUNCTION DEFINITIONS  /////////////////////////////////////

static capi_err_t  spf_ipcmd_detach_md_from_cntr(uint32_t log_id, void *context_ptr, module_cmn_md_list_t *md_list_ptr);
static ar_result_t spf_ipcmd_destroy_node_info(uint32_t log_id, spf_ipcmd_node_info_t *md_node_ref_ptr);
// Updated: all functions take log_id as the first parameter
static spf_ipcmd_node_info_t *spf_ipcmd_get_md_from_token_util_(uint32_t log_id,
                                                                uint32_t token_lsw,
                                                                uint32_t token_msw);
static ar_result_t            spf_ipmd_thread_pool_job_handler(void *job_context_ptr);
static ar_result_t            spf_ipcmd_handle_event_update_ref_count(uint32_t      log_id,
                                                                      gpr_packet_t *packet_ptr,
                                                                      bool_t        is_incr_ref_count_event);

static ar_result_t spf_ipcmd_handle_tracking_md_event(uint32_t log_id, gpr_packet_t *packet_ptr);

static ar_result_t spf_ipcmd_handle_event_clone_md(uint32_t log_id, gpr_packet_t *packet_ptr);

static ar_result_t spf_ipcmd_handle_tracking_md_event_util_(uint32_t               log_id,
                                                            spf_ipcmd_node_info_t *md_node_ref_ptr,
                                                            bool_t                 is_dropped,
                                                            uint32_t               rendering_miid,
                                                            uint32_t               rendering_src_domain_id);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void spf_ipcmd_return_job_obj(tp_job_info_t *job_ptr)
{
   posal_mutex_lock(spf_ipcmd_global.mutex);
   memset(job_ptr, 0, sizeof(tp_job_info_t));
   posal_mutex_unlock(spf_ipcmd_global.mutex);
}

static tp_job_info_t *spf_ipcmd_get_job_obj()
{
   posal_mutex_lock(spf_ipcmd_global.mutex);
   for (uint32_t i = 0; i < MAX_SUPPORTED_CONCURRENT_JOBS; i++)
   {
      if (FALSE == spf_ipcmd_global.job_list[i].in_use)
      {
         spf_ipcmd_global.job_list[i].in_use = TRUE;
         tp_job_info_t *temp_ptr             = &spf_ipcmd_global.job_list[i];
         posal_mutex_unlock(spf_ipcmd_global.mutex);
         return temp_ptr;
      }
   }
   posal_mutex_unlock(spf_ipcmd_global.mutex);
   return NULL;
}

// todo_mdf: call these functions from SSR and PDR contexts if required
ar_result_t spf_ipcmd_init()
{
   AR_MSG(DBG_HIGH_PRIO, "Initializing SPF IPCMD utility");

   // deinit if not uninitialized earlier.
   if (spf_ipcmd_global.thread_pool_ptr)
   {
      spf_ipcmd_deinit();
   }

   // initialize the  thread pool which will be reserved
   spf_thread_pool_get_instance(&spf_ipcmd_global.thread_pool_ptr,
                                POSAL_HEAP_DEFAULT,
                                THREAD_PRIO,
                                TRUE, /*is_dedicated_pool*/
                                THREAD_POOL_DEFAULT_STACK_SIZE,
                                1,
                                SPF_IPCMD_LOG_ID);

   posal_mutex_create(&spf_ipcmd_global.mutex, HEAP_ID_DEFAULT);

   AR_MSG(DBG_HIGH_PRIO, "Initializing SPF IPCMD utility, Done!");

   return AR_EOK;
}

ar_result_t spf_ipcmd_deinit()
{
   AR_MSG(DBG_HIGH_PRIO, "Deinitializing SPF IPCMD utility");

   spf_ipcmd_destroy_pending_tracked_md_info(0, FALSE, 0 /* dont care*/, FALSE);

   // destroying the thread first.
   spf_thread_pool_release_instance(&spf_ipcmd_global.thread_pool_ptr, 0);

   // destory mutex
   posal_mutex_destroy(&spf_ipcmd_global.mutex);

   memset(&spf_ipcmd_global, 0, sizeof(spf_ipcmd_tracker_util_t));

   AR_MSG(DBG_HIGH_PRIO, "Deinitializing SPF IPCMD utility, Done!");

   return AR_EOK;
}

static ar_result_t spf_ipmd_thread_pool_job_handler(void *job_context_ptr)
{
   ar_result_t    result  = AR_EOK;
   tp_job_info_t *job_ptr = (tp_job_info_t *)job_context_ptr;

   // check if its a GPR job
   if (NULL == job_ptr->gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Invalid GPR packet ptr");
      return AR_EFAILED;
   }

   gpr_packet_t *gpr_pkt_ptr = job_ptr->gpr_pkt_ptr;

   switch (gpr_pkt_ptr->opcode)
   {
      case EVENT_ID_IPC_METADATA_INCR_REF_COUNT:
      {
         result = spf_ipcmd_handle_event_update_ref_count(gpr_pkt_ptr->dst_domain_id, gpr_pkt_ptr, TRUE);
         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
      case EVENT_ID_IPC_METADATA_DECR_REF_COUNT:
      {
         result = spf_ipcmd_handle_event_update_ref_count(gpr_pkt_ptr->dst_domain_id, gpr_pkt_ptr, FALSE);
         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
      case EVENT_ID_MODULE_CMN_METADATA_CLONE_MD:
      {
         result = spf_ipcmd_handle_event_clone_md(gpr_pkt_ptr->dst_domain_id, gpr_pkt_ptr);
         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
      case EVENT_ID_MODULE_CMN_METADATA_TRACKING_EVENT:
      {
         result = spf_ipcmd_handle_tracking_md_event(gpr_pkt_ptr->dst_domain_id, gpr_pkt_ptr);
         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "unexpected packet received. opcode: 0x%x, token: 0x%x",
                gpr_pkt_ptr->opcode,
                gpr_pkt_ptr->token);

         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
   }

   spf_ipcmd_return_job_obj(job_ptr);

   return result;
}

/***********************************************************************************
 * Functions invoked by the HLOS and Ack from APM. This function has be to lite weight and push the job to thread pool
 *to reduce overhead in the caller context (currently APM)
 ***********************************************************************************/
uint32_t spf_ipmd_gpr_callback_handler(gpr_packet_t *gpr_pkt_ptr)
{
   ar_result_t result = AR_EOK;
   if (NULL == gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "received NULL gpr packet");
      return AR_EBADPARAM;
   }

   if (NULL == spf_ipcmd_global.thread_pool_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IPCMD thread pool not initalized/deinitialized yet.");
      return AR_EBADPARAM;
   }

   tp_job_info_t *job_ptr = spf_ipcmd_get_job_obj();
   if (job_ptr)
   {
      job_ptr->job.job_context_ptr = job_ptr;
      job_ptr->job.job_func_ptr    = spf_ipmd_thread_pool_job_handler;
      job_ptr->gpr_pkt_ptr         = gpr_pkt_ptr;
      spf_thread_pool_push_job(spf_ipcmd_global.thread_pool_ptr, &job_ptr->job, 0);
   }

   return AR_EOK;
}

/** need to call this is SSR/PDR context to destory all the nodes. if valid arguments are passed deletes the nodes
 which matches the arguments, else deletes all the nodes. Usually arguments are not valid in the PDR/SSR context and we
need free all the nodes and dont need acquire any locks.

Lock needs to be acquired if nodes are getting destoryed based on the up/down notification of a specific remote proc
domain ID.
**/
ar_result_t spf_ipcmd_destroy_pending_tracked_md_info(uint32_t log_id,
                                                      uint32_t has_valid_args,
                                                      uint32_t remote_proc_domain_id,
                                                      bool_t   render_event)
{
   // todo_mdf: check proc domain thats going down and clear refcount corresponding to that proc domain.
   ar_result_t result = AR_EOK;

   // Important: not aquiring lock since its in the SSR context and we dont expect other threads to be sending any
   // commands at this point.
   if (has_valid_args)
   {
      posal_mutex_lock(spf_ipcmd_global.mutex);
   }

   if (spf_ipcmd_global.tracking_md_list_ptr == NULL)
   {
      return AR_EOK;
   }

   AR_MSG(DBG_LOW_PRIO, "Destroying pending tracked nodes");

   // iterate through the tracking list and find if the proc domain id matches with  that of the proc domain
   // going down or coming up. if so, clear the refcount
   spf_list_node_t *temp_node_ptr = spf_ipcmd_global.tracking_md_list_ptr;
   spf_list_node_t *next_node_ptr = NULL;
   while (temp_node_ptr)
   {
      spf_ipcmd_node_info_t *node_info_ptr = (spf_ipcmd_node_info_t *)temp_node_ptr->obj_ptr;
      next_node_ptr                        = temp_node_ptr->next_ptr;

      for (uint32_t i = 0; i < MAX_PROC_DOMAIN_COUNT; i++)
      {
         if (has_valid_args)
         {
            if (remote_proc_domain_id != node_info_ptr->ref_count_list[i].domain_id)
            {
               continue;
            }
         }

         node_info_ptr->ref_count_list[i].domain_id = 0;
         node_info_ptr->total_ref_count -= node_info_ptr->ref_count_list[i].ref_count;
         node_info_ptr->ref_count_list[i].ref_count = 0;

         if (has_valid_args && render_event)
         {
            spf_ipcmd_handle_tracking_md_event_util_(log_id, node_info_ptr, TRUE, log_id, remote_proc_domain_id);
         }
         else if (0 == node_info_ptr->total_ref_count)
         {
            spf_ipcmd_destroy_node_info(SPF_IPCMD_LOG_ID, node_info_ptr);
         }
      }

      temp_node_ptr = next_node_ptr;
   }

   if (has_valid_args)
   {
      posal_mutex_unlock(spf_ipcmd_global.mutex);
   }

   return AR_EOK;
}

static spf_ipcmd_node_info_t *spf_ipcmd_get_md_from_token_util_(uint32_t log_id, uint32_t token_lsw, uint32_t token_msw)
{
   spf_ipcmd_node_info_t *local_info_ptr = NULL;

   spf_list_node_t *cur_ptr = spf_ipcmd_global.tracking_md_list_ptr;

   while (cur_ptr)
   {
      spf_ipcmd_node_info_t *node_info_ptr = (spf_ipcmd_node_info_t *)cur_ptr->obj_ptr;

      if (NULL == node_info_ptr->md_ptr)
      {
         continue;
      }

      if (node_info_ptr->token_lsw == token_lsw && node_info_ptr->token_msw == token_msw)
      {
         IPMD_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "token_lsw = 0x%x token_msw = 0x%x, found matching md_ptr = %p",
                  token_lsw,
                  token_msw,
                  node_info_ptr->md_ptr);
         return node_info_ptr;
      }

      cur_ptr = cur_ptr->next_ptr;
   }
   IPMD_MSG(log_id,
            DBG_HIGH_PRIO,
            "Warning! token_lsw = 0x%x token_msw = 0x%x, no matching md found",
            token_lsw,
            token_msw);
   return NULL;
}

/** checks if the given MD is client originated and being tracked already */
static bool_t spf_ipcmd_is_md_already_being_tracked_util_(uint32_t log_id, module_cmn_md_t *md_ptr)
{
   uint32_t self_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&self_domain_id);

   if ((MODULE_CMN_MD_TRACKING_CONFIG_DISABLE != md_ptr->metadata_flag.tracking_mode) &&
       (MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD == md_ptr->metadata_flag.is_client_metadata))
   {
      // check the metadata originating source domain
      if (md_ptr->tracking_ptr->src_domain_id == self_domain_id)
      {

         spf_ipcmd_node_info_t *ret_ptr =
            spf_ipcmd_get_md_from_token_util_(log_id, md_ptr->tracking_ptr->token_lsw, md_ptr->tracking_ptr->token_msw);

         if (ret_ptr)
         {
            // it means MD was originiated in the current proc domain, and remote proc domain propagated back
            // to the current proc domain.
            return TRUE;
         }
         else
         {
            // md has just been created in the current proc domain, and yet to be tracked.
            return FALSE;
         }
      }
      else
      {
         // it means this MD is being tracked by a remote proc domain already
         return TRUE;
      }
   }

   // tracking is disabled or MD came from an external client domain.
   // if MD came from external client, it must have been orginiated in the current domain.
   // because when MD is propagated across proc domain boundary from the source domain
   // it will be converted into MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD
   return FALSE;
}

bool_t spf_ipcmd_is_md_already_being_tracked(uint32_t log_id, module_cmn_md_t *md_ptr)
{
   // verify if the token is found in the tracking md list
   posal_mutex_lock(spf_ipcmd_global.mutex);
   bool_t ret = spf_ipcmd_is_md_already_being_tracked_util_(log_id, md_ptr);
   posal_mutex_unlock(spf_ipcmd_global.mutex);
   return ret;
}

/** This function needs to be called in the IPC TX module of the tracking metadata originating domain, only if it
hasn't been already being tracked. */
ar_result_t spf_add_md_to_inter_proc_md_tracker(
   uint32_t               log_id,
   void                  *md_extn_context_ptr, // this context ptr is same the one set by the INTF_EXTN_METADATA_HANDLER
   module_cmn_md_list_t  *md_node_ptr,
   module_cmn_md_list_t **src_md_list_pptr,
   metadata_header_t     *md_data_header_ptr,
   POSAL_HEAP_ID          heap_id)
{
   ar_result_t result = AR_EOK;

   posal_mutex_lock(spf_ipcmd_global.mutex);

   // firstly check if the node is already being tracked.
   // do we need to check the token maybe?
   if (spf_ipcmd_is_md_already_being_tracked_util_(log_id, md_node_ptr->obj_ptr))
   {
      IPMD_MSG(log_id,
               DBG_HIGH_PRIO,
               "MD: Failed to add node 0x%lx token (0x%lx, 0x%lx) to tracking list, its already tracked",
               md_node_ptr,
               md_node_ptr->obj_ptr->tracking_ptr->token_lsw,
               md_node_ptr->obj_ptr->tracking_ptr->token_msw);
      posal_mutex_unlock(spf_ipcmd_global.mutex);
      return AR_EFAILED;
   }

   // If the tracking mode is enable, we need to keep the metadata
   // in the IPC tx input
   spf_ipcmd_node_info_t *md_node_ref_ptr =
      (spf_ipcmd_node_info_t *)posal_memory_malloc(sizeof(spf_ipcmd_node_info_t), heap_id);
   if (NULL == md_node_ref_ptr)
   {
      return AR_ENOMEMORY;
   }

   // add node_ptr to the tracking list (md_node_ref_ptr)
   memset((void *)md_node_ref_ptr, 0, sizeof(spf_ipcmd_node_info_t));

   IPMD_MSG(log_id, DBG_LOW_PRIO, "MD: Adding node 0x%lx to tracking list", md_node_ptr);

   result = spf_list_insert_tail((spf_list_node_t **)&spf_ipcmd_global.tracking_md_list_ptr,
                                 md_node_ref_ptr,
                                 (POSAL_HEAP_ID)heap_id,
                                 TRUE /* use pool */);
   if (AR_EOK != result)
   {
      posal_mutex_unlock(md_node_ref_ptr);
      posal_memory_free(md_node_ref_ptr);
      return result;
   }

   posal_mutex_unlock(spf_ipcmd_global.mutex);

   // move the node from the src list to the tracker info object.
   spf_list_move_node_to_another_list((spf_list_node_t **)&md_node_ref_ptr->md_ptr,
                                      (spf_list_node_t *)md_node_ptr,
                                      (spf_list_node_t **)src_md_list_pptr);

   // token stores the address of the metadata node pointer.
   // For 32bit, the LSW will have the original value and msw will have a encrypted value
   // For 64bit, the LSW stores the lower 32bits and MSW stores the higher 32bits.
   // the token is not expected to be returned back to client
#if defined(__x86_64__) || defined(__LP64__) || defined(_WIN64)
   md_data_header_ptr->token_lsw = (uint32_t)((uint64_t)md_node_ptr & 0xFFFFFFFF);
   md_data_header_ptr->token_msw = (uint32_t)(((uint64_t)md_node_ptr >> 32) & 0xFFFFFFFF);
#else
   md_data_header_ptr->token_lsw = (uint32_t)md_node_ptr;
   md_data_header_ptr->token_msw = md_data_header_ptr->token_lsw ^ SPF_MAGIC_MD_TRACKING_KEY;
#endif

   // cache the token in the book keeping node
   md_node_ref_ptr->token_lsw = md_data_header_ptr->token_lsw;
   md_node_ref_ptr->token_msw = md_data_header_ptr->token_msw;

   uint32_t temp_flags =
      (MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_EACH << MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);
   temp_flags |=
      (MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME << MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);

   md_data_header_ptr->flags |= temp_flags;

   temp_flags = (MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY
                 << MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG); // removing the tracking config flag for
                                                                 // drops only
   temp_flags = ~temp_flags;

   md_data_header_ptr->flags &= temp_flags;

   result |= spf_ipcmd_detach_md_from_cntr(log_id, md_extn_context_ptr, md_node_ptr);

#ifdef DEBUG_SPF_MD_TRACKING_UTILS
   IPMD_MSG(log_id,
            DBG_LOW_PRIO,
            "MD: metadata_id= 0x%lx, offset= 0x%lx, token_lsw= 0x%x, "
            "token_msw=0x%x, payload_size= %lu, flags= 0x%lx, md_node_ptr 0x%lx ",
            md_data_header_ptr->metadata_id,
            md_data_header_ptr->offset,
            md_data_header_ptr->token_lsw,
            md_data_header_ptr->token_msw,
            md_data_header_ptr->payload_size,
            md_data_header_ptr->flags,
            md_node_ptr);
#endif

   return result;
}

static ar_result_t spf_ipcmd_incr_ref_count_util_(uint32_t               log_id,
                                                  spf_ipcmd_node_info_t *node_info_ptr,
                                                  uint32_t               domain_id)
{
   IPMD_MSG(log_id, DBG_LOW_PRIO, "MD: Increment node info ptr: %lx, domain_id: %x", node_info_ptr, domain_id);

   int32_t available_index = MAX_PROC_DOMAIN_COUNT;
   for (uint32_t i = 0; i < MAX_PROC_DOMAIN_COUNT; i++)
   {
      if (node_info_ptr->ref_count_list[i].domain_id && (domain_id == node_info_ptr->ref_count_list[i].domain_id))
      {
         node_info_ptr->ref_count_list[i].ref_count++;
         if (node_info_ptr->ref_count_list[i].ref_count > node_info_ptr->ref_count_list[i].max_ref_count)
         {
            node_info_ptr->ref_count_list[i].max_ref_count = node_info_ptr->ref_count_list[i].max_ref_count;
         }
         node_info_ptr->total_ref_count++;

         IPMD_MSG(log_id,
                  DBG_LOW_PRIO,
                  "MD: Incrmented ref count for domain_id: %x ref_count=%u, total_ref_count=%u",
                  node_info_ptr->ref_count_list[i].domain_id,
                  node_info_ptr->ref_count_list[i].ref_count,
                  node_info_ptr->total_ref_count);

         return AR_EOK;
      }
      else
      {
         available_index = i;
      }
   }

   if (available_index < MAX_PROC_DOMAIN_COUNT)
   {
      node_info_ptr->ref_count_list[available_index].domain_id     = domain_id;
      node_info_ptr->ref_count_list[available_index].ref_count     = 1;
      node_info_ptr->ref_count_list[available_index].max_ref_count = 1;
      node_info_ptr->total_ref_count                               = 1;

      IPMD_MSG(log_id,
               DBG_LOW_PRIO,
               "MD: Incrmented ref count for domain_id: %x ref_count=%u, total_ref_count=%u",
               node_info_ptr->ref_count_list[available_index].domain_id,
               node_info_ptr->ref_count_list[available_index].ref_count,
               node_info_ptr->total_ref_count);
   }
   else
   {
      IPMD_MSG(log_id,
               DBG_ERROR_PRIO,
               "MD: Failed tp incrmented ref count for domain_id: %x total_ref_count=%u, couldnt find free proc domain "
               "slot.",
               domain_id,
               node_info_ptr->total_ref_count);
      return AR_EFAILED;
   }

   return AR_EOK;
}

static ar_result_t spf_ipcmd_decr_ref_count_util_(uint32_t               log_id,
                                                  spf_ipcmd_node_info_t *node_info_ptr,
                                                  uint32_t               domain_id)
{
   IPMD_MSG(log_id, DBG_LOW_PRIO, "MD: decrement node info ptr: %lx, domain_id: %x", node_info_ptr, domain_id);

   for (uint32_t i = 0; i < MAX_PROC_DOMAIN_COUNT; i++)
   {
      if (0 == node_info_ptr->ref_count_list[i].domain_id)
      {
         continue;
      }

      if (domain_id != node_info_ptr->ref_count_list[i].domain_id)
      {
         continue;
      }

      if (node_info_ptr->ref_count_list[i].ref_count)
      {
         node_info_ptr->ref_count_list[i].ref_count--;
      }
      else
      {
         IPMD_MSG(log_id, DBG_ERROR_PRIO, "MD: ERROR: ref count is already 0 for domain_id: %x", domain_id);
         return AR_EFAILED;
      }

      if (0 == node_info_ptr->ref_count_list[i].ref_count)
      {
         node_info_ptr->ref_count_list[i].domain_id = 0;
      }

      if (node_info_ptr->total_ref_count)
      {
         node_info_ptr->total_ref_count--;
      }
      else
      {
         IPMD_MSG(log_id, DBG_ERROR_PRIO, "MD: ERROR: total ref count is already 0");
         return AR_EFAILED;
      }

      IPMD_MSG(log_id,
               DBG_LOW_PRIO,
               "MD: decrement ref count for domain_id: %x  updated ref_count: %lu total_ref_count: %lu",
               domain_id,
               node_info_ptr->ref_count_list[i].ref_count,
               node_info_ptr->total_ref_count);

      return AR_EOK;
   }

   IPMD_MSG(log_id,
            DBG_ERROR_PRIO,
            "MD: Failed to decrement ref count for domain_id: %x total_ref_count: %lu",
            domain_id,
            node_info_ptr->total_ref_count);

   return AR_EFAILED;
}

static ar_result_t spf_ipcmd_handle_event_clone_md(uint32_t log_id, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   payload_size = 0;
   metadata_tracking_event_t *md_te_ptr    = NULL;

   IPMD_MSG(log_id, DBG_HIGH_PRIO, "MD: process MD clone event");

   posal_mutex_lock(spf_ipcmd_global.mutex);

   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);

   if (payload_size < sizeof(metadata_tracking_event_t))
   {
      THROW(result, AR_EBADPARAM);
   }

   md_te_ptr = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);

   spf_ipcmd_node_info_t *md_node_ref_ptr =
      spf_ipcmd_get_md_from_token_util_(log_id, md_te_ptr->token_lsw, md_te_ptr->token_msw);

   VERIFY(result, (NULL != md_node_ref_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr->tracking_ptr));

   module_cmn_md_t *ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;

   TRY(result, spf_ipcmd_incr_ref_count_util_(log_id, md_node_ref_ptr, packet_ptr->src_domain_id));

   CATCH(result, "MIID[%lx]", log_id)
   {
   }

   posal_mutex_unlock(spf_ipcmd_global.mutex);
   return result;
}

static ar_result_t spf_ipcmd_handle_event_update_ref_count(uint32_t      log_id,
                                                           gpr_packet_t *packet_ptr,
                                                           bool_t        is_incr_ref_count_event)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   payload_size = 0;
   metadata_tracking_event_t *md_te_ptr    = NULL;

   IPMD_MSG(log_id,
            DBG_HIGH_PRIO,
            "MD: process MD update ref count event, is_incr_ref_count_event?%lu",
            is_incr_ref_count_event);

   posal_mutex_lock(spf_ipcmd_global.mutex);

   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);

   if (payload_size < sizeof(metadata_tracking_event_t))
   {
      THROW(result, AR_EBADPARAM);
   }

   md_te_ptr = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   spf_ipcmd_node_info_t *md_node_ref_ptr =
      spf_ipcmd_get_md_from_token_util_(log_id, md_te_ptr->token_lsw, md_te_ptr->token_msw);

   VERIFY(result, (NULL != md_node_ref_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr->tracking_ptr));

   module_cmn_md_t *ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;

   if (is_incr_ref_count_event)
   {
      TRY(result, spf_ipcmd_incr_ref_count_util_(log_id, md_node_ref_ptr, packet_ptr->src_domain_id));
   }
   else
   {
      TRY(result, spf_ipcmd_decr_ref_count_util_(log_id, md_node_ref_ptr, packet_ptr->src_domain_id));
   }

   CATCH(result, "gpr packet [%lx]", packet_ptr)
   {
   }

   posal_mutex_unlock(spf_ipcmd_global.mutex);
   return result;
}

static ar_result_t spf_ipcmd_handle_tracking_md_event_util_(uint32_t               log_id,
                                                            spf_ipcmd_node_info_t *md_node_ref_ptr,
                                                            bool_t                 is_dropped,
                                                            uint32_t               rendering_miid,
                                                            uint32_t               rendering_src_domain_id)
{
   ar_result_t result = AR_EOK;

   module_cmn_md_t *ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;

   // incrementing ref count just so that when tracking event function decrements
   // with spf_ref_counter_remove_ref() count is non zero
   if (1 < md_node_ref_ptr->total_ref_count)
   {
      spf_ref_counter_add_ref((void *)ref_md_ptr->tracking_ptr);
   }

   gen_topo_raise_tracking_event(log_id, rendering_miid, md_node_ref_ptr->md_ptr, !is_dropped, NULL, FALSE);

   // for both dropped or consumed decrease the ref count by 1.
   spf_ipcmd_decr_ref_count_util_(log_id, md_node_ref_ptr, rendering_src_domain_id);

   // end tracking because the last render/drop event has been recevied for the given metadata.
   if (0 == md_node_ref_ptr->total_ref_count)
   {
      spf_ipcmd_destroy_node_info(log_id, md_node_ref_ptr);
   }

   return result;
}

static ar_result_t spf_ipcmd_handle_tracking_md_event(uint32_t log_id, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   payload_size = 0;
   metadata_tracking_event_t *md_te_ptr    = NULL;

   IPMD_MSG(log_id, DBG_HIGH_PRIO, "MD: Received tracking metadata event");

   posal_mutex_lock(spf_ipcmd_global.mutex);

   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);

   // cases where the event comes within the same SPF process domain.
   if (sizeof(metadata_tracking_event_t) > payload_size)
   {
      THROW(result, AR_EBADPARAM);
   }

   md_te_ptr = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);

   spf_ipcmd_node_info_t *md_node_ref_ptr =
      spf_ipcmd_get_md_from_token_util_(log_id, md_te_ptr->token_lsw, md_te_ptr->token_msw);

   VERIFY(result, (NULL != md_node_ref_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr));
   VERIFY(result, (NULL != md_node_ref_ptr->md_ptr->obj_ptr->tracking_ptr));

   IPMD_MSG(log_id,
            DBG_HIGH_PRIO,
            "MD: Tracking MD event details MD_ID 0x%lX, source_miid 0x%lx, miid 0x%lx",
            md_te_ptr->metadata_id,
            md_te_ptr->source_module_instance,
            md_te_ptr->module_instance_id);

   IPMD_MSG(log_id,
            DBG_HIGH_PRIO,
            "MD: Tracking MD event details status 0x%lx, token (lsw, msw) 0x%lx,0x%lx, flags 0x%lx",
            md_te_ptr->status,
            md_te_ptr->token_lsw,
            md_te_ptr->token_msw,
            md_te_ptr->flags);

   // indicates the the metadata has been been dropped or consumed by sink module
   bool_t is_dropped = (MD_TRACKING_STATUS_IS_DROPPED == md_te_ptr->status) ? TRUE : FALSE;

   IPMD_MSG(log_id,
            DBG_HIGH_PRIO,
            "MD: process tr_md_event,  dropping MD,  total_ref_count %lu",
            md_node_ref_ptr->total_ref_count);

   spf_ipcmd_handle_tracking_md_event_util_(log_id,
                                            md_node_ref_ptr,
                                            is_dropped,
                                            md_te_ptr->module_instance_id,
                                            packet_ptr->src_domain_id);

   CATCH(result, "MD:0x%lx", 0)
   {
   }

   posal_mutex_unlock(spf_ipcmd_global.mutex);
   return result;
}

/** This extension must be same as the md extension context ptr thats set under INTF_EXTN_PARAM_ID_METADATA_HANDLER */
static capi_err_t spf_ipcmd_detach_md_from_cntr(uint32_t              log_id,
                                                void                 *md_extn_context_ptr,
                                                module_cmn_md_list_t *md_list_ptr)
{
   capi_err_t         result       = CAPI_EOK;
   ar_result_t        ar_result    = AR_EOK;
   gen_topo_module_t *module_ptr   = (gen_topo_module_t *)md_extn_context_ptr;
   gen_topo_t        *topo_ptr     = module_ptr->topo_ptr;
   uint32_t           md_sink_miid = 0;

   if (NULL == md_list_ptr)
   {
      return result;
   }

   module_cmn_md_t *md_ptr = (module_cmn_md_t *)md_list_ptr->obj_ptr;

   switch (md_ptr->metadata_id)
   {
      case MODULE_CMN_MD_ID_EOS:
      {
         module_cmn_md_eos_t *eos_metadata_ptr = NULL;
         uint32_t             is_out_band      = md_ptr->metadata_flag.is_out_of_band;
         if (is_out_band)
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }

#ifdef METADATA_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         "MD_DBG: Detaching EOS from cntr! freeing the EOS cargo, eos_node_ptr 0x%p cntr_ref_ptr 0x%p "
                         "eos_metadata_ptr "
                         "0x%p, 0x%p",
                         md_list_ptr,
                         eos_metadata_ptr->cntr_ref_ptr,
                         eos_metadata_ptr,
                         md_ptr);
#endif

         /** even if EoS splits, we can destroy metadata_ptr, eos_metadata_ptr and
          eos_node_ptr
          * as these will be unique per path. Only cntr_ref_ptr is ref counted.
          * ext input port lists & flags must be cleared only when cargo ref count
          reaches zero.

          Even for non-flushing EOS below function needs to be called (to remove
          reference to ext in port)*/
         gen_topo_free_eos_cargo(topo_ptr, md_ptr, eos_metadata_ptr);

         eos_metadata_ptr->cntr_ref_ptr = NULL;

         break;
      }
      default: // For MODULE_CMN_MD_ID_DFG, this is sufficient
      {
#ifdef METADATA_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         "MD_DBG: detaching MD from cntr, md node_ptr 0x%p, metadata_ptr 0x%p, md_id 0x%08lX",
                         md_list_ptr,
                         md_ptr,
                         md_ptr->metadata_id);
#endif
      }
   }

   // decrement the thin topo cntr MD counter to indicate this MD has been detached and propagated to downstream.
   // even though the MD hasnt been truly destroyed and being tracked by IPMD driver.
   thin_topo_decr_active_md_nodes(topo_ptr, md_list_ptr);

   result = ar_result_to_capi_err(ar_result);

   return result;
}

static ar_result_t spf_ipcmd_destroy_node_info(uint32_t log_id, spf_ipcmd_node_info_t *md_node_ref_ptr)
{
   ar_result_t result = AR_EOK;

   IPMD_MSG(log_id, DBG_ERROR_PRIO, "MD_DBG:  destorying node info 0x%p");

   // free MD related memory
   if (md_node_ref_ptr->md_ptr)
   {
      IPMD_MSG(log_id,
               DBG_ERROR_PRIO,
               "MD_DBG:  destorying MD id %lu 0x%p",
               md_node_ref_ptr->md_ptr->obj_ptr->metadata_id,
               md_node_ref_ptr->md_ptr);

      module_cmn_md_t *ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;

      // generic metadata is assumed to not require deep cloning
      uint32_t is_out_band = ref_md_ptr->metadata_flag.is_out_of_band;
      if (is_out_band)
      {
         bool_t pool_used = spf_lpi_pool_is_addr_from_md_pool(ref_md_ptr->metadata_ptr);
         gen_topo_check_free_md_ptr(&(ref_md_ptr->metadata_ptr), pool_used);
      }

      if (md_node_ref_ptr->total_ref_count)
      {
         IPMD_MSG(log_id,
                  DBG_ERROR_PRIO,
                  "MD: process tr_md_event, dropping MD ref count is non zero, total_ref_count %lu",
                  md_node_ref_ptr->total_ref_count);
      }

      bool_t pool_used = spf_lpi_pool_is_addr_from_md_pool(ref_md_ptr);
      gen_topo_check_free_md_ptr((void **)&(md_node_ref_ptr->md_ptr->obj_ptr), pool_used);

      module_cmn_md_list_t *node_ptr = (module_cmn_md_list_t *)(md_node_ref_ptr->md_ptr);

      spf_list_find_delete_node((spf_list_node_t **)&md_node_ref_ptr->md_ptr, ref_md_ptr, TRUE);
   }

   // free tracker node memory
   spf_list_find_delete_node((spf_list_node_t **)&spf_ipcmd_global.tracking_md_list_ptr, md_node_ref_ptr, TRUE);

   posal_memory_free(md_node_ref_ptr);

   return result;
}

void spf_ipcmd_convert_client_md_flag_to_int_md_flags(uint32_t               log_id,
                                                      uint32_t               client_md_flags,
                                                      module_cmn_md_flags_t *int_md_flags)
{
   int_md_flags->word           = 0;
   int_md_flags->version        = MODULE_CMN_MD_VERSION;
   int_md_flags->is_out_of_band = FALSE;
   int_md_flags->is_client_metadata =
      spf_get_bits(client_md_flags, MD_HEADER_FLAGS_BIT_MASK_CLIENT_INFO, MD_HEADER_FLAGS_SHIFT_CLIENT_INFO);
   int_md_flags->is_client_metadata = ~int_md_flags->is_client_metadata;
   int_md_flags->tracking_mode      = spf_get_bits(client_md_flags,
                                              MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
                                              MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);
   int_md_flags->tracking_policy    = spf_get_bits(client_md_flags,
                                                MD_HEADER_FLAGS_BIT_MASK_TRACKING_EVENT_POLICY,
                                                MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);
   int_md_flags->buf_sample_association =
      spf_get_bits(client_md_flags, MD_HEADER_FLAGS_BIT_MASK_ASSOCIATION, MD_HEADER_FLAGS_SHIFT_ASSOCIATION_FLAG);

   int_md_flags->needs_propagation_to_client_buffer =
      spf_get_bits(client_md_flags,
                   MD_HEADER_FLAGS_BIT_MASK_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER,
                   MD_HEADER_FLAGS_SHIFT_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER_FLAG);

   int_md_flags->needs_propagation_to_client_buffer = ~int_md_flags->needs_propagation_to_client_buffer;
}

// This event should have been raised by raised from IPC RX to increment ref count on receving MD and decremented by IPC
// Tx when it starts propagating MD downstream. Because the tracker in originating domain needs to know which peer proc
// domain has cloned/received the MD in order to increment the ref count per MD.
ar_result_t spf_ipcmd_raise_event_to_update_ref_count(uint32_t                          log_id,
                                                      module_cmn_md_tracking_payload_t *md_tracking_ptr,
                                                      uint32_t                          metadata_id,
                                                      bool_t                            is_increment_ref_count)
{
   ar_result_t result = AR_EOK;

   if (NULL == md_tracking_ptr)
   {
      IPMD_MSG(log_id, DBG_ERROR_PRIO, "MD_DBG: invalid tracking payload info, failed to raise cloning event");
      return AR_EBADPARAM;
   }

   uint32_t opcode =
      is_increment_ref_count ? EVENT_ID_IPC_METADATA_INCR_REF_COUNT : EVENT_ID_IPC_METADATA_DECR_REF_COUNT;
   metadata_tracking_event_t md_te_payload = { 0 };

   md_te_payload.metadata_id            = metadata_id;
   md_te_payload.source_module_instance = md_tracking_ptr->src_port;
   md_te_payload.module_instance_id     = 0;
   md_te_payload.token_lsw              = md_tracking_ptr->token_lsw;
   md_te_payload.token_msw              = md_tracking_ptr->token_msw;
   md_te_payload.flags                  = 0;
   md_te_payload.status                 = 0; // ignore

   bool_t is_registered = FALSE;
   (void)__gpr_cmd_is_registered(md_tracking_ptr->src_port, &is_registered);

   // if stream close is done prior to render EOS, then client must not receive render EOS
   if (is_registered)
   {
      gpr_cmd_alloc_send_t args;
      args.src_domain_id = md_tracking_ptr->src_domain_id;
      args.dst_domain_id = md_tracking_ptr->dst_domain_id;
      args.src_port      = md_tracking_ptr->src_port;
      args.dst_port      = md_tracking_ptr->dest_port;
      args.token         = md_tracking_ptr->token_msw;
      args.opcode        = opcode;
      args.payload       = &md_te_payload;
      args.payload_size  = sizeof(metadata_tracking_event_t);
      args.client_data   = 0;
      __gpr_cmd_alloc_send(&args);

      IPMD_MSG(log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: raise ref count update event for MD_ID (0x%lx) "
               "(src port, dst_port) : 0x%lX, 0x%lX), cmd_opcode  0x%lX is_increment_ref_count?%lu",
               metadata_id,
               md_tracking_ptr->src_port,
               md_tracking_ptr->dest_port,
               opcode,
               is_increment_ref_count);
   }
   else
   {
      IPMD_MSG(log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: Failed to raise ref count updatee event for MD_ID (0x%lx) "
               "(src port, dst_port) : 0x%lX, 0x%lX), cmd_opcode  0x%lX, is_increment_ref_count?%lu dest not "
               "registered with GPR",
               metadata_id,
               md_tracking_ptr->src_port,
               md_tracking_ptr->dest_port,
               opcode,
               is_increment_ref_count);
   }

   return result;
}