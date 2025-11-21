/**
 * \file st_topo_data_process_island.c
 * \brief
 *     This file contains functions for topo common data processing
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "gen_topo_buf_mgr.h"
#include "irm_cntr_prof_util.h"
#include "gen_topo_pure_st_i.h"

/**
 * 1. copies data from prev to next module, handles if prev has media fmt.
 * 2. TS disc is ignored for Signal triggered containers.
 * 3. EOF is set only if prev have mf.
 * 4. EOF flag is cleared when input data of the next module is fully consumed.
 * 5. Also returns if atleast one input has a buffer
 *
 * Between 2 modules EOF may be set due to above reasons or due to propagation from prev module.
 */
GEN_TOPO_STATIC ar_result_t st_topo_check_copy_between_modules(gen_topo_t *            topo_ptr,
                                                               gu_module_list_t *      next_module_list_ptr,
                                                               gen_topo_module_t *     next_module_ptr,
                                                               gen_topo_module_t *     prev_module_ptr,
                                                               gen_topo_input_port_t * next_in_port_ptr,
                                                               gen_topo_output_port_t *prev_out_port_ptr,
                                                               bool_t *                atleast_one_input_has_data)
{
   ar_result_t            result;
   topo_buf_t *           next_bufs_ptr    = next_in_port_ptr->common.bufs_ptr;
   topo_buf_t *           prev_bufs_ptr    = prev_out_port_ptr->common.bufs_ptr;
   capi_stream_data_v2_t *next_sdata_ptr   = &next_in_port_ptr->common.sdata;
   capi_stream_data_v2_t *prev_sdata_ptr   = &prev_out_port_ptr->common.sdata;
   uint32_t               prev_sdata_flags = prev_sdata_ptr->flags.word;
   uint32_t               next_sdata_flags = next_sdata_ptr->flags.word; // for debug

   bool_t prev_has_data_next_can_accept = FALSE;
   if (prev_bufs_ptr[0].actual_data_len)
   {
      uint32_t empty_space_in_next_inp_per_buf = next_bufs_ptr[0].data_ptr
                                                    ? (next_bufs_ptr[0].max_data_len - next_bufs_ptr[0].actual_data_len)
                                                    : next_in_port_ptr->common.max_buf_len_per_buf;
      prev_has_data_next_can_accept = (empty_space_in_next_inp_per_buf > 0);
   }

   if (prev_out_port_ptr->common.flags.media_fmt_event)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "media format changed between module 0x%lX and Module 0x%lX. next buf actual len %lu",
                      prev_module_ptr->gu.module_instance_id,
                      next_module_ptr->gu.module_instance_id,
                      next_bufs_ptr[0].actual_data_len);

      // propagate only after next module's input has no data left.
      if (0 == next_bufs_ptr[0].actual_data_len)
      {
         gen_topo_propagate_media_fmt_from_module(topo_ptr, TRUE /* is_data_path*/, next_module_list_ptr);
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, media_fmt_event);
         // ensures we break process, propagate MF from next module, assign threshold, bufs_num and come back.

         // Copying media fmt could results in thresh change. after finishing thresh handling etc, we can copy buf.
         // Copying data now causes unnecessary buf creation so leave this function here.
         return AR_EOK;
      }
   }

/*
 * For signal triggered cntrs,
 *   1. Timestamp discontinuity is ignored, so it cannot cause disc.
 *   2. MF propagates from the prev output by dropping the data immediately, so it cannot cause discontuinty.
 *   3. Buffer is needed to be obtained (get_buf), at EOS even if prev_has_data_next_can_accept is False.
 *   4. EOF can be set only due to EOS for Signal triggers.
 *
 *  TODO: for ST containers is it necessary to handle non-flushing ?
 * If next port already has flushing eos, we can not copy data. Thus only if next_has_end_flags is due to
 * discontinuity, this is not applicable for ST ?
 * we cannot copy new data, but if it's due to EOS we can. When this happens flushing EOS is changed to non-flushing
 * by gen_topo_check_modify_flushing_eos
 *
 * If EOS is non-flushing, then we should have no partial data
 * But if partial data occurs, its ok to behave like flushing EoS, where we push out algo delay, squeeze etc.
 *
 * non-flushing EOS is treated like begin metadata
 */
#ifdef VERBOSE_DEBUGGING
   bool_t dbg_is_force_process = FALSE;
   bool_t discontinuity        = FALSE;
   bool_t copy_begin_flags = FALSE; /* whether to copy metadata pertaining to beginning of buf, such as TS, erasure */
   bool_t copy_end_flags   = FALSE; /* whether to copy metadata pertaining to end of buf, such as TS discont*/
#endif
   bool_t get_buf   = FALSE; /* whethet to get a buf. a buf is required to call process on module.  */
   bool_t copy_data = FALSE; /* whether to copy data to the next port buf */

   // check if not steady state, due to metadata or flags
   bool_t is_steady_state =
      FALSE == (((prev_sdata_flags | next_sdata_flags) & ~(SDATA_FLAG_STREAM_DATA_VERSION_BIT_MASK)) ||
                (prev_sdata_ptr->metadata_list_ptr || next_sdata_ptr->metadata_list_ptr));

   if (is_steady_state)
   {
      if (prev_has_data_next_can_accept)
      {
         get_buf   = TRUE;
         copy_data = TRUE;
      }
   }
   else // is not steady state copy between modules
   {
      bool_t next_has_end_flags = next_sdata_ptr->flags.end_of_frame;

      // discontinuity is about changes from prev modules. force_process is about both next and prev
      // Do not mark discontinuity for EOS as discontinuity is not before the data but afterwards.
      //   E.g., if we have 2 ms in next module and 3 ms in prev module followed by EOS, then 2+3+eos is the sequence,
      //   not
      //   2+eos+3.
      bool_t prev_has_end_flags = prev_sdata_ptr->flags.end_of_frame || prev_sdata_ptr->flags.marker_eos;

      // In general, always process if there's metadata (for non-EOS, if MD is stuck inside the module we don't call)
      bool_t is_force_process =
         (prev_sdata_ptr->metadata_list_ptr || next_sdata_ptr->metadata_list_ptr || next_has_end_flags ||
          prev_has_end_flags || next_sdata_ptr->flags.marker_eos || prev_sdata_ptr->flags.erasure);

      /** Copy if there's data to copy and downstream needs it, except at EoS */
      if (prev_has_data_next_can_accept || is_force_process)
      {
         // check for timestamp discontinuity if next buf has nonzero data & we are copying data.
         // next input timestamp is updated to have the expected timestmap
         // after process in module_process, but existing data needs to be accounted here.
         // for raw compr cases, we cannot determine expected TS
         bool_t ts_disc = gen_topo_check_copy_incoming_ts(topo_ptr,
                                                          next_module_ptr,
                                                          next_in_port_ptr,
                                                          prev_sdata_ptr->timestamp,
                                                          (bool_t)prev_sdata_ptr->flags.is_timestamp_valid,
                                                          FALSE /*continue timestamp */);
         if (ts_disc)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_LOW_PRIO,
                            "Module 0x%lX: Discontinuity ignored for signal triggerred mode",
                            next_module_ptr->gu.module_instance_id);
         }

