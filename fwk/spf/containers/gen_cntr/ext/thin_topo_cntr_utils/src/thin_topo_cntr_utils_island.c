/**
 * \file thin_topo_cntr_utils_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// clang-format off
/*
$Header:
*/
// clang-format on

#include "gen_cntr_i.h"
#include "thin_topo_cntr_utils_i.h"
#include "irm_cntr_prof_util.h"

THIN_TOPO_STATIC ar_result_t thin_topo_output_buf_set_up_peer_cntr(gen_cntr_t *             me_ptr,
                                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                  gen_topo_output_port_t * out_port_ptr);

THIN_TOPO_STATIC ar_result_t thin_topo_send_peer_cntr_out_buffers(gen_cntr_t *             me_ptr,
                                                                 gen_cntr_ext_out_port_t *ext_out_port_ptr);

THIN_TOPO_STATIC ar_result_t thin_topo_input_dataQ_trigger_peer_cntr(gen_cntr_t *            me_ptr,
                                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr);

THIN_TOPO_STATIC ar_result_t thin_topo_input_data_set_up_peer_cntr(gen_cntr_t *            me_ptr,
                                                                  gen_cntr_ext_in_port_t *ext_in_port_ptr);

THIN_TOPO_STATIC ar_result_t thin_topo_copy_timestamp_from_input(gen_cntr_t *            me_ptr,
                                                                gen_cntr_ext_in_port_t *ext_in_port_ptr);

THIN_TOPO_STATIC ar_result_t thin_topo_copy_data_from_prev_to_next(gen_topo_t *            topo_ptr,
                                                                  gen_topo_module_t *     next_module_ptr,
                                                                  gen_topo_input_port_t * next_in_port_ptr,
                                                                  gen_topo_output_port_t *prev_out_port_ptr)
{
   ar_result_t            result         = AR_EOK;
   topo_buf_t *           next_bufs_ptr  = next_in_port_ptr->common.bufs_ptr;
   topo_buf_t *           prev_bufs_ptr  = prev_out_port_ptr->common.bufs_ptr;
   capi_stream_data_v2_t *prev_sdata_ptr = &prev_out_port_ptr->common.sdata;
   capi_stream_data_v2_t *next_sdata_ptr = &next_in_port_ptr->common.sdata;

   // No partial data to be present at input, because thin topo drops any remaining data after process,
   // check thin_topo_handle_input_sdata_after_process_per_port
#ifdef THIN_TOPO_SAFE_MODE
   if (!next_bufs_ptr[0].data_ptr || !prev_bufs_ptr[0].data_ptr)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: Buffers invalid input port id 0x%lx buffer 0x%lx prev output buffer 0x%lx",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      next_bufs_ptr[0].data_ptr,
                      prev_bufs_ptr[0].data_ptr);
      return AR_EFAILED;
   }

   if (next_bufs_ptr[0].actual_data_len)
   {
      // Drop partial data in signal trigerred container only if the data is not sin DM module's variable nblc
      // path
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: input port id 0x%lx, dropping %lu for signal trigger.",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      next_in_port_ptr->common.bufs_ptr[0].actual_data_len);

      gen_topo_set_all_bufs_len_to_zero(&next_in_port_ptr->common);
   }

   // within in NBLC chain a given input and prev output must always use the same buffer in thin topo
   if (next_bufs_ptr[0].data_ptr != prev_bufs_ptr[0].data_ptr)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: input port id 0x%lx, and prev output has different buffers.",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      next_in_port_ptr->common.bufs_ptr[0].actual_data_len);
      return AR_EFAILED;
   }

   if ((0 != next_bufs_ptr[0].actual_data_len) || (next_bufs_ptr[0].max_data_len != prev_bufs_ptr[0].max_data_len) ||
       (prev_out_port_ptr->common.sdata.bufs_num != next_in_port_ptr->common.sdata.bufs_num))
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: in-port-id 0x%lx: Prev out buf and next in buf data_ptr same, but next buf "
                      "has "
                      "%lu. max len: next %lu, prev %lu, bufs_num: prev %lu, next %lu",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      next_bufs_ptr[0].actual_data_len,
                      next_bufs_ptr[0].max_data_len,
                      prev_bufs_ptr[0].max_data_len,
                      prev_out_port_ptr->common.sdata.bufs_num,
                      next_in_port_ptr->common.sdata.bufs_num);
   }
#endif

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " copy_data_from_prev_to_next - same buf: next module 0x%lX: in-port-id 0x%lx: "
                   "bytes_to_copy_per_buf = %lu, bufs_num %lu",
                   next_module_ptr->gu.module_instance_id,
                   next_in_port_ptr->gu.cmn.id,
                   prev_bufs_ptr[0].actual_data_len,
                   next_in_port_ptr->common.sdata.bufs_num);
#endif

   // since buffer ptrs are same just copy the actual len, to complete the data copy
   for (uint32_t b = 0; b < next_in_port_ptr->common.sdata.bufs_num; b++)
   {
      next_bufs_ptr[b].actual_data_len = prev_bufs_ptr[b].actual_data_len;
      prev_bufs_ptr[b].actual_data_len = 0;
   }

   // Check for timestamp disc with the next expect input TS w.r.t to incoming prev output TS.
   // NOTE: next_sdata_ptr->timestamp is update for input after process with expected TS only if the
   //       THIN_TOPO_TS_DISC_DEBUG macro is defined.
#ifdef THIN_TOPO_TS_DISC_DEBUG
   bool_t ts_disc = gen_topo_is_timestamp_discontinuous(next_sdata_ptr->flags.is_timestamp_valid,
                                                        next_sdata_ptr->timestamp,
                                                        prev_sdata_ptr->flags.is_timestamp_valid,
                                                        prev_sdata_ptr->timestamp);

   if (ts_disc)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      "timestamp discontinuity detected for module 0x%lX,0x%lx. expected (valid: %u, "
                      "ts: %ld us), incoming (valid: %u, ts: %ld us)",
                      in_port_ptr->gu.common.module_ptr->gu.module_instance_id,
                      in_port_ptr->gu.common.id,
                      next_sdata_ptr->flags.is_timestamp_valid,
                      (uint32_t)in_port_ptr->ts_to_sync.ivalue,
                      prev_sdata_ptr->flags.is_timestamp_valid,
                      (uint32_t)prev_sdata_ptr->timestamp);
   }
#endif

   // copy timestamp from prev out to next input only if valid
   // No need to reset prev output TS, it gets reset in output sdata before process.
   next_sdata_ptr->flags.is_timestamp_valid = prev_sdata_ptr->flags.is_timestamp_valid;
   if (prev_sdata_ptr->flags.is_timestamp_valid)
   {
      next_sdata_ptr->timestamp = prev_sdata_ptr->timestamp;
   }

#ifdef VERBOSE_DEBUGGING
   gen_topo_module_t *prev_module_ptr      = (gen_topo_module_t *)prev_out_port_ptr->gu.cmn.module_ptr;
   uint32_t               prev_sdata_flags = prev_sdata_ptr->flags.word;
   uint32_t               next_sdata_flags = next_sdata_ptr->flags.word; // for debug
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX -> next module 0x%lX: next timestamp %lu (0x%lx%lx). prev_has_data_next_can_accept = "
                   "%u. port cmn flags prev0x%lX, next0x%lX",
                   prev_module_ptr->gu.module_instance_id,
                   next_module_ptr->gu.module_instance_id,
                   (int32_t)next_sdata_ptr->timestamp,
                   (int32_t)(next_sdata_ptr->timestamp >> 32),
                   (int32_t)next_sdata_ptr->timestamp,
                   1, // prev_has_data_next_can_accept,
                   prev_out_port_ptr->common.flags.word,
                   next_in_port_ptr->common.flags.word);

   // Flags: last in capi_stream_flags_t is MSB.  Ver1|   Version0|Eras|M3|M2|  M1|EOS|EOF|TSValid

   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " prev_sdata_flags0x%lX -> next_sdata_flags0x%lX, next_port_flag0x%lX, "
                   "next bytes_from_prev_buf %lu",
                   prev_sdata_flags,
                   next_sdata_flags,
                   next_in_port_ptr->flags.word,
                   next_in_port_ptr->bytes_from_prev_buf);
#endif

   return result;
}

/* Poll for the input buffers for PCM only and only if,
 *  1. Internal input buffer is not filled and data buffers are present on the queue.
 */
THIN_TOPO_STATIC inline bool_t thin_topo_need_to_poll_for_input_data(gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // since all bufs are equally filled, it's sufficient to check first one.
   uint32_t max_len = in_port_ptr->common.bufs_ptr[0].data_ptr ? in_port_ptr->common.bufs_ptr[0].max_data_len
                                                               : in_port_ptr->common.max_buf_len_per_buf;

   if ((max_len > in_port_ptr->common.bufs_ptr[0].actual_data_len) && (0 == ext_in_port_ptr->buf.actual_data_len))
   {
      return TRUE;
   }

   return FALSE;
}

/**
 * Iteratively propagates FORWARDS from the external input until the NBLC end input and assigns the external input
 * buffer.
 *    1. For the external inputs internal port, buffer is expected to be assigned before this function is called.
 *    2. Ideally all the ports in the NBLC chain should have their origin reset to invalid.
 *    3. THere is a corner case, when the entire topo is a single NBLC chain or only 1 module in topo. In this case,
 *       a) External input will propagate first till the last/nblc end module's input. Note that
 *       b) Note that when ext output port propagates the buffer backwards it should check and stop since ext input
 *          already propagated, check thin_topo_propagate_ext_out_buffer_to_nblc.
 *    4. Note that we are not using refcount, because the module process happens in sorted order and the buffers are
 * freed by the last port thats processed i.e NBLC end input context only.
 */
