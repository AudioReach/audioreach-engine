/**
 * \file spl_topo_data_process.c
 *
 * \brief
 *
 *     Implementation of topo_process and related helper functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_topo_i.h"
#include "simp_topo_i.h"
#include "gen_topo_buf_mgr.h"

static void        spl_topo_copy_sdata_flags(capi_stream_data_v2_t *sdata_to,
                                             capi_stream_data_v2_t *sdata_from);
static ar_result_t spl_topo_assign_start_module(spl_topo_t *               topo_ptr,
                                                gu_module_list_t *         bw_kick_module_ptr,
                                                gu_module_list_t **        fw_kick_module_pptr,
                                                spl_topo_process_status_t *fw_kick_module_proc_status_ptr);
static capi_err_t spl_topo_bypass_input_to_output(spl_topo_t *        topo_ptr,
                                                  spl_topo_module_t * module_ptr,
                                                  capi_stream_data_t *inputs[],
                                                  capi_stream_data_t *outputs[]);
static ar_result_t spl_topo_process_module(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);

static ar_result_t spl_topo_setup_proc_ctx_in_port(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
static void spl_topo_setup_int_in_port_sdata(spl_topo_t *            topo_ptr,
                                             spl_topo_input_port_t * in_port_ptr,
                                             spl_topo_output_port_t *connected_out_port_ptr);
static ar_result_t spl_topo_setup_ext_in_port_sdata(spl_topo_t *           topo_ptr,
                                                    spl_topo_input_port_t *in_port_ptr,
                                                    spl_topo_input_port_t *ext_in_port_ptr);
static void spl_topo_setup_dummy_in_sdata(spl_topo_t *            topo_ptr,
                                          spl_topo_input_port_t * in_port_ptr,
                                          uint32_t                port_idx,
                                          capi_buf_t *            scratch_bufs_ptr,
                                          spl_topo_output_port_t *prev_out_port_ptr);
static void spl_topo_setup_dummy_out_sdata(spl_topo_t *            topo_ptr,
                                           spl_topo_output_port_t *out_port_ptr,
                                           capi_buf_t *            scratch_bufs_ptr);

static ar_result_t spl_topo_setup_proc_ctx_out_port(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);

static ar_result_t spl_topo_adjust_in_buf(spl_topo_t *           topo_ptr,
                                          spl_topo_input_port_t *in_port_ptr,
                                          bool_t *               data_was_consumed_ptr);
static ar_result_t spl_topo_adjust_ext_in_buf(spl_topo_t *           topo_ptr,
                                              spl_topo_input_port_t *in_port_ptr,
                                              spl_topo_ext_buf_t *   ext_buf_ptr);
static ar_result_t spl_topo_adjust_int_in_buf(spl_topo_t *            topo_ptr,
                                              spl_topo_input_port_t * in_port_ptr,
                                              spl_topo_output_port_t *connected_out_port_ptr);

static void spl_topo_adjust_out_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);

static void        spl_topo_adjust_int_out_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);

static uint32_t spl_topo_check_reduce_sdata_input_length(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);
static ar_result_t spl_topo_check_drop_partial_input_for_thresh_mod(spl_topo_t *       topo_ptr,
                                                                    spl_topo_module_t *module_ptr,
                                                                    uint32_t           reduced_prev_len);
static void spl_topo_set_start_samples(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_max);
static ar_result_t spl_topo_in_port_check_set_data_flow_begin(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
static ar_result_t spl_topo_process_attached_modules(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);

/* =======================================================================
Function Definitions
========================================================================== */
/*
 * Utility function that copies sdata flags & timestamp but not buf_ptr or bufs_num
 *
 * This function doesn't touch the marker_eos, since that gets transferred along with eos md.
 */
static void spl_topo_copy_sdata_flags(capi_stream_data_v2_t *sdata_to_ptr, capi_stream_data_v2_t *sdata_from_ptr)
{
   // Save old marker_eos.
   uint32_t to_eos = sdata_to_ptr->flags.marker_eos;

   sdata_to_ptr->flags     = sdata_from_ptr->flags;
   sdata_to_ptr->timestamp = sdata_from_ptr->timestamp;

   // Save old marker_eos.
   sdata_to_ptr->flags.marker_eos = to_eos;

   return;
}

/**
 * Setup sdata bufs before process. For input, sdata buffers should point to the beginning
 * of data.
 * - in SPL_CNTR only deinterleaved unpacked is accepted.
 * - in SPL_CNTR for input ports, buffers live in the connected output ports, so use the connected output
 *   ports to adjust ptrs and then transfer over to the input port.
 */
static inline ar_result_t spl_topo_populate_int_input_sdata_bufs(spl_topo_t *            topo_ptr,
                                                                 spl_topo_input_port_t * in_port_ptr,
                                                                 spl_topo_output_port_t *connected_out_port_ptr)

{
   return simp_topo_populate_int_input_sdata_bufs(topo_ptr, in_port_ptr, connected_out_port_ptr);
}

/**
 * Check where to start the backward kick from. This should be the module closest to the
 * tail of the sorted list which either contains unconsumed input data has a pending input
 * media format to apply.
 *
 * Unconsumed input data: We try to process the data that is furthest downstream first to
 *                        avoid data building up in internal buffers.
 *
 * Pending media format: We want to propagate pending media format downstream first so that
 *                       upstream modules aren't blocked from processing due to waiting for
 *                       the pending media format. We look at the module's input side compared
 *                       to the output side because that's one less module to visit in the loop.
 *                       However we also need to check the output pending media format of external
 *                       output ports since those would otherwise not be visited.
 *
 * If a module meets these criteria, we need to start the backwards kick from that depth. All
 * modules of the same depth should be processed, so keep searching backwards until you find the
 * earliest module in the sorted list with that same depth. That is the starting module.
 */
static ar_result_t spl_topo_assign_start_module(spl_topo_t *               topo_ptr,
                                                gu_module_list_t *         bw_kick_module_ptr,
                                                gu_module_list_t **        fw_kick_module_pptr,
                                                spl_topo_process_status_t *fw_kick_module_proc_status_ptr)
{
   ar_result_t       result           = AR_EOK;
   gu_module_list_t *current_list_ptr = NULL;

   *fw_kick_module_pptr            = NULL; // If no valid start module found, should return null ptr
   *fw_kick_module_proc_status_ptr = SPL_TOPO_PROCESS_UNEVALUATED; // return default as un evaluated.

   for (current_list_ptr = bw_kick_module_ptr; current_list_ptr; LIST_RETREAT(current_list_ptr))
   {
      spl_topo_module_t *curr_module_ptr = (spl_topo_module_t *)current_list_ptr->module_ptr;

      bool_t is_set_start_module = FALSE;
      /* If input media fmt is pending, set start module */
      if (spl_topo_module_has_pending_in_media_fmt(topo_ptr, curr_module_ptr))
      {
         /* If media format is pending at any port then set the capi event so that it is handled after module
          * process.*/
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&topo_ptr->t_base, media_fmt_event);
         is_set_start_module = TRUE;
      }
      /* If a module is within nblc and it doesn't have pending eos or data
       * then its trigger won't be satisfied, skip this module.
       */
      else if (SPL_TOPO_PROCESS == spl_topo_module_processing_decision(topo_ptr, curr_module_ptr))
      {
         is_set_start_module = TRUE;
         // return proc status of the module, spl_topo_process() can avoid redundant evaluation
         *fw_kick_module_proc_status_ptr = SPL_TOPO_PROCESS;
      }

      if (is_set_start_module)
      {
         *fw_kick_module_pptr = current_list_ptr;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "found module with starting criteria: miid 0x%lx",
                  curr_module_ptr->t_base.gu.module_instance_id);
#endif

         break;
      }
   }

   return result;
}

/**
 * Checks if the module should be processed. All of the following
 * should hold:
 * - Not disabled. (if not, should bypass)
 * - Connected. (if not, should skip)
 * - Module state is started. (if not, should skip)
 * - Other skip cases are input or output specific. Check spl_topo_triggger_policy.c.
 */
spl_topo_process_status_t spl_topo_module_processing_decision(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   bool_t IS_EXT_TRIGGER_NOT_SATISFIED_UNUSED = FALSE;

   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;
   for (uint32_t port_idx = 0; port_idx < proc_context_ptr->num_in_ports; port_idx++)
   {
      proc_context_ptr->in_port_scratch_ptr[port_idx].flags.is_trigger_present =
         TOPO_PROC_CONTEXT_TRIGGER_NOT_EVALUATED;
   }

   for (uint32_t port_idx = 0; port_idx < proc_context_ptr->num_out_ports; port_idx++)
   {
      proc_context_ptr->out_port_scratch_ptr[port_idx].flags.is_trigger_present =
         TOPO_PROC_CONTEXT_TRIGGER_NOT_EVALUATED;
   }

   // skipped modules should not be processed
   if (module_ptr->flags.is_skip_process)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo mpd skipping module: miid 0x%lx.",
               module_ptr->t_base.gu.module_instance_id);
#endif
      return SPL_TOPO_PROCESS_SKIP;
   }

   if (module_ptr->flags.is_any_inp_port_at_gap)
   {
      // Assign data flow state, which is necessary before checking the trigger policy.
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
           LIST_ADVANCE(in_port_list_ptr))
      {
         spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->t_base.common.data_flow_state)
         {
            spl_topo_in_port_check_set_data_flow_begin(topo_ptr, in_port_ptr);

            // If we are still at gap that implies there was no data on the input port. If there's an eof, then go ahead
            // and clear it since there's nothing to flush.
            if ((TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->t_base.common.data_flow_state) &&
                spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr))
            {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_ERROR_PRIO,
                        "Clearing pending eof on input port miid 0x%lx idx 0x%lx (and its upstream connected port), "
                        "port "
                        "is "
                        "at gap.",
                        in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        in_port_ptr->t_base.gu.cmn.index);
#endif

               // EOF is cleared, try to process again.
               spl_topo_input_port_clear_pending_eof(topo_ptr, in_port_ptr);
               topo_ptr->proc_info.state_changed_flags.data_moved = TRUE;
            }
         }
      }
   }

   // If module is not at the nblc boundary and if there is not trigger policy in topo then run a simple check.
   if (!module_ptr->t_base.flags.is_nblc_boundary_module && (0 == topo_ptr->t_base.num_data_tpm))
   {
      if (module_ptr->t_base.gu.input_port_list_ptr)
      {
         spl_topo_input_port_t *in_port_ptr =
            (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
         bool_t is_trigger_present = FALSE;

         proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;

         if ((TOPO_PORT_STATE_STARTED == in_port_ptr->t_base.common.state) &&
             (in_port_ptr->t_base.common.flags.is_mf_valid))
         {
            if (spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr) ||
                spl_topo_ip_contains_metadata(topo_ptr, in_port_ptr))
            {
               is_trigger_present = TRUE;
               proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present =
                  TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
            }
         }

         if (!is_trigger_present)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "spl_topo mpd skipping module: miid 0x%lx input trigger not satisifed (simple check).",
                     module_ptr->t_base.gu.module_instance_id);
#endif
            return SPL_TOPO_PROCESS_SKIP;
         }
      }

      if (module_ptr->t_base.gu.output_port_list_ptr)
      {
         spl_topo_output_port_t *out_port_ptr =
            (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;
         bool_t is_trigger_preset = FALSE;

         proc_context_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;

         // Even for simple module processing decision, we have to check for nonzero output port actual data length.
         // This is because the nblc_end module may choose not to consume input data, leaving a full data buffer
         // upstream. If we are in a backward kick scenario, the upstream module will see the output buffer as full. In
         // this case module process should be avoided.
         if (TOPO_PORT_STATE_STARTED == out_port_ptr->t_base.common.state &&
             (out_port_ptr->t_base.common.flags.is_mf_valid) && (!out_port_ptr->t_base.common.flags.media_fmt_event) &&
             (0 != spl_topo_get_out_port_empty_space(topo_ptr, out_port_ptr)))
         {
            is_trigger_preset = TRUE;
            proc_context_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present =
               TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
         }
         if (!is_trigger_preset)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "spl_topo mpd skipping module: miid 0x%lx output trigger not satisifed (simple check).",
                     module_ptr->t_base.gu.module_instance_id);
#endif
            return SPL_TOPO_PROCESS_SKIP;
         }
      }
   }
   else if (!gen_topo_is_module_data_trigger_condition_satisfied(&(module_ptr->t_base),
                                                                 &IS_EXT_TRIGGER_NOT_SATISFIED_UNUSED,
                                                                 &(topo_ptr->t_base.proc_context)))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo mpd skipping module: miid 0x%lx trigger not satisifed.",
               module_ptr->t_base.gu.module_instance_id);
#endif
      return SPL_TOPO_PROCESS_SKIP;
   }

   return SPL_TOPO_PROCESS;
}