// copy_begin_flags: timestamp, would be overwritten if copied before actual len becomes zero.
#ifdef VERBOSE_DEBUGGING
         dbg_is_force_process = is_force_process;
         discontinuity        = ts_disc;
         copy_begin_flags     = (0 == next_bufs_ptr[0].actual_data_len);
         copy_end_flags       = TRUE;
#endif

         if (0 == next_bufs_ptr[0].actual_data_len)
         {
            // timestamp is handled in gen_topo_check_copy_incoming_ts
            next_sdata_ptr->flags.erasure = prev_sdata_ptr->flags.erasure;
            // clear after copying to next port
            prev_sdata_ptr->flags.erasure = FALSE;
         }

         // even if next module actual_data_len can never become zero, EoS has to go; even for media fmt discontinuity.

         // if (copy_end_flags)
         {
            // When propagating EoS, EoS may get stuck at some input. previous modules would be cleared. Hence OR with
            // both
            // prev and next. EOF is cleared either either by module (if it supports metadata) or fwk after processing.
            next_sdata_ptr->flags.end_of_frame |= (next_has_end_flags || prev_has_end_flags);

            // clear after copying to next port
            prev_sdata_ptr->flags.end_of_frame = FALSE;
         }

         get_buf = TRUE;
         copy_data =
            !next_has_end_flags ||
            next_sdata_ptr->flags.marker_eos; // flushing EOS will be changed to non-flushing if new data is copied.
      }
   }

   if (get_buf)
   {
      // for ST contianers, mark need more input to false by default
      // if get buf is true, it means prev_has_data_next_can_accept = TRUE
      next_in_port_ptr->flags.need_more_input = FALSE;
      // this flag is used in gen_topo_in_port_needs_input_buf

      /*Ex: if each leg of the SAL input is a separate subgraph, we need to check if this
         input port's state is "started" before we allocate buffers for it.
         If upstream of any input port is not started, we continue.*/
      if (AR_DID_FAIL(result = gen_topo_check_get_in_buf_from_buf_mgr(topo_ptr, next_in_port_ptr, prev_out_port_ptr)))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         "Failed to get buf mgr buffer for Module 0x%lX, in_port 0x%lx",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id);
         // continue with other ports if any.
         *atleast_one_input_has_data |= FALSE; /* Must be or operation */
      }
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX -> next module 0x%lX: next timestamp %lu (0x%lx%lx). prev_has_data_next_can_accept = "
                   "%u. port cmn flags prev0x%lX, next0x%lX",
                   prev_module_ptr->gu.module_instance_id,
                   next_module_ptr->gu.module_instance_id,
                   (int32_t)next_sdata_ptr->timestamp,
                   (int32_t)(next_sdata_ptr->timestamp >> 32),
                   (int32_t)next_sdata_ptr->timestamp,
                   prev_has_data_next_can_accept,
                   prev_out_port_ptr->common.flags.word,
                   next_in_port_ptr->common.flags.word);

   // Flags: last in capi_stream_flags_t is MSB.  Ver1|   Version0|Eras|M3|M2|  M1|EOS|EOF|TSValid
   bool_t   in_needs_data = TRUE;
   uint32_t temp = (dbg_is_force_process << 6) | (discontinuity << 5) | (copy_begin_flags << 4) | (get_buf << 3) |
                   (copy_data << 2) | (copy_end_flags << 1) | in_needs_data;
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "  temp_flags0x%lX prev_sdata_flags0x%lX -> next_sdata_flags0x%lX, next_port_flag0x%lX, "
                   "next bytes_from_prev_buf %lu is_steady_state:%lu",
                   temp,
                   prev_sdata_flags,
                   next_sdata_flags,
                   next_in_port_ptr->flags.word,
                   next_in_port_ptr->bytes_from_prev_buf,
                   is_steady_state);
#endif

   // if (copy_data && ) we do this inside gen_topo_copy_data_from_prev_to_next
   if (copy_data && prev_has_data_next_can_accept)
   {
      gen_topo_copy_data_from_prev_to_next(topo_ptr,
                                           next_module_ptr,
                                           next_in_port_ptr,
                                           prev_out_port_ptr,
                                           FALSE /*after */);
      *atleast_one_input_has_data = TRUE;

      // If previous output buffer has data set a flag to copy remaining data after module process
      // assume all channels are finished (deint-raw compr is the only case where this can go wrong)
      if (prev_out_port_ptr->common.bufs_ptr[0].actual_data_len > 0)
      {
         gen_topo_process_context_t *pc                                                     = &topo_ptr->proc_context;
         pc->in_port_scratch_ptr[next_in_port_ptr->gu.cmn.index].flags.data_pending_in_prev = TRUE;
      }
      else
      {
         // return buffer if no data left
         gen_topo_output_port_return_buf_mgr_buf(topo_ptr, prev_out_port_ptr);
      }
   }
   else // (!(copy_data && prev_has_data_next_can_accept)), copy only metadata
   {
      uint32_t total_bytes_in_next = gen_topo_get_actual_len_for_md_prop(&next_in_port_ptr->common);
      uint32_t total_bytes_in_prev = gen_topo_get_actual_len_for_md_prop(&prev_out_port_ptr->common);
      gen_topo_move_md_from_prev_to_next(next_module_ptr,
                                         next_in_port_ptr,
                                         prev_out_port_ptr,
                                         0 /* bytes copied*/,
                                         total_bytes_in_prev,
                                         total_bytes_in_next);

      // if next has data or metadata then set the atleast one input has data to true
      // if EOS is stuck inside the module, marker_eos will be set and input is considered to have trigger
      // Erasure can be set without any data, Ex: Mailbox RX can push erausre, Vocoders are expected to
      // push zeros in that case.
      if (gen_topo_input_has_data_or_md(next_in_port_ptr) || next_sdata_ptr->flags.marker_eos ||
          next_sdata_ptr->flags.erasure)
      {
         *atleast_one_input_has_data = TRUE;
      }
   }

   gen_topo_handle_eof_history(next_in_port_ptr, prev_has_data_next_can_accept);

   if (TOPO_DATA_FLOW_STATE_AT_GAP == next_in_port_ptr->common.data_flow_state)
   {
      // if we have data or metadata, move to flow-begin
      if (prev_has_data_next_can_accept || next_in_port_ptr->common.sdata.metadata_list_ptr)
      {
         gen_topo_handle_data_flow_begin(topo_ptr, &next_in_port_ptr->common, &next_in_port_ptr->gu.cmn);
      }

      // if a port is at gap, then, since module may not be called, it's better to clear EOF here.
      if (next_in_port_ptr->common.sdata.flags.end_of_frame)
      {
         next_in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         "Resetting EOF at port as it's at gap 0x%lX, in_port 0x%lx",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id);
#endif
      }
   }

   // trying to release here helps case where get_buf is FALSE and prev_out has no data.
   // at EOS we cannot return buf as we need buf to call module process
   if (!get_buf)
   {
      gen_topo_return_buf_mgr_buf_for_both(topo_ptr, next_in_port_ptr, prev_out_port_ptr);
   }

   return AR_EOK;
}

