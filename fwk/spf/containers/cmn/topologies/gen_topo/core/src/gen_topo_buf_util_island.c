/**
 * \file gen_topo_buf_util_island.c
 * \brief
 *     This file contains utility functions for GEN_CNTR buffer handling
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_buf_mgr.h"

/**
 * recursive.
 *
 * nblc is inplace if all modules in the nblc are inplace
 */
static bool_t gen_topo_is_inplace_until_nblc_end(gen_topo_t *            topo_ptr,
                                                 gen_topo_module_t *     module_ptr,
                                                 gen_topo_output_port_t *start_out_port_ptr)
{
   // only if nblc end is ext-out port, we can see a buffer there by virtue of assignment in
   // gen_cntr_init_after_popping_peer_cntr_out_buf
   if (!start_out_port_ptr->nblc_end_ptr)
   {
      return FALSE;
   }

   gen_topo_output_port_t *curr_out_port_ptr = start_out_port_ptr;
   while (curr_out_port_ptr != start_out_port_ptr->nblc_end_ptr)
   {
      gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)curr_out_port_ptr->gu.conn_in_port_ptr;

      if (!next_in_port_ptr)
      {
         return FALSE;
      }

      gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr;

      if (!next_module_ptr->flags.inplace) // inplace is always SISO
      {
         return FALSE;
      }

      if (next_module_ptr->gu.output_port_list_ptr)
      {
         curr_out_port_ptr = (gen_topo_output_port_t *)next_module_ptr->gu.output_port_list_ptr->op_port_ptr;
      }
      else
      {
         return FALSE;
      }
   }

   if (curr_out_port_ptr == start_out_port_ptr->nblc_end_ptr)
   {
      // all modules in this nblc are inplace.
      return TRUE;
   }

   return FALSE;
}

static bool_t gen_topo_is_inplace_nblc_from_ext_input_util_(gen_topo_t            *topo_ptr,
                                                            gen_topo_module_t     *module_ptr,
                                                            gen_topo_input_port_t *start_in_port_ptr)
{
   // is nblc start from ext-in port.
   if (!start_in_port_ptr->nblc_start_ptr || !start_in_port_ptr->nblc_start_ptr->gu.ext_in_port_ptr ||
       !start_in_port_ptr->nblc_end_ptr)
   {
      return FALSE;
   }

   gen_topo_input_port_t *next_in_port_ptr = start_in_port_ptr;
   while (next_in_port_ptr != start_in_port_ptr->nblc_end_ptr)
   {
      // if the next module is inplace then it's SISO
      gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr;
      if (!next_module_ptr->flags.inplace)
      {
         return FALSE;
      }

      if (next_module_ptr->gu.output_port_list_ptr)
      {
         gen_topo_output_port_t *next_out_port_ptr =
            (gen_topo_output_port_t *)next_module_ptr->gu.output_port_list_ptr->op_port_ptr;
         next_in_port_ptr = (gen_topo_input_port_t *)next_out_port_ptr->gu.conn_in_port_ptr;
      }
      else
      {
         return FALSE;
      }
   }

   // we reached the end of the inplace without hitting non-inplace module, hence nblc is inplace.
   if (next_in_port_ptr == start_in_port_ptr->nblc_end_ptr)
   {
      return TRUE;
   }

   return FALSE;
}

bool_t gen_topo_is_inplace_nblc_from_ext_input(gen_topo_t            *topo_ptr,
                                               gen_topo_module_t     *module_ptr,
                                               gen_topo_input_port_t *start_in_port_ptr)
{
   return gen_topo_is_inplace_nblc_from_ext_input_util_(topo_ptr, module_ptr, start_in_port_ptr);
}