THIN_TOPO_STATIC ar_result_t thin_topo_propagate_ext_in_buffer_to_nblc(uint32_t                log_id,
                                                                       gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                       gen_topo_input_port_t  *nblc_start_ptr,
                                                                       uint32_t                buf_origin)
{
   topo_buf_t *nblc_start_bufs_arr_ptr = nblc_start_ptr->common.bufs_ptr;
   uint32_t    max_data_len_per_buf    = nblc_start_bufs_arr_ptr[0].max_data_len;

   /** propagate the ext buf ptr to NBLC in the FORWARD direction.*/
   gen_topo_input_port_t *cur_in_port_ptr = nblc_start_ptr;
   gen_topo_input_port_t *nblc_end_ptr    = nblc_start_ptr->nblc_end_ptr;
   while (nblc_end_ptr != cur_in_port_ptr)
   {
      /** propagate buffer from current input to the data link upstream i.e
       *  to the current modules output and next modules input. */
      gen_topo_output_port_t *cur_out_port_ptr =
         (gen_topo_output_port_t *)cur_in_port_ptr->gu.cmn.module_ptr->output_port_list_ptr->op_port_ptr;

      // in inplace chain check if a module is dynamic inplace
      if (FALSE == cur_out_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer)
      {
#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
         GEN_CNTR_MSG(log_id,
                      DBG_LOW_PRIO,
                      "ext input buffer propagation: cannot propagate ext buf 0x%lx beyond non-inplace module"
                      "(MIID,Port):(0x%lX,%lx)",
                      nblc_start_ptr->common.bufs_ptr[0].data_ptr,
                      cur_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gu.cmn.id);
#endif
         return AR_EOK;
      }

      gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)cur_out_port_ptr->gu.conn_in_port_ptr;

#ifdef THIN_TOPO_SAFE_MODE
      /** Note that buf pointers are not cleared from port unless safe mode is enabled.*/
      if (cur_out_port_ptr->common.bufs_ptr[b].data_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      "ext input buffer propagation: cannot propagate across to output (MIID,Port):(0x%lX,%lx) already "
                      "has an ext buffer assigned 0x%lx origin:%lu",
                      cur_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gu.cmn.id,
                      cur_out_port_ptr->common.bufs_ptr[b].data_ptr,
                      cur_out_port_ptr->common.flags.buf_origin);
      }

      /** Note that buf pointers are not cleared from port unless safe mode is enabled.*/
      if (next_in_port_ptr->common.bufs_ptr[b].data_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      "ext input buffer propagation: cannot propagate ext buf 0x%lx to next input "
                      "(MIID,Port):(0x%lX,%lx) already has an ext buffer assigned 0x%lx origin:%lu",
                      next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      next_in_port_ptr->common.bufs_ptr[b].data_ptr,
                      next_in_port_ptr->common.flags.buf_origin);
      }
#endif

      // todo: move to safe mode, origin not being used for the propagated buffers. required if topo needs to be
      // switched to gen topo.

      gen_topo_common_port_t *cur_out_cmn_port_ptr = &cur_out_port_ptr->common;
      gen_topo_common_port_t *next_in_cmn_port_ptr = &next_in_port_ptr->common;

      /** set buffer to the connected/next module's input */
      cur_out_cmn_port_ptr->flags.buf_origin = buf_origin;
      next_in_cmn_port_ptr->flags.buf_origin = buf_origin;
      for (uint32_t b = 0; b < cur_out_cmn_port_ptr->sdata.bufs_num; b++)
      {
         int8_t *ch_buf_ptr = nblc_start_bufs_arr_ptr[b].data_ptr;

         /** assign buffer to current modules output */
         cur_out_cmn_port_ptr->bufs_ptr[b].data_ptr     = ch_buf_ptr;
         cur_out_cmn_port_ptr->bufs_ptr[b].max_data_len = max_data_len_per_buf;

         /** assign buffer to next modules input */
         next_in_cmn_port_ptr->bufs_ptr[b].data_ptr     = ch_buf_ptr;
         next_in_cmn_port_ptr->bufs_ptr[b].max_data_len = max_data_len_per_buf;
      }

      /** move the iterator to next module's input*/
      cur_in_port_ptr = next_in_port_ptr;

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
      if (nblc_end_ptr == cur_in_port_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_LOW_PRIO,
                      "ext input buffer propagation: cannot propagate ext buf 0x%lx beyond NBLC end port "
                      "(MIID,Port):(0x%lX,%lx)",
                      nblc_start_ptr->common.bufs_ptr[0].data_ptr,
                      nblc_end_ptr->gu.cmn.module_ptr->module_instance_id,
                      nblc_end_ptr->gu.cmn.id);
      }
#endif
   }
   return AR_EOK;
}

/**
 * Iteratively propagates BACKWARDS from the external output until the NBLC start output and assigns the external output
 * buffer.
 *    1. For the external outputs internal port, buffer is expected to be assigned before this function is called, skip
 * it here.
 *    2. Ideally all the ports in the NBLC chain should have their origin reset to invalid.
 *    3. THere is a corner case, when the entire topo is a single NBLC chain or only 1 module in topo. In this case,
 *          a) If external input buffer propagation started first it would have propagated until the last modules input.
 *             So stops prop backwards if external input buffer is assigned. This is a VERY LIKELY scenario because ext
 *             input propagation always happens first due to sorted module order.
 *    4. Note that ref count is not required here because buffer is only freed in the ext output post process context,
 * at which point buffers all US modules are assumed to have finished process.
 */
THIN_TOPO_STATIC ar_result_t thin_topo_propagate_ext_out_buffer_to_nblc(uint32_t                log_id,
                                                                        gen_topo_output_port_t *nblc_end_ptr,
                                                                        uint32_t                buf_origin)
{
   topo_buf_t *nblc_end_bufs_arr_ptr = nblc_end_ptr->common.bufs_ptr;
   uint32_t    max_data_len_per_buf  = nblc_end_bufs_arr_ptr[0].max_data_len;

   /** propagate the ext buf ptr to NBLC in the BACKWARD direction till nbcl start*/
   gen_topo_output_port_t *cur_out_port_ptr = nblc_end_ptr;
   gen_topo_output_port_t *nblc_start_ptr   = nblc_end_ptr->nblc_start_ptr;
   while (nblc_start_ptr != cur_out_port_ptr)
   {
      /** Propagate across the current module's input port*/
      gen_topo_input_port_t *cur_in_port_ptr =
         (gen_topo_input_port_t *)cur_out_port_ptr->gu.cmn.module_ptr->input_port_list_ptr->ip_port_ptr;

      if (FALSE == cur_in_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer)
      {
#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
         GEN_CNTR_MSG(log_id,
                      DBG_LOW_PRIO,
                      "ext output buffer propagation: ext buf 0x%lx cannot be propagated backwards beyond non-inplace "
                      "module output port (MIID,Port):(0x%lX,%lx)",
                      nblc_end_ptr->common.bufs_ptr[0].data_ptr,
                      cur_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gu.cmn.id);
#endif
         return AR_EOK;
      }

      /** propagate from current modules input to connected/prev modules input as well */
      gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)cur_in_port_ptr->gu.conn_out_port_ptr;

#ifdef THIN_TOPO_SAFE_MODE
      if (cur_in_port_ptr->common.bufs_ptr[b].data_ptr)
      {
         /** THis is unexpected scenario, buffer is not expected to assigned.*/
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext output buffer propagation: cannot be propagated across to input of (MIID,Port):(0x%lX,%lx) "
                      "already has an ext buffer assigned 0x%lx origin:%lu",
                      cur_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gu.cmn.id,
                      cur_in_port_ptr->common.bufs_ptr[b].data_ptr,
                      cur_in_port_ptr->common.flags.buf_origin);
      }

      if (prev_out_port_ptr->common.bufs_ptr[b].data_ptr)
      {
         /** THis is unexpected scenario, buffer is not expected to assigned.*/
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext output buffer propagation: cannot be propagated to the output of (MIID,Port):(0x%lX,%lx) "
                      "already has an ext buffer assigned 0x%lx origin:%lu",
                      prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      prev_out_port_ptr->gu.cmn.id,
                      prev_out_port_ptr->common.bufs_ptr[b].data_ptr,
                      prev_out_port_ptr->common.flags.buf_origin);
      }