// Check if the port has an empty buffer to process the frame. State is not checked here, it checked in the caller.
static bool_t st_topo_out_port_has_trigger(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr)
{
   // Drop partial data in signal trigered container only if the data is not in DM module's variable nblc path
   if (gen_topo_output_has_data(out_port_ptr))
   {
      if (gen_topo_is_port_in_realtime_path(&out_port_ptr->common) &&
          (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_out_port_in_dm_variable_nblc(topo_ptr, out_port_ptr)))
      {
         gen_topo_exit_island_temporarily(topo_ptr);
         // move all err handling out of LPI
         st_topo_drop_stale_data(topo_ptr, out_port_ptr);
         return TRUE;
      }
      else
      {
         return FALSE;
      }
   }
   return TRUE;
}

/**
 * return code includes need_more
 */
GEN_TOPO_STATIC ar_result_t st_topo_module_process(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   capi_err_t          result                        = CAPI_EOK;
   capi_err_t          proc_result                   = CAPI_EOK;
   bool_t              need_more                     = FALSE; // insufficient input
   bool_t              is_first_out_port             = TRUE;
   bool_t              is_first_in_port              = TRUE;
   bool_t              at_least_one_eof              = FALSE;
   bool_t              input_fields_set              = FALSE;
   bool_t              input_has_metadata_or_eos     = FALSE;
   bool_t              fwk_owns_md_prop              = gen_topo_fwk_owns_md_prop(module_ptr);
   int64_t             input_ts                      = 0;
   uint64_t            bytes_to_time                 = 0; // for incrementing input TS
   uint32_t            in_bytes_before_for_all_ch    = 0; // for MD prop
   uint32_t            in_bytes_consumed_for_all_ch  = 0; // for SISO, for MD prop
   uint32_t            out_bytes_produced_for_all_ch = 0; // for SISO, for MD prop and TS
   capi_stream_flags_t input_flags = {.word = 0 };

#if defined(ERROR_CHECK_MODULE_PROCESS)
   bool_t err_check = TRUE;
#endif

   bool_t   is_siso_input_raw_and_output_pcm = FALSE;
   uint32_t op_idx = 0, ip_idx = 0;
   uint32_t m_iid = module_ptr->gu.module_instance_id;

   gen_topo_process_context_t *pc           = &topo_ptr->proc_context;
   gen_topo_input_port_t *     in_port_ptr  = NULL;
   gen_topo_output_port_t *    out_port_ptr = NULL;
   capi_stream_data_v2_t *     sdata_ptr    = NULL;

   gen_topo_reset_process_context_sdata(pc, module_ptr);

   // Graph utils puts the ports in the index order.
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      in_port_ptr                                = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      ip_idx                                     = in_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &in_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->in_port_scratch_ptr[ip_idx];

      if ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.max_input_ports))
      {
         input_fields_set = TRUE;
         input_ts         = sdata_ptr->timestamp;
         input_flags      = sdata_ptr->flags;

         if (SPF_IS_RAW_COMPR_DATA_FMT(in_port_ptr->common.media_fmt_ptr->data_format))
         {
            is_siso_input_raw_and_output_pcm = TRUE;
         }
      }

      // pushes zeros only when fwk owns metadata (checks are inside)
      if (topo_is_sdata_flag_EOS_or_EOF_set(sdata_ptr))
      {
         gen_topo_push_zeros_at_eos(topo_ptr, module_ptr, in_port_ptr);
         at_least_one_eof |= sdata_ptr->flags.end_of_frame;
         input_has_metadata_or_eos |= sdata_ptr->flags.marker_eos;
      }

      // Note that these scratch flags are implicitly reset if eos/eof is not present.
      scr_data_ptr->flags.prev_eos_dfg = sdata_ptr->flags.marker_eos;

      if (sdata_ptr->metadata_list_ptr)
      {
         input_has_metadata_or_eos = TRUE;
         // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
         // island while copying md at container input or propagating md
         scr_data_ptr->flags.prev_eos_dfg |= gen_topo_md_list_has_dfg(sdata_ptr->metadata_list_ptr);
      }

      scr_data_ptr->prev_actual_data_len[0] = in_port_ptr->common.bufs_ptr[0].actual_data_len;
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == in_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         // for unpacked v1 set sdata lengths for all the channels
         uint32_t actual_data_len = in_port_ptr->common.sdata.buf_ptr[0].actual_data_len;
         uint32_t max_data_len    = in_port_ptr->common.sdata.buf_ptr[0].max_data_len;
         for (uint32_t b = 1; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            in_port_ptr->common.sdata.buf_ptr[b].actual_data_len = actual_data_len;
            in_port_ptr->common.sdata.buf_ptr[b].max_data_len    = max_data_len;
         }
      }
      else if (GEN_TOPO_MF_NON_PCM_UNPACKED == in_port_ptr->common.flags.is_pcm_unpacked)
      {
         // cache prev lengths for all the buffers if not pcm unpacked v1/v2
         for (uint32_t b = 1; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            scr_data_ptr->prev_actual_data_len[b] = in_port_ptr->common.bufs_ptr[b].actual_data_len;
         }
      }
      in_bytes_before_for_all_ch = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);

#ifdef VERBOSE_DEBUGGING
      PRINT_PORT_INFO_AT_PROCESS(m_iid, in_port_ptr->gu.cmn.id, in_port_ptr->common, result, "input", "before");
#endif

      pc->in_port_sdata_pptr[ip_idx] = &in_port_ptr->common.sdata;

#ifdef VERBOSE_DEBUGGING
      
      TOPO_MSG(topo_ptr->gu.log_id,DBG_LOW_PRIO,"M_iid 0x%lX input ts_valid - %d , TS[MSW, LSW] - [%d, %d]",
               m_iid,
              pc->in_port_sdata_pptr[ip_idx]->flags.is_timestamp_valid,
              (uint32_t )(pc->in_port_sdata_pptr[ip_idx]->timestamp >>32),
              (uint32_t )pc->in_port_sdata_pptr[ip_idx]->timestamp );
#endif

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &in_port_ptr->common,
                                            TRUE /**is_input*/,
                                            in_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            FALSE);
      if (AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &out_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      if ((1 == module_ptr->gu.num_output_ports) && (1 == module_ptr->gu.max_output_ports))
      {
         if ((is_siso_input_raw_and_output_pcm) &&
             (!SPF_IS_PCM_DATA_FORMAT(out_port_ptr->common.media_fmt_ptr->data_format)))
         {
            is_siso_input_raw_and_output_pcm = FALSE;
         }
      }

      // store bytes from prev looping. except for looping, in gen_cntr we dont call module if there's prev data
      scr_data_ptr->prev_actual_data_len[0] = out_port_ptr->common.bufs_ptr[0].actual_data_len;
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         // for unpacked v1 set sdata lengths for all the channels
         uint32_t actual_data_len = out_port_ptr->common.sdata.buf_ptr[0].actual_data_len;
         uint32_t max_data_len    = out_port_ptr->common.sdata.buf_ptr[0].max_data_len;
         for (uint32_t b = 1; b < out_port_ptr->common.sdata.bufs_num; b++)
         {
            out_port_ptr->common.sdata.buf_ptr[b].actual_data_len = actual_data_len;
            out_port_ptr->common.sdata.buf_ptr[b].max_data_len    = max_data_len;
         }
      }
      else if (GEN_TOPO_MF_NON_PCM_UNPACKED == out_port_ptr->common.flags.is_pcm_unpacked)
      {
         /** cache prev lengths for all the buffers if not pcm unpacked v1/v2.*/
         for (uint32_t b = 1; b < out_port_ptr->common.sdata.bufs_num; b++)
         {
            scr_data_ptr->prev_actual_data_len[b] = out_port_ptr->common.bufs_ptr[b].actual_data_len;
         }
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: output port id 0x%lx, process before: length_per_buf %lu of %lu. buff addr 0x%p",
                      m_iid,
                      out_port_ptr->gu.cmn.id,
                      out_port_ptr->common.bufs_ptr[0].actual_data_len,
                      out_port_ptr->common.bufs_ptr[0].max_data_len,
                      out_port_ptr->common.bufs_ptr[0].data_ptr);