// This function must be called when ever the module raise,
// The purpose of marking the buffers to force return is, in low latency cases buffers can be held based on statically.
// based on the inplace status of the modules. But if one of the modules changes its inplace natures, earlier static
// buffer assignment is no longer valid and buffers should be re-assinged based on the new inplace nature of the
// modules. For the same reason, buffer are marked for forced return and susbequent return_buf() calls in topo will free
// the earlier static assignment and the following susbequent get_bufs() will assign the new buffers based on new
// inplace status. new inplace status of the modules.
//
//  1. a dynamic inplace event
//  2. Module changes from bypass to non-bypass, or vice versa
capi_err_t gen_topo_mark_buf_mgr_buffers_to_force_return(gen_topo_t *topo_ptr)
{
   // only loop through started_sorted_module_list_ptr. Modules which are part of non-started SG should not be using any
   // buffer statically anyway. this also ensures that the modules are not being operated on simultaneously from the
   // thread-pool (for async command handling) and the container thread which might be running this event handling.
   for (gu_module_list_t *module_list_ptr = topo_ptr->started_sorted_module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         // mark for forced return
         if ((GEN_TOPO_BUF_ORIGIN_BUF_MGR)&in_port_ptr->common.flags.buf_origin)
         {
            in_port_ptr->common.flags.force_return_buf = TRUE;

            // returns the buffer only if actual data len =0
            gen_topo_input_port_return_buf_mgr_buf(topo_ptr, in_port_ptr);
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         // mark for forced return
         if ((GEN_TOPO_BUF_ORIGIN_BUF_MGR)&out_port_ptr->common.flags.buf_origin)
         {
            // mark for forced return
            out_port_ptr->common.flags.force_return_buf = TRUE;

            // returns the buffer only if actual data len =0
            gen_topo_output_port_return_buf_mgr_buf(topo_ptr, out_port_ptr);
         }
      }
   }

   return CAPI_EOK;
}

/**
 * Dont call this function directly, use gen_topo_check_get_out_buf_from_buf_mgr() instead
 *
 * Buffer management in topo-cmn
 *
 * First module calls gen_topo_check_get_in_buf_from_buf_mgr from container
 * Other modules call gen_topo_check_get_in_buf_from_buf_mgr before copying data from prev module output
 *    - previous buf is reused unless there's already a buf at the module input.
 * All modules call gen_topo_check_get_out_buf_from_buf_mgr before calling process.
 *    - for inplace modules input buf is reused.
 * After every module process its input buffer and previous out buf are returned if
 *    empty by calling gen_topo_return_one_buf_mgr_buf.
 * Last module releases the buf from container by calling gen_topo_return_one_buf_mgr_buf
 */
/**
 * get out buf from buf mgr
 * in case of inplace out buf can be same as in buf, which should be assigned by this time.
 */