static ar_result_t spl_topo_process_attached_modules(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (out_port_ptr->t_base.gu.attached_module_ptr)
      {
         capi_err_t attached_proc_result = CAPI_EOK;

         spl_topo_module_t *out_attached_module_ptr = (spl_topo_module_t *)out_port_ptr->t_base.gu.attached_module_ptr;

         gen_topo_input_port_t *attached_mod_ip_port_ptr =
            (gen_topo_input_port_t *)out_attached_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;

         if ((!out_port_ptr->t_base.common.flags.is_mf_valid) || (!out_port_ptr->t_base.common.sdata.buf_ptr) ||
             ((0 == out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len) &&
              (NULL == out_port_ptr->t_base.common.sdata.metadata_list_ptr)) ||
             (attached_mod_ip_port_ptr->common.flags.module_rejected_mf))
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
            uint32_t mask = (out_attached_module_ptr->t_base.flags.disabled << 2) |
                            (out_port_ptr->t_base.common.flags.is_mf_valid << 1) |
                            attached_mod_ip_port_ptr->common.flags.module_rejected_mf;
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Skipping process on elementary module miid 0x%lx for input port idx %ld id 0x%lx -  %ld, "
                     "(0x%X B2:disable, B1:mf_valid, B0:rej_mf), no buffer %ld, no data provided %ld, md list ptr NULL "
                     "%ld",
                     out_attached_module_ptr->t_base.gu.module_instance_id,
                     out_port_ptr->t_base.gu.cmn.index,
                     module_ptr->t_base.gu.module_instance_id,
                     mask,
                     (NULL == out_port_ptr->t_base.common.sdata.buf_ptr),
                     (NULL == out_port_ptr->t_base.common.sdata.buf_ptr)
                        ? 0
                        : (0 == out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len),
                     (NULL == out_port_ptr->t_base.common.sdata.metadata_list_ptr));
#endif
            continue;
         }

         // update data-flow state for the attached module's input port.
         gen_topo_handle_data_flow_begin(&(topo_ptr->t_base),
                                         &(attached_mod_ip_port_ptr->common),
                                         &(attached_mod_ip_port_ptr->gu.cmn));

         if (out_attached_module_ptr->t_base.flags.disabled)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Skipping process on disabled elementary module miid 0x%lx",
                     out_attached_module_ptr->t_base.gu.module_instance_id);
#endif
            continue;
         }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Before process elementary module miid 0x%lx for output port idx %ld id 0x%lx",
                  out_attached_module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id);
#endif

         // Before calling capi process, for unpacked V1 update lens for all the channels
         uint32_t out_port_idx = out_port_ptr->t_base.gu.cmn.index;
         if (GEN_TOPO_MF_PCM_UNPACKED_V1 == attached_mod_ip_port_ptr->common.flags.is_pcm_unpacked)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Before attached process: output port idx = %ld, attached miid = 0x%lx setting up sdata for "
                     "unpacked V1",
                     out_port_ptr->t_base.gu.cmn.index,
                     out_attached_module_ptr->t_base.gu.module_instance_id);
#endif

            capi_stream_data_v2_t *out_port_sdata_ptr = topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx];
            for (uint32_t i = 1; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
            {
               out_port_sdata_ptr->buf_ptr[i].actual_data_len = out_port_sdata_ptr->buf_ptr[0].actual_data_len;
               out_port_sdata_ptr->buf_ptr[i].max_data_len    = out_port_sdata_ptr->buf_ptr[0].max_data_len;
            }
         }

         simp_topo_set_process_begin(topo_ptr);
#ifdef PROC_DELAY_DEBUG
         uint64_t time_before = posal_timer_get_time();
#endif

         // We only pass the attached module's output port index stream data to the attached module. Elementary modules
         // are always SISO so they expect to have only a single sdata in the stream data array for inputs and outputs.
         // clang-format off
         IRM_PROFILE_MOD_PROCESS_SECTION(out_attached_module_ptr->t_base.prof_info_ptr, topo_ptr->t_base.gu.prof_mutex,
         attached_proc_result =
            out_attached_module_ptr->t_base.capi_ptr->vtbl_ptr
               ->process(out_attached_module_ptr->t_base.capi_ptr,
                         (capi_stream_data_t **)&(topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx]),
                         (capi_stream_data_t **)&(topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx]));
         );
         // clang-format on

#ifdef PROC_DELAY_DEBUG
         if (IS_VOICE_SCENARIO_ID(module_ptr->t_base.gu.sg_ptr->sid))
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_HIGH_PRIO,
                     "PROC_DELAY_DEBUG: SC Module 0x%lX: Process took %lu us",
                     out_attached_module_ptr->t_base.gu.module_instance_id,
                     (uint32_t)(posal_timer_get_time() - time_before));
         }
#endif
         simp_topo_set_process_end(topo_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4

        TOPO_MSG(topo_ptr->t_base.gu.log_id,DBG_LOW_PRIO,"M_iid 0x%lX output ts_valid - %d , TS[MSW, LSW] - [%d, %d]",
                out_attached_module_ptr->t_base.gu.module_instance_id,
                topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx].flags.is_timestamp_valid,
                (uint32_t )(topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx].timestamp >>32),
                (uint32_t )topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx].timestamp);
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "After process elementary module miid 0x%lx for output port idx %ld id 0x%lx",
                  out_attached_module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id);
#endif

         // Don't ignore need more for attached modules.
         if (CAPI_FAILED(attached_proc_result))
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_ERROR_PRIO,
                     "elementary module miid 0x%lx for output port idx %ld id 0x%lx returned error 0x%lx during "
                     "process",
                     out_attached_module_ptr->t_base.gu.module_instance_id,
                     out_port_ptr->t_base.gu.cmn.index,
                     module_ptr->t_base.gu.module_instance_id,
                     attached_proc_result);
            return AR_EFAILED;
         }
      }
   }

   return result;
}

/**
 * Calls the module's process() vtable function using input and output sdata arguments
 * from the topo_ptr->proc_context.
 */
static ar_result_t spl_topo_process_module(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   bool_t in_marker_eos  = 0;
   bool_t out_marker_eos = 0;

   if (spl_topo_fwk_handles_flushing_eos(topo_ptr, module_ptr))
   {
      spl_topo_input_port_t *in_port_ptr =
         (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *out_port_ptr =
         (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

      in_marker_eos  = in_port_ptr->t_base.common.sdata.flags.marker_eos;
      out_marker_eos = out_port_ptr->t_base.common.sdata.flags.marker_eos;
   }

   // If there is not enough space in the output to consume all input, reduce the input actual data length sent
   // to the module. This should be handled by modules, but might not be so it is safest to handle in the fwk.
   uint32_t reduced_prev_len_first_port = spl_topo_check_reduce_sdata_input_length(topo_ptr, module_ptr);

   TRY(result, simp_topo_process_module(topo_ptr, module_ptr));

   TRY(result, spl_topo_check_drop_partial_input_for_thresh_mod(topo_ptr, module_ptr, reduced_prev_len_first_port));

   if (spl_topo_fwk_handles_flushing_eos(topo_ptr, module_ptr))
   {
      spl_topo_input_port_t *in_port_ptr =
         (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *out_port_ptr =
         (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

      if ((in_marker_eos != in_port_ptr->t_base.common.sdata.flags.marker_eos) ||
          (out_marker_eos != out_port_ptr->t_base.common.sdata.flags.marker_eos))
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_1
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Warning: module id 0x%8x claims to not handle metadata however it overwrote marker_eos. input "
                  "before %ld, after %ld, output before %ld, after %ld. Fwk overwriting input and output marker_eos "
                  "with values from before process.",
                  module_ptr->t_base.gu.module_instance_id,
                  in_marker_eos,
                  in_port_ptr->t_base.common.sdata.flags.marker_eos,
                  out_marker_eos,
                  out_port_ptr->t_base.common.sdata.flags.marker_eos);
#endif

         in_port_ptr->t_base.common.sdata.flags.marker_eos  = in_marker_eos;
         out_port_ptr->t_base.common.sdata.flags.marker_eos = out_marker_eos;
      }
   }

   // Check fail cases, capi writes input actual data len > max data len.
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (in_port_ptr->common.sdata.flags.marker_eos)
      {
         in_port_ptr->common.sdata.flags.end_of_frame = TRUE;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Sets up the input port related fields of the process context for a particular input port and index.
 */
static ar_result_t spl_topo_setup_proc_ctx_in_port(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                 result                 = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr       = &topo_ptr->t_base.proc_context;
   spl_topo_output_port_t *    connected_out_port_ptr = NULL;
   spl_topo_input_port_t *     ext_in_port_ptr        = NULL;
   bool_t                      short_circuit          = FALSE;

   spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &connected_out_port_ptr, &ext_in_port_ptr);

   if (ext_in_port_ptr && !ext_in_port_ptr->ext_in_buf_ptr->send_to_topo)
   {
      // port blocked from external input
      short_circuit = TRUE;
   }
   else if (TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT !=
            proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present)
   {
      short_circuit = spl_topo_in_port_trigger_blocked(topo_ptr, in_port_ptr);
   }

   in_port_ptr->flags.short_circuited = short_circuit;
   // Short circuit handling of this input port.
   if (short_circuit)
   {
      // spl_topo_setup_dummy_in_sdata clears the marker_eos flag, so we have to add it back.
      bool_t prev_marker_eos = in_port_ptr->t_base.common.sdata.flags.marker_eos;

      capi_buf_t *scratch_bufs_ptr = proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].bufs;
      spl_topo_output_port_t *DUMMY_OUTPUT_PTR = NULL;
      spl_topo_setup_dummy_in_sdata(topo_ptr,
                                    in_port_ptr,
                                    in_port_ptr->t_base.gu.cmn.index,
                                    scratch_bufs_ptr,
                                    DUMMY_OUTPUT_PTR);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Short circuit from setup input port for input port idx = %ld miid = 0x%x, mf_not_recieved? %ld, "
               "connected_out_port_ptr 0x%lx, ext_in_port_ptr 0x%lx",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               spl_topo_media_format_not_received_on_port(topo_ptr, &(in_port_ptr->t_base.common)),
               connected_out_port_ptr,
               ext_in_port_ptr);
#endif

      // spl_topo_setup_dummy_in_sdata clears the marker_eos flag, so we have to add it back.
      in_port_ptr->t_base.common.sdata.flags.marker_eos = prev_marker_eos;
   }
   else
   {
      // Update the in_port's sdata fields. Then sets the process context's sdata_ptr to point to
      // the in_port's sdata fields.
      if (connected_out_port_ptr)
      {
         // Connected port is internal cases.
         spl_topo_setup_int_in_port_sdata(topo_ptr, in_port_ptr, connected_out_port_ptr);
      }
      else if (ext_in_port_ptr)
      {
         // Connected port is an external buffer case.
         TRY(result, spl_topo_setup_ext_in_port_sdata(topo_ptr, in_port_ptr, ext_in_port_ptr));
      }
      // Set previous actual data len, so we can determine if all input data was consumed after process.
      // All actual data lengths should be equal, so using first channel is enough.
      proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0] =
         in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

      // Set the previous marker eos/eos_dfg/eof flags.
      proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.prev_marker_eos =
         in_port_ptr->t_base.common.sdata.flags.marker_eos;
      proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.prev_eos_dfg =
         spl_topo_input_port_has_dfg_or_flushing_eos(&in_port_ptr->t_base);
   }

   proc_context_ptr->in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index] = &in_port_ptr->t_base.common.sdata;

   if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == in_port_ptr->t_base.common.flags.is_pcm_unpacked) &&
       ((gen_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr)->capi_ptr)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: input port idx = %ld, miid = 0x%lx setting up sdata for unpacked V1",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      capi_stream_data_v2_t *in_port_sdata_ptr = proc_context_ptr->in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index];
      for (uint32_t i = 1; i < in_port_ptr->t_base.common.sdata.bufs_num; i++)
      {
         in_port_sdata_ptr->buf_ptr[i].actual_data_len = in_port_sdata_ptr->buf_ptr[0].actual_data_len;
         in_port_sdata_ptr->buf_ptr[i].max_data_len    = in_port_sdata_ptr->buf_ptr[0].max_data_len;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Checks if a moves to TOPO_DATA_FLOW_STATE_FLOWING.
 */
static ar_result_t spl_topo_in_port_check_set_data_flow_begin(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result             = AR_EOK;
   spl_topo_module_t *module_ptr         = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   uint32_t           in_port_data_len   = 0;
   bool_t             in_port_marker_eos = FALSE;

   // If the port state is already flowing, nothing to do.
   if (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->t_base.common.data_flow_state)
   {
      return result;
   }

   in_port_data_len   = spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr);
   in_port_marker_eos = spl_topo_input_port_has_pending_flushing_eos(topo_ptr, in_port_ptr);

   // Arrival of first data (eos counts as data) after at gap signals back to start state. Flushing EOS should
   // count as data begin since zero pushing counts as data.
   if ((0 != in_port_data_len) || (in_port_marker_eos))
   {
      spl_topo_update_check_data_flow_event_flag(topo_ptr, &(in_port_ptr->t_base.gu.cmn), TOPO_DATA_FLOW_STATE_FLOWING);

      TRY(result,
          gen_topo_handle_data_flow_begin(&(topo_ptr->t_base),
                                          &(in_port_ptr->t_base.common),
                                          &(in_port_ptr->t_base.gu.cmn)));

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Input port idx = %ld, miid = 0x%lx received first data after at gap, state becomes flowing. in port "
               "data len %d marker eos %d",
               in_port_ptr->t_base.gu.cmn.index,
               module_ptr->t_base.gu.module_instance_id,
               in_port_data_len,
               in_port_ptr->t_base.common.sdata.flags.marker_eos);
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Sets up the input port's stream data structure based on the connected output port.
 */
static void spl_topo_setup_int_in_port_sdata(spl_topo_t *            topo_ptr,
                                             spl_topo_input_port_t * in_port_ptr,
                                             spl_topo_output_port_t *connected_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                 result           = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;
   capi_buf_t *       scratch_bufs_ptr = proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].bufs;
   spl_topo_module_t *module_ptr       = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   bool_t             DST_IS_INPUT     = TRUE;
   bool_t             SRC_IS_OUTPUT    = FALSE;

   if (connected_out_port_ptr->md_list_ptr)
   {
      // Copy metadata from the connected upstream output port to this input port.
      TRY(result,
          spl_topo_transfer_md_between_ports(topo_ptr,
                                             (void *)in_port_ptr,
                                             DST_IS_INPUT,
                                             (void *)connected_out_port_ptr,
                                             SRC_IS_OUTPUT));
   }

   // If we don't have any input data, get an output buffer for connected upstream port in case of:
   // 1. Need to flush zeros.
   // 2. Metadata exists.
   // 3. Marker eos is set. If EOS is in internal list of the module and module handles metadata, pending zero
   //    will can be zero since module needs to handle zero pushing. In that case we need to rely upon marker_eos
   if ((!connected_out_port_ptr->t_base.common.bufs_ptr[0].data_ptr) &&
       (module_ptr->t_base.pending_zeros_at_eos || in_port_ptr->t_base.common.sdata.metadata_list_ptr ||
        in_port_ptr->t_base.common.sdata.flags.marker_eos))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Input port idx 0x%lx miid 0x%lx has %ld bytes per channel of zeros to flush but no input data. Getting "
               "a buffer from the buf mgr.",
               in_port_ptr->t_base.gu.cmn.index,
               module_ptr->t_base.gu.module_instance_id,
               module_ptr->t_base.pending_zeros_at_eos);