#endif

      /**
       * Choose one input port timestamp for the output ports (SISO). Modules may override it.
       * TS can be copied independent of metadata
       *
       * Metadata is not copied to output before process (including EOS).
       *
       * If there is stale output data, don't overwrite output ts with input ts.
       * This can happen in FTRT [Dam module] case.
       */
      if (input_fields_set && (0 == out_port_ptr->common.bufs_ptr[0].actual_data_len))
      {
         sdata_ptr->timestamp                = input_ts - module_ptr->algo_delay;
         sdata_ptr->flags.is_timestamp_valid = input_flags.is_timestamp_valid;
         sdata_ptr->flags.ts_continue        = input_flags.ts_continue;
         sdata_ptr->flags.erasure            = input_flags.erasure;

         if ((is_siso_input_raw_and_output_pcm) && (input_flags.ts_continue))
         {
            sdata_ptr->flags.ts_continue        = FALSE;
            sdata_ptr->flags.is_timestamp_valid = TRUE;
         }
      }

#if defined(REQUIRED_FOR_ST_TOPO) || defined(ERROR_CHECK_MODULE_PROCESS)
      ar_result_t local_result = AR_EOK;
      /* ST topo doesnt support num_proc_loops > 1, so no need to do err check*/
      if ((module_ptr->num_proc_loops) > 1 || err_check)
      {
         if (AR_EOK != (local_result = gen_topo_populate_sdata_bufs(topo_ptr, module_ptr, out_port_ptr, err_check)))
         {
            return local_result;
         }
      }
#endif

      // if there's any metadata in the output bufs, move it to separate list (happens when looping)
      scr_data_ptr->md_list_ptr = NULL;
      if (sdata_ptr->metadata_list_ptr)
      {
         spf_list_merge_lists((spf_list_node_t **)&scr_data_ptr->md_list_ptr,
                              (spf_list_node_t **)&sdata_ptr->metadata_list_ptr);
      }

      pc->out_port_sdata_pptr[op_idx] = &out_port_ptr->common.sdata;

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &out_port_ptr->common,
                                            FALSE /**is_input*/,
                                            out_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            FALSE);
      if (AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif
   }

   /**
    * At input of process, the input->actual_len is the size of input & data starts from data_ptr.
    *                      the output->actual_len is uninitialized & CAPI can write from data_ptr.
    *                                remaining data is from data_ptr+actual_len
    * At output of process, the input->actual_len is the amount of data consumed (read) by CAPI.
    *                      the output->actual_len is output data, & data starts from data_ptr.
    */
   if (module_ptr->capi_ptr && (!module_ptr->bypass_ptr))
   {
      pc->process_info.is_in_mod_proc_context = TRUE;
      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,
      result = module_ptr->capi_ptr->vtbl_ptr->process(module_ptr->capi_ptr,
                                                       (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                                       (capi_stream_data_t **)pc->out_port_sdata_pptr);
      );
      // clang-format on
      pc->process_info.is_in_mod_proc_context = FALSE;

      /* Not handling TS for decoders in ST topo, check vocoders*/
      if (is_siso_input_raw_and_output_pcm)
      {
         capi_stream_data_t *input_stream_temp_ptr = (capi_stream_data_t *)pc->in_port_sdata_pptr[0];
         if ((input_stream_temp_ptr) && (input_stream_temp_ptr->flags.ts_continue))
         {

            capi_stream_data_t *output_stream_temp_ptr = (capi_stream_data_t *)pc->out_port_sdata_pptr[0];
            if (output_stream_temp_ptr)
            {
               output_stream_temp_ptr->flags.ts_continue        = FALSE;
               output_stream_temp_ptr->flags.is_timestamp_valid = TRUE;
            }
         }
      }
   }
   else // PCM use cases, SH MEM EP, bypass use cases etc
   {
      // metadata prop is taken care in gen_topo_propagate_metadata
      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,
      result = gen_topo_copy_input_to_output(topo_ptr,
                                             module_ptr,
                                             (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                             (capi_stream_data_t **)pc->out_port_sdata_pptr);
      );
      // clang-format on
   }

   proc_result = result;

// module must not return need for signal triggers
#ifdef REQUIRED_FOR_ST_TOPO
   need_more = (result == CAPI_ENEEDMORE);
#endif

   /**
    * loop over output ports to do sanity checks, adjust lengths and populate few variables.
    */
   bool_t atleast_one_elementary_mod_is_present = FALSE;
   is_first_out_port                            = TRUE;
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &out_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      atleast_one_elementary_mod_is_present |= (NULL != out_port_ptr->gu.attached_module_ptr);

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &out_port_ptr->common,
                                            FALSE /**is_input*/,
                                            out_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            TRUE);
      if(AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif

#ifdef REQUIRED_FOR_ST_TOPO
      capi_stream_flags_t output_flags = {.word = 0 };
      bool_t              output_fields_set = FALSE;
      output_flags                          = sdata_ptr->flags;
      if ((1 == module_ptr->gu.num_output_ports) && (1 == module_ptr->gu.max_output_ports))
      {
         output_fields_set = TRUE;
      }
      if (CAPI_STREAM_V2 != sdata_ptr->flags.stream_data_version)
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Warning: Module 0x%lX: output port id 0x%lx, Invalid stream data version %lu, flags 0x%lX",
                         m_iid,
                         out_port_ptr->gu.cmn.id,
                         sdata_ptr->flags.stream_data_version,
                         sdata_ptr->flags.word);
      }

      /* Since 61937 depacketizer can mark EOF to indicate that output has integral number of frames, we skip error
       * check for it. */
      if (AMDB_MODULE_TYPE_DEPACKETIZER != module_ptr->gu.module_type)
      {
         // for SISO modules check if module propagated metadata by mistake.
         if (fwk_owns_md_prop && (sdata_ptr->metadata_list_ptr ||
                                  (output_fields_set && ((output_flags.end_of_frame ^ sdata_ptr->flags.end_of_frame) ||
                                                         (output_flags.marker_eos ^ sdata_ptr->flags.marker_eos)))))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: output port id 0x%lx, module is not supposed to propagate metadata but yet "
                            "it did. ignoring module changes. md list ptr 0x%lX, flags (before: 0x%lX, after: 0x%lX). "
                            "output_fields_set%u",
                            m_iid,
                            out_port_ptr->gu.cmn.id,
                            sdata_ptr->metadata_list_ptr,
                            output_flags.word,
                            sdata_ptr->flags.word,
                            output_fields_set);

            sdata_ptr->flags.end_of_frame = output_flags.end_of_frame;
            sdata_ptr->flags.marker_eos   = output_flags.marker_eos;
            sdata_ptr->metadata_list_ptr  = NULL;
         }
      }
#endif