ar_result_t gen_topo_check_get_out_buf_from_buf_mgr_util_(gen_topo_t *            topo_ptr,
                                                          gen_topo_module_t *     module_ptr,
                                                          gen_topo_output_port_t *curr_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (gen_topo_is_inplace_or_disabled_siso(module_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
      if (in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         gen_topo_assign_bufs_ptr(topo_ptr->gu.log_id,
                                  &curr_out_port_ptr->common,
                                  &in_port_ptr->common,
                                  module_ptr,
                                  curr_out_port_ptr->gu.cmn.id);
         // in place modules are SISO (already checked)
         curr_out_port_ptr->common.flags.buf_origin = in_port_ptr->common.flags.buf_origin;
         gen_topo_buf_mgr_wrapper_inc_ref_count(&curr_out_port_ptr->common);
      }
   }
   else
   {
      // check if the buffer can be reused from the nblc end.
      // 1. from ext out case, icb buffer will be assigned.
      // 2. for internal outputs, nblc ends module's input buffer is assigned to the current output.
      if (gen_topo_is_inplace_until_nblc_end(topo_ptr, module_ptr, curr_out_port_ptr))
      {
         // for last module, ext-out buf is assigned already at its output port. In case we can reach the ext out
         // through inplace nblc, then we can use that buffer throughout. This saves additional topo buf allocation.
         // ext out buf must be present, big enough and empty
         if (curr_out_port_ptr->nblc_end_ptr->gu.ext_out_port_ptr &&
             curr_out_port_ptr->nblc_end_ptr->common.bufs_ptr[0].data_ptr
#ifdef SAFE_MODE
             &&
             (curr_out_port_ptr->nblc_end_ptr->common.bufs_ptr[0].max_data_len >=
              curr_out_port_ptr->common.max_buf_len) &&
             (0 == curr_out_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len)
#endif
                )
         {
            gen_topo_assign_bufs_ptr(topo_ptr->gu.log_id,
                                     &curr_out_port_ptr->common,
                                     &curr_out_port_ptr->nblc_end_ptr->common,
                                     module_ptr,
                                     curr_out_port_ptr->gu.cmn.id);

            curr_out_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
            // no ref counting for borrowed ext-buf
         }
         else if (curr_out_port_ptr->nblc_end_ptr->gu.conn_in_port_ptr)
         {
            /**
             * For low latency optimization,
             * If nblc end module has an input buffer it can be borrowed by the module upstream.
             * max data length is adjusted based on the free space in nblc end buffer. This optimization
             * also makes sure that upstream module produces just enough to fill the nblc end modules input.
             *
             * Ideally modules will not hold the buffer at its input unless it has partial data and requires
             * data buffering module.
             *
             * For example, consider DTMF_GEN->MFC->SAL graph. dtmf generator can reuse the buffer from MFC's input.
             * This will allow dtmf to produce data as must as req at mfc input and avoids pile up.
             *
             * Rd shm module would assign a borrowed buffer(GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED) to its input, which
             * can be further borrowed by the modules in the NBLC upstream. For example Enc -> log -> Rd Shm. Enc output
             * can borrow Rd shm's input borrowed buffer.
             */
            gen_topo_input_port_t *nblc_end_in_port_ptr =
               (gen_topo_input_port_t *)curr_out_port_ptr->nblc_end_ptr->gu.conn_in_port_ptr;
            if (nblc_end_in_port_ptr->common.bufs_ptr[0].data_ptr &&
                ((TOPO_BUF_LOW_LATENCY == topo_ptr->buf_mgr.mode) ||
                 ( GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED == nblc_end_in_port_ptr->common.flags.buf_origin))

#ifdef SAFE_MODE
                && (nblc_end_in_port_ptr->common.bufs_ptr[0].max_data_len >= curr_out_port_ptr->common.max_buf_len) &&
                (0 == nblc_end_in_port_ptr->common.bufs_ptr[0].actual_data_len)
#endif
            )
            {
               if (!(((SPF_IS_PCM_DATA_FORMAT(curr_out_port_ptr->common.media_fmt_ptr->data_format)) &&
                      (TOPO_INTERLEAVED != curr_out_port_ptr->common.media_fmt_ptr->pcm.interleaving)) ||
                     ((SPF_IS_PCM_DATA_FORMAT(nblc_end_in_port_ptr->common.media_fmt_ptr->data_format)) &&
                      (TOPO_INTERLEAVED != nblc_end_in_port_ptr->common.media_fmt_ptr->pcm.interleaving)) ||
                     (nblc_end_in_port_ptr->gu.cmn.module_ptr->module_type == AMDB_MODULE_TYPE_DEPACKETIZER) ||
                     (curr_out_port_ptr->gu.cmn.module_ptr->module_type == AMDB_MODULE_TYPE_PACKETIZER)))
               {
                  /* In cases where hwep -> cop depack or cop pack -> hwep are in the same container then the hwep
                   * should not share the buffer with cop pack/depack because there is potential unconsumed data at
                   * the boundary almost every process call. */
                  /** nblc end might have some data. point after that data. interleaved data means only one buf.*/

                  curr_out_port_ptr->common.bufs_ptr[0].data_ptr =
                     nblc_end_in_port_ptr->common.bufs_ptr[0].data_ptr +
                     nblc_end_in_port_ptr->common.bufs_ptr[0].actual_data_len;
                  curr_out_port_ptr->common.bufs_ptr[0].max_data_len =
                     nblc_end_in_port_ptr->common.bufs_ptr[0].max_data_len -
                     nblc_end_in_port_ptr->common.bufs_ptr[0].actual_data_len;
                  curr_out_port_ptr->common.flags.buf_origin =
                     (GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED == nblc_end_in_port_ptr->common.flags.buf_origin)
                        ? GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED
                        : GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED;
               }
            }
         }
      }
   }

   // even after nblc end look up above, if we don't have buf, use topo buf mgr
   if (NULL == curr_out_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      result = gen_topo_buf_mgr_wrapper_get_buf(topo_ptr, &curr_out_port_ptr->common);
   }
#ifdef BUF_MGMT_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: Port 0x%lx, got output buffer 0x%p, size %lu, max_buf_len %lu, buf_origin%u",
            module_ptr->gu.module_instance_id,
            curr_out_port_ptr->gu.cmn.id,
            curr_out_port_ptr->common.bufs_ptr[0].data_ptr,
            curr_out_port_ptr->common.bufs_ptr[0].max_data_len,
            curr_out_port_ptr->common.max_buf_len,
            curr_out_port_ptr->common.flags.buf_origin);
#endif

   return result;
}