#endif

      spl_topo_get_output_port_buffer(topo_ptr, connected_out_port_ptr);
   }

   if (connected_out_port_ptr->t_base.common.bufs_ptr[0].data_ptr)
   {
      // Internal port has a held buffer.
      spl_topo_input_port_t *DUMMY_EXT_IN_PTR = NULL;

      // Update sdata. This does not update the buf_ptr. We need this even for a dummy buffer.
      // EOF Was already set on upstream port due to being needed by the module processing decision.
      spl_topo_copy_sdata_flags(&(in_port_ptr->t_base.common.sdata), &(connected_out_port_ptr->t_base.common.sdata));
      simp_topo_clear_sdata_flags(&(connected_out_port_ptr->t_base.common.sdata));

      if (module_ptr->t_base.pending_zeros_at_eos)
      {
         topo_2_append_eos_zeros(topo_ptr, module_ptr, in_port_ptr, connected_out_port_ptr, DUMMY_EXT_IN_PTR);
      }

      spl_topo_populate_int_input_sdata_bufs(topo_ptr, in_port_ptr, connected_out_port_ptr);

      in_port_ptr->t_base.common.sdata.flags.end_of_frame = spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: internal input port idx = %ld, miid = 0x%lx actual data len 1 ch = %ld of %ld, num "
               "bufs %d dfg %d eos %ld eof %ld",
               in_port_ptr->t_base.gu.cmn.index,
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
               in_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len,
               in_port_ptr->t_base.common.sdata.bufs_num,
               gen_topo_md_list_has_dfg(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
               in_port_ptr->t_base.common.sdata.flags.marker_eos,
               in_port_ptr->t_base.common.sdata.flags.end_of_frame);

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: internal input port idx = %ld, miid = 0x%lx ts = %ld",
               in_port_ptr->t_base.gu.cmn.index,
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.common.sdata.timestamp);
#endif
   }
   else
   {
/**
 * Internal port does not have a held buffer.
 * This case will come during backwards kick. Consider the following topo:
 * A -> B -> C -> D. We begin spl_topo_process() with partial data in module C.
 * So the start_module is module C, data lies in B's output buffer. We process C -> D.
 * Then we come back and process B. Since there was no data in module B before process,
 * B's input (A's output) does not have an output buffer. We still need to call capi_process
 * because the capi might output data even without any input data (example buffering module).
 *
 * In this case we need to setup the input stream data to have dummy structures that are non-NULL
 * to avoid crashes in capis. The dummy stream data will have actual and max data len = 0;
 *
 * Keep the flags, since flag transfer sometimes needs to happen via topo_process in the absence of any data,
 * such as for data flow gap state.
 */
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_LOW_PRIO,
               "The connected output port to input port idx = %ld, miid = 0x%lx does not have a bufmgr buffer, eos "
               "0x%lx!",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               connected_out_port_ptr->t_base.common.sdata.flags.marker_eos);
#endif

      spl_topo_setup_dummy_in_sdata(topo_ptr,
                                    in_port_ptr,
                                    in_port_ptr->t_base.gu.cmn.index,
                                    scratch_bufs_ptr,
                                    connected_out_port_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: internal input port idx = %ld, miid = 0x%lx is a dummy, data flow gap 0x%lx eos 0x%lx "
               "eof %ld",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               gen_topo_md_list_has_dfg(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
               in_port_ptr->t_base.common.sdata.flags.marker_eos,
               in_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
}

/**
 * Called only from spl_topo_process_module.
 *
 * Reduces input side to the minimum of:
 * 1. If there is not enough space in the output to consume all input, reduce the input actual data length sent
 * to the module. This should be handled by modules, but might not be, so it is safest to handle in the fwk.
 *
 * This needs to be done after setting prev_actual_data_len since even if all passed-in data is consumed, it is
 * still a partially-consumed buffer case since the non-passed in data still remains unconsumed.
 *
 * 2. If there is a timestamp discontinuity in the input buffer, we can only send data before the timestamp
 *    discontinuity.
 *
 * The final input length should be the minimum of both above cases.
 *
 * Returns the reduced length of the first port. This is needed for checking whether to drop partial data
 * for threshold modules EOF cases (otherwise we don't know how much partial data was sent to the module).
 */
static uint32_t spl_topo_check_reduce_sdata_input_length(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{

   uint32_t new_in_len    = 0;
   uint32_t case_1_in_len = 0;
   uint32_t case_2_in_len = 0;

   uint32_t reduced_prev_len       = 0;
   bool_t   wrote_reduced_prev_len = FALSE;

#if 0
   uint32_t out_actual_len = 0;
   // Case 1 - This is only for SISO.
   // Not handling this for:
   // - non-SISO modules since we don't know the input-output mapping.
   if ((1 == module_ptr->t_base.gu.num_input_ports) && (1 == module_ptr->t_base.gu.num_output_ports))
   {
      spl_topo_input_port_t *in_port_ptr =
         (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *out_port_ptr =
         (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

      bool_t short_circuit_input  = in_port_ptr->flags.short_circuited;
      bool_t short_circuit_output = out_port_ptr->flags.short_circuited;

      if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (DM_MODE_FIXED_OUTPUT == module_ptr->dm_mode))
      {
         // DM modules can be given more input than output.
      }
      else if ((!short_circuit_input) && (!short_circuit_output))
      {
         uint32_t in_sr         = in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate;
         uint32_t in_bps        = in_port_ptr->t_base.common.media_fmt_ptr->pcm.bits_per_sample;
         uint32_t in_actual_len = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

         uint32_t out_sr          = out_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate;
         uint32_t out_bps         = out_port_ptr->t_base.common.media_fmt_ptr->pcm.bits_per_sample;
         out_actual_len           = out_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len;
         uint32_t out_len_samples = out_actual_len / out_bps;

         // First, floor to integer samples. Then scale bytes.
         uint32_t scaled_in_samples = (out_len_samples * in_sr) / out_sr;
         uint32_t scaled_in_bytes   = (scaled_in_samples * in_bps);
         case_1_in_len              = MIN(in_actual_len, scaled_in_bytes);
      }
   }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4_FORCE
   else
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Can't scale input to output for module miid 0x%lx. dynamic # in ports %ld, dynamic # out ports %ld",
               module_ptr->t_base.gu.module_instance_id,
               module_ptr->t_base.gu.num_input_ports,
               module_ptr->t_base.gu.num_output_ports);
   }
#endif
#endif
   // Check for timestamp discontinuity case. This is for any SISO/MIMO case.
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      // 1. External input case. No need to handle since we block buffering when timestamp is discontinuous.
      // 2. Internal input case. We need to handle this since we can only check for discontinuity after the post-disc
      //    data is already in the buffer.
      spl_topo_input_port_t * in_port_ptr       = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *conn_out_port_ptr = NULL;
      spl_topo_input_port_t * conn_in_port_ptr  = NULL;

      if (in_port_ptr->flags.short_circuited)
      {
         continue;
      }

      spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &conn_out_port_ptr, &conn_in_port_ptr);

      uint32_t in_actual_len = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

      // If there's no ts disc, just use existing input data len.
      if (conn_out_port_ptr && conn_out_port_ptr->flags.ts_disc)
      {
         case_2_in_len = capi_cmn_divide(conn_out_port_ptr->ts_disc_pos_bytes, in_port_ptr->t_base.common.sdata.bufs_num);
      }
      else
      {
         case_2_in_len = in_actual_len;
      }

      // Don't take into account case_1 for non SISO cases. Otherwise it's the min.
      new_in_len = (case_1_in_len != 0) ? MIN(case_1_in_len, case_2_in_len) : case_2_in_len;

#if 0
      // If this module is a dm module, shrink input to required input samples requested by the module.
      if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (DM_MODE_FIXED_OUTPUT == module_ptr->dm_mode))
      {
         uint32_t dm_req_bytes = in_port_ptr->req_samples_info.samples_in *
                                 TOPO_BITS_TO_BYTES(in_port_ptr->t_base.common.media_fmt_ptr->pcm.bits_per_sample);

         if (new_in_len > dm_req_bytes)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_HIGH_PRIO,
                     "DM reducing inp len on input port idx %ld miid 0x%lx  to dm req bytes "
                     "%ld from in len in bytes %d",
                     in_port_ptr->t_base.gu.cmn.index,
                     module_ptr->t_base.gu.module_instance_id,
                     dm_req_bytes,
                     new_in_len);

            new_in_len = dm_req_bytes;
#endif
         }
         else if (new_in_len < dm_req_bytes)
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_ERROR_PRIO,
                     "DM req bytes not satisfied after reducing inp len on input port idx %ld miid 0x%lx dm req bytes "
                     "%ld in len in bytes %d",
                     in_port_ptr->t_base.gu.cmn.index,
                     module_ptr->t_base.gu.module_instance_id,
                     dm_req_bytes,
                     new_in_len);
            // We can't break on error because for timestamp discontinuity we have to try sending partial data.
         }
      }
#endif
      if (new_in_len != in_actual_len)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "input port idx %ld miid 0x%lx reducing input size to %ld bytes per ch. in_actual_len (%ld bytes per "
                  "ch) before resizing, ts_disc_pos_all_ch %ld",
                  in_port_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id,
                  new_in_len,
                  in_actual_len,
                  conn_out_port_ptr ? conn_out_port_ptr->ts_disc_pos_bytes : 0);
#endif

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
         for (uint32_t ch_idx = 0; ch_idx < in_port_ptr->t_base.common.sdata.bufs_num; ch_idx++)
         {
            in_port_ptr->t_base.common.sdata.buf_ptr[ch_idx].actual_data_len = new_in_len;
         }
#else
         in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len = new_in_len;
#endif
      }

      if (!wrote_reduced_prev_len)
      {
         wrote_reduced_prev_len = TRUE;
         reduced_prev_len       = new_in_len;
      }
   }

   return reduced_prev_len;
}

/**
 * For threshold modules that don't handle metadata, we need to handle the case where they were sent
 * EOF and a partial input frame. If they don't consume that input, the framework should drop the data.
 * To accomplish dropping the data, we simply mark the input as consumed. We need to do this before
 * propagating metadata to ensure that metadata corresponding to dropped input data makes it to the output
 * port.
 */
static ar_result_t spl_topo_check_drop_partial_input_for_thresh_mod(spl_topo_t *       topo_ptr,
                                                                    spl_topo_module_t *module_ptr,
                                                                    uint32_t           reduced_prev_len)
{
   ar_result_t result = AR_EOK;

   // Only needed for threshold modules and SISO, source, or sink.
   if (!module_ptr->threshold_data.is_threshold_module || (TOPO_MODULE_TYPE_MULTIPORT == module_ptr->flags.module_type))
   {
      return result;
   }

   // Only needed if fwk handles metadata.
   if (module_ptr->t_base.flags.supports_metadata)
   {
      return result;
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      spl_topo_input_port_t *in_port_ptr                 = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      uint32_t               threshold_bytes_per_channel = 0;

      if (in_port_ptr->flags.short_circuited)
      {
         continue;
      }

      threshold_bytes_per_channel =
         spl_topo_get_module_threshold_bytes_per_channel(&in_port_ptr->t_base.common, module_ptr);

      // Only needed if module was given partial input data.
      if (reduced_prev_len < threshold_bytes_per_channel)
      {
         // If the module consumed less than what it was given, pretend it consumed everything.
         if (in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len < reduced_prev_len)
         {
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len = reduced_prev_len;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_HIGH_PRIO,
                     "Threshold module miid = 0x%lx did not consume partial data on EOF, dropping data. Input port idx "
                     "= %ld",
                     in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->t_base.gu.cmn.index);
#endif
         }
      }
   }

   return result;
}

/**
 * Sets up the input port's stream data structure based on the spl_topo_ext_buf. We always call process()
 * with unconsumed input starting at the beginning of the buffer.
 *
 * No metadata handling is needed here: external input metadata is copied directly into the external
 * input's internal port structure's metadata list from the framework layer.
 */