#endif

      gen_topo_common_port_t *cur_in_cmn_port_ptr   = &cur_in_port_ptr->common;
      gen_topo_common_port_t *prev_out_cmn_port_ptr = &prev_out_port_ptr->common;

      cur_in_cmn_port_ptr->flags.buf_origin   = buf_origin;
      prev_out_cmn_port_ptr->flags.buf_origin = buf_origin;
      for (uint32_t b = 0; b < cur_in_cmn_port_ptr->sdata.bufs_num; b++)
      {
         int8_t *ch_buf_ptr = nblc_end_bufs_arr_ptr[b].data_ptr;

         /** assign buffer to current modules input */
         cur_in_cmn_port_ptr->bufs_ptr[b].data_ptr     = ch_buf_ptr;
         cur_in_cmn_port_ptr->bufs_ptr[b].max_data_len = max_data_len_per_buf;

         /** assign buffer to prev modules output */
         prev_out_cmn_port_ptr->bufs_ptr[b].data_ptr     = ch_buf_ptr;
         prev_out_cmn_port_ptr->bufs_ptr[b].max_data_len = max_data_len_per_buf;
      }

      /** Move the iterator to the prev module's output port */
      cur_out_port_ptr = prev_out_port_ptr;

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
      if (nblc_start_ptr == cur_out_port_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_LOW_PRIO,
                      "ext output buffer propagation: ext buf 0x%lx cannot be propagated backwards beyond NBLC start "
                      "output port (MIID,Port):(0x%lX,%lx)",
                      nblc_end_ptr->common.bufs_ptr[0].data_ptr,
                      nblc_start_ptr->gu.cmn.module_ptr->module_instance_id,
                      nblc_start_ptr->gu.cmn.id);
      }
#endif
   }

   return AR_EOK;
}

/* Setup the internal input port buffers on signal trigger.
 *  1. Copies data from the external buffer to internal buffer.
 *  2. In the process context, if there is not enough data input UNDERRUNS. */
THIN_TOPO_STATIC ar_result_t thin_topo_setup_internal_input_port_and_preprocess(gen_cntr_t *            me_ptr,
                                                                               gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Setup ext input port 0x%lx of Module 0x%lX ",
                in_port_ptr->gu.cmn.id,
                in_port_ptr->gu.cmn.module_ptr->module_instance_id);
#endif

#ifdef VERBOSE_DEBUGGING
   uint32_t bytes_in_int_inp_md_prop = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);
   bool_t   sufficient_bytes_copied  = TRUE; /*always true for ST cntrs*/
   bool_t   is_input_discontinuity =
      in_port_ptr->common.sdata.flags.end_of_frame ||
      (in_port_ptr->nblc_end_ptr && in_port_ptr->nblc_end_ptr->common.sdata.flags.end_of_frame);
   bool_t dbg_inp_insufficient = FALSE;
#endif

   /** actual_data_len can be valid only when the frame size of DS > US, where thin topo polls and copies ICB buffer
    * data until one DS frame len is filled. */
   uint32_t bytes_copied_per_buf =
      in_port_ptr->common.max_buf_len_per_buf - in_port_ptr->common.bufs_ptr[0].actual_data_len;

   /** There is always data that needs to be copied if the ext input buffer is present in thin topo. Hold the ext input
    *  buf if,
    *    1. If a static buffer is not yet assigned. Which happens in ST topo only if the frame sizes US == DS and not a
    * DM nblc input.
    *    2. And only if ext input buffer has full frame length of data.
    */

   /** When the external input buffer is reused ideally the topo buffer is not assigned because external buffer is
    *  reused and buf origin must be INVALID at this point always. Because for this ext input buffer info is reset after
    *  process. Note that buf origin is reset to invalid only if safe mode is enabled. Else it will remain what ever was
    *  assigned with the buffer the last time.*/
   bool_t is_topo_buffer_assigned = (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr);

   if ((TRUE == ext_in_port_ptr->flags.pass_thru_upstream_buffer) && !is_topo_buffer_assigned)
   {
      /** Check if ext buffer has full frame length worth of data*/
      uint32_t expected_actual_len = in_port_ptr->common.max_buf_len;
      if (expected_actual_len == ext_in_port_ptr->buf.actual_data_len)
      {
         /** Hold the external buffer for nblc modules processing. expected actual length is already checked hence
          * assuming per buf actual length to be 'max_buf_len_per_buf'*/
         uint32_t actual_len_per_buf = in_port_ptr->common.max_buf_len_per_buf;
         in_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;
         int8_t *data_ptr                     = ext_in_port_ptr->buf.data_ptr;
         for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            in_port_ptr->common.bufs_ptr[b].data_ptr        = data_ptr;
            in_port_ptr->common.bufs_ptr[b].actual_data_len = actual_len_per_buf;
            in_port_ptr->common.bufs_ptr[b].max_data_len    = actual_len_per_buf;
            data_ptr += actual_len_per_buf;
         }

         /** Propagates the held buffer until the nblc end input port. */
         thin_topo_propagate_ext_in_buffer_to_nblc(me_ptr->topo.gu.log_id,
                                                   ext_in_port_ptr,
                                                   in_port_ptr,
                                                   GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED);
         ext_in_port_ptr->buf.actual_data_len = 0;
         bytes_copied_per_buf                 = in_port_ptr->common.bufs_ptr[0].actual_data_len;
      }
      else
      {
         /** if external input buffer doesnt have a full frame, allocate a topo buffer since container needs to
          * underrun. Its complicated to underrun with a resused ext input buffer, it involves moving the deint-packed
          * data and inserting zeros for each channel in the ext buffer. And it also requires checking the external
          * buffer max data length. */
         result = gen_topo_check_get_in_buf_from_buf_mgr(&me_ptr->topo,
                                                         in_port_ptr,
                                                         (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr);
         if (AR_DID_FAIL(result))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX: Input port id 0x%lx, error getting buffer",
                         in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         in_port_ptr->gu.cmn.id);
         }

         /** propagate it to NBLC end*/
         thin_topo_propagate_ext_in_buffer_to_nblc(me_ptr->topo.gu.log_id,
                                                   ext_in_port_ptr,
                                                   in_port_ptr,
                                                   GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED);

         /** copy the partial frame to the topo buffer, underrun is handled after polling is complete. */
         result |= gen_cntr_copy_peer_cntr_input_to_int_buf(me_ptr, ext_in_port_ptr, &bytes_copied_per_buf);
      }

      /** This flag is set for an optimization to skip external inputs post processing if a external buffer is
       * propagated to NBLC. External input buffer pointers are reset based on this flag at the end of topo
       * processing.*/
      me_ptr->topo.proc_context.process_info.atleast_one_inp_holds_ext_buffer = TRUE;
   }
   else
   {
      /** Copies data from ext input to internal input, and returns the external buffer immediately if there is no
       * data.*/
      result |= gen_cntr_copy_peer_cntr_input_to_int_buf(me_ptr, ext_in_port_ptr, &bytes_copied_per_buf);
   }

   /** updates timestamp validity flag to true/false  */
   in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;

   /** copy external input timestamp and update ts_to_sync to the next expected timestmap if buffer has valid, ts to
    * sync will be updated based on the ext buffer TS so no need to update here. */
   if (in_port_ptr->ts_to_sync.ts_valid)
   {
      in_port_ptr->common.sdata.timestamp = in_port_ptr->ts_to_sync.ivalue;

      // update timestamp if ext buffer has more data.
      if (ext_in_port_ptr->buf.actual_data_len)
      {
         if (TOPO_DEINTERLEAVED_UNPACKED == in_port_ptr->common.media_fmt_ptr->pcm.interleaving)
         {
            in_port_ptr->ts_to_sync.ivalue +=
               (int64_t)topo_bytes_per_ch_to_us(bytes_copied_per_buf, in_port_ptr->common.media_fmt_ptr, NULL);
         }
         else // could be interleaved data in case of thin topo
         {
            in_port_ptr->ts_to_sync.ivalue +=
               (int64_t)topo_bytes_to_us(bytes_copied_per_buf, in_port_ptr->common.media_fmt_ptr, NULL);
         }
      }
   }

#ifdef VERBOSE_DEBUGGING
   {
      uint32_t force_process    = 1;
      bool_t   ext_cond_not_met = TRUE;
      bool_t   buf_present      = (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr);
      uint32_t flags            = (sufficient_bytes_copied << 7) | (buf_present << 6) | (ext_cond_not_met << 5) |
                       (in_port_ptr->common.sdata.flags.marker_eos << 4) | (is_input_discontinuity << 3) |
                       (force_process << 2) | dbg_inp_insufficient;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "preprocess input: prev len %lu module (%lu of %lu) per buf, ext in (%lu of %lu), flags %08lX, "
                   "curr_trigger%u, inport flags0x%lX",
                   bytes_in_int_inp_md_prop,
                   in_port_ptr->common.bufs_ptr[0].actual_data_len,
                   in_port_ptr->common.bufs_ptr[0].max_data_len,
                   ext_in_port_ptr->buf.actual_data_len,
                   ext_in_port_ptr->buf.max_data_len,
                   flags,
                   me_ptr->topo.proc_context.curr_trigger,
                   in_port_ptr->flags.word);
   }
#endif

   return result;
}

THIN_TOPO_STATIC ar_result_t thin_topo_poll_and_setup_ext_inputs(gen_cntr_t *me_ptr, gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)in_port_ptr->gu.ext_in_port_ptr;

#ifdef VERBOSE_DEBUGGING
   uint32_t num_polled_buffers = 0;
#endif

   while (TRUE)
   {
      if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
      {
         // if already holding on to an input buffer [partial buffer], copy data from the buffer.
         TRY(result, thin_topo_setup_internal_input_port_and_preprocess(me_ptr, ext_in_port_ptr));
      }

      // Check if polling can be stopped,
      //   1. Need to poll returns FALSE or
      //   2. If there is no trigger signal set for the queue.
      if (!thin_topo_need_to_poll_for_input_data(ext_in_port_ptr) ||
          !posal_queue_poll(ext_in_port_ptr->gu.this_handle.q_ptr))
      {
         break;
      }

      /** need to poll buffer. try to fill buf for Signal trigger */
      TRY(result, thin_topo_input_dataQ_trigger_peer_cntr(me_ptr, ext_in_port_ptr));

      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "process failed for ext input port 0x%lx of Module 0x%lX ",
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id);
      }