/* Dont call this function directly, use gen_topo_check_get_in_buf_from_buf_mgr() instead.
 * connection = prev_out_port_ptr -> curr_in_port_ptr
 */
ar_result_t gen_topo_check_get_in_buf_from_buf_mgr_util_(gen_topo_t *            topo_ptr,
                                                         gen_topo_input_port_t * curr_in_port_ptr,
                                                         gen_topo_output_port_t *prev_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)curr_in_port_ptr->gu.cmn.module_ptr;
   // If next module doesn't have a buffer, use current module's output buf as next module input bufs_ptr[0].
   // max len must be same or higher
   // although in most cases bufs_num is same, there care cases with channel mixer, virtualizer etc (virt_tu_7)
   // where they can be different due to partial data.
   if (prev_out_port_ptr && prev_out_port_ptr->common.bufs_ptr[0].data_ptr &&
       (prev_out_port_ptr->common.bufs_ptr[0].max_data_len >= curr_in_port_ptr->common.max_buf_len_per_buf) &&
       (prev_out_port_ptr->common.sdata.bufs_num == curr_in_port_ptr->common.sdata.bufs_num))
   {
      // generally mismatch in bufs_num shouldn't happen as we have same MF b/w input and output when in-place.
      gen_topo_assign_bufs_ptr(topo_ptr->gu.log_id,
                               &curr_in_port_ptr->common,
                               &prev_out_port_ptr->common,
                               module_ptr,
                               curr_in_port_ptr->gu.cmn.id);

      curr_in_port_ptr->common.flags.buf_origin = prev_out_port_ptr->common.flags.buf_origin;
      gen_topo_buf_mgr_wrapper_inc_ref_count(&curr_in_port_ptr->common);
      // don't release prev_out_port_ptr->common.bufs_ptr[0].data_ptr here, as return_buf is called.
   }
   else
   {
      if (curr_in_port_ptr->nblc_end_ptr &&
          gen_topo_is_inplace_nblc_from_ext_input_util_(topo_ptr, module_ptr, curr_in_port_ptr))
      {
         gen_topo_input_port_t *nblc_end_ptr = curr_in_port_ptr->nblc_end_ptr;
         // if nblc end is inplace, & it has a buf
         if (nblc_end_ptr->common.bufs_ptr[0].data_ptr /*&&
             (nblc_end_ptr->common.bufs_ptr[0].max_data_len >= curr_in_port_ptr->common.bufs_ptr[0].max_data_len) &&
             (0 == nblc_end_ptr->common.bufs_ptr[0].actual_data_len)*/)
         // not being empty is fine because, in gen_cntr_setup_internal_input_port_and_preprocess, we copy only what
         // this buf can hold. for the same reason, not of max len is also fine.
         {
            // for Raw compressed/packetized etc or PCM interleaved, we can use the contiguous data as a new buffer with
            // reduced max-length.
            // But for PCM deinterleaved data, nblc_end will have LLL_____|RRR_____. We will need num_ch num of pointers
            // for the empty space. In fwk, we cannot carry so many ptrs.
            if (!(((SPF_IS_PCM_DATA_FORMAT(curr_in_port_ptr->common.media_fmt_ptr->data_format)) &&
                   (TOPO_INTERLEAVED != curr_in_port_ptr->common.media_fmt_ptr->pcm.interleaving)) ||
                  ((SPF_IS_PCM_DATA_FORMAT(nblc_end_ptr->common.media_fmt_ptr->data_format)) &&
                   (TOPO_INTERLEAVED != nblc_end_ptr->common.media_fmt_ptr->pcm.interleaving))))
            {
               /** nblc end might have some data. point after that data. */
               // in interleaved cases there should be only one buffer
               curr_in_port_ptr->common.bufs_ptr[0].data_ptr =
                  nblc_end_ptr->common.bufs_ptr[0].data_ptr + nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
               curr_in_port_ptr->common.bufs_ptr[0].max_data_len =
                  nblc_end_ptr->common.bufs_ptr[0].max_data_len - nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
               curr_in_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED;
            }
         }
      }

      // Why we don't use inplace-nblc-end's buffer for first module?
      //  the inplace-nblc-end's buffer contains some processed data. If we give this buf to first module, then
      //  we need to make sure, we offset by actual_len. But we cannot reduce max_len as max_len is decided by threshold
      //  propagation. This leads to mem corruption in a) modules which see higher frame len b) for deinterleaved
      //  unpacked
      //  channel spacing calculation in gen_cntr_copy_peer_or_olc_client_input goes wrong.
      if (NULL == curr_in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         result = gen_topo_buf_mgr_wrapper_get_buf(topo_ptr, &curr_in_port_ptr->common);
      }
   }