#ifdef REQUIRED_FOR_ST_TOPO
      // ST topo supports only one loop so this fucntion is not required
      if (AR_EOK != gen_topo_update_out_buf_len_after_process(topo_ptr,
                                                              out_port_ptr,
                                                              err_check,
                                                              scr_data_ptr->prev_actual_data_len))
      {
         return AR_EFAILED;
      }

      // ST topo cannot have partial data at the output so no need to adjust TS
      // adjust timestamp for data already in the buffer (if any)
      // If no output data is generated in this process cycle, then retain previous TS.
      if (sdata_ptr->flags.is_timestamp_valid &&
          SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format) &&
          ((out_port_ptr->common.bufs_ptr[0].actual_data_len - scr_data_ptr->prev_actual_data_len[0]) > 0))
      {
         if (out_port_ptr->common.flags.is_pcm_unpacked)
         {
            sdata_ptr->timestamp -=
               topo_bytes_per_ch_to_us(scr_data_ptr->prev_actual_data_len[0], out_port_ptr->common.media_fmt_ptr, NULL);
         }
         else
         {
            sdata_ptr->timestamp -=
               topo_bytes_to_us(scr_data_ptr->prev_actual_data_len[0], out_port_ptr->common.media_fmt_ptr, NULL);
         }
      }
#endif

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,DBG_LOW_PRIO,"M_iid 0x%lX output ts_valid - %d TS[MSW, LSW] - [%d, %d]",
                 m_iid,
                 sdata_ptr->flags.is_timestamp_valid,
                 (uint32_t )(sdata_ptr->timestamp >>32),
                 (uint32_t )sdata_ptr->timestamp);

      PRINT_PORT_INFO_AT_PROCESS(m_iid, out_port_ptr->gu.cmn.id, out_port_ptr->common, proc_result, "output", "after");
#endif

      // for PCM & packetized output store the output duration as frame duration
      if (is_first_out_port) // otherwise bytes_to_time is overwritten
      {
         is_first_out_port = FALSE;
         // subtract prev bytes to take care of offset adjustments
         out_bytes_produced_for_all_ch =
            (out_port_ptr->common.bufs_ptr[0].actual_data_len - scr_data_ptr->prev_actual_data_len[0]);
         // see logic inside gen_topo_get_actual_len_for_md_prop
         if (SPF_DEINTERLEAVED_RAW_COMPRESSED != out_port_ptr->common.media_fmt_ptr->data_format)
         {
            out_bytes_produced_for_all_ch *= out_port_ptr->common.sdata.bufs_num;
         }

         if (sdata_ptr->flags.is_timestamp_valid &&
             SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format))
         {
            bytes_to_time = topo_bytes_to_us(out_bytes_produced_for_all_ch,
                                             out_port_ptr->common.media_fmt_ptr,
                                             &out_port_ptr->common.fract_timestamp_pns);
         }
      }

      // if we data or metadata is produced, move to flow-begin
      if ((out_port_ptr->common.bufs_ptr[0].actual_data_len > scr_data_ptr->prev_actual_data_len[0]) ||
          out_port_ptr->common.sdata.metadata_list_ptr || out_port_ptr->common.sdata.flags.erasure)
      {
         gen_topo_handle_data_flow_begin(topo_ptr, &out_port_ptr->common, &out_port_ptr->gu.cmn);
#ifdef REQUIRED_FOR_ST_TOPO
         // useful for nblc modules to process without checking for triggers.
         out_port_ptr->any_data_produced = TRUE;

         // since output/metada is produced by module mark the change.
         process_info_ptr->anything_changed = TRUE;
#endif
      }
   }

   /**
    * loop over input port to adjust length, check error etc
    */
   is_first_in_port = TRUE;
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      in_port_ptr                                   = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      ip_idx                                        = in_port_ptr->gu.cmn.index;
      sdata_ptr                                     = &in_port_ptr->common.sdata;
      topo_media_fmt_t *            med_fmt_ptr     = in_port_ptr->common.media_fmt_ptr;
      topo_buf_t *                  bufs_ptr        = in_port_ptr->common.bufs_ptr;
      gen_topo_port_scratch_data_t *scr_data_ptr    = &pc->in_port_scratch_ptr[ip_idx];
      bool_t                        local_need_more = need_more;

#if defined(REQUIRED_FOR_ST_TOPO) || defined(ERROR_CHECK_MODULE_PROCESS)
      if (err_check)
      {
         if (AR_EOK !=
             gen_topo_err_check_inp_buf_len_after_process(module_ptr, in_port_ptr, scr_data_ptr->prev_actual_data_len))
         {
            return AR_EFAILED;
         }
      }
#endif

      // by default disable TS check. Enable it only if we could update input TS based on data consumed at the input
      // or data produced at the output
      in_port_ptr->flags.disable_ts_disc_check = TRUE;

      // remaining size after per buffer.
      uint32_t remaining_size_after = scr_data_ptr->prev_actual_data_len[0] - bufs_ptr[0].actual_data_len;

      if (!in_port_ptr->flags.processing_began)
      {
         in_port_ptr->flags.processing_began = CAPI_SUCCEEDED(result);
      }
// before gen_topo_move_data_to_beginning_after_process

#ifdef REQUIRED_FOR_ST_TOPO /* need more need not be handled in ST topo*/
      /**
       * if no input remains
       * if input didn't get consumed & output didn't get produced
       */
      bool_t any_input_consumed = FALSE;
      any_input_consumed        = (remaining_size_after != scr_data_ptr->prev_actual_data_len[0]);
      if (local_need_more || (0 == remaining_size_after) || (!any_input_consumed && !any_out_produced))
      {
         // else EoS is overwritten
         *terminate_ptr      = TRUE;
         result              = CAPI_ENEEDMORE;
         local_need_more     = TRUE;
         bool_t is_in_filled = (remaining_size_after == bufs_ptr[0].max_data_len);
         // end_of_frame helps stop inf loop in case module don't clear EOF, however, doing so causes some genuine tests
         // to hang as need_more_input won't be set. wmastd_dec_nt_9
         // For pause module (trigger policy module) in container with WR EP and MFC (DM module), inf loop is possible
         //  if we don't check for !in_port_ptr->flags.need_more_input (as MFC keeps returning need-more but due to
         //  anything_changed set as TRUE we keep looping back.
         //  if we don't check for !in_port_ptr->flags.need_more_input (as MFC keeps returning need-more but due to
         //  anything_changed set as TRUE we keep looping back.
         if (!is_in_filled /*&& !sdata_ptr->flags.end_of_frame*/ && !in_port_ptr->flags.need_more_input)
         {
            in_port_ptr->flags.need_more_input = TRUE;
            // removing anything_changed can cause hangs in nt mode tests
            process_info_ptr->anything_changed = TRUE;
         }
      }