#ifdef VERBOSE_DEBUGGING
      num_polled_buffers++;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Ext input port 0x%lx of Module 0x%lX. num_polled_buffers %lu ",
                   in_port_ptr->gu.cmn.id,
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   num_polled_buffers);
#endif
   }

   if (in_port_ptr->common.bufs_ptr[0].actual_data_len != in_port_ptr->common.bufs_ptr[0].max_data_len)
   {
      if (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Ext input port 0x%lx of Module 0x%lX is AT_GAP, skipping underrun",
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id);
#endif
         return AR_EOK;
      }

      /** if external input buffer is not present assign topo buffer to underrun and free it after process
       */
      if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         /** Assign topo buffer */
         result = gen_topo_check_get_in_buf_from_buf_mgr(&me_ptr->topo,
                                                         in_port_ptr,
                                                         (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr);
         if (AR_DID_FAIL(result))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX: Input port id 0x%lx, error getting buffer",
                         in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         in_port_ptr->gu.cmn.id);
            THROW(result, AR_EFAILED);
         }

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Underrun ext input port 0x%lx of Module 0x%lX ext buffer not present, assing and propagate topo "
                      "buf",
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id);
#endif

         if (TRUE == ext_in_port_ptr->flags.pass_thru_upstream_buffer)
         {
            // propagate it to NBLC end
            thin_topo_propagate_ext_in_buffer_to_nblc(me_ptr->topo.gu.log_id,
                                                      ext_in_port_ptr,
                                                      in_port_ptr,
                                                      GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED);
         }
      }

      gen_cntr_st_underrun(me_ptr, ext_in_port_ptr, in_port_ptr->common.bufs_ptr[0].max_data_len);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * loop over input port to adjust length, check error etc
 */
THIN_TOPO_STATIC void thin_topo_handle_input_sdata_after_process_per_port(gen_topo_t            *topo_ptr,
                                                                          gen_topo_module_t     *module_ptr,
                                                                          gen_topo_input_port_t *in_port_ptr)
{

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
   PRINT_PORT_INFO_AT_PROCESS(module_ptr->gu.module_instance_id,
                              in_port_ptr->gu.cmn.id,
                              in_port_ptr->common,
                              topo_ptr->proc_context.proc_result,
                              "input",
                              "after");
#endif

#ifdef THIN_TOPO_SAFE_MODE
   /* input is expected to be fully consumed after process. Note that here its assumed that input has max len before
    * calling process. If input was not full, there would be another print in before process context. */
   if (in_port_ptr->common.bufs_ptr[0].max_data_len != in_port_ptr->common.bufs_ptr[0].actual_data_len)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: input port id 0x%lx, frame not consumed fully. (%lu != %lu)",
                      next_module_ptr->gu.module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->common.bufs_ptr[0].actual_data_len,
                      in_port_ptr->common.bufs_ptr[0].max_data_len);
   }
#endif

#ifdef THIN_TOPO_TS_DISC_DEBUG
   /* update next expected inputTS, to check for disc when new data is copied from prev output. TS disc will be checked
    * before the next process call, when the data is moved from prev output to this input port. */
   if (in_port_ptr->common.sdata.flags.is_timestamp_valid)
   {
      if (TOPO_DEINTERLEAVED_UNPACKED == in_port_ptr->common.media_fmt_ptr->pcm.interleaving)
      {
         in_port_ptr->common.sdata.timestamp +=
            (int64_t)topo_bytes_per_ch_to_us(bufs_ptr[0].actual_data_len, in_port_ptr->common.media_fmt_ptr, NULL);
      }
      else
      {
         in_port_ptr->common.sdata.timestamp +=
            (int64_t)topo_bytes_to_us(bufs_ptr[0].actual_data_len, in_port_ptr->common.media_fmt_ptr, NULL);
      }
   }
#endif

   // todo: if there is any partial data then switch to GEN topo

   /*remaining data always assumed to be 0*/
   gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
}

THIN_TOPO_STATIC void thin_topo_handle_input_sdata_after_process(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   // Loop over input ports after proces
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      // prepare sdata after process call
      thin_topo_handle_input_sdata_after_process_per_port(topo_ptr, module_ptr, in_port_ptr);
   }
}

/**  For each external out port, postprocess data, send media fmt and data down. */
THIN_TOPO_STATIC ar_result_t thin_topo_post_process_peer_ext_output(gen_cntr_t *             me_ptr,
                                                                   gen_topo_output_port_t * out_port_ptr,
                                                                   gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   uint32_t bytes_produced_by_pp = out_port_ptr->common.bufs_ptr[0].actual_data_len;
#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "postprocess output: before module (%lu of %lu), ext out (%lu of %lu), "
                "bytes_produced_by_pp %lu",
                out_port_ptr->common.bufs_ptr[0].actual_data_len,
                out_port_ptr->common.bufs_ptr[0].max_data_len,
                ext_out_port_ptr->buf.actual_data_len,
                ext_out_port_ptr->buf.max_data_len,
                bytes_produced_by_pp);
#endif

   /* Check if there is enough space to the ext out buffer only if,
         1. If the external buffer exist.
         2. And if the ext buffer is not being reused as internal buffer. If its being reused ext buffer
           already has the processed data so we don't have to copy. */
   if (!ext_out_port_ptr->buf.data_ptr)
   {
      gen_cntr_handle_st_overrun_at_post_process(me_ptr, ext_out_port_ptr);

      /* Mark output buffer for force return Generally we use the external output buffer for the last output port. In
       * case we use the internal buffer because ext out buffers were not available, every time we'll have to*/
      out_port_ptr->common.flags.force_return_buf = TRUE;
      gen_topo_output_port_return_buf_mgr_buf(&me_ptr->topo, out_port_ptr);
      return AR_EOK;
   }

   // note: assuming only deint unpacked
   uint32_t total_actual_len = bytes_produced_by_pp * out_port_ptr->common.sdata.bufs_num;
   ext_out_port_ptr->buf.actual_data_len += total_actual_len;

   // NOTE: no need to move data from topo to ext output buf for thin topo, topo uses ext buffer for module process.
   //       In overrun case, topo buffer assigned and dropped after process, hence no copy req

   // send output to the peer container
   result = thin_topo_send_peer_cntr_out_buffers(me_ptr, ext_out_port_ptr);

   return result;
}

/* Process context sdata is common for all the module's capi process calls in the topo. Make sure to call reset
   in the begining of each module's process context. */
THIN_TOPO_STATIC void thin_topo_reset_process_context_sdata(gen_topo_process_context_t *pc,
                                                           gen_topo_module_t *         module_ptr)
{
   for (uint32_t ip_idx = 0; ip_idx < module_ptr->gu.max_input_ports; ip_idx++)
   {
      pc->in_port_sdata_pptr[ip_idx] = NULL;
   }

   for (uint32_t op_idx = 0; op_idx < module_ptr->gu.max_output_ports; op_idx++)
   {
      pc->out_port_sdata_pptr[op_idx] = NULL;
   }
}

THIN_TOPO_STATIC capi_err_t thin_topo_process_attached_module_to_output(gen_topo_t             *topo_ptr,
                                                                        gen_topo_module_t      *module_ptr,
                                                                        gen_topo_output_port_t *out_port_ptr)
{
   capi_err_t         attached_proc_result    = CAPI_EOK;
   gen_topo_module_t *out_attached_module_ptr = (gen_topo_module_t *)out_port_ptr->gu.attached_module_ptr;

   if (out_attached_module_ptr->flags.disabled)
   {
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "Skipping process on attached elementary module miid 0x%lx for input port idx %ld id 0x%lx - "
               "disabled %ld, is_mf_valid %ld, no buffer %ld, no data provided %ld, md list ptr NULL %ld",
               out_attached_module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.index,
               module_ptr->gu.module_instance_id,
               out_attached_module_ptr->flags.disabled,
               out_port_ptr->common.flags.is_mf_valid,
               (NULL == out_port_ptr->common.sdata.buf_ptr),
               (NULL == out_port_ptr->common.sdata.buf_ptr)
                  ? 0
                  : (0 == out_port_ptr->common.sdata.buf_ptr[0].actual_data_len),
               (NULL == out_port_ptr->common.sdata.metadata_list_ptr));
#endif
      return;
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_MED_PRIO,
            "Before process attached elementary module miid 0x%lx for output port idx %ld id 0x%lx",
            out_attached_module_ptr->gu.module_instance_id,
            out_port_ptr->gu.cmn.index,
            module_ptr->gu.module_instance_id);
#endif

   uint32_t out_port_idx = out_port_ptr->gu.cmn.index;
   // clang-format off
   IRM_PROFILE_MOD_PROCESS_SECTION(out_attached_module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,
   attached_proc_result =
      out_attached_module_ptr->capi_ptr->vtbl_ptr
         ->process(out_attached_module_ptr->capi_ptr,
                   (capi_stream_data_t **)&(topo_ptr->proc_context.out_port_sdata_pptr[out_port_idx]),
                   (capi_stream_data_t **)&(topo_ptr->proc_context.out_port_sdata_pptr[out_port_idx]));
   );
   // clang-format on

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_MED_PRIO,
            "After process attached elementary module miid 0x%lx for output port idx %ld id 0x%lx",
            out_attached_module_ptr->gu.module_instance_id,
            out_port_ptr->gu.cmn.index,
            module_ptr->gu.module_instance_id);