#ifdef BUF_MGMT_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: Port 0x%lx, got input buffer 0x%p, size %lu, max_buf_len %lu, buf_origin%u",
            curr_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
            curr_in_port_ptr->gu.cmn.id,
            curr_in_port_ptr->common.bufs_ptr[0].data_ptr,
            curr_in_port_ptr->common.bufs_ptr[0].max_data_len,
            curr_in_port_ptr->common.max_buf_len_per_buf,
            curr_in_port_ptr->common.flags.buf_origin);
#endif

   return result;
}

/*********************************
 *
 *
 * TOPO BUF STATIC FUNCTIONS
 *
 *
 *
 ************************************/

static int8_t *topo_buf_manager_allocate_buf(gen_topo_t *topo_ptr, uint32_t buf_size)
{
   topo_buf_manager_element_t *buf_element_ptr;
   int8_t *                    buf_ptr;
   int8_t *                    ptr;

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "---topo_buf_manager_allocate_buf---");
#endif

   uint32_t alloc_size = TBF_EXTRA_ALLOCATION + buf_size;

   ptr = (int8_t *)posal_memory_malloc(alloc_size, topo_ptr->heap_id);
   if (NULL == ptr)
   {
      TBF_MSG(topo_ptr->gu.log_id,
              DBG_ERROR_PRIO,
              "topo_buf_manager_allocate_buf: Failed to allocate memory for the "
              "buffer");
      return NULL;
   }

   buf_element_ptr = (topo_buf_manager_element_t *)ptr;

   buf_element_ptr->list_node.obj_ptr  = buf_element_ptr;
   buf_element_ptr->list_node.prev_ptr = NULL;
   buf_element_ptr->list_node.next_ptr = NULL;
   buf_element_ptr->unused_count       = 0;
   buf_element_ptr->ref_count          = 1;
   buf_element_ptr->size               = buf_size;
   buf_ptr                             = (int8_t *)buf_element_ptr + TBF_BUF_PTR_OFFSET;

   topo_ptr->buf_mgr.current_memory_allocated += buf_size;
   if (topo_ptr->buf_mgr.current_memory_allocated > topo_ptr->buf_mgr.max_memory_allocated)
   {
      topo_ptr->buf_mgr.max_memory_allocated = topo_ptr->buf_mgr.current_memory_allocated;
   }

   topo_ptr->buf_mgr.total_num_bufs_allocated++;

   return buf_ptr;
}