#endif

      /**
       * Data flow can never be stalled at an input in End point if the port has a trigger. Hence module will either
       * consumes the complete or partial(in case of DM) frame .
       *
       * If the module is not consuming data it will be dropped. So input is consumed or dropped always. So module
       * will end up having space for more input. Thats why we set need more by default here.
       *
       * There is one possible issue if DM module doesnt consume input even if sufficient data is provided,
       * this can happen only due to an erratic module implementation. In that case also need more will be set and
       * it could result in data pile in the DM module path.
       *
       * Dam and Speaker protection uses Gen topo.
       *
       */
      in_port_ptr->flags.need_more_input = TRUE;

      /** when processing signal trigger , module must finish input.
       * Otherwise, it can cause backlog & upstream can lose regular timing.
       * (E.g. SPR and TRM)
       * Also if module doesn't require-data-buffering it must consume all input.
       *
       * Don't drop when module is raising media fmt or threshold as we will call the module again.
       *
       * Dont drop when data is non-PCM. Sincle LCM threshold is only honored for pcm modules, this dropping
       * to meet threshold reqs should also take place only for pcm modules.
       *
       * For eg: Added this condition to support cop depack module in EP container. When depack raises threshold, it
       * is not included in the lcm calculation(pack format not considered when pcm threshold module is present)
       * and container operates at hwep raised threshold
       * */
      if ((0 != remaining_size_after) && !gen_topo_any_process_call_events(topo_ptr))
      {
         bool_t is_dropped = FALSE;

         // Drop partial data in signal trigerred container only if the data is not in DM module's variable nblc path
         if (SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format) &&
             gen_topo_is_port_in_realtime_path(&in_port_ptr->common) &&
             (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr, in_port_ptr)))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX: in-port-id 0x%lx: dropping %lu bytes_per_buf as modules must consume all "
                            "input at signal trigger. ",
                            m_iid,
                            in_port_ptr->gu.cmn.id,
                            remaining_size_after);
            // if we dont do this EP clients don't get uniform timing. Also ICB will have to be increased if we hold.
            is_dropped = TRUE;
         }
         else if (!module_ptr->flags.requires_data_buf)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX: in-port-id 0x%lx: dropping %lu bytes_per_buf as modules with "
                            "requires-data-buffering FALSE must consume all input",
                            m_iid,
                            in_port_ptr->gu.cmn.id,
                            remaining_size_after);
            is_dropped = TRUE;
         }

         if (is_dropped)
         {
            remaining_size_after = 0;

            // if metadata is stuck inside module, it's NOT dropped here. When fwk is responsible for prop, it's
            // handled thru propagation below.
            for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&in_port_ptr->common); b++)
            {
               bufs_ptr[b].actual_data_len = scr_data_ptr->prev_actual_data_len[b];
            }
            // even for modules that propagate metadata, drop data at the input port.
            // Call with keep_buffer_associated_md set to true. This is needed to ensure dfg, eos,
            // start end md are not dropped along with the data

            if (in_port_ptr->common.sdata.metadata_list_ptr)
            {
               // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
               // island while copying md at container input or propagating md
               gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                       module_ptr,
                                                       &in_port_ptr->common,
                                                       gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                       TRUE /*keep_eos_and_ba_md*/);
            }
         }
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: Input port id 0x%lx: process after: inLengh consumed per buf %lu, result 0x%lx, "
                      "Flags0x%lX, need_more_input %u, remaining_size_after%lu",
                      m_iid,
                      in_port_ptr->gu.cmn.id,
                      bufs_ptr[0].actual_data_len,
                      proc_result,
                      sdata_ptr->flags.word,
                      in_port_ptr->flags.need_more_input,
                      remaining_size_after);
#endif

      // buf_ptr->actual_data_len is updated here
      if (at_least_one_eof && fwk_owns_md_prop)
      {
         gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);
         gen_topo_handle_end_of_frame_after_process(topo_ptr,
                                                    module_ptr,
                                                    in_port_ptr,
                                                    scr_data_ptr->prev_actual_data_len,
                                                    local_need_more);
      }

      if (sdata_ptr->flags.is_timestamp_valid)
      {
         // if input is PCM/Packetized then update the timestamp based on the input data consumed.
         if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format))
         {
            // actual_data_len=consumed amount, whether process is successful or not.
            if (in_port_ptr->common.flags.is_pcm_unpacked)
            {
               bytes_to_time = topo_bytes_per_ch_to_us(bufs_ptr[0].actual_data_len,
                                                       med_fmt_ptr,
                                                       &in_port_ptr->common.fract_timestamp_pns);
            }
            else
            {
               bytes_to_time =
                  topo_bytes_to_us(bufs_ptr[0].actual_data_len, med_fmt_ptr, &in_port_ptr->common.fract_timestamp_pns);
            }
         }
         else
         {
            // bytes_to_time is updated based on the the output data generated.
         }

         /**
          * input TS is incremented either by frame size or by amount of data dropped.
          * Frame size obtained from PCM/packetized side or from input side if input & output both are
          * PCM/Packetized.
          * it's important to have input override output because we may drop lot of data before generating
          * output. for decoder: erroneous bit streams can have lot of data drop before successful decode.
          * however, time-stamp cannot be recovered unless new input buffer is picked. for depacketizer +
          * decoder: data drops are counted in correctly.
          */
         sdata_ptr->timestamp += (int64_t)bytes_to_time;

         // When some input is consumed, if corresponding time duration is known, then input timestamp is updated
         // properly and we can enable the disc check.
         if ((bufs_ptr[0].actual_data_len > 0) && bytes_to_time)
         {
            in_port_ptr->flags.disable_ts_disc_check = FALSE;
         }
      }
      else
      {
         // when TS is invalid, TS disc check can be enabled (validity diff)
         in_port_ptr->flags.disable_ts_disc_check = FALSE;
      }

      if (is_first_in_port)
      {
         in_bytes_consumed_for_all_ch = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);
         is_first_in_port             = FALSE;
      }

      // handles default propagation for SISO modules and fwk owns metadata
      gen_topo_propagate_metadata(topo_ptr,
                                  module_ptr,
                                  input_has_metadata_or_eos,
                                  in_bytes_before_for_all_ch,
                                  in_bytes_consumed_for_all_ch,
                                  out_bytes_produced_for_all_ch);

      /**
       * loop over input port once more to check need more, moving data back to beginning etc
      * Also handle metadata offset update (this has to be done after metadata prop)
      */
      uint32_t data_consumed_per_buf = bufs_ptr[0].actual_data_len;

      // For inplace modules, if input got consumed and there's output data remaining, it's an error
      if (remaining_size_after && out_bytes_produced_for_all_ch && gen_topo_is_inplace_or_disabled_siso(module_ptr))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Either the inplace module 0x%lx didnt consume all data or inplace "
                         "module used with looping which is not supported ",
                         module_ptr->gu.module_instance_id);
         // this causes data corruption. moving remaining data to beginning causes overwriting output (for inplace)
      }

      gen_topo_move_data_to_beginning_after_process(topo_ptr,
                                                    in_port_ptr,
                                                    remaining_size_after,
                                                    scr_data_ptr->prev_actual_data_len);

      // metadata logic common for both modules that supports_metadata and those don't
      bool_t has_eos_dfg = sdata_ptr->flags.marker_eos;
      if (sdata_ptr->metadata_list_ptr)
      {
         uint32_t bytes_consumed_for_md_prop =
            gen_topo_get_bytes_for_md_prop_from_len_per_buf(&in_port_ptr->common, data_consumed_per_buf);
         gen_topo_metadata_adj_offset_after_process_for_inp(topo_ptr, in_port_ptr, bytes_consumed_for_md_prop);

         // check if input has dfg md
         // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
         // island while copying md at container input or propagating md
         has_eos_dfg |= gen_topo_md_list_has_dfg(sdata_ptr->metadata_list_ptr);
      }

      gen_topo_sync_to_input_timestamp(topo_ptr, in_port_ptr, data_consumed_per_buf);

      // if flushing EOS leaves the input port, reset the input port. this helps in resetting timestamp related
      // variables after EOS prop. moves port to at-gap.
      if (pc->in_port_scratch_ptr[ip_idx].flags.prev_eos_dfg && !has_eos_dfg)
      {
         topo_basic_reset_input_port(topo_ptr, in_port_ptr, TRUE /*use_bufmgr*/);
         pc->in_port_scratch_ptr[ip_idx].flags.prev_eos_dfg = FALSE;
      }

      // for sink modules if any data was consumed, think of it as full frame being processed.
      // this is needed incase of venc (where mailbox is the sink).
      // This frame delivery event is used in CDRX-40 usecases for dynamic de-voting of resources.
      if (module_ptr->gu.flags.is_sink && (data_consumed_per_buf > 0) && topo_ptr->flags.need_to_handle_frame_done)
      {
         if (topo_ptr->topo_to_cntr_vtable_ptr->handle_frame_done)
         {
            topo_ptr->topo_to_cntr_vtable_ptr->handle_frame_done(topo_ptr, module_ptr->gu.path_index);
         }
      }

      // return input buffers if cannot be held (DM path)
      gen_topo_input_port_return_buf_mgr_buf(topo_ptr, in_port_ptr);
   }

   if (atleast_one_elementary_mod_is_present)
   {
      gen_topo_process_attached_elementary_modules(topo_ptr, module_ptr);
   }