#endif

#ifdef VERBOSE_DEBUGGING
   // Don't ignore need more for attached modules.
   if (CAPI_FAILED(attached_proc_result))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Attached elementary module miid 0x%lx for output port idx %ld id 0x%lx returned error 0x%lx "
               "during process",
               out_attached_module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.index,
               module_ptr->gu.module_instance_id,
               attached_proc_result);
   }
#endif
   return attached_proc_result;
}

/**
 * after input and output buffers are set-up, topo process is called once on signal triggers.
 * 1. If sufficient input is not present, ext in will underrun at this point.
 * 3. If output is ready, delivers the buffers to downstream containers.
 */
THIN_TOPO_STATIC ar_result_t thin_topo_data_process_one_frame(gen_cntr_t *me_ptr)
{
   ar_result_t                 result                   = AR_EOK;
   capi_err_t                  proc_result              = CAPI_EOK;
   gen_topo_t                 *topo_ptr                 = &me_ptr->topo;
   gen_topo_process_context_t *pc                       = &topo_ptr->proc_context;
   gen_topo_process_info_t    *proc_info_ptr            = &topo_ptr->proc_context.process_info;
   gu_module_list_t           *module_list_ptr          = me_ptr->topo.thin_topo_ptr->active_module_list_ptr;
   gu_ext_in_port_list_t  *active_ext_in_port_list_ptr  = topo_ptr->thin_topo_ptr->active_ext_in_list_ptr;
   gu_ext_out_port_list_t *active_ext_out_port_list_ptr = topo_ptr->thin_topo_ptr->active_ext_out_list_ptr;
   INIT_EXCEPTION_HANDLING

#ifdef VERBOSE_DEBUGGING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
   if (module_list_ptr)
   {
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Starting thin topo process from Module 0x%lX ",
                   module_ptr->gu.module_instance_id);
   }
   else
   {
      GEN_CNTR_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Started module list is empty, nothing to process ");
      return AR_EOK;
   }

#endif

   /** -------- PRE-PROCESS EXTERNAL INPUTS ---------------
    * 1. Either pass thru ext buffer or copies data from ext in to a topo buffer
    * 2. If metadata is received or port receives data when at gap, it will throw an exception to switch to Gen topo.
    */
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = active_ext_in_port_list_ptr; (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      {
         TRY(result, thin_topo_poll_and_setup_ext_inputs(me_ptr, in_port_ptr));
      }
   }

   /** -------- PRE-PROCESS EXTERNAL OUTPUTS ---------------
    * Prepare output for the external outputs if present, else assigns a topo buffer for the internal port.
    * Propagates the external output buffer backwards to the NBLC start.
    */
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = active_ext_out_port_list_ptr; (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t * out_port_ptr     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      TRY(result, thin_topo_output_buf_set_up_peer_cntr(me_ptr, ext_out_port_ptr, out_port_ptr));

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Module 0x%lX external output 0x%lx int buf: (%p %lu %lu) ",
                   module_ptr->gu.module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   out_port_ptr->common.bufs_ptr[0].data_ptr,
                   out_port_ptr->common.bufs_ptr[0].actual_data_len,
                   out_port_ptr->common.bufs_ptr[0].max_data_len);
#endif
   }

   /** ------------- MODULE PROCESS LOOP ---------------------
    *  Iterates through each modules, prepare input and output sdata and calls the module capi process.
    */
   proc_info_ptr->is_in_mod_proc_context = TRUE;
   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t  *module_ptr       = (gen_topo_module_t *)module_list_ptr->module_ptr;
      capi_stream_flags_t simo_input_flags = { .word = 0 };
      int64_t             simo_input_ts;

      thin_topo_reset_process_context_sdata(pc, module_ptr);

      // NOTE: every module is expected to be start state since thin topo supports only one SG,
      // for the external input port case if upstream is no start, zeros are pushed always

      /* Iterate through modules inputs and copy data from prev output if required. */
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr  = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         capi_stream_data_v2_t *in_sdata_ptr = &in_port_ptr->common.sdata;

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "setup thin topo process from Module 0x%lX input 0x%lx ",
                      module_ptr->gu.module_instance_id,
                      in_port_ptr->gu.cmn.id);
#endif

         // do this only for internal ports.
         if (in_port_ptr->gu.conn_out_port_ptr)
         {
            // input buffer must have been already assigned at this point
            TRY(result,
                thin_topo_copy_data_from_prev_to_next(topo_ptr,
                                                      module_ptr,
                                                      in_port_ptr,
                                                      (gen_topo_output_port_t *)
                                                         in_port_list_ptr->ip_port_ptr->conn_out_port_ptr));
         }

         // Setup input sdata before process //

         /** cache input flags only for SISO/SIMO cases, will be used to propagate to output */
         if (1 == module_ptr->gu.max_input_ports)
         {
            simo_input_flags.word = in_sdata_ptr->flags.word;
            simo_input_ts         = in_sdata_ptr->timestamp;
         }

#ifdef THIN_TOPO_SAFE_MODE
         if (in_port_ptr->common.bufs_ptr[0].actual_data_len != in_port_ptr->common.bufs_ptr[0].max_data_len)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: input port id 0x%lx threshold not met (%lu, %lu) before process, "
                            "underruning",
                            module_ptr->gu.module_instance_id,
                            in_port_ptr->gu.cmn.id,
                            in_port_ptr->common.bufs_ptr[0].actual_data_len);
            in_port_ptr->common.bufs_ptr[0].actual_data_len = in_port_ptr->common.bufs_ptr[0].max_data_len;
         }
#endif
         pc->in_port_sdata_pptr[in_port_ptr->gu.cmn.index] = in_sdata_ptr;

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
         PRINT_PORT_INFO_AT_PROCESS(module_ptr->gu.module_instance_id,
                                    in_port_ptr->gu.cmn.id,
                                    in_port_ptr->common,
                                    pc->proc_result,
                                    "input",
                                    "before");
#endif
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr  = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         capi_stream_data_v2_t  *out_sdata_ptr = &out_port_ptr->common.sdata;

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Setup thin topo process from Module 0x%lX output 0x%lx ",
                      module_ptr->gu.module_instance_id,
                      out_port_ptr->gu.cmn.id);
#endif
         // prepare sdata before process call //

         /** Note the simo input flags variable can be set only if module has single input. If module has multi in,
          * output sdata will be reset because simo_input_ts are reset.
          */
         out_sdata_ptr->timestamp                = simo_input_ts - module_ptr->algo_delay;
         out_sdata_ptr->flags.is_timestamp_valid = simo_input_flags.is_timestamp_valid;
         out_sdata_ptr->flags.erasure            = simo_input_flags.erasure;

         /* data not expected on the output before process, because data either when data gets moved to next input
          * during topo process or data gets dropped/overrun for the external outputs. this is an unexpected scenario.

          * corner case: downstream module might be stopped due to propagated state, in that case data needs to be
          dropped
          * before calling next process.

          */
         if (out_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning! Module 0x%lX: output port id 0x%lx, has state data %lubytes before process, "
                            "dropping it",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            out_port_ptr->common.bufs_ptr[0].actual_data_len);

            gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
         }

         pc->out_port_sdata_pptr[out_port_ptr->gu.cmn.index] = out_sdata_ptr;

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
         PRINT_PORT_INFO_AT_PROCESS(module_ptr->gu.module_instance_id,
                                    out_port_ptr->gu.cmn.id,
                                    out_port_ptr->common,
                                    topo_ptr->proc_context.proc_result,
                                    "output",
                                    "before");
#endif
      }

      /**
       * At input of process, the input->actual_len is the size of input & data starts from data_ptr.
       *                      the output->actual_len is uninitialized & CAPI can write from data_ptr.
       *                                remaining data is from data_ptr+actual_len
       * At output of process, the input->actual_len is the amount of data consumed (read) by CAPI.
       *                      the output->actual_len is output data, & data starts from data_ptr.
       */

      // clang-format on
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,

      if (module_ptr->capi_ptr && (!module_ptr->bypass_ptr))
      {
         proc_result |= module_ptr->capi_ptr->vtbl_ptr->process(module_ptr->capi_ptr,
                                                               (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                                               (capi_stream_data_t **)pc->out_port_sdata_pptr);
      }
      else // PCM use cases, SH MEM EP, bypass use cases etc
      {
         // metadata prop is taken care in gen_topo_propagate_metadata
         proc_result |= gen_topo_copy_input_to_output(topo_ptr,
                                                     module_ptr,
                                                     (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                                     (capi_stream_data_t **)pc->out_port_sdata_pptr);

      }
      ); // end of IRM_PROFILE_MOD_PROCESS_SECTION
      // clang-format on

      // if module raised any events or created metadata as part of process call, exit thin topo here.
      // Important: check only process events i.e MF,thesh, process state? because if its TGP event/KPPS type of events
      // we can continue processing rest of the modules and switch to gen topo at the end of this interrupt in the event
      // hanlding context.
      if (gen_topo_any_process_call_events(topo_ptr))
      {
         me_ptr->topo.thin_topo_ptr->state = THIN_TOPO_EXITED_AT_MODULE_PROCESS_EVENTS;

         // continue process from the current module in gen topo
         topo_ptr->thin_topo_ptr->rest_of_module_proc_list_ptr = module_list_ptr;

         THROW(result, AR_ENOTREADY);
      }

#ifdef VERBOSE_DEBUGGING
      pc->proc_result = proc_result;
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "capi process done for Module 0x%lX result 0x%lx",
                   module_ptr->gu.module_instance_id,
                   proc_result);