void topo_buf_manager_destroy_all_unused_buffers(gen_topo_t *topo_ptr)
{
   topo_buf_manager_element_t *last_element_ptr = (topo_buf_manager_element_t *)(topo_ptr->buf_mgr.last_node_ptr);
   spf_list_node_t *           prev_node_ptr;

   // Iterate and delete the buffers from the end of the list, if the unused count reaches the limit
   // End the iteration on hitting a node whose unused count is less than MAX.
   while (last_element_ptr && (MAX_BUF_UNUSED_COUNT <= last_element_ptr->unused_count) &&
          (topo_ptr->buf_mgr.total_num_bufs_allocated < last_element_ptr->unused_count))
   {
#ifdef TOPO_BUF_MGR_DEBUG
      TBF_MSG(topo_ptr->gu.log_id,
              DBG_ERROR_PRIO,
              "topo_buf_manager_check_destroy_unused_buf: destroying last buffer "
              "in the stack...");
#endif
      prev_node_ptr = topo_ptr->buf_mgr.last_node_ptr->prev_ptr;
      if (NULL != prev_node_ptr)
      {
         prev_node_ptr->next_ptr = NULL;
      }
      else /* case where the list has only one element */
      {
         topo_ptr->buf_mgr.head_node_ptr = NULL;
      }
      topo_ptr->buf_mgr.current_memory_allocated -= last_element_ptr->size;
      topo_ptr->buf_mgr.total_num_bufs_allocated--;

      gen_topo_exit_island_temporarily(topo_ptr);

      TBF_MSG(topo_ptr->gu.log_id,
              DBG_LOW_PRIO,
              "topo_buf_manager_check_destroy_unused_buf: destroyed buffer 0x%p. Total num of buffers %lu. Num used "
              "buffers %lu",
              (((topo_buf_manager_element_t *)topo_ptr->buf_mgr.last_node_ptr) + 1),
              topo_ptr->buf_mgr.total_num_bufs_allocated,
              topo_ptr->buf_mgr.num_used_buffers);

      int8_t *ptr = (int8_t *)topo_ptr->buf_mgr.last_node_ptr;
      posal_memory_free(ptr);
      topo_ptr->buf_mgr.last_node_ptr = prev_node_ptr;

      // move to the prev node
      last_element_ptr = (topo_buf_manager_element_t *)(topo_ptr->buf_mgr.last_node_ptr);
   }

   return;
}

static void topo_buf_manager_check_destroy_unused_buf(gen_topo_t *topo_ptr)
{
#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "---topo_buf_manager_check_destroy_unused_buf---");
#endif

   if (NULL == topo_ptr->buf_mgr.last_node_ptr)
   {
      return;
   }

   /*check if process needs to continue */
   uint64_t cur_ts = posal_timer_get_time();
   uint64_t diff   = cur_ts - topo_ptr->buf_mgr.prev_destroy_unused_call_ts_us;
   if (diff < TBF_UNUSED_BUFFER_CALL_INTERVAL_US)
   {
      return;
   }
   topo_ptr->buf_mgr.prev_destroy_unused_call_ts_us = cur_ts;

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_HIGH_PRIO,
           "topo_buf_manager_check_destroy_unused_buf: processing cur_ts:%lu, diff:%lu ",
           cur_ts,
           diff);
#endif

   /* Skip buffer free if the container has voted for island entry. If container is non-island, aggregated vote is going
      to be EXIT/DONT care so enters if case every time.
      We also call destroy buffers at the end of event/cmd handling, to free any pending buffers. */
   if (PM_ISLAND_VOTE_ENTRY != topo_ptr->flags.aggregated_island_vote)
   {
      topo_buf_manager_destroy_all_unused_buffers(topo_ptr);
   }

   // update unused count for the unused nodes at the end
   spf_list_node_t *last_node_ptr = topo_ptr->buf_mgr.last_node_ptr;
   while (NULL != last_node_ptr)
   {
      // check if buffers can be incremented
      topo_buf_manager_element_t *last_element_ptr = (topo_buf_manager_element_t *)last_node_ptr;
      if (last_element_ptr->unused_count < MAX_BUF_UNUSED_COUNT)
      {
         last_element_ptr->unused_count++;
#ifdef TOPO_BUF_MGR_DEBUG
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_HIGH_PRIO,
                         "topo_buf_manager_check_destroy_unused_buf: Incrementing last_element_ptr:%lp, "
                         "unused_count:%lu ",
                         last_element_ptr,
                         last_element_ptr->unused_count);
#endif
         break;
      }
      else // if unused count is already >= MAX, move and update the previous node
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_HIGH_PRIO,
                         "topo_buf_manager_check_destroy_unused_buf: Already MAX. last_element_ptr:%lp, "
                         "unused_count:%lu ",
                         last_element_ptr,
                         last_element_ptr->unused_count);