static ar_result_t spl_topo_setup_ext_in_port_sdata(spl_topo_t *           topo_ptr,
                                                    spl_topo_input_port_t *in_port_ptr,
                                                    spl_topo_input_port_t *ext_in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result             = AR_EOK;
   spl_topo_ext_buf_t *    ext_buf_ptr        = ext_in_port_ptr->ext_in_buf_ptr;
   spl_topo_output_port_t *DUMMY_CONN_OUT_PTR = NULL;

   in_port_ptr->t_base.common.sdata.flags.end_of_frame = spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr);

   // If the topo has already consumed some data, we need to adjust the sdata buffer pointers to
   // the start of unconsumed data. This happens in the case of multiple threshold modules with
   // different thresholds (module w/ smaller threshold needs to get called multiple times).
   //
   // Not needed for internal ports since data is moved to top of internal buffers in topo layer.

   // We can't consume more data than exists. Checking max_data_len due to pushing zeros.
   VERIFY(result, ext_buf_ptr->bytes_consumed_per_ch <= ext_buf_ptr->buf_ptr[0].max_data_len);

   if (((spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr)->t_base.pending_zeros_at_eos)
   {
      topo_2_append_eos_zeros(topo_ptr,
                              (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr,
                              in_port_ptr,
                              DUMMY_CONN_OUT_PTR,
                              ext_in_port_ptr);
   }

   simp_topo_setup_ext_in_port_sdata(topo_ptr, in_port_ptr);

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * In the case that there is no buffer on an input port (aka no data to pass to
 * process), we still need to pass a proper stream data structure to the capi.
 * This function sets up that dummy stream data object.
 */
static void spl_topo_setup_dummy_in_sdata(spl_topo_t *            topo_ptr,
                                          spl_topo_input_port_t * in_port_ptr,
                                          uint32_t                port_idx,
                                          capi_buf_t *            scratch_bufs_ptr,
                                          spl_topo_output_port_t *prev_out_ptr)
{
   gen_topo_common_port_t *port_common_ptr = &in_port_ptr->t_base.common;

   if (spl_topo_media_format_not_received_on_port(topo_ptr, port_common_ptr))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: setup dummy port on in port idx = %ld miid = 0x%lx where media fmt not yet received.",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      // If media format was not yet received, everything is NULL/0, except we
      // can still transfer prev_conn_flags. We don't transfer pending data flow state flags
      // in this case.
      port_common_ptr->sdata.buf_ptr           = NULL;
      port_common_ptr->sdata.bufs_num          = 0;
      port_common_ptr->sdata.timestamp         = 0;
      port_common_ptr->sdata.metadata_list_ptr = NULL;

      // Transfer flags if they exist. Otherwise set flags to zero.
      if (prev_out_ptr)
      {
         bool_t DST_IS_INPUT  = TRUE;
         bool_t SRC_IS_OUTPUT = FALSE;

         spl_topo_copy_sdata_flags(&(in_port_ptr->t_base.common.sdata), &(prev_out_ptr->t_base.common.sdata));
         simp_topo_clear_sdata_flags(&(prev_out_ptr->t_base.common.sdata));

         spl_topo_transfer_md_between_ports(topo_ptr,
                                            (void *)in_port_ptr,
                                            DST_IS_INPUT,
                                            (void *)prev_out_ptr,
                                            SRC_IS_OUTPUT);
      }
      else
      {
         port_common_ptr->sdata.flags.word = 0;
      }

      port_common_ptr->sdata.flags.is_timestamp_valid = FALSE;
   }
   // Media format was received, we can initialize according to media format.
   else
   {
      uint32_t num_channels = port_common_ptr->media_fmt_ptr->pcm.num_channels;

      // data_ptr = NULL, actual_data_len = 0, max_data_len = 0.
      memset(scratch_bufs_ptr, 0, sizeof(capi_buf_t) * num_channels);

      port_common_ptr->sdata.buf_ptr  = scratch_bufs_ptr;
      port_common_ptr->sdata.bufs_num = num_channels;

      // Transfer flags if they exist. Otherwise set flags to zero.
      if (prev_out_ptr)
      {
         bool_t DST_IS_INPUT  = TRUE;
         bool_t SRC_IS_OUTPUT = FALSE;

         spl_topo_copy_sdata_flags(&(in_port_ptr->t_base.common.sdata), &(prev_out_ptr->t_base.common.sdata));
         simp_topo_clear_sdata_flags(&(prev_out_ptr->t_base.common.sdata));
         spl_topo_transfer_md_between_ports(topo_ptr,
                                            (void *)in_port_ptr,
                                            DST_IS_INPUT,
                                            (void *)prev_out_ptr,
                                            SRC_IS_OUTPUT);
      }
      else
      {
         port_common_ptr->sdata.flags.word = 0;
      }

      // Mark timestamp as invalid.
      port_common_ptr->sdata.flags.is_timestamp_valid = FALSE;
      port_common_ptr->sdata.timestamp                = 0;
   }

   // Dummy ports will always be V2.
   port_common_ptr->sdata.flags.stream_data_version = CAPI_STREAM_V2;
}

/**
 * In the case that media format was not received on an output port, this function
 * is used to assing sdata fields to NULL.
 */
static void spl_topo_setup_dummy_out_sdata(spl_topo_t *            topo_ptr,
                                           spl_topo_output_port_t *out_port_ptr,
                                           capi_buf_t *            scratch_bufs_ptr)
{
   gen_topo_common_port_t *port_common_ptr = &out_port_ptr->t_base.common;
   if (spl_topo_media_format_not_received_on_port(topo_ptr, port_common_ptr))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: setup dummy port on out port idx = %ld miid = 0x%lx where media fmt not yet recieved.",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      port_common_ptr->sdata.buf_ptr = NULL;
      /*bufs_num is by default 1, need to make is zero here.
       * otherwise module will try to access null buf_ptr.
       */
      port_common_ptr->sdata.bufs_num                 = 0;
      port_common_ptr->sdata.flags.is_timestamp_valid = FALSE;
      port_common_ptr->sdata.timestamp                = 0;

      port_common_ptr->sdata.flags.word                = 0;
      port_common_ptr->sdata.flags.stream_data_version = CAPI_STREAM_V2;
      port_common_ptr->sdata.metadata_list_ptr         = NULL;
   }
   // Media format was received, we can initialize according to media format.
   else
   {
      uint32_t num_channels = port_common_ptr->media_fmt_ptr->pcm.num_channels;

      // data_ptr = NULL, actual_data_len = 0, max_data_len = 0.
      memset(scratch_bufs_ptr, 0, sizeof(capi_buf_t) * num_channels);
      port_common_ptr->sdata.buf_ptr = scratch_bufs_ptr;

      // All metadata and timestamp fields will be empty. The timestamp is marked as
      // invalid.
      port_common_ptr->sdata.flags.is_timestamp_valid  = FALSE;
      port_common_ptr->sdata.timestamp                 = 0;
      port_common_ptr->sdata.metadata_list_ptr         = NULL;
      port_common_ptr->sdata.flags.word                = 0;
      port_common_ptr->sdata.flags.stream_data_version = CAPI_STREAM_V2;
   }
}

/**
 * Sets up the output port related fields of the process context for a particular output port and index.
 */
static ar_result_t spl_topo_setup_proc_ctx_out_port(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // Update the output port's sdata fields. Then sets the process context's sdata_ptr to point to
   // the output port's sdata fields.
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;

   bool_t short_circuit = FALSE;
   if (TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT !=
       proc_context_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].flags.is_trigger_present)
   {
      short_circuit = spl_topo_out_port_trigger_blocked(topo_ptr, out_port_ptr);
   }

   out_port_ptr->flags.short_circuited = short_circuit;
   if (short_circuit)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: short circuited from output port idx %ld miid 0x%lx",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
      spl_topo_setup_dummy_out_sdata(topo_ptr,
                                     out_port_ptr,
                                     proc_context_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].bufs);

      proc_context_ptr->out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index] = &out_port_ptr->t_base.common.sdata;

      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->t_base.common.flags.is_pcm_unpacked) &&
          ((gen_topo_module_t *)out_port_ptr->t_base.gu.cmn.module_ptr)->capi_ptr)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Before process: output port idx = %ld, miid = 0x%lx setting up sdata for unpacked V1",
                  out_port_ptr->t_base.gu.cmn.index,
                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

         capi_stream_data_v2_t *out_port_sdata_ptr =
            proc_context_ptr->out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index];
         for (uint32_t i = 1; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
         {
            out_port_sdata_ptr->buf_ptr[i].actual_data_len = out_port_sdata_ptr->buf_ptr[0].actual_data_len;
            out_port_sdata_ptr->buf_ptr[i].max_data_len    = out_port_sdata_ptr->buf_ptr[0].max_data_len;
         }
      }
      return result;
   }
   else
   {
      TRY(result, simp_topo_setup_proc_ctx_out_port(topo_ptr, out_port_ptr));
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Adjust an input port's buffer after process based on data consumed. Return the buffer
 * to the topo buffer manager if it is empty. Adjust the connected output buffer
 * so it reflects the adjustments in the input buffer. Different cases are requred for internal and
 * external ports.
 */
static ar_result_t spl_topo_adjust_in_buf(spl_topo_t *           topo_ptr,
                                          spl_topo_input_port_t *in_port_ptr,
                                          bool_t *               data_was_consumed_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   spl_topo_output_port_t *    connected_out_port_ptr = NULL;
   spl_topo_input_port_t *     ext_in_port_ptr        = NULL;
   gen_topo_process_context_t *proc_context_ptr       = &topo_ptr->t_base.proc_context;
   bool_t                      has_eos_dfg            = FALSE;

   VERIFY(result, topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_in_port_dfg_eos_left_port && data_was_consumed_ptr);

   if (in_port_ptr->flags.short_circuited)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "After process: not adjusting input port idx = %ld miid = 0x%x, input was short circuited.",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
      // In Spl topo bufs_ptr, and bufs_num are temporary for input.
      in_port_ptr->t_base.common.bufs_ptr       = NULL;
      in_port_ptr->t_base.common.sdata.bufs_num = 0;
      return result;
   }

   spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &connected_out_port_ptr, &ext_in_port_ptr);

   // Check if data was consumed is same for all types of input ports: nonzero actual data len.
   *data_was_consumed_ptr = (0 != in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len);

   // If eos/dfg was cleared on this process call, data flow ends.
   has_eos_dfg = spl_topo_input_port_has_dfg_or_flushing_eos(&in_port_ptr->t_base);

   if (connected_out_port_ptr)
   {
      // Connected port is internal cases.
      TRY(result, spl_topo_adjust_int_in_buf(topo_ptr, in_port_ptr, connected_out_port_ptr));
   }
   else if (ext_in_port_ptr)
   {
      // Connected port is external cases -> setup buffer.
      TRY(result, spl_topo_adjust_ext_in_buf(topo_ptr, in_port_ptr, ext_in_port_ptr->ext_in_buf_ptr));
   }

   // If eos or dfg was set on the input before process but cleared after, that means that during process it moved
   // past the input port. This the input port should be marked at_gap.
   if ((!has_eos_dfg) && (proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.prev_eos_dfg))
   {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Resetting inp idx = %ld, miid = 0x%lx which just moved to flow gap.",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      spl_topo_update_check_data_flow_event_flag(topo_ptr, &(in_port_ptr->t_base.gu.cmn), TOPO_DATA_FLOW_STATE_AT_GAP);

      // Below function resets input port without doing algo reset/destroying metadata. It also does
      // handle_data_flow_end.
      bool_t USE_BUFMGR_FALSE = FALSE;
      topo_basic_reset_input_port(&(topo_ptr->t_base), &(in_port_ptr->t_base), USE_BUFMGR_FALSE);

      // Allow external input ports to move to data flow gap state.
      if (ext_in_port_ptr)
      {
         topo_ptr->t_base.topo_to_cntr_vtable_ptr
            ->ext_in_port_dfg_eos_left_port(&(topo_ptr->t_base), ext_in_port_ptr->t_base.gu.ext_in_port_ptr);
      }

      // If there is an upstream connected output port, reset that port too. Since these ports share a md list, they
      // need to be reset at the same time (dfg/eos moves out of that md list).
      if (connected_out_port_ptr)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Resetting out port idx = %ld, miid = 0x%lx which just moved to flow gap.",
                  connected_out_port_ptr->t_base.gu.cmn.index,
                  connected_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

         topo_basic_reset_output_port(&(topo_ptr->t_base), &(connected_out_port_ptr->t_base), USE_BUFMGR_FALSE);
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   // In Spl topo bufs_ptr, and bufs_num are temporary for input.
   in_port_ptr->t_base.common.bufs_ptr       = NULL;
   in_port_ptr->t_base.common.sdata.bufs_num = 0;

   return result;
}

/**
 * Adjust an external input buffer after spl_topo_process(). Sets the bytes_consumed_per_ch
 * field (used by fwk layer) according to the actual_data_len field.
 */
static ar_result_t spl_topo_adjust_ext_in_buf(spl_topo_t *           topo_ptr,
                                              spl_topo_input_port_t *in_port_ptr,
                                              spl_topo_ext_buf_t *   ext_buf_ptr)
{
   ar_result_t result = AR_EOK;

   // External input ports can't have an internal timestamp discontinuity.
   bool_t   HAS_INT_TS_DISC_FALSE = FALSE;
   uint32_t TS_POS_ZERO           = 0;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   spl_topo_module_t *module_ptr = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
#endif

   // ext_buf_ptr being NULL means the fwk did not send down this external port. So nothing to do.
   if (!ext_buf_ptr->send_to_topo)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Not adjusting ext inp idx = %ld, miid = 0x%lx from fwk, send_to_topo %ld, nothing "
               "to adjust.",
               in_port_ptr->t_base.gu.cmn.index,
               module_ptr->t_base.gu.module_instance_id,
               ext_buf_ptr->send_to_topo);
#endif
      return result;
   }

   simp_topo_adjust_ext_in_buf(topo_ptr, in_port_ptr, ext_buf_ptr);

   if (in_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      spl_topo_ip_check_prop_eof(topo_ptr,
                                 in_port_ptr,
                                 ext_buf_ptr->bytes_consumed_per_ch,
                                 ext_buf_ptr->buf_ptr[0].actual_data_len,
                                 HAS_INT_TS_DISC_FALSE,
                                 TS_POS_ZERO);
   }

   // Transfer end_of_frame flag back to external buffer.
   // We do this for case of threshold modules with thresh < container thresh. For the multiple iters,
   // we need to read EOF from the internal port, not the external buffer. This breaks the design of
   // not writing fields back to the external buffer structure from the topo layer, however,
   // maintenance of eof becomes much more complicated with a different approach.
   ext_buf_ptr->end_of_frame = in_port_ptr->t_base.common.sdata.flags.end_of_frame;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "After process: external input idx %ld miid 0x%lx buffer bytes_consumed_per_ch = %ld, actual_data_len = "
            "%ld eof %ld",
            in_port_ptr->t_base.gu.cmn.index,
            module_ptr->t_base.gu.module_instance_id,
            ext_buf_ptr->bytes_consumed_per_ch,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            in_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

   return result;
}