#endif

      // Loop over ouptut ports after proces
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

#ifdef THIN_TOPO_PROCESS_BUF_DEBUG
         PRINT_PORT_INFO_AT_PROCESS(module_ptr->gu.module_instance_id,
                                    out_port_ptr->gu.cmn.id,
                                    out_port_ptr->common,
                                    topo_ptr->proc_context.proc_result,
                                    "output",
                                    "after");
#endif

#ifdef THIN_TOPO_SAFE_MODE
         /* every module is expected to generate threshold amount of data after process in thin topo. */
         if (out_port_ptr->common.bufs_ptr[0].max_data_len != out_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: output port id 0x%lx, did not produce a full frame (%lu != %lu)",
                            next_module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            out_port_ptr->common.bufs_ptr[0].actual_data_len,
                            out_port_ptr->common.bufs_ptr[0].max_data_len);
         }
#endif

         /* thin topo doesnt propagate MD, so drop it if module generates during process */

         // if metadata is present thin topo cannot deliver it to peer container or the next module, hence
         // if there is any post process required for the outputs cannot be handled in thin topo.
         // currently only external outputs require post process hence differing it to handle in gen topo.
         //
         bool_t has_metadata          = out_port_ptr->common.sdata.metadata_list_ptr ? TRUE : FALSE;
         bool_t is_data_flow_starting = ((TOPO_DATA_FLOW_STATE_AT_GAP == out_port_ptr->common.data_flow_state) &&
                                         (out_port_ptr->common.sdata.flags.erasure || has_metadata ||
                                          out_port_ptr->common.bufs_ptr[0].actual_data_len));

         if (has_metadata || is_data_flow_starting)
         {
            topo_ptr->thin_topo_ptr->state = THIN_TOPO_EXITED_AT_OUTPUT_POST_PROCESS;

            gen_topo_handle_data_flow_begin(topo_ptr, &out_port_ptr->common, &out_port_ptr->gu.cmn);

            // process data flow start for attached module and call attached module process
            if (out_port_ptr->gu.attached_module_ptr)
            {
               if (is_data_flow_starting)
               {
                  gen_topo_module_t *out_attached_module_ptr =
                     (gen_topo_module_t *)out_port_ptr->gu.attached_module_ptr;
                  gen_topo_input_port_t *attached_mod_ip_port_ptr =
                     (gen_topo_input_port_t *)out_attached_module_ptr->gu.input_port_list_ptr->ip_port_ptr;

                  gen_topo_handle_data_flow_begin(topo_ptr,
                                                  &(attached_mod_ip_port_ptr->common),
                                                  &(attached_mod_ip_port_ptr->gu.cmn));
               }

               proc_result |= thin_topo_process_attached_module_to_output(topo_ptr, module_ptr, out_port_ptr);
            }

            // if the current output has metadata, it will be processed from gen topo context. nothing to do here.
         }
         else // steady state handling
         {
            if (out_port_ptr->gu.attached_module_ptr)
            {
               proc_result |= thin_topo_process_attached_module_to_output(topo_ptr, module_ptr, out_port_ptr);
            }

            // skip post process here in thin topo if the current output has metadata
            if (out_port_ptr->gu.ext_out_port_ptr)
            {
               /** if metadata is present in the output post process will be handled in gen topo. */

               /** prepare ext output and send it downstream. and the GEN_TOPO_BUF_ORIGIN_EXT_BUF will be cleared */
               result |=
                  thin_topo_post_process_peer_ext_output(me_ptr,
                                                         out_port_ptr,
                                                         (gen_cntr_ext_out_port_t *)out_port_ptr->gu.ext_out_port_ptr);
            }
         }
      }

      // Loop over input ports after proces
      // imp: can continue to post process inputs even if output post process has triggered thin topo exit due to
      // presence of Metadata or data flow state change.
      thin_topo_handle_input_sdata_after_process(topo_ptr, module_ptr);

      if (THIN_TOPO_EXITED_AT_OUTPUT_POST_PROCESS == topo_ptr->thin_topo_ptr->state)
      {
         // process from the next module in gen topo
         topo_ptr->thin_topo_ptr->rest_of_module_proc_list_ptr = module_list_ptr->next_ptr;

         THROW(result, AR_ENOTREADY);
      }
   }
   proc_info_ptr->is_in_mod_proc_context = FALSE;

   /**------------------- POST PROCESS EXTERNAL INPUT PORTS ---------------------------
    * Conditionally post process external input ports,
    * 1. Returns help external input buffer since the NBLC finished processing the buffer. */
   if (TRUE == proc_info_ptr->atleast_one_inp_holds_ext_buffer)
   {
      for (gu_ext_in_port_list_t *ext_in_port_list_ptr = active_ext_in_port_list_ptr; (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         gen_topo_input_port_t * in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

         /** Check if the external buffer can be held in internal ports.*/
         if (TRUE == ext_in_port_ptr->flags.pass_thru_upstream_buffer)
         {
            /** If external buffer is held for process return buffer to peer and clear the buffer pointer*/
            if (GEN_TOPO_BUF_ORIGIN_EXT_BUF & in_port_ptr->common.flags.buf_origin)
            {
               gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);

               in_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
               in_port_ptr->common.bufs_ptr[0].max_data_len = 0;
               in_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
               gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
            }
            else
            {
               /** else it means topo buffer was assigned due to an underrun, return the buffer so that external buffer
                * can be reused for the next signal trigger.*/
               in_port_ptr->common.flags.force_return_buf = TRUE;
               gen_topo_check_return_one_buf_mgr_buf(&me_ptr->topo,
                                                     &in_port_ptr->common,
                                                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                     in_port_ptr->gu.cmn.id);
            }

#ifdef THIN_TOPO_SAFE_MODE
            /** Clear the propagated buffer pointers*/
            thin_topo_propagate_ext_in_buffer_to_nblc(topo_ptr->gu.log_id,
                                                      ext_in_port_ptr,
                                                      in_port_ptr,
                                                      GEN_TOPO_BUF_ORIGIN_INVALID);
#endif
         }
      }
      proc_info_ptr->atleast_one_inp_holds_ext_buffer = FALSE;
   }

   gen_cntr_handle_fwk_events_in_data_path(me_ptr);

   /** Poll control channel and check for incoming ctrl msgs.
    * If any present, do set param and return the msgs. */
   result |= cu_poll_and_process_ctrl_msgs(&me_ptr->cu);

   result |= proc_result;

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      if (check_if_needed_to_exit_thin_topo(topo_ptr) || (THIN_TOPO_ENTERED != topo_ptr->thin_topo_ptr->state))
      {
         gen_topo_exit_island_temporarily(topo_ptr);
         return gen_cntr_switch_from_thin_topo_to_gen_topo_during_process(me_ptr);
      }
   }

   return result;
}

/* thn container's callback for the interrupt signal.*/
ar_result_t thin_topo_signal_trigger_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "thin_topo_trigger: Received signal trigger");
#endif

   // Increment in process context
   me_ptr->st_module.processed_interrupt_counter++;
   me_ptr->st_module.steady_state_interrupt_counter++;

   /* Check for signal miss, if raised_interrupt_counter is greater than process counter,
    * one or more interrupts have not been serviced.
    * This check will be skipped for timer modules eg:spr, rat since raised_interrupt_counter is always 0
    * This checks kind of signal miss because of the container thread being busy and one or more interrupts have not
    * been serviced. */
   if (me_ptr->st_module.raised_interrupt_counter > me_ptr->st_module.processed_interrupt_counter)
   {
      bool_t continue_processing = TRUE;
      gen_cntr_check_handle_signal_miss(me_ptr, FALSE /*is_after_process*/, &continue_processing);
      if (!continue_processing)
      {
         return result;
      }
   }

   /*clear the trigger signal */
   posal_signal_clear_inline(me_ptr->st_module.trigger_signal_ptr);

   // querying the signal triggered timestamp before invoking the topo
   if (me_ptr->st_module.update_stm_ts_fptr && me_ptr->st_module.st_module_ts_ptr)
   {
      me_ptr->st_module.update_stm_ts_fptr(me_ptr->st_module.stm_ts_ctxt_ptr,
                                           &me_ptr->st_module.st_module_ts_ptr->timestamp);
      me_ptr->st_module.st_module_ts_ptr->is_valid = TRUE;
   }

   if (me_ptr->cu.cmd_msg.payload_ptr)
   {
      // if async command processing is going on then check for any pending event handling
      gen_cntr_handle_fwk_events_in_data_path(me_ptr);
   }

   /** Process one frame per signal trigger. */
   result = thin_topo_data_process_one_frame(me_ptr);

   /* Check for signal miss, can detect if process took too long and raised_interrupt_counter was incremented
    *  This checks signal miss because of the process being delayed and one or more interrupts have not been serviced.
    */
   if (me_ptr->st_module.raised_interrupt_counter > me_ptr->st_module.processed_interrupt_counter)
   {
      bool_t continue_processing = TRUE; // dummy
      gen_cntr_check_handle_signal_miss(me_ptr, TRUE /*is_after_process*/, &continue_processing);
   }

   me_ptr->topo.flags.need_to_ignore_signal_miss = FALSE;
   me_ptr->prev_err_print_time_ms                = me_ptr->topo.proc_context.err_print_time_in_this_process_ms;

   return result;
}