#endif
         last_node_ptr = last_node_ptr->prev_ptr;
      }
   }
}

ar_result_t topo_buf_manager_get_buf(gen_topo_t *topo_ptr, int8_t **buf_pptr, uint32_t buf_size)
{
   spf_list_node_t *           buf_mgr_list_ptr;
   topo_buf_manager_element_t *buf_element_ptr;
   spf_list_node_t *           buf_list_closest_size_ptr = NULL;

#ifdef SAFE_MODE
   if (0 == buf_size)
   {
      return AR_EOK;
   }

   if (NULL == buf_pptr)
   {
      TBF_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "topo_buf_manager_get_buf: buffer return pointer is NULL");
      return AR_EBADPARAM;
   }
#endif

   *buf_pptr = NULL;

   /* The buffer list is sorted in ascending order, so break on finding the buffer greater than or equal to
      requested size. */
   buf_mgr_list_ptr = topo_ptr->buf_mgr.head_node_ptr;
   while (NULL != buf_mgr_list_ptr)
   {
      buf_element_ptr = (topo_buf_manager_element_t *)buf_mgr_list_ptr->obj_ptr;
      if (buf_size <= buf_element_ptr->size)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_get_buf: buffer a found closest to requested size: %lu closest buf size: %lu",
                 buf_size,
                 buf_element_ptr->size);
#endif
         buf_list_closest_size_ptr = buf_mgr_list_ptr;

         // remove the buf_list_closest_size_ptr from the list and update the list.
         buf_element_ptr               = (topo_buf_manager_element_t *)buf_list_closest_size_ptr;
         *buf_pptr                     = (int8_t *)buf_element_ptr + TBF_BUF_PTR_OFFSET;
         buf_element_ptr->unused_count = 0;
         buf_element_ptr->ref_count    = 1;
         spf_list_remove_node_from_list_only(buf_list_closest_size_ptr);

         /* updating head node and last node if applicable */
         if (NULL == buf_list_closest_size_ptr->prev_ptr)
         {
            topo_ptr->buf_mgr.head_node_ptr = buf_list_closest_size_ptr->next_ptr;
         }
         if (NULL == buf_list_closest_size_ptr->next_ptr)
         {
            topo_ptr->buf_mgr.last_node_ptr = buf_list_closest_size_ptr->prev_ptr;
         }
         topo_ptr->buf_mgr.num_used_buffers++;

         goto destory_unused_buf_n_ret; /* Exit the loop on finding closest buf*/
      }
      LIST_ADVANCE(buf_mgr_list_ptr);
   }

   // if buffer couldnt be found
   if (!buf_list_closest_size_ptr)
   {
      gen_topo_exit_island_temporarily(topo_ptr);

      *buf_pptr = (int8_t *)topo_buf_manager_allocate_buf(topo_ptr, buf_size);
      if (NULL == *buf_pptr)
      {
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_ERROR_PRIO,
                 "topo_buf_manager_get_buf: Failed to allocate memory for the buffer, buf size: %lu",
                 buf_size);
         return AR_ENOMEMORY;
      }
      else
      {
         topo_ptr->buf_mgr.num_used_buffers++;
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_LOW_PRIO,
                 "topo_buf_manager_get_buf: Allocated buffer 0x%p of size %lu. Total num bufs allocated %lu. Num used "
                 "buffers %lu ",
                 *buf_pptr,
                 buf_size,
                 topo_ptr->buf_mgr.total_num_bufs_allocated,
                 topo_ptr->buf_mgr.num_used_buffers);
      }
   }