/**
 * Adjust an interal input buffer after spl_topo_process(). Returns the input buffer if it is fully
 * consumed, otherwise removes any consumed data from the buffer.
 */
static ar_result_t spl_topo_adjust_int_in_buf(spl_topo_t *            topo_ptr,
                                              spl_topo_input_port_t * in_port_ptr,
                                              spl_topo_output_port_t *connected_out_port_ptr)
{
   ar_result_t                 result               = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr     = &topo_ptr->t_base.proc_context;
   uint32_t                    consumed_data_per_ch = 0;
   uint32_t                    consumed_data_all_ch = 0;
   capi_stream_data_v2_t *     in_sdata_ptr         = &in_port_ptr->t_base.common.sdata;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "After process: Internal input port idx %ld miid 0x%lx buffer actual data len = %ld of %ld, num bufs %d "
            "eos %ld eof %ld",
            in_port_ptr->t_base.gu.cmn.index,
            in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len,
            in_port_ptr->t_base.common.sdata.bufs_num,
            in_port_ptr->t_base.common.sdata.flags.marker_eos,
            in_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

   consumed_data_per_ch = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len; // only deint-unpacked supported.

   // If eos was propagated, clear the stored timestamp.
   bool_t prev_eos_marker =
      topo_ptr->t_base.proc_context.in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.prev_marker_eos;
   bool_t eos_was_propagated = (prev_eos_marker && (!in_port_ptr->t_base.common.sdata.flags.marker_eos));
   if (eos_was_propagated)
   {
      in_sdata_ptr->flags.is_timestamp_valid = false;
      in_sdata_ptr->timestamp                = 0;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Internal input port idx %ld miid 0x%lx propagated eos, clearing cached timestamp.",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
   }

   simp_topo_adjust_int_in_buf(topo_ptr, in_port_ptr, connected_out_port_ptr);

   if (in_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      spl_topo_ip_check_prop_eof(topo_ptr,
                                 in_port_ptr,
                                 consumed_data_per_ch,
                                 proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index]
                                    .prev_actual_data_len[0],
                                 connected_out_port_ptr->flags.ts_disc,
                                 connected_out_port_ptr->ts_disc_pos_bytes);
   }

   // Send sdata (includes timestamps, but not metadata) back to the connected output port.
   spl_topo_copy_sdata_flags(&(connected_out_port_ptr->t_base.common.sdata),
                             &(in_port_ptr->t_base.common.sdata));

   // Check to clear ts discontinuity.
   if (connected_out_port_ptr->flags.ts_disc)
   {
      consumed_data_all_ch = consumed_data_per_ch * in_port_ptr->t_base.common.media_fmt_ptr->pcm.num_channels;

      connected_out_port_ptr->ts_disc_pos_bytes -= consumed_data_all_ch;

      if (0 == connected_out_port_ptr->ts_disc_pos_bytes)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "output port idx %ld miid 0x%lx adjusting ts disc pos_bytes, consumed_data_all_ch "
                  "%ld, new ts_disc_pos_bytes %ld, setting ts_disc %ld to output port sdata (ts was %ld).",
                  connected_out_port_ptr->t_base.gu.cmn.index,
                  connected_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  consumed_data_all_ch,
                  connected_out_port_ptr->ts_disc_pos_bytes,
                  (int32_t)connected_out_port_ptr->disc_timestamp,
                  connected_out_port_ptr->t_base.common.sdata.timestamp);
#endif

         connected_out_port_ptr->t_base.common.sdata.timestamp = connected_out_port_ptr->disc_timestamp;
         spl_topo_clear_output_timestamp_discontinuity(topo_ptr, connected_out_port_ptr);
      }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      else
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "output port idx %ld miid 0x%lx adjusting ts disc pos_bytes, consumed_data_all_ch "
                  "%ld, new ts_disc_pos_bytes %ld",
                  connected_out_port_ptr->t_base.gu.cmn.index,
                  connected_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  consumed_data_all_ch,
                  connected_out_port_ptr->ts_disc_pos_bytes);
      }
#endif
   }

   return result;
}

/**
 * Adjust an output port's buffer after process based on data consumed. Different cases are required for
 * internal/external ports.
 */
static void spl_topo_adjust_out_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   bool_t   data_was_generated    = FALSE;
   uint32_t amount_data_generated = 0;
   bool_t   is_end_of_frame       = FALSE;

   // If media format wasn't received on this output port before the module process is called then buf ptr will be
   // NULL this is because we assumed it is part of a path not meant to be processed and thus topo_process wouldn't have
   // written any data to this port. So there's nothing to do.
   // We can not check for valid media format here as media format may have been raised from the module process context.
   if (NULL == out_port_ptr->t_base.common.sdata.buf_ptr)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "After process: not adjusting output port idx = %ld miid = 0x%x which was short circuited.",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
      return;
   }

   // If output port is short circuited then amount of data generated will be zero. But we still need to propagate
   // the metadata.
   amount_data_generated = out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

   // Check if data was generated is same for all types of output ports: nonzero actual data len.
   data_was_generated = (0 != amount_data_generated);

   // If data was generated, mark data_moved in process context.
   topo_ptr->proc_info.state_changed_flags.data_moved |= data_was_generated;

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      spl_topo_ext_buf_t *ext_out_buf_ptr =
         (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
            out_port_ptr->t_base.gu.ext_out_port_ptr);

      if (ext_out_buf_ptr->send_to_topo)
      {
         simp_topo_adjust_ext_out_buf(topo_ptr, out_port_ptr, ext_out_buf_ptr);
      }

      is_end_of_frame |= (ext_out_buf_ptr->timestamp_discontinuity || ext_out_buf_ptr->end_of_frame);
   }
   else
   {
      // Internal port case.
      spl_topo_adjust_int_out_buf(topo_ptr, out_port_ptr);

      is_end_of_frame |= out_port_ptr->t_base.common.sdata.flags.end_of_frame;
   }

   simp_topo_adjust_out_buf_md(topo_ptr, out_port_ptr, amount_data_generated);

   if (is_end_of_frame)
   {
      topo_ptr->simpt_event_flags.check_eof = TRUE;
#ifdef SPL_SIPT_DBG
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Output port idx = %ld, miid = 0x%lx check_eof becomes TRUE.",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
   }
}

/**
 * Adjust an internal output buffer after process based on data consumed.
 */
static void spl_topo_adjust_int_out_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   simp_topo_adjust_int_out_buf(topo_ptr, out_port_ptr);

   spl_topo_input_port_t *conn_in_port_ptr = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;

   /* If this output port is blocking the state propagation (#INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP)
    * then there can be a scenario where this output port is started (due to sg start) but connected_in_port
    * is stopped (due to propagated state). In this case, we need to drop the data here at the output port.
    * We can not drop the data on the connected input port because connected module processing will be skipped.
    */
   if ((out_port_ptr->t_base.common.flags.is_upstream_realtime) &&
       (TOPO_PORT_STATE_STARTED != conn_in_port_ptr->t_base.common.state) &&
       (out_port_ptr->t_base.common.bufs_ptr[0].data_ptr))
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: output port id 0x%lx, dropping %lu bytes per buf because real time data reached a "
               "port "
               "which is not started.",
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->t_base.gu.cmn.id,
               out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len);

      uint32_t bytes_across_all_ch = gen_topo_get_total_actual_len(&out_port_ptr->t_base.common);
      gen_topo_drop_all_metadata_within_range(topo_ptr->t_base.gu.log_id,
                                              (gen_topo_module_t *)out_port_ptr->t_base.gu.cmn.module_ptr,
                                              &out_port_ptr->t_base.common,
                                              bytes_across_all_ch,
                                              FALSE /*keep_eos_and_ba_md*/);

      gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->t_base.common);

      spl_topo_return_output_buf(topo_ptr, out_port_ptr);

      simp_topo_clear_sdata_flags(&out_port_ptr->t_base.common.sdata);
      simp_topo_clear_sdata_flags(&conn_in_port_ptr->t_base.common.sdata);
   }

   return;
}
/**
 * Checking and caching discontinuous timestamps for internal port cases.
 *
 * Due to supporting modules which partially consume input data in spl topo, we can only check for timestamp
 * discontinuity after process. To do so, we cache the previous output timestamp (per port) before process. After
 * process we extrapolate the old timestamp through data existing before process to get the expected timestamp, and
 * check for discontinuity between the expected timestamp and the new timestamp. If there is a timestamp discontinuity,
 * we cache the timestamp and it's position in the output port.
 */
void spl_topo_handle_internal_timestamp_discontinuity(spl_topo_t *            topo_ptr,
                                                      spl_topo_output_port_t *out_port_ptr,
                                                      uint32_t                prev_actual_data_len_all_ch,
                                                      bool_t                  is_ext_op)
{
   gen_topo_port_scratch_data_t *out_scratch_ptr =
      &(topo_ptr->t_base.proc_context.out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index]);

   // If no data was produced, we need to preserve the original output timestamp. Fwk copying the
   // timestamp from input is incorrect if no data got produced.
   if (0 == out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len)
   {
      // We can always copy to output. If output timestamp is invalid, timestamp is don't care. Skipping this for
      // optimization.
      if (/* out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid && */ out_scratch_ptr->flags.is_timestamp_valid)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Reverting to old timestamp in out port idx %ld miid 0x%lx since no data was produced, "
                  "old ts = %ld, new ts = %ld ",
                  out_port_ptr->t_base.gu.cmn.index,
                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  out_scratch_ptr->timestamp,
                  out_port_ptr->t_base.common.sdata.timestamp);
#endif
         out_port_ptr->t_base.common.sdata.timestamp = out_scratch_ptr->timestamp;
      }
      return;
   }

   // For external output ports, if there's a timestamp discontinuity when there's no pending output data,
   // there's no actions to take since there's no modules to send EOF to, and we aren't combining data before
   // and after the ts disc.
   if ((is_ext_op) && (0 == prev_actual_data_len_all_ch))
   {
      return;
   }

   // Don't need to do all this timestamp calculation if a new timestamp wasn't generated or old ts didn't
   // exist.
   if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid && out_scratch_ptr->flags.is_timestamp_valid)
   {
      // If the output buffer was NOT empty before process, this timestamp corresponds to a data point
      // part-way within the buffer. If there is no timestamp discontinuity, we can discard this timestamp
      // and extrapolate from the timestamp at the beginning of the buffer.
      // If there is a timestamp discontinuity, we have to store both timestamps and the point where the
      // discontinuity occurs.
      bool_t    TS_IS_VALID_TRUE   = TRUE;
      uint64_t *FRAC_TIME_PTR_NULL = NULL;

      // Extrapolate the previous timestamp out until the position of the new timestamp.
      uint32_t actual_data_len_samples_per_ch =
         topo_bytes_to_samples_per_ch(prev_actual_data_len_all_ch, out_port_ptr->t_base.common.media_fmt_ptr);

      int64_t expected_ts =
         out_scratch_ptr->timestamp + (topo_samples_per_ch_to_us(actual_data_len_samples_per_ch,
                                                                 out_port_ptr->t_base.common.media_fmt_ptr,
                                                                 FRAC_TIME_PTR_NULL));

      // if there wasn't any previous data then don't need to check for timestamp discontinuity.
      // copy the new timestamp.
      if ((actual_data_len_samples_per_ch) &&
          gen_topo_is_timestamp_discontinuous1(TS_IS_VALID_TRUE,
                                               expected_ts,
                                               TS_IS_VALID_TRUE,
                                               out_port_ptr->t_base.common.sdata.timestamp))
      {
         out_port_ptr->flags.ts_disc     = TRUE;
         out_port_ptr->ts_disc_pos_bytes = prev_actual_data_len_all_ch;

         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "ts discontinuity detected in out port idx %ld miid 0x%lx "
                  "old ts = %ld, expected ts = %ld, new ts = %ld at bytes (all ch) %ld",
                  out_port_ptr->t_base.gu.cmn.index,
                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  out_scratch_ptr->timestamp,
                  expected_ts,
                  out_port_ptr->t_base.common.sdata.timestamp,
                  out_port_ptr->ts_disc_pos_bytes);

         // The old timestamp should still be at the head of the buffer, and store the new timestamp in a cached
         // place for later.
         out_port_ptr->disc_timestamp                = out_port_ptr->t_base.common.sdata.timestamp;
         out_port_ptr->t_base.common.sdata.timestamp = out_scratch_ptr->timestamp;
      }
      else
      {
         // There is no discontinuity, DO NOT throw away the new timestamp and reassign the previous timestamp to the
         // head of the buffer.
         // Re-use the timestamp output from the process since it will be more accurate.
         // Don't set ts_disc to false - maybe there was a previous ts discontinuity, although in that case we
         // probably shouldn't reach this line of code.
         if (out_scratch_ptr->flags.is_timestamp_valid)
         {
            // If there is data in the buffer already, then make sure that the timestamp points to the first sample
            if (prev_actual_data_len_all_ch)
            {
               out_port_ptr->t_base.common.sdata.timestamp = out_scratch_ptr->timestamp;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_MED_PRIO,
                        "keeping old ts in out port idx %ld miid 0x%lx, with continuous timestamps "
                        "old ts = %ld, expected ts = %ld, new ts = %ld prev data len (all ch) %ld",
                        out_port_ptr->t_base.gu.cmn.index,
                        out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        out_scratch_ptr->timestamp,
                        expected_ts,
                        out_port_ptr->t_base.common.sdata.timestamp,
                        prev_actual_data_len_all_ch);
#endif
            }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
            else
            {

               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_MED_PRIO,
                        "keeping new ts in out port idx %ld miid 0x%lx, with continuous timestamps "
                        "old ts = %ld, expected ts = %ld, new ts = %ld",
                        out_port_ptr->t_base.gu.cmn.index,
                        out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        out_scratch_ptr->timestamp,
                        expected_ts,
                        out_port_ptr->t_base.common.sdata.timestamp);
            }