///////////////////////   Thin topo external output utilities ///////////////////////////////

/**
 * sets up output buffers and calls process on topo.
 *
 * any of the output port can trigger this.
 */
THIN_TOPO_STATIC ar_result_t thin_topo_output_buf_set_up_peer_cntr(gen_cntr_t *             me_ptr,
                                                                   gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                   gen_topo_output_port_t * out_port_ptr)
{
   ar_result_t result = AR_EOK;

   /* pops a buffer, polls & recreate buffers if required */
   result = gen_cntr_process_pop_out_buf(me_ptr, ext_out_port_ptr);
   if (AR_FAILED(result))
   {
      // error can be due to buffer reallocation failure, return buffer to make sure
      // process is not called with incorrect sized buffers.
      gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
      return result;
   }

   /** if ext output buf is not available print overrun and return */
   if (NULL == ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      // its a genuine overrun if mf is valid and buffers were created else.
      // note that ext buf may not be avaible if downstream is not connected/ICB is not available in that scenario
      // overrun needs to be handled by assigning a topo buffer until ICB buffers are allocated, which is capable
      // only by gen topo.
      if (out_port_ptr->common.flags.is_mf_valid && out_port_ptr->common.max_buf_len)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: Ext output port id 0x%lx buffer is unavailable, switching to gen topo to handle "
                      "overrun. ",
                      out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      out_port_ptr->gu.cmn.id);

         me_ptr->topo.thin_topo_ptr->state = THIN_TOPO_EXITED_AT_EXT_OUT_BUFFER_SETUP;
         return AR_ENOTREADY;
      }

      return result;
   }

   spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   ext_out_port_ptr->buf.data_ptr        = (int8_t *)(&(out_buf_ptr->data_buf));
   ext_out_port_ptr->buf.actual_data_len = 0;
   ext_out_port_ptr->buf.max_data_len    = out_buf_ptr->max_size;

   /** thin topo always assumes only one frame per buffer for peer container buffers.
    * Its never set, so no need reset everytime.
    * ext_out_port_ptr->num_frames_in_buf = 0; */

   out_buf_ptr->actual_size = 0;

   // todo: optimize the div, remove and use topo max len per buf
   uint32_t max_len_per_buf = topo_div_num(ext_out_port_ptr->buf.max_data_len, out_port_ptr->common.sdata.bufs_num);

   /** assign for the internal output port which is nothing but the end of an NBLC and propagate the same buffer
    * backwards.*/
   out_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;
   for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
   {
      out_port_ptr->common.bufs_ptr[b].data_ptr        = ext_out_port_ptr->buf.data_ptr + b * max_len_per_buf;
      out_port_ptr->common.bufs_ptr[b].actual_data_len = 0;
      out_port_ptr->common.bufs_ptr[b].max_data_len    = max_len_per_buf;
   }

   /** Propagate the ext output buffer backwards till start of the NBLC. Currently no result returned. */
   thin_topo_propagate_ext_out_buffer_to_nblc(me_ptr->topo.gu.log_id,
                                              out_port_ptr,
                                              GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED);

   return result;
}

THIN_TOPO_STATIC ar_result_t thin_topo_send_data_to_downstream_peer_cntr(gen_cntr_t *             me_ptr,
                                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                        spf_msg_header_t *       out_buf_header)
{
   ar_result_t result = AR_EOK;

#ifdef THIN_TOPO_SAFE_MODE
   // buffer existence is check in the topo layer itself.
   if (!out_buf_header)
   {
      return result;
   }
#endif

#ifdef VERBOSE_DEBUGGING
   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&out_buf_header->payload_start;
   gen_topo_module_t *    module_ptr  = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "gen_cntr_send_pcm_to_downstream_peer_cntr: (0x%lx, 0x%lx) timestamp: %lu (0x%lx%lx). size %lu",
                module_ptr->gu.module_instance_id,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->timestamp >> 32),
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->actual_size));
#endif

   // Reinterpret node's buffer as a header.
   posal_bufmgr_node_t out_buf_node = ext_out_port_ptr->cu.out_bufmgr_node;

   // Reinterpret the node itself as a spf_msg_t.
   spf_msg_t *msg_ptr   = (spf_msg_t *)&out_buf_node;
   msg_ptr->msg_opcode  = SPF_MSG_DATA_BUFFER;
   msg_ptr->payload_ptr = out_buf_node.buf_ptr;

   // TODO: we can avoid setting these for other topos as well for the data buffers. we can introduce a new spf msg API
   //       for data buffer handling
#ifdef THIN_TOPO_SAFE_MODE
   spf_msg_header_t *header_ptr = (spf_msg_header_t *)out_buf_node->buf_ptr;
   header_ptr->return_q_ptr     = out_buf_node->return_q_ptr;
   header_ptr->token            = (NULL != msg_token_ptr) ? *msg_token_ptr : (spf_msg_token_t){ 0 };
   header_ptr->rsp_result       = rsp_result;
   header_ptr->rsp_handle_ptr   = resp_handle_ptr;
   header_ptr->dst_handle_ptr   = ext_out_port_ptr;

   // If DS is disconnected or not opened yet, the self/downgraded state should not STARTED
   // and if port is not STARTED, execution will not reach here.
   if (!ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "downstream not connected. dropping buffer");
      gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
      goto __bailout;
   }
#endif

#ifdef PROC_DELAY_DEBUG
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "PROC_DELAY_DEBUG: GC Module 0x%lX: Ext output data sent from port 0x%lX",
                module_ptr->gu.module_instance_id,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
#endif

   result = posal_queue_push_back(ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr,
                                  (posal_queue_element_t *)msg_ptr);
   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to deliver buffer dowstream. Dropping");
      gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
      goto __bailout;
   }
   else
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "pushed buffer downstream 0x%p. Current bit mask 0x%x",
                   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr,
                   me_ptr->cu.curr_chan_mask);
#endif
   }

__bailout:
   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
   return result;
}

/*
 * sends media fmt, output buf, and EoS.
 * if output buffer size is > 0,
 * deliver it to downstream, otherwise return the buffer to bufQ
 */
THIN_TOPO_STATIC ar_result_t thin_topo_send_peer_cntr_out_buffers(gen_cntr_t *             me_ptr,
                                                                 gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t             result              = AR_EOK;
   gen_topo_output_port_t *out_port_ptr        = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   spf_msg_header_t *      header              = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
   spf_msg_data_buffer_t * out_buf_ptr         = (spf_msg_data_buffer_t *)&header->payload_start;
   topo_port_state_t       ds_downgraded_state = cu_get_external_output_ds_downgraded_port_state(&ext_out_port_ptr->cu);

   // can happen during overrun in GEN_CNTR or if we force process at DFG or if any downstream is stopped.
   if (TOPO_PORT_STATE_STARTED != ds_downgraded_state)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   " Dropping data %lu and/or metadata 0x%p as there's no external buf",
                   ext_out_port_ptr->buf.actual_data_len,
                   ext_out_port_ptr->md_list_ptr);

      ext_out_port_ptr->buf.actual_data_len = 0;

      gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
      return result;
   }

   // preparing out buf msg header, similar to gen_cntr_populate_peer_cntr_out_buf()
   out_buf_ptr->flags             = 0;
   out_buf_ptr->actual_size       = ext_out_port_ptr->buf.actual_data_len;
   out_buf_ptr->max_size          = ext_out_port_ptr->cu.buf_max_size;
   out_buf_ptr->metadata_list_ptr = NULL;

   // update only timestamp
   // in thin topo, we directly copy timestamp from internal buffer to external buffer.
   // thats why ext_out_port_ptr->next_out_buf_ts is unused
   if (out_port_ptr->common.sdata.flags.is_timestamp_valid)
   {
      cu_set_bits(&out_buf_ptr->flags,
                  out_port_ptr->common.sdata.flags.is_timestamp_valid,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

      out_buf_ptr->timestamp = out_port_ptr->common.sdata.timestamp;
   }
   else if (me_ptr->st_module.st_module_ts_ptr)
   {
      // if TS is not valid and this is STM container get latest signal trigger TS
      // from the STM module and populate
      cu_set_bits(&out_buf_ptr->flags,
                  out_port_ptr->common.sdata.flags.is_timestamp_valid,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

      out_buf_ptr->timestamp = out_port_ptr->common.sdata.timestamp;
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Outgoing timestamp: setting STM module's trigger ts msw %lu, lsw %lu",
                   (uint32_t)out_buf_ptr->timestamp,
                   (uint32_t)(out_buf_ptr->timestamp >> 32));
#endif
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Outgoing timestamp: %lu (0x%lx%lx), flag=0x%lx, size=%lu",
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->timestamp >> 32),
                (uint32_t)out_buf_ptr->timestamp,
                out_buf_ptr->flags,
                out_buf_ptr->actual_size);
#endif

   // pack the buffer
   // NOTE: if module output unpacked, container needs to convert to packed.
   //       This topo modules should always output max_data_len, so there wont be any need
   //       to move data.