destory_unused_buf_n_ret:
   topo_buf_manager_check_destroy_unused_buf(topo_ptr);
   return AR_EOK;
}

void topo_buf_manager_return_buf(gen_topo_t *topo_ptr, int8_t *buf_ptr)
{
   spf_list_node_t *returned_buf_node_ptr;

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "topo_buf_manager_return_buf()");
#endif

   /* getting the address of list node of the returned buffer
    * memory allocated for each buffer node is populated as follows:
    * topo_buf_manager_element_t
    * buffer
    */
   returned_buf_node_ptr = (spf_list_node_t *)(buf_ptr - TBF_BUF_PTR_OFFSET);

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_LOW_PRIO,
           "topo_buf_manager_return_buf: returned buffer ptr: 0x%lx",
           returned_buf_node_ptr);
#endif

#ifdef SAFE_MODE
   // check for returning same buffer twice
   {
      spf_list_node_t *node_ptr = topo_ptr->buf_mgr.head_node_ptr;
      while (node_ptr)
      {
         if (returned_buf_node_ptr == node_ptr)
         {
            *((volatile uint32_t *)0) = 0;
         }
         node_ptr = node_ptr->next_ptr;
      }
   }
#endif

   topo_buf_manager_element_t *ret_buf_element_ptr = (topo_buf_manager_element_t *)returned_buf_node_ptr->obj_ptr;
   spf_list_node_t *           cur_list_node_ptr   = topo_ptr->buf_mgr.head_node_ptr;
   uint32_t                    buf_size            = ret_buf_element_ptr->size;

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "topo_buf_manager_return_buf: ret_element_size:%lu", buf_size);
#endif

   /* Insert the returned buffer in the sorted ascending list of buffers.*/
   while (NULL != cur_list_node_ptr)
   {
      topo_buf_manager_element_t *cur_buf_element_ptr = (topo_buf_manager_element_t *)cur_list_node_ptr->obj_ptr;
#ifdef TOPO_BUF_MGR_DEBUG
      TBF_MSG(topo_ptr->gu.log_id,
              DBG_HIGH_PRIO,
              "topo_buf_manager_return_buf: cur_element_size:%lu",
              cur_buf_element_ptr->size);
#endif
      // if returned buffer size is less than or equal to current insert before the cur element.
      if (buf_size <= cur_buf_element_ptr->size)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_return_buf: inseting before cur_element_size:%lu",
                 cur_buf_element_ptr->size);
#endif
         // insert before the cur node and return
         spf_list_insert_before_node(&(topo_ptr->buf_mgr.head_node_ptr), returned_buf_node_ptr, cur_list_node_ptr);
         topo_ptr->buf_mgr.num_used_buffers--;
         return;
      }

      // If returned buffer is of largest size, it will be inserted at the end.
      if (NULL == cur_list_node_ptr->next_ptr)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_return_buf: inseting at the tail. last_element_size:%lu",
                 cur_buf_element_ptr->size);
#endif
         cur_list_node_ptr->next_ptr     = returned_buf_node_ptr;
         returned_buf_node_ptr->next_ptr = NULL;
         returned_buf_node_ptr->prev_ptr = cur_list_node_ptr;
         break;
      }
      else
      {
         LIST_ADVANCE(cur_list_node_ptr);
         continue;
      }
   }

   // if head node is NULL, push it to head and update head
   if (NULL == topo_ptr->buf_mgr.head_node_ptr)
   {
      spf_list_push_node(&(topo_ptr->buf_mgr.head_node_ptr), returned_buf_node_ptr);
   }

   // if returned node is last node of the list, update last node ptr of buffer mgr
   if (NULL == returned_buf_node_ptr->next_ptr)
   {
      topo_ptr->buf_mgr.last_node_ptr = returned_buf_node_ptr;
   }

   topo_ptr->buf_mgr.num_used_buffers--;

   return;
}