#if defined(REQUIRED_FOR_ST_TOPO) || defined(ERROR_CHECK_MODULE_PROCESS)
   if (err_check)
   {
      // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
      // island while copying md at container input or propagating md
      gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);
      gen_topo_validate_metadata_eof(module_ptr);
   }
#endif

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      // metadata logic common for both modules that supports_metadata and those don't
      // update offset of new metadata to be from the beginning of the buffer
      if (out_port_ptr->common.sdata.metadata_list_ptr)
      {
         uint32_t bytes_existing = scr_data_ptr->prev_actual_data_len[0];
         if (SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format) &&
             (TU_IS_ANY_DEINTERLEAVED_UNPACKED(out_port_ptr->common.media_fmt_ptr->pcm.interleaving)))
         {
            bytes_existing = scr_data_ptr->prev_actual_data_len[0] * out_port_ptr->common.sdata.bufs_num;
         }
         gen_topo_metadata_adj_offset_after_process_for_out(topo_ptr,
                                                            module_ptr,
                                                            out_port_ptr,
                                                            bytes_existing,
                                                            proc_result);

         // merge back the metadata from scratch list to main list keeping in mind the order
         spf_list_node_t *temp_list_ptr               = (spf_list_node_t *)out_port_ptr->common.sdata.metadata_list_ptr;
         out_port_ptr->common.sdata.metadata_list_ptr = pc->out_port_scratch_ptr[op_idx].md_list_ptr;
         spf_list_merge_lists((spf_list_node_t **)&out_port_ptr->common.sdata.metadata_list_ptr, &temp_list_ptr);
         pc->out_port_scratch_ptr[op_idx].md_list_ptr = NULL;
      }
      else
      {
         out_port_ptr->common.sdata.metadata_list_ptr = pc->out_port_scratch_ptr[op_idx].md_list_ptr;
      }
   }

   if (CAPI_FAILED(result))
   {
      gen_topo_exit_island_temporarily(topo_ptr);
      // All error handling to be done outside island
      gen_topo_print_process_error(topo_ptr, module_ptr);
   }

   return capi_err_to_ar_result(result);
}

/** Algorithm iterates through all the modules. For each module does -
 *   1. Skips module process if active flag is not set.
 *   2. Setup input/output buffers for STARTED ports.
 *   3. If module trigger is satisfied, call module process.
 *   4. Drop data if module trigger is not satisified.
 *   5. Break process if module raises process events.
 */
ar_result_t st_topo_process(gen_topo_t *topo_ptr, gu_module_list_t **start_module_list_pptr)
{
   ar_result_t              result           = AR_EOK;
   gen_topo_process_info_t *process_info_ptr = &topo_ptr->proc_context.process_info;

   for (gu_module_list_t *module_list_ptr = *start_module_list_pptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *         module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
      gen_topo_process_context_t *pc         = &topo_ptr->proc_context;

      /** Call module processe only if it the module is active. In ST path, only stopped modules will be skipped.*/
      if (!module_ptr->flags.active)
      {
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX is not active or top level trigger cond not satisfied; skipping process",
                         module_ptr->gu.module_instance_id);
#endif
         continue;
      }

      // by default consider active/enabled source modules are assumed to have input [Ex: DTMF gen]
      // module active check is already done in the begining itself
      // If atleast one input has data it means, media format is also valid
      bool_t atleast_one_input_has_data = module_ptr->gu.flags.is_source;

      /* Iterate through modules inputs and copy data from prev output if required. */
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         // skip stopped ports for signal triggers.
         if (TOPO_PORT_STATE_STARTED != in_port_ptr->common.state)
         {
            continue;
         }

         // note: even though data pending is in out buf, it's stored at input port index. different prev ports may have
         // same index.
         // ST containers ext input has pending data in case of DM modules
         pc->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.data_pending_in_prev = FALSE;

         // do this only for internal ports.
         if (in_port_list_ptr->ip_port_ptr->conn_out_port_ptr)
         {
            gen_topo_output_port_t *prev_out_ptr =
               (gen_topo_output_port_t *)in_port_list_ptr->ip_port_ptr->conn_out_port_ptr;
            gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)prev_out_ptr->gu.cmn.module_ptr;

            // copy output of first CAPI to input of second except for first one.
            // first one has input already copied earlier
            // for last buffer output buffer is the dec svc output buffer. already assigned.

            // Start state is checked inside
            (void)st_topo_check_copy_between_modules(topo_ptr,
                                                     module_list_ptr,
                                                     module_ptr,
                                                     prev_module_ptr,
                                                     in_port_ptr,
                                                     prev_out_ptr,
                                                     &atleast_one_input_has_data);
         }
         else if (gen_topo_input_has_data_or_md(in_port_ptr) || in_port_ptr->common.sdata.flags.marker_eos ||
                  in_port_ptr->common.sdata.flags.erasure) // if ext input has data/md
         {
            atleast_one_input_has_data = TRUE;
         }

         if (in_port_ptr->common.sdata.flags.end_of_frame ||
             (in_port_ptr->bytes_from_prev_buf == in_port_ptr->common.bufs_ptr[0].actual_data_len))
         {
            // bytes_from_prev_buf will be update again in gen_topo_input_port_is_trigger_present
            in_port_ptr->bytes_from_prev_buf = 0;
         }
      }

      /* For module trigger to be satisifed from the outputs perspective following conditions should be met,
          1. Atleast one output is started [if a module is at SG/ext boundary and the peer module SG is stopped,
             then it cannot process]
          2. All the started outputs must have an empty buffer. [If there is partial data at an output port, dont
             call process] */
      bool_t atleast_one_op_started             = module_ptr->gu.flags.is_sink;
      bool_t all_started_out_ports_have_trigger = TRUE;
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         /*Ex: if each leg of the splitter output is a separate subgraph, we need to check if each
         output port state is started before we allocate buffers for splitter's outputs.
         If downstream of any output port is not started, we continue.*/
         if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
         {
            continue;
         }

         atleast_one_op_started = TRUE;

         bool_t out_has_trigger = st_topo_out_port_has_trigger(topo_ptr, out_port_ptr);