#ifdef THIN_TOPO_SAFE_MODE
   topo_media_fmt_t *module_med_fmt_ptr = out_port_ptr->common.media_fmt_ptr;
   if (TOPO_DEINTERLEAVED_UNPACKED == module_med_fmt_ptr->pcm.interleaving)
   {
      // if actual=max len, then already packed.
      if (ext_out_port_ptr->buf.actual_data_len < ext_out_port_ptr->buf.max_data_len)
      {
         topo_media_fmt_t *med_fmt_ptr      = &ext_out_port_ptr->cu.media_fmt;
         uint32_t          num_ch           = med_fmt_ptr->pcm.num_channels;
         uint32_t          bytes_per_ch     = ext_out_port_ptr->buf.actual_data_len / num_ch;
         uint32_t          max_bytes_per_ch = ext_out_port_ptr->buf.max_data_len / num_ch;

         // ch=0 is at the right place already
         for (uint32_t ch = 1; ch < num_ch; ch++)
         {
            memsmove(ext_out_port_ptr->buf.data_ptr + ch * bytes_per_ch,
                     bytes_per_ch,
                     ext_out_port_ptr->buf.data_ptr + ch * max_bytes_per_ch,
                     bytes_per_ch);
         }
      }
   }
#endif

   // check and send prebuffer
   gen_cntr_check_and_send_prebuffers(me_ptr, ext_out_port_ptr, out_buf_ptr);

   // todo: can send prebuffer at external port start in the command handling itself ?
   result = thin_topo_send_data_to_downstream_peer_cntr(me_ptr, ext_out_port_ptr, header);

   // if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   // todo: no need to check buffer origin since only ext buffer is used.

   // it's enough to reset first buf, as only this is checked against.
   out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
   out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
   out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
   gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);

   ext_out_port_ptr->buf.data_ptr        = NULL;
   ext_out_port_ptr->buf.actual_data_len = 0;
   ext_out_port_ptr->buf.max_data_len    = 0;

#ifdef THIN_TOPO_SAFE_MODE
   /** Clear the propagated buffer pointers*/
   thin_topo_propagate_ext_out_buffer_to_nblc(me_ptr->topo.gu.log_id, out_port_ptr, GEN_TOPO_BUF_ORIGIN_INVALID);
#endif

   return result;
}

///////////////////////////////////////////////// THIN TOPO external input port utils
////////////////////////////////////////////////////
THIN_TOPO_STATIC ar_result_t thin_topo_input_data_set_up_peer_cntr(gen_cntr_t             *me_ptr,
                                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result        = AR_EOK;
   gen_topo_input_port_t *in_port_ptr   = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   spf_msg_header_t      *header        = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   // if input buffer do not contain integer PCM samples per channel, return it immediately with error code.
   // Under safe mode to reduce MPPS for HW-EP containers (1 ms)
#ifdef SAFE_MODE
   if (SPF_FIXED_POINT == ext_in_port_ptr->cu.media_fmt.data_format)
   {
      uint32_t unit_size = ext_in_port_ptr->cu.media_fmt.pcm.num_channels *
                           TOPO_BITS_TO_BYTES(ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample);

      if (input_buf_ptr->actual_size % unit_size)
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "Returning an input buffer that do not contain the same PCM samples on all channel!");
         return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
      }
   }
#endif

#ifdef PROC_DELAY_DEBUG
   GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                       DBG_HIGH_PRIO,
                       "PROC_DELAY_DEBUG: GC Module 0x%lX: Ext Input data received on port 0x%lX",
                       module_ptr->gu.module_instance_id,
                       in_port_ptr->gu.cmn.id);
#endif

   // drop incoming metadata in thin topo,
   // todo: check if there are any other conditions for which buffer needs to reutnred to the queue, and switch to gen
   // topo. may be media format ?

   bool_t any_non_steady_state_flags_are_set =
      cu_get_bits(input_buf_ptr->flags,
                  DATA_BUFFER_FLAG_EOF_MASK,
                  DATA_BUFFER_FLAG_EOF_SHIFT); // todo: can we ignore EOF for ST cntrs ?

   if (input_buf_ptr->metadata_list_ptr || (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state) ||
       any_non_steady_state_flags_are_set)
   {
      me_ptr->topo.thin_topo_ptr->state = THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_HIGH_PRIO,
                          "Exiting thin topo from MID 0x%lX, Ext Inp 0x%lX since data flow is starting?%lu or "
                          "received "
                          "md?%lu",
                          in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                          in_port_ptr->gu.cmn.id,
                          (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state),
                          (input_buf_ptr->metadata_list_ptr != NULL));
#endif

      return AR_ENOTREADY;
   }

   ext_in_port_ptr->buf.data_ptr        = (int8_t *)&input_buf_ptr->data_buf;
   ext_in_port_ptr->buf.actual_data_len = input_buf_ptr->actual_size;
   ext_in_port_ptr->buf.max_data_len    = input_buf_ptr->actual_size; // deinterleaving is wrt this size.
   ext_in_port_ptr->buf.mem_map_handle  = 0; // mem map handle is not used for inter-container buffers.

   // First time TS gets copied here to ts_to_sync, later adjusted depending upon the
   thin_topo_copy_timestamp_from_input(me_ptr, ext_in_port_ptr);

   // data will be copied from ext to int buffer in the topo context for thin topo
   return result;
}

THIN_TOPO_STATIC ar_result_t thin_topo_copy_timestamp_from_input(gen_cntr_t *            me_ptr,
                                                                gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t   new_ts_valid = FALSE;
   int64_t  new_ts       = 0;
   uint32_t size         = 0;

   spf_msg_header_t *     header  = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;
   new_ts_valid =
      cu_get_bits(buf_ptr->flags, DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK, DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

   new_ts = buf_ptr->timestamp;
   size   = buf_ptr->actual_size;

   // Check TS disc whenever a new buffer is popped from the external input.
   // in_port_ptr->common.sdata.timestamp is updated with the next input expected TS only if the THIN_TOPO_TS_DISC_DEBUG
   // enabled.
#ifdef THIN_TOPO_TS_DISC_DEBUG
   bool_t ts_disc = gen_topo_is_timestamp_discontinuous(in_port_ptr->common.sdata.flags.is_timestamp_valid,
                                                        in_port_ptr->common.sdata.timestamp,
                                                        new_ts_valid,
                                                        new_ts);

   if (ts_disc)
   {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         "timestamp discontinuity detected for module 0x%lX,0x%lx. expected (valid: %u, "
                         "ts: %ld us), incoming (valid: %u, ts: %ld us)",
                         in_port_ptr->gu.common.module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.common.id,
                         next_sdata_ptr->flags.is_timestamp_valid,
                         (uint32_t)in_port_ptr->common.sdata.timestamp,
                         new_ts_valid,
                         (uint32_t)new_ts);
   }
#endif

   // cache the external input timestamp in ts_to_sync
   if (size)
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
      // next_in_port_ptr->ts_to_sync.ts_continue = continue_ts;
      in_port_ptr->ts_to_sync.ts_valid = new_ts_valid;
      in_port_ptr->ts_to_sync.ivalue   = new_ts;
      in_port_ptr->ts_to_sync.fvalue   = 0;
   }

   return AR_EOK;
}

/**
 * Encode (AEnc, VEnc), split A2DP (encode, decode), decode with ASM loopback, push mode.
 */
THIN_TOPO_STATIC ar_result_t thin_topo_input_dataQ_trigger_peer_cntr(gen_cntr_t             *me_ptr,
                                                                     gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->topo;
   INIT_EXCEPTION_HANDLING

   result = posal_queue_pop_front(ext_in_port_ptr->gu.this_handle.q_ptr,
                                  (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg));
   if (AR_EOK != result)
   {
      ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
      return result;
   }

#ifdef BUF_CNT_DEBUG
   if (ext_in_port_ptr->vtbl_ptr->is_data_buf(me_ptr, ext_in_port_ptr))
   {
      ext_in_port_ptr->cu.buf_recv_cnt++;
   }
#endif

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Popped an input msg buffer 0x%lx with opcode 0x%x from (miid,port-id) (0x%lX, 0x%lx) queue",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
#endif

   // process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      {
         TRY(result, thin_topo_input_data_set_up_peer_cntr(me_ptr, ext_in_port_ptr));
         break;
      }
      // thin topo cannot handle the following data msg opcodes, hence exiting
      case SPF_MSG_DATA_MEDIA_FORMAT:
      default:
      {
         GEN_CNTR_MSG_ISLAND(topo_ptr->gu.log_id,
                             DBG_HIGH_PRIO,
                             "Thin topo cannot handle input data msg opcode 0x%lx on module, port id  "
                             "(0x%lX, %lx), exiting thin topo.",
                             ext_in_port_ptr->cu.input_data_q_msg.msg_opcode,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         topo_ptr->thin_topo_ptr->state = THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP;

         THROW(result, AR_ENOTREADY);
         break;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      if (THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP == topo_ptr->thin_topo_ptr->state)
      {
         // return to the input q and swtich to gen topo
         ar_result_t tempresult =
            posal_queue_insert_front(ext_in_port_ptr->gu.this_handle.q_ptr,
                                     (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg));
         if (AR_EOK != tempresult)
         {
            GEN_CNTR_MSG_ISLAND(topo_ptr->gu.log_id,
                                DBG_ERROR_PRIO,
                                "Failed to insert buffer back to front of the queue");
         }

         ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;

         // continue process from the first the module
         topo_ptr->thin_topo_ptr->rest_of_module_proc_list_ptr = topo_ptr->started_sorted_module_list_ptr;
      }
   }

   return result;
}