#endif
         }
         out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid = out_scratch_ptr->flags.is_timestamp_valid;
      }

      // Clear out scratch ptr data as sanity.
      out_scratch_ptr->timestamp                = 0;
      out_scratch_ptr->flags.is_timestamp_valid = FALSE;
   }
}

static capi_err_t spl_topo_bypass_input_to_output(spl_topo_t *        topo_ptr,
                                                  spl_topo_module_t * module_ptr,
                                                  capi_stream_data_t *inputs[],
                                                  capi_stream_data_t *outputs[])
{
   capi_err_t              result           = CAPI_EOK;
   gu_input_port_list_t *  in_port_list_ptr = NULL;
   gen_topo_input_port_t * in_port_ptr      = NULL;
   uint32_t                ip_idx           = 0;
   gen_topo_output_port_t *out_port_ptr     = NULL;
   uint32_t                op_idx           = 0;

#if SAFE_MODE
   // memcpy input to output
   // works for only one input port. for metadata only one output port must be present (as we are not cloning here).
   if ((1 != module_ptr->t_base.gu.num_input_ports) || (1 != module_ptr->t_base.gu.num_output_ports))
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: for memcpy from input to output, only one input or output must be present ",
               module_ptr->t_base.gu.module_instance_id);
      return CAPI_EFAILED;
   }
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Topo bypassing process, module id 0x%8x",
            module_ptr->t_base.gu.module_instance_id);
#endif

   in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr;
   in_port_ptr      = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
   ip_idx           = in_port_ptr->gu.cmn.index;

   out_port_ptr = (gen_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;
   op_idx       = out_port_ptr->gu.cmn.index;

   // For threshold modules, copy at most threshold amount of data.
   if (module_ptr->threshold_data.is_threshold_module)
   {
      uint32_t thresh_bytes_per_ch = spl_topo_get_module_threshold_bytes_per_channel(&in_port_ptr->common, module_ptr);

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      for (uint32_t ch_idx = 0; ch_idx < in_port_ptr->common.sdata.bufs_num; ch_idx++)
      {
         inputs[ip_idx]->buf_ptr[ch_idx].actual_data_len =
            MIN(inputs[ip_idx]->buf_ptr[ch_idx].actual_data_len, thresh_bytes_per_ch);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "For threshold module, input consumed became %ld, threshold bytes per ch %ld",
                  inputs[ip_idx]->buf_ptr[ch_idx].actual_data_len,
                  thresh_bytes_per_ch);
#endif
      }
#else // SAFE_MODE_SDATA_BUF_LENGTHS

      inputs[ip_idx]->buf_ptr[0].actual_data_len = MIN(inputs[ip_idx]->buf_ptr[0].actual_data_len, thresh_bytes_per_ch);
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "For threshold module, input consumed became %ld, threshold bytes per ch %ld",
               inputs[ip_idx]->buf_ptr[0].actual_data_len,
               thresh_bytes_per_ch);
#endif

#endif
   }

   // Reduce input length based on input timestamp discontinuity or output size.
   spl_topo_check_reduce_sdata_input_length(topo_ptr, module_ptr);

   if (inputs[ip_idx]->bufs_num != outputs[op_idx]->bufs_num)
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: bufs_num must match between input[0x%lx] (%lu) and output[0x%lx] (%lu)",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->gu.cmn.id,
               inputs[ip_idx]->bufs_num,
               out_port_ptr->gu.cmn.id,
               outputs[op_idx]->bufs_num);
   }

   simp_topo_bypass_input_to_output((capi_stream_data_t *)topo_ptr->t_base.proc_context.in_port_sdata_pptr[ip_idx],
                                    (capi_stream_data_t *)topo_ptr->t_base.proc_context.out_port_sdata_pptr[op_idx]);

   return result;
}

/**
 * Process the topology.
 */
ar_result_t spl_topo_process(spl_topo_t *topo_ptr, uint8_t path_index)
{
   INIT_EXCEPTION_HANDLING
   gu_module_list_t *          current_list_ptr  = NULL;
   ar_result_t                 result            = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr  = &topo_ptr->t_base.proc_context;
   bool_t                      IS_DATA_PATH_TRUE = TRUE;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &topo_ptr->t_base, FALSE /*do_reconcile*/);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Entering topo process, path index 0x%x", path_index);
#endif

   // Check where to start the backward kick from. This should be the module closest to the
   // tail of the sorted list which either contains unconsumed input data has a pending input
   // media format to apply.
   gu_module_list_t *bw_kick_module_ptr = NULL;

   /* Module to begin forwards kick */
   gu_module_list_t         *start_module_ptr   = topo_ptr->t_base.started_sorted_module_list_ptr;
   gu_module_list_t *        fw_kick_module_ptr = start_module_ptr;
   spl_topo_process_status_t proc_status        = SPL_TOPO_PROCESS_UNEVALUATED;

   // if bkwd kick is needed then find the module to start topo-process.
   if (topo_ptr->simpt1_flags.backwards_kick)
   {
      spf_list_get_tail_node((spf_list_node_t *)topo_ptr->t_base.started_sorted_module_list_ptr,
                             (spf_list_node_t **)&bw_kick_module_ptr);
      spl_topo_assign_start_module(topo_ptr, bw_kick_module_ptr, &fw_kick_module_ptr, &proc_status);
   }

   // Backwards kick loop. Will be skipped if no modules have pending mf or unconsumed data.
   // Backwards kicks until valid start module is found, then forward kicks to process each module
   for (; fw_kick_module_ptr;
        spl_topo_assign_start_module(topo_ptr, bw_kick_module_ptr, &fw_kick_module_ptr, &proc_status))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Topo process outer loop begin");
#endif
      bool_t sent_to_fwk_layer = FALSE;

      /* Select previous module to start backwards kick for next iteration */
      bw_kick_module_ptr = fw_kick_module_ptr;
      LIST_RETREAT(bw_kick_module_ptr);

      // Forward kick loop.
      for (current_list_ptr = fw_kick_module_ptr; current_list_ptr;
           LIST_ADVANCE(current_list_ptr), proc_status = SPL_TOPO_PROCESS_UNEVALUATED)
      {
         //bool_t   did_consume_input = FALSE;
         bool_t   mf_propped        = FALSE;
         uint32_t num_iters         = 1;

         // Get buffers, allocate sdata pointers, call cmn_module_process
         // manage input buffers.
         spl_topo_module_t *    curr_module_ptr = (spl_topo_module_t *)current_list_ptr->module_ptr;
         spl_topo_module_type_t module_type     = TOPO_MODULE_TYPE_SINGLE_PORT;

         if ((SPL_TOPO_INVALID_PATH_INDEX != path_index) && (path_index != curr_module_ptr->t_base.gu.path_index))
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_5
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Topo process, ignoring miid 0x%lx with path index 0x%x. ",
                     curr_module_ptr->t_base.gu.module_instance_id,
                     curr_module_ptr->t_base.gu.path_index);
#endif
            continue;
         }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Topo process inner loop begin mid 0x%lx",
                  curr_module_ptr->t_base.gu.module_instance_id);
#endif

         module_type = spl_topo_get_module_port_type(curr_module_ptr);

         if (SPL_TOPO_PROCESS_UNEVALUATED == proc_status)
         {
            proc_status = spl_topo_module_processing_decision(topo_ptr, curr_module_ptr);
         }

         if (SPL_TOPO_PROCESS_SKIP == proc_status)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Skipping module 0x%lx!",
                     curr_module_ptr->t_base.gu.module_instance_id);
#endif

            // Even when we skip a module, we can still try to propagate metadata through it. Metadata would only
            // propagate for metadata with offset 0 and no algo delay (or buffer associated). This is needed for
            // DFG propagation while modules are in stop state.
            bool_t   input_has_metadata_or_eos = FALSE;
            uint32_t input_size_before         = 0;
            TRY(result,
                spl_topo_propagate_metadata(topo_ptr,
                                            curr_module_ptr,
                                            input_has_metadata_or_eos,
                                            input_size_before,
                                            proc_status));

            // In realtime use cases, we can't allow data to stall due to a not connected graph. So, we should
            // drop data when it reaches a module that is not fully connected.
            bool_t is_realtime = topo_ptr->flags.is_real_time_topo;
            if (is_realtime && curr_module_ptr->flags.is_skip_process)
            {
               bool_t data_was_dropped = FALSE;
               TRY(result, spl_topo_module_drop_all_data(topo_ptr, curr_module_ptr, &data_was_dropped));

               // If was dropped, data moved.
               topo_ptr->proc_info.state_changed_flags.data_moved |= data_was_dropped;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
               if (!data_was_dropped)
               {
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "There was no data to drop for module 0x%lx!",
                           curr_module_ptr->t_base.gu.module_instance_id);
               }
#endif
            }

            if (capi_event_flag_ptr->media_fmt_event)
            {
               // If we have a pending media format at an input port, try to apply even for the skipped case.
               spl_topo_propagate_media_fmt_single_module(topo_ptr, IS_DATA_PATH_TRUE, curr_module_ptr, &mf_propped);
               topo_ptr->proc_info.state_changed_flags.mf_moved |= mf_propped;
            }
            continue;
         }

         num_iters = curr_module_ptr->t_base.num_proc_loops;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "module 0x%lx: Number of process iterations: %u",
                  curr_module_ptr->t_base.gu.module_instance_id,
                  num_iters);
#endif

         // Zero out the fwd kick context of input ports.
         for (gu_input_port_list_t *in_port_list_ptr = curr_module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
              LIST_ADVANCE(in_port_list_ptr))
         {
            spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
            memset(&(in_port_ptr->fwd_kick_flags), 0, sizeof(in_port_ptr->fwd_kick_flags));
         }

         for (uint32_t i = 0; i < num_iters; i++, proc_status = SPL_TOPO_PROCESS_UNEVALUATED)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "module 0x%lx: begin iteration %ld",
                     curr_module_ptr->t_base.gu.module_instance_id,
                     i);
#endif

            bool_t   input_has_metadata_or_eos = FALSE;
            uint32_t input_size_before         = 0;

            // Check the module processing decision for all remaining iterations. If it ever becomes false (eof cases),
            // we can exit the loop.
            if (SPL_TOPO_PROCESS_UNEVALUATED == proc_status)
            {
               proc_status = spl_topo_module_processing_decision(topo_ptr, curr_module_ptr);
               if (SPL_TOPO_PROCESS != proc_status)
               {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "module 0x%lx: process status is %d on iteration %d, breaking from iter loop.",
                           curr_module_ptr->t_base.gu.module_instance_id,
                           proc_status,
                           i);
#endif

                  break;
               }
            }

            /**
             * Setup input and output process context's sdata to pass to the module's process(). Also
             * setup process context's scratch_ptr.prev_actual_data_len field.
             */

            gen_topo_reset_process_context_sdata(proc_context_ptr, &curr_module_ptr->t_base);

            // Zero out scratch input structures.
            for (uint32_t port_idx = 0; port_idx < proc_context_ptr->num_in_ports; port_idx++)
            {
               proc_context_ptr->in_port_scratch_ptr[port_idx].prev_actual_data_len[0] = 0;
            }

            // Setup input ports.
            for (gu_input_port_list_t *in_port_list_ptr = curr_module_ptr->t_base.gu.input_port_list_ptr;
                 in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

               // Initialize to NULL. In non-error case it will be set in spl_topo_setup_proc_ctx_in_port.
               in_port_ptr->t_base.common.sdata.buf_ptr = NULL;

               TRY(result, spl_topo_setup_proc_ctx_in_port(topo_ptr, in_port_ptr));

               // Check if eos or metadata exists for this iteration of this module. Needed below for fwk propagation of
               // md.
               input_has_metadata_or_eos |=
                  ((in_port_ptr->t_base.common.sdata.metadata_list_ptr) || (curr_module_ptr->t_base.int_md_list_ptr) ||
                   in_port_ptr->t_base.common.sdata.flags.marker_eos);

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &in_port_ptr->t_base.common,
                                                TRUE /**is_input*/,
                                                in_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                FALSE));
#endif
            }

            // Zero out scratch output structures.
            for (uint32_t port_idx = 0; port_idx < proc_context_ptr->num_out_ports; port_idx++)
            {
               proc_context_ptr->out_port_scratch_ptr[port_idx].prev_actual_data_len[0] = 0;
            }

            // Setup output ports.
            for (gu_output_port_list_t *out_port_list_ptr = curr_module_ptr->t_base.gu.output_port_list_ptr;
                 out_port_list_ptr;
                 LIST_ADVANCE(out_port_list_ptr))
            {
               spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

               TRY(result, spl_topo_setup_proc_ctx_out_port(topo_ptr, out_port_ptr));

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &out_port_ptr->t_base.common,
                                                FALSE /**is_input*/,
                                                out_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                FALSE));