#ifdef TRIGGER_DEBUG
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX, out port id 0x%lx, out_has_trigger%u ",
                         module_ptr->gu.module_instance_id,
                         out_port_ptr->gu.cmn.id,
                         out_has_trigger);
#endif
         if (out_has_trigger)
         {
            // in HW-EP overrun cases, ext buf is NULL and topo-buf will be obtained. This is one case
            // where peer-svc buf is not used for last module.
            result = gen_topo_check_get_out_buf_from_buf_mgr(topo_ptr, module_ptr, out_port_ptr);
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_ERROR_PRIO,
                               " Module 0x%lX: Output port id 0x%lx, error getting buffer",
                               module_ptr->gu.module_instance_id,
                               out_port_ptr->gu.cmn.id);
               all_started_out_ports_have_trigger = FALSE;
               // continue with other outputs
            }
         }
         else
         {
            // If out_has_trigger is false, it means there is partial data stuck at the output,
            // cannot process the module in such case. Data can be stuck in NRT paths in ST container
            // [Eg: stale data can be present in DS of prebuffer Dam in Acoustic Activity Detection usecase].
            out_port_ptr->common.flags.force_return_buf = TRUE;
            gen_topo_output_port_return_buf_mgr_buf(topo_ptr, out_port_ptr);
            all_started_out_ports_have_trigger = FALSE;
         }
      }

      // For signal triggered, if atleast one input has buffer then call process.
      // note that module active flag is checked so atleast one input/output must be in started state.
      //    1. Source module - DTMF gen - input is always assumed to present, so call the module.
      //    2. Else if atleast one input has a buffer means,
      //           a) Ext input case data will be definitely present
      //           b) internal input case, prev produced data or metadata is present.
      //    3. If all the input ports are at data flow gap, then atleast_one_input_has_data = FALSE
      //       Ex: if SAL has only one input from DTMF gen, and input is at flow gap skip SAL module
      ///   4. No need to check output triggers, module.flag.active = TRUE is sufficient
      //    5. STM process must be called on signal triggers
      bool_t trigger_cond_satisfied =
         (atleast_one_input_has_data && (atleast_one_op_started && all_started_out_ports_have_trigger)) ||
         module_ptr->flags.need_stm_extn;
#ifdef TRIGGER_DEBUG
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: NBLC module trigger_cond_satisfied:%lu",
                      module_ptr->gu.module_instance_id,
                      trigger_cond_satisfied);
#endif

      // Call the module if tigger conditions are met.
      if (trigger_cond_satisfied)
      {
         // if the module supports DM extension set samples required to be produced depending upon the mode.
         // In future if fractional rate is supported for mfc in island we need to move dm extension code into
         // Island, for now gen_topo_is_dm_enabled check avoids crash for mfc in island for non fractional sampling
         // rate.
         if (topo_ptr->flags.is_dm_enabled)
         {
            if (!POSAL_IS_ISLAND_HEAP_ID(topo_ptr->heap_id) && (module_ptr->flags.need_dm_extn)
                  && (gen_topo_is_dm_enabled(module_ptr)))
            {
               gen_topo_updated_expected_samples_for_dm_modules(topo_ptr, module_ptr);
            }
         }

         /** module process
          * return code: fail, success, need-more */
         result = st_topo_module_process(topo_ptr, module_ptr);
      }
      else // drop input data if trigger cond is not satisfied
      {
         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            // Note that ST topo does supports only num_proc_loops == 1.
            // So cannot have partial data at the previous output in signal triggered containers. so no need to attempt
            // to copy data from prev out to next input.

            // Modules in signal triggered containers cannot return need more, so input is dropped unless
            // there is a DM module in the downstream
            if (in_port_ptr->common.bufs_ptr[0].actual_data_len &&
                gen_topo_is_port_in_realtime_path(&in_port_ptr->common) &&
                (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr, in_port_ptr)))
            {
               // Drop partial data in signal trigerred container only if the data is not sin DM module's variable nblc
               // path
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_ERROR_PRIO,
                               " Module 0x%lX: input port id 0x%lx, dropping %lu for signal trigger as trigger "
                               "condition "
                               "not satisfied.",
                               module_ptr->gu.module_instance_id,
                               in_port_ptr->gu.cmn.id,
                               in_port_ptr->common.bufs_ptr[0].actual_data_len);

               if (in_port_ptr->common.sdata.metadata_list_ptr)
               {
                  // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have
                  // exited island while copying md at container input or propagating md
                  gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                          module_ptr,
                                                          &in_port_ptr->common,
                                                          gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                          FALSE /*keep_eos_and_ba_md*/);
               }

               gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
            }

            // bytes_from_prev_buf can be non-zero only for DM modules.
            // this is used for syncing incoming TS
            in_port_ptr->bytes_from_prev_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;

            gen_topo_input_port_return_buf_mgr_buf(topo_ptr, in_port_ptr);
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr     = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         gen_topo_input_port_t * conn_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
         /* If this output port is blocking the state propagation (#INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP)
          * then there can be a scenario where this output port is started (due to sg start) but connected_in_port
          * is stopped/suspended (due to propagated state). In this case, we need to drop the data here at the output
          * port. We can not drop the data on the connected input port because connected module processing will be
          * skipped.
          */
         if ((TOPO_PORT_STATE_STARTED == out_port_ptr->common.state) &&
             (out_port_ptr->common.flags.is_upstream_realtime) && (conn_in_port_ptr) &&
             (TOPO_PORT_STATE_STARTED != conn_in_port_ptr->common.state))
         {
            gen_topo_exit_island_temporarily(topo_ptr);
            // move all err handling out of LPI
            gen_topo_drop_data_after_mod_process(topo_ptr, module_ptr, out_port_ptr);
         }

         /** check and release bufs, this will help release earlier (when next-in has its own buf
          * and enable this buf to be re-used as out buf for next module. */
         gen_topo_output_port_return_buf_mgr_buf(topo_ptr, out_port_ptr);
      }

      /* Currently module process result is not being used in ST topo. so overwriting to AR_EOK at the end of topo
       * process call.
       * no need to do it for each module. */
      // result = AR_EOK;

      /** check after calling gen_topo_check_copy_between_modules, which propagates media fmt */
      if (gen_topo_any_process_call_events(topo_ptr))
      {
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX: topo has pending media fmt or port thresh or process state before processing "
                         "this module.",
                         module_ptr->gu.module_instance_id);
#endif
         /**
          * if media fmt or thresh event occurs when process is called (decoder)
          * then after copying this data to next module (assuming next module in sorted list is module connected to
          * dec) handle all events in the fmwk, and come back here.
          *
          * If we don't continue from the module next to decoder, and instead start from the beginning,
          * then we will have more buffers stuck inside the topo (2 decoded frames).
          */
         *start_module_list_pptr = module_list_ptr;
         break; // breaks topo process
      }

      *start_module_list_pptr = NULL;
   }

   /** Anything changed true can be set by default for the signal triggered topo */
   process_info_ptr->anything_changed = TRUE;

   return AR_EOK; /* overwriting result to EOK here */
}