#endif
            }

            // For single input and output port, copy over input buffer attributes such as timestamp and flags to
            // output, since all modules may not take care of this.
            if (TOPO_MODULE_TYPE_SINGLE_PORT == module_type)
            {
               // SISO -> Only one input port, only one output port.
               spl_topo_input_port_t *in_port_ptr =
                  (spl_topo_input_port_t *)curr_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
               spl_topo_output_port_t *out_port_ptr =
                  (spl_topo_output_port_t *)curr_module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

               bool_t short_circuited = in_port_ptr->flags.short_circuited || out_port_ptr->flags.short_circuited;

               // Don't copy sdata if input is external input and we didn't send to topo, or if output is
               // external output and we didn't send to topo.
               if (!short_circuited)
               {
                  input_size_before = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

                  // Copy sdata flags from input to output for SISO module. Do not clear the input till process is
                  // complete. Don't transfer md until after process.
                  // Don't copy eof.
                  uint32_t old_op_eof    = out_port_ptr->t_base.common.sdata.flags.end_of_frame;
                  spl_topo_copy_sdata_flags(&(out_port_ptr->t_base.common.sdata), &(in_port_ptr->t_base.common.sdata));
                  out_port_ptr->t_base.common.sdata.flags.end_of_frame = old_op_eof;

                  if (in_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
                  {
                     simp_topo_fwk_estimate_process_timestamp(topo_ptr,
                                                              curr_module_ptr,
                                                              topo_ptr->t_base.proc_context
                                                                 .in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index],
                                                              topo_ptr->t_base.proc_context.out_port_sdata_pptr
                                                                 [out_port_ptr->t_base.gu.cmn.index]);
                  }
               }
            }
            else if ((TOPO_MODULE_TYPE_SINK == module_type) && (1 == curr_module_ptr->t_base.gu.num_input_ports))
            {
               // metadata propagation for one input Sink module
               spl_topo_input_port_t *in_port_ptr =
                  (spl_topo_input_port_t *)curr_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;

               input_size_before = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;
            }

            if (curr_module_ptr->t_base.bypass_ptr)
            {

#ifdef PROC_DELAY_DEBUG
               uint64_t time_before = posal_timer_get_time();
#endif
               // clang-format off
               IRM_PROFILE_MOD_PROCESS_SECTION(curr_module_ptr->t_base.prof_info_ptr, topo_ptr->t_base.gu.prof_mutex,
               result = spl_topo_bypass_input_to_output(topo_ptr,
                                                        curr_module_ptr,
                                                        (capi_stream_data_t **)
                                                           topo_ptr->t_base.proc_context.in_port_sdata_pptr,
                                                        (capi_stream_data_t **)
                                                           topo_ptr->t_base.proc_context.out_port_sdata_pptr);
               );
               // clang-format on

#ifdef PROC_DELAY_DEBUG
               if (IS_VOICE_SCENARIO_ID(curr_module_ptr->t_base.gu.sg_ptr->sid))
               {
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_HIGH_PRIO,
                           "PROC_DELAY_DEBUG: SC Module 0x%lX: Process took %lu us",
                           curr_module_ptr->t_base.gu.module_instance_id,
                           (uint32_t)(posal_timer_get_time() - time_before));
               }
#endif
               VERIFY(result, AR_SUCCEEDED(result));
            }
            else
            {
               TRY(result, spl_topo_process_module(topo_ptr, curr_module_ptr));
            }

            if (input_has_metadata_or_eos)
            {
               TRY(result,
                   spl_topo_propagate_metadata(topo_ptr,
                                               curr_module_ptr,
                                               input_has_metadata_or_eos,
                                               input_size_before,
                                               proc_status));
            }

            TRY(result, spl_topo_process_attached_modules(topo_ptr, curr_module_ptr));

            // Check how much data was consumed on each input port, and take actions accordingly.
            for (gu_input_port_list_t *in_port_list_ptr = curr_module_ptr->t_base.gu.input_port_list_ptr;
                 in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr            = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
               bool_t                 consumed_input_on_port = FALSE;

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &in_port_ptr->t_base.common,
                                                TRUE /**is_input*/,
                                                in_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                TRUE));
#endif

               TRY(result, spl_topo_adjust_in_buf(topo_ptr, in_port_ptr, &consumed_input_on_port));

               // If data was consumed, data did move.
               topo_ptr->proc_info.state_changed_flags.data_moved |= consumed_input_on_port;
               //did_consume_input |= consumed_input_on_port;
            }

            // Adjust actual len on output buffers.
            // New length = old length + amount of newly consumed data.
            // Buffers are not packed between modules, so the actual data len does
            // not indicate how much spacing there is between channels. Actual is
            // only used for populating the data length that module uses.
            for (gu_output_port_list_t *out_port_list_ptr = curr_module_ptr->t_base.gu.output_port_list_ptr;
                 out_port_list_ptr;
                 LIST_ADVANCE(out_port_list_ptr))
            {
               spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &out_port_ptr->t_base.common,
                                                FALSE /**is_input*/,
                                                out_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                TRUE));
#endif

               spl_topo_adjust_out_buf(topo_ptr, out_port_ptr);
            }
         }

         if (capi_event_flag_ptr->media_fmt_event)
         {
            // If we have a pending media format at an input port, we try to apply it after calling process. Process
            // might have consumed all the input data which would allow us to apply the new media format.
            spl_topo_propagate_media_fmt_single_module(topo_ptr, IS_DATA_PATH_TRUE, curr_module_ptr, &mf_propped);
            topo_ptr->proc_info.state_changed_flags.mf_moved |= mf_propped;
         }

      } // End of forward kick loop.

      if (topo_ptr->proc_info.state_changed_flags.mf_moved || capi_event_flag_ptr->media_fmt_event)
      {
         spl_topo_check_send_media_fmt_to_fwk_layer(topo_ptr, &sent_to_fwk_layer);
      }

      // If an output media format event reached an external output port, return
      // from topo process. After we return, we will deliver any partial data and
      // send the media format downstream. If there is no partial data to deliver,
      // we will continue to satisfy the gpd conditions and therefore will call topo_process()
      // again, which will push the input data to the output. In this way, we can
      // both send the media format downstream and also deliver output in the same
      // topo_process call.
      if (sent_to_fwk_layer)
      {
         // Since we return early from topo_process, we should try again to process.
         topo_ptr->proc_info.state_changed_flags.mf_moved |= TRUE;
         break;
      }

      if (NULL != bw_kick_module_ptr)
      {
         // DM modules need their expected_out_samples updated at the end of every backwards kick.
         // but if bw kick module is null then this will be updated in gpd context, after delivering the output buffer.
         bool_t IS_MAX_FALSE = FALSE;
         TRY(result, spl_topo_get_required_input_samples(topo_ptr, IS_MAX_FALSE));
      }
   } // End of backward kick loop.

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Topo process end");
#endif

   return result;
}

//set expected samples on the output port based on the container frame length.
static void spl_topo_set_start_samples_on_output_port(spl_topo_t *            topo_ptr,
                                                      spl_topo_output_port_t *out_port_ptr,
                                                      bool_t                  is_max)
{
   spl_topo_req_samples_t *samples_info_ptr =
      (is_max) ? &out_port_ptr->req_samples_info_max : &out_port_ptr->req_samples_info;

   //for max buffer size calculation, use the round up sample values.
   uint32_t req_samples = (is_max)
                             ? topo_us_to_samples(topo_ptr->cntr_frame_len.frame_len_us,
                                                  out_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate)
                             : spl_topo_get_scaled_samples(topo_ptr,
                                                           topo_ptr->cntr_frame_len.frame_len_samples,
                                                           topo_ptr->cntr_frame_len.sample_rate,
                                                           out_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_HIGH_PRIO,
            "Updating start samples on output port miid 0x%lx index %lu, "
            "expected_samples_out %lu, is_max %d",
            out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            out_port_ptr->t_base.gu.cmn.index,
            req_samples,
            is_max);
#endif

   samples_info_ptr->expected_samples_out = req_samples;
   samples_info_ptr->is_updated           = TRUE;
}

static void spl_topo_init_start_samples_on_us_ports(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_max)
{
   gu_input_port_list_t *in_list_ptr = module_ptr->t_base.gu.input_port_list_ptr;
   for (; in_list_ptr; LIST_ADVANCE(in_list_ptr))
   {
      spl_topo_input_port_t * in_port_ptr       = (spl_topo_input_port_t *)in_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *conn_out_port_ptr = NULL;
      spl_topo_input_port_t * conn_in_port_ptr  = NULL;

      spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &conn_out_port_ptr, &conn_in_port_ptr);

      if (conn_out_port_ptr)
      {
         spl_topo_set_start_samples_on_output_port(topo_ptr, conn_out_port_ptr, is_max);
      }
      else
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Error updating upstream start samples for Module 0x%lX, input port index %lu has no "
                  "connected port.",
                  in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  in_port_ptr->t_base.gu.cmn.index);
      }
   }
}

static void spl_topo_init_start_samples_on_ds_ports(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_max)
{
   gu_output_port_list_t *out_list_ptr = module_ptr->t_base.gu.output_port_list_ptr;
   for (; out_list_ptr; LIST_ADVANCE(out_list_ptr))
   {
      spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_list_ptr->op_port_ptr;
      spl_topo_set_start_samples_on_output_port(topo_ptr, out_port_ptr, is_max);
   }
}

/**
 * Called on the first module in the search to initialize number of samples. Other number of samples values
 * propagate backwards based on this first value.
 */
static void spl_topo_set_start_samples(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_max)
{
   // req_samples equals the container frame length scaled by the output port media format.
   // Set it on all the output ports' expected_samples_out.
   // If the start module is also a trigger policy module, don't go through the trigger policy module, just start
   // on the input ports.
   if (gen_topo_is_module_data_trigger_policy_active(&module_ptr->t_base) ||
       spl_topo_fwk_ext_is_module_dm_fixed_in(module_ptr) || module_ptr->t_base.gu.flags.is_sink)
   {
      spl_topo_init_start_samples_on_us_ports(topo_ptr, module_ptr, is_max);
   }
   else
   {
      spl_topo_init_start_samples_on_ds_ports(topo_ptr, module_ptr, is_max);
   }
}

static void spl_topo_scale_required_input_samples(spl_topo_t *           topo_ptr,
                                                  uint32_t               source_samples,
                                                  uint32_t               source_sample_rate,
                                                  spl_topo_input_port_t *in_port_ptr,
                                                  bool_t                 is_max)
{
   spl_topo_req_samples_t *in_samples_info_ptr =
      (is_max) ? &in_port_ptr->req_samples_info_max : &in_port_ptr->req_samples_info;

   uint32_t port_max_len      = 0;
   uint32_t port_max_samples  = 0;

   // rescale for input, mark as updated
   in_samples_info_ptr->samples_in =
      spl_topo_get_scaled_samples(topo_ptr,
                                  source_samples,
                                  source_sample_rate,
                                  in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

   port_max_len     = spl_topo_get_in_port_max_data_len(topo_ptr, in_port_ptr);
   port_max_samples = topo_bytes_to_samples_per_ch(port_max_len, in_port_ptr->t_base.common.media_fmt_ptr);

   if ((in_samples_info_ptr->samples_in > port_max_samples) && (!is_max))
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Scaling samples resulted in samples %ld above maximum space (%ld samples). Flooring to "
               "maximum.",
               in_samples_info_ptr->samples_in,
               port_max_samples);
      in_samples_info_ptr->samples_in = port_max_samples;
   }

   in_samples_info_ptr->is_updated = TRUE;
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_HIGH_PRIO,
            "Scaling out samples %lu to in %lu, miid 0x%lX",
            source_samples,
            in_samples_info_ptr->samples_in,
            in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
}

ar_result_t spl_topo_get_required_input_samples(spl_topo_t *topo_ptr, bool_t is_max)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   /*
    * Iterate through sorted list backwards, looking for DM modules which are configured in fixed output mode.
    * If the output sample requirement is not updated in the port, then set it to the nominal operating frame size,
    * set param to the module, and in the event callback, update the input port with input samples needed. Start
    * another loop along the connected modules to the DM module (port by port) till you reach either another DM module
    * or an external input port.
    */
   gu_module_list_t *                  list_end_ptr       = NULL;
   fwk_extn_dm_param_id_req_samples_t *dm_req_samples_ptr = topo_ptr->fwk_extn_info.dm_info.dm_req_samples_ptr;

   // if there are no fixed output DM modules, just return
   if (NULL == topo_ptr->req_samp_query_start_list_ptr)
   {
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4) || defined(TOPO_DM_DEBUG)
      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Not doing DM check if dm is not present.");
#endif
      return AR_EOK;
   }

   DBG_VERIFY(result, topo_ptr->t_base.topo_to_cntr_vtable_ptr->clear_topo_req_samples);
   DBG_VERIFY(result, dm_req_samples_ptr);

   // max samples will get updated by the end of this call, if needed.
   if (is_max)
   {
      bool_t IS_MAX_TRUE = TRUE;
      topo_ptr->t_base.topo_to_cntr_vtable_ptr->clear_topo_req_samples(&(topo_ptr->t_base), IS_MAX_TRUE);

      topo_ptr->during_max_traversal = TRUE;
      list_end_ptr                   = topo_ptr->t_base.gu.sorted_module_list_ptr;
   }
   else
   {
      // for data-path-query, use started_sorted_module_list
      list_end_ptr = topo_ptr->t_base.started_sorted_module_list_ptr;
   }

   //find the last module in sorted module list where sample-query should start from.
   for (gu_module_list_t *module_list_ptr = topo_ptr->req_samp_query_start_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;
      spl_topo_set_start_samples(topo_ptr, module_ptr, is_max);

      // If this module is after the previous module in the sorted list then start query from this module.
      gu_module_list_t *tmp_list_end_ptr = NULL;
      if (AR_EOK == spf_list_find_list_node((spf_list_node_t *)list_end_ptr,
                                            (void *)module_ptr,
                                            (spf_list_node_t **)&tmp_list_end_ptr))
      {
         list_end_ptr = tmp_list_end_ptr;
      }
   }

#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4) || defined(TOPO_DM_DEBUG)
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "required sample query start module is miid 0x%lx.",
               list_end_ptr->module_ptr->module_instance_id);
#endif

   dm_req_samples_ptr->is_input = FALSE;
   /*
    * Start the iteration from the module preceding the dm_start module, which is either the sync module,
    * or the earliest threshold module in the topology. This assumes that the graph shape is setup in a way
    * that if sync is present, all external input ports go to it. If a threshold module is present, expectation
    * is that all prior modules in sorted list are sending data to it, so the sample requirement computed based
    * on threshold module is applicable.
    */
   for (; list_end_ptr; LIST_RETREAT(list_end_ptr))
   {

      uint32_t               req_index                = 0;
      spl_topo_module_t *    module_ptr               = (spl_topo_module_t *)list_end_ptr->module_ptr;
      gu_output_port_list_t *outport_list_ptr         = module_ptr->t_base.gu.output_port_list_ptr;
      uint32_t               output_samples_to_scale  = 0;
      uint32_t               output_scale_sample_rate = 0;
      bool_t                 out_samples_valid        = FALSE;
      dm_req_samples_ptr->num_ports                   = 0;

      for (; outport_list_ptr; LIST_ADVANCE(outport_list_ptr))
      {
         spl_topo_output_port_t *outport_ptr = (spl_topo_output_port_t *)outport_list_ptr->op_port_ptr;

         DBG_VERIFY(result, outport_ptr);
         /*
          * Do not process port if
          * 1. Port doesn't exist
          * 2. Port isn't running for non-is_max case
          * 3. Port does not have valid media format
          */
         if (((TOPO_PORT_STATE_STARTED != outport_ptr->t_base.common.state) && (!is_max)) ||
             (!outport_ptr->t_base.common.flags.is_mf_valid))
         {
            continue;
         }
         uint32_t num_samples = 0;
         if (is_max)
         {
            if (!outport_ptr->req_samples_info_max.is_updated)
            {
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4) || defined(TOPO_DM_DEBUG)
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Output port dm info is NOT updated for miid 0x%lX, skipping",
                        module_ptr->t_base.gu.module_instance_id);
#endif
               continue;
            }

            num_samples                                  = outport_ptr->req_samples_info_max.expected_samples_out;
            outport_ptr->req_samples_info_max.is_updated = FALSE;
         }
         else
         {
            if (!outport_ptr->req_samples_info.is_updated)
            {
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4) || defined(TOPO_DM_DEBUG)
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Output port dm info is NOT updated for miid 0x%lX, skipping",
                        module_ptr->t_base.gu.module_instance_id);
#endif
               continue;
            }

            num_samples = outport_ptr->req_samples_info.expected_samples_out;

            uint32_t current_samples = 0;
            // Subtract any existing samples that may be present in the buffer already
            if (outport_ptr->t_base.common.flags.media_fmt_event)
            {
              gen_topo_common_port_t *conn_port_ptr =
                 (outport_ptr->t_base.gu.conn_in_port_ptr)
                    ? &((gen_topo_input_port_t *)outport_ptr->t_base.gu.conn_in_port_ptr)->common
                    : &outport_ptr->t_base.common;

               /* scaling based on the connected input port pointer
                * if media format is pending on this output port then any data in buffer
                * is for the connected port's media format.
                */

               current_samples =
                  topo_bytes_to_samples_per_ch(spl_topo_get_out_port_actual_data_len(topo_ptr, outport_ptr),
                                               conn_port_ptr->media_fmt_ptr);

               current_samples = spl_topo_get_scaled_samples(topo_ptr,
                                                             current_samples,
                                                             conn_port_ptr->media_fmt_ptr->pcm.sample_rate,
                                                             outport_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);
            }
            else
            {
               current_samples =
                  topo_bytes_to_samples_per_ch(spl_topo_get_out_port_actual_data_len(topo_ptr, outport_ptr),
                                               outport_ptr->t_base.common.media_fmt_ptr);
            }

#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
            if (current_samples)
            {
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX had left over samples %lu on output port index %lu",
                        module_ptr->t_base.gu.module_instance_id,
                        current_samples,
                        outport_ptr->t_base.gu.cmn.index);
            }
#endif
            // Subtract out any stale data in the output buffer.
            if (num_samples > current_samples)
            {
               num_samples -= current_samples;
            }
            else
            {
               num_samples = 0;
            }

            outport_ptr->req_samples_info.expected_samples_out = num_samples;
            outport_ptr->req_samples_info.is_updated           = FALSE;
         }

#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Setting %lu samples on port index %lu of miid 0x%lX",
                  num_samples,
                  outport_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id);
#endif
         // For DM modules, do set param query. For rest of modules, pass through duration (scaling by mf).
         // For bypassed DM modules, pass through duration, dm_req_samples_ptr should also be non NULL in this case
         if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (GEN_TOPO_DM_FIXED_OUTPUT_MODE == module_ptr->t_base.flags.dm_mode) &&
             (!spl_topo_is_module_disabled(module_ptr)))
         {
            // set param to be done only for dm modules, for all other modules, just re-scale input from output
            dm_req_samples_ptr->req_samples[req_index].port_index          = outport_ptr->t_base.gu.cmn.index;
            dm_req_samples_ptr->req_samples[req_index].samples_per_channel = num_samples;
            req_index++;
            dm_req_samples_ptr->num_ports++;
         }
         else
         {
            /*  if it's not a dm module, we can't set param to the module, so we have to rescale output sample
             *  requirement and set it to input port. Generally multiport modules that are running in fixed output
             *  cases will have to be DM modules. If they aren't, we don't know the port mapping, so we can only
             *  set value based on some output port, with this logic, it'll be the last one
             */
            output_samples_to_scale  = num_samples;
            output_scale_sample_rate = outport_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate;
            out_samples_valid        = TRUE;
         }
      } // loop over output ports

      // Sanity check
      if (req_index > topo_ptr->fwk_extn_info.dm_info.num_ports_dm)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_FATAL_PRIO,
                  "Wrote (%lu) beyond size (%lu) of req_samples",
                  req_index,
                  topo_ptr->fwk_extn_info.dm_info.num_ports_dm);
         THROW(result, AR_EFAILED);
      }

      // Do a set param for all the DM modules
      result = spl_topo_fwk_ext_set_dm_samples_per_module(topo_ptr, module_ptr, dm_req_samples_ptr, is_max);
      VERIFY(result, (AR_EOK == result));

      // check for whether event was raised and what values were set
      // setting samples is not necessary, event could've been raised because module needs different
      // number of samples
      gu_input_port_list_t *inport_list_ptr = module_ptr->t_base.gu.input_port_list_ptr;
      for (; inport_list_ptr; LIST_ADVANCE(inport_list_ptr))
      {
         spl_topo_input_port_t * in_port_ptr = (spl_topo_input_port_t *)inport_list_ptr->ip_port_ptr;
         spl_topo_req_samples_t *in_samples_info_ptr =
            (is_max) ? &in_port_ptr->req_samples_info_max : &in_port_ptr->req_samples_info;

         if (out_samples_valid)
         {
            spl_topo_scale_required_input_samples(topo_ptr,
                                                  output_samples_to_scale,
                                                  output_scale_sample_rate,
                                                  in_port_ptr,
                                                  is_max);
         }

         if (!in_samples_info_ptr->is_updated)
         {
            continue;
         }
         else
         {
            // required samples info will be same in NBLC chain, skip the intermediate ports and update the nblc start
            // this can be done only if there is no pending data on this input port.
            if ((!is_max) && (in_port_ptr->t_base.nblc_start_ptr) &&
                (in_port_ptr->t_base.nblc_start_ptr != (gen_topo_input_port_t *)in_port_ptr) &&
                (0 == spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr)))
            {
               spl_topo_input_port_t *nblc_start_ptr = (spl_topo_input_port_t *)in_port_ptr->t_base.nblc_start_ptr;

               // scale samples fron input port to its nblc start input prt.
               spl_topo_scale_required_input_samples(topo_ptr,
                                                     in_samples_info_ptr->samples_in,
                                                     in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate,
                                                     nblc_start_ptr,
                                                     is_max);

               in_samples_info_ptr->is_updated = FALSE;

               continue;
            }

            spl_topo_output_port_t *conn_out_ptr = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;
            spl_topo_req_samples_t *out_samples_info_ptr;

            // If this port is connected to an external input port, we need to update/report accordingly.
            if (in_port_ptr->t_base.gu.ext_in_port_ptr)
            {
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Updating external input port sample requirement %lu, miid 0x%lX, is_max %lu",
                        in_samples_info_ptr->samples_in,
                        in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        is_max);
#endif
               if (is_max)
               {
                  result =
                     topo_ptr->t_base.topo_to_cntr_vtable_ptr
                        ->update_input_port_max_samples((gen_topo_t *)topo_ptr, (gen_topo_input_port_t *)in_port_ptr);
               }
               else
               {
                  // nothing to do, flag is already set and value is updated
               }
               // move on to next port
               continue;
            }
            else if (conn_out_ptr)
            {
               // reset updated flag
               in_samples_info_ptr->is_updated = FALSE;

               // set on connected output port.
               out_samples_info_ptr = (is_max) ? &conn_out_ptr->req_samples_info_max : &conn_out_ptr->req_samples_info;

               if (conn_out_ptr->t_base.common.flags.media_fmt_event)
               {
                  // If media format is pending then input port and connected output port can have different media
                  // format.
                  out_samples_info_ptr->expected_samples_out =
                     spl_topo_get_scaled_samples(topo_ptr,
                                                 in_samples_info_ptr->samples_in,
                                                 in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate,
                                                 conn_out_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);
               }
               else
               {
                  out_samples_info_ptr->expected_samples_out = in_samples_info_ptr->samples_in;
               }

               out_samples_info_ptr->is_updated = TRUE;
#if (SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3) || defined(TOPO_DM_DEBUG)
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Expected out samples %lu, in_port_mid 0x%lX, out_port_mid 0x%lX",
                        out_samples_info_ptr->expected_samples_out,
                        in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        conn_out_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
            }
         }
      } // input port list
   }    // Sorted list loop
   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   topo_ptr->during_max_traversal = FALSE;
   return result;
}

/**
 * Drops all data and metadata for all ports of the module. Returns whether there was data to drop or not.
 */
ar_result_t spl_topo_module_drop_all_data(spl_topo_t *       topo_ptr,
                                          spl_topo_module_t *module_ptr,
                                          bool_t *           data_was_dropped_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result    = AR_EOK;
   *data_was_dropped_ptr = FALSE;

   VERIFY(result, data_was_dropped_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Dropping all data and metadata for module with miid 0x%lx",
            module_ptr->t_base.gu.module_instance_id);
#endif

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      spl_topo_input_port_t * in_port_ptr            = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *connected_out_port_ptr = NULL;
      spl_topo_input_port_t * ext_in_port_ptr        = NULL;

      if (in_port_ptr->t_base.common.sdata.metadata_list_ptr)
      {
         bool_t IS_DROPPED_TRUE = TRUE;
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Destroying metadata for input port idx %ld miid 0x%lx",
                  in_port_ptr->t_base.gu.cmn.index,
                  in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

         TRY(result,
             gen_topo_destroy_all_metadata(topo_ptr->t_base.gu.log_id,
                                           (void *)module_ptr,
                                           &(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                           IS_DROPPED_TRUE));
      }

      if(in_port_ptr->t_base.common.bufs_ptr)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Warning: input port idx %ld miid 0x%lx had non-NULL bufs_ptr during drop_all_data, "
                  "setting to NULL.",
                  in_port_ptr->t_base.gu.cmn.index,
                  in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
         in_port_ptr->t_base.common.bufs_ptr = NULL;
      }

      // If upstream is internal, return upstream output buffer.
      spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &connected_out_port_ptr, &ext_in_port_ptr);
      if (connected_out_port_ptr)
      {
         if (spl_topo_int_out_port_has_data(topo_ptr, connected_out_port_ptr))
         {
            TRY(result, spl_topo_flush_return_output_buf(topo_ptr, connected_out_port_ptr));
            *data_was_dropped_ptr = TRUE;
         }
      }
      // If upstream is external, all we need to do is mark external input as completely consumed and the fwk layer
      // will take care of it, including destroying metadata.
      else if (ext_in_port_ptr)
      {
         spl_topo_ext_buf_t *ext_buf_ptr = ext_in_port_ptr->ext_in_buf_ptr;

         if ((ext_buf_ptr) && (ext_buf_ptr->buf_ptr) && (ext_buf_ptr->buf_ptr[0].actual_data_len))
         {
             TOPO_MSG(topo_ptr->t_base.gu.log_id,
                          DBG_HIGH_PRIO,
                              "Warning: Dropping data on ext input port index %d, miid 0x%lx len %d",
                              ext_in_port_ptr->t_base.gu.cmn.index,
                              ext_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                              ext_buf_ptr->buf_ptr[0].actual_data_len);
            // Drop data by letting input consumed = prev_actual_data_len. Increment bytes_consumed_per_ch by this
            // amount.
            ext_buf_ptr->bytes_consumed_per_ch = ext_buf_ptr->buf_ptr[0].actual_data_len;
            *data_was_dropped_ptr              = TRUE;
         }
      }
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (spl_topo_int_out_port_has_data(topo_ptr, out_port_ptr))
      {
         TRY(result, spl_topo_flush_return_output_buf(topo_ptr, out_port_ptr));
         *data_was_dropped_ptr = TRUE;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}
