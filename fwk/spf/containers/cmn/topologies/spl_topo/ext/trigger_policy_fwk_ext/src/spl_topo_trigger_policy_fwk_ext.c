/**
 * \file spl_topo_trigger_policy.c
 *
 * \brief
 *
 *     Topo 2 functions for managing trigger policy/processing decision.
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spl_topo_trigger_policy_fwk_ext.h"
#include "spl_topo_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */
static inline bool_t spl_topo_length_meets_threshold(uint32_t length_bytes, uint32_t threshold_bytes);
static ar_result_t spl_topo_int_in_port_has_enough_data(spl_topo_t *           topo_ptr,
                                                        spl_topo_input_port_t *in_port_ptr,
                                                        bool_t *               has_data_ptr);
/* =======================================================================
Function Definitions
========================================================================== */

// Checks if the length passed in is less than the threshold.
// If there is no threshold (pass in zero for threshold), it's checks if the buffer is not empty.
static inline bool_t spl_topo_length_meets_threshold(uint32_t length_bytes, uint32_t threshold_bytes)
{
   if (0 == threshold_bytes)
   {
      return 0 != length_bytes;
   }

   return length_bytes >= threshold_bytes;
}

/**
 * Called for both internal and external input ports, this is the internal (topo-layer, NOT framework layer) check if
 * the input port has enough data to process. For non-threshold modules this is true if any data exists, for threshold
 * modules this is true if the data meets the threshold. If the input has EOF, this is true regardless of the amount of
 * data.
 */
static ar_result_t spl_topo_int_in_port_has_enough_data(spl_topo_t *           topo_ptr,
                                                        spl_topo_input_port_t *in_port_ptr,
                                                        bool_t *               has_data_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result           = AR_EOK;
   spl_topo_module_t *module_ptr       = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   bool_t             port_has_data    = FALSE;
   bool_t             cur_ip_eof       = FALSE;
   uint32_t           in_port_data_len = 0;
   VERIFY(result, has_data_ptr);

   cur_ip_eof       = spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr);
   in_port_data_len = spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr);

   if (in_port_ptr->ext_in_buf_ptr && !in_port_ptr->ext_in_buf_ptr->send_to_topo)
   {
      // if external port is disabled (send_to_topo false) by container then consider there is no trigger.
      port_has_data = FALSE;
   }
   else if (cur_ip_eof)
   {
      port_has_data = TRUE;
   }
   else if (in_port_ptr->t_base.flags.is_threshold_disabled_prop)
   {
      port_has_data = ((0 != in_port_data_len) || spl_topo_ip_contains_metadata(topo_ptr, in_port_ptr));
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!port_has_data)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx input port idx = %ld required at least one sample, "
                  "input length %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  in_port_ptr->t_base.gu.cmn.index,
                  in_port_data_len);
      }
#endif
   }
   // If there is less data at module input than dm req samples in fixed out mode, wait to
   // accumulate enough and then call module process
   else if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (GEN_TOPO_DM_FIXED_OUTPUT_MODE == module_ptr->t_base.flags.dm_mode))
   {
      uint32_t required_bytes =
         topo_samples_to_bytes(in_port_ptr->req_samples_info.samples_in, in_port_ptr->t_base.common.media_fmt_ptr);
      port_has_data = (in_port_data_len >= required_bytes);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!port_has_data)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx input port idx = %ld required dm bytes %ld, "
                  "input length %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  in_port_ptr->t_base.gu.cmn.index,
                  required_bytes,
                  in_port_data_len);
      }
#endif
   }
   else if (module_ptr->threshold_data.is_threshold_module)
   {
      uint32_t threshold_bytes_per_channel =
         spl_topo_get_module_threshold_bytes_per_channel(&in_port_ptr->t_base.common, module_ptr);
      uint32_t required_bytes =
         topo_bytes_per_ch_to_bytes(threshold_bytes_per_channel, in_port_ptr->t_base.common.media_fmt_ptr);
      port_has_data = (in_port_data_len >= required_bytes);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!port_has_data)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx input port idx = %ld required threshold bytes %ld, "
                  "input length %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  in_port_ptr->t_base.gu.cmn.index,
                  required_bytes,
                  in_port_data_len);
      }
#endif
   }
   else if (module_ptr->t_base.flags.need_sync_extn)
   {
      uint32_t req_samples = spl_topo_get_scaled_samples(topo_ptr,
                                                         topo_ptr->cntr_frame_len.frame_len_samples,
                                                         topo_ptr->cntr_frame_len.sample_rate,
                                                         in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

      uint32_t required_bytes = topo_samples_to_bytes(req_samples, in_port_ptr->t_base.common.media_fmt_ptr);

      port_has_data = (in_port_data_len >= required_bytes);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!port_has_data)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx input port idx = %ld required bytes for sync module %ld, "
                  "input length %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  in_port_ptr->t_base.gu.cmn.index,
                  required_bytes,
                  in_port_data_len);
      }
#endif
   }
   else
   {
      port_has_data = ((0 != in_port_data_len) || spl_topo_ip_contains_metadata(topo_ptr, in_port_ptr));
   }

   *has_data_ptr = port_has_data;

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Topo layer check that the trigger policy is satisfied for this port.
 */
bool_t spl_topo_int_in_port_is_trigger_present(void *  ctx_topo_ptr,
                                                    void *  ctx_in_port_ptr,
                                                    bool_t *is_ext_trigger_not_satisfied_ptr)
{
   bool_t IS_INTERNAL_TRUE = TRUE;
   return spl_topo_in_port_is_trigger_present(ctx_topo_ptr,
                                              ctx_in_port_ptr,
                                              is_ext_trigger_not_satisfied_ptr,
                                              IS_INTERNAL_TRUE);
}

/**
 * Function returns TRUE if the trigger policy is satisfied for this input port. Different buffer fullness
 * checks are required for framework/topo level checks. For example, consider a configuration where the
 * nominal frame size is 10ms but the external input port's module has a threshold of 1ms. We need the
 * framework check to make sure the buffer is filled to 10ms, but the topo check needs to make sure the
 * buffer is filled to 1ms.
 */
bool_t spl_topo_in_port_is_trigger_present(void   *ctx_topo_ptr,
                                           void   *ctx_in_port_ptr,
                                           bool_t *is_ext_trigger_not_satisfied_ptr,
                                           bool_t  is_internal_check)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result          = AR_EOK;
   spl_topo_t             *topo_ptr        = (spl_topo_t *)ctx_topo_ptr;
   spl_topo_input_port_t  *in_port_ptr     = (spl_topo_input_port_t *)ctx_in_port_ptr;
   bool_t                  has_trigger     = TRUE;
   bool_t                  port_has_data   = FALSE;
   spl_topo_output_port_t *out_port_ptr    = NULL;
   spl_topo_input_port_t  *ext_in_port_ptr = NULL;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   spl_topo_module_t *module_ptr = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
#endif

   if (spl_topo_media_format_not_received_on_port(topo_ptr, &(in_port_ptr->t_base.common)))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo miid 0x%lx, input port idx = %ld doesn't satisfy trigger policy: mf not received.",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.gu.cmn.index);
#endif
      has_trigger      = FALSE;
      return has_trigger;
   }

   if (is_internal_check)
   {
      // Internal check if the module has enough data.
      TRY(result, spl_topo_int_in_port_has_enough_data(topo_ptr, in_port_ptr, &port_has_data));
   }
   else
   {
      spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &out_port_ptr, &ext_in_port_ptr);
      if (ext_in_port_ptr)
      {
         // External port.
         port_has_data =
            topo_ptr->t_base.topo_to_cntr_vtable_ptr
               ->ext_in_port_has_enough_data(&(topo_ptr->t_base), ext_in_port_ptr->t_base.gu.ext_in_port_ptr);
      }
   }

   if (/*!is_stopped &&*/ (!port_has_data))
   {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo miid 0x%lx, input port idx = %ld doesn't satisfy trigger policy: not enough input data.",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.gu.cmn.index);
#endif
      has_trigger = FALSE;
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   if (has_trigger)
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo miid 0x%lx, input port idx = %ld trigger policy satisfied.",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.gu.cmn.index);
   }
#endif

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return has_trigger;
}

/**
 * This is used to determine if a port's trigger policy is satisfied on the output side when the policy is ABSENT.
 * It's just the inverse of is_trigger_present.
 */
bool_t spl_topo_in_port_is_trigger_absent(void *ctx_topo_ptr, void *ctx_in_port_ptr)
{
   bool_t *IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR = NULL;

   return spl_topo_int_in_port_is_trigger_present(ctx_topo_ptr,
                                                  ctx_in_port_ptr,
                                                  IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR);
}

/**
 * During the spl_topo_process loop, this function is used to check if setup/adjust is necessary for this input port.
 * This also returns the connected output port/connected external input port.
 * Blocks input if:
 * 1. Media format isn't valid.
 * 2. There's no proper upstream connection.
 * 3. The input port is not started.
 * 4. Nontrigger policy is blocked.
 * 5. Trigger is not present.
 */
bool_t spl_topo_in_port_trigger_blocked(spl_topo_t *             topo_ptr,
                                        spl_topo_input_port_t *  in_port_ptr)
{
   spl_topo_output_port_t *connected_out_port_ptr = NULL;
   spl_topo_input_port_t * ext_in_port_ptr        = NULL;
   spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &connected_out_port_ptr, &ext_in_port_ptr);

   // 1. Media format isn't valid.
   if (spl_topo_media_format_not_received_on_port(topo_ptr, &(in_port_ptr->t_base.common)))
   {
      return TRUE;
   }

   // 2. There's no proper upstream connection.
   if ((!(connected_out_port_ptr)) && (!(ext_in_port_ptr)))
   {
      return TRUE;
   }

   // 3. The input port is not started.
   if (TOPO_PORT_STATE_STARTED != in_port_ptr->t_base.common.state)
   {
      return TRUE;
   }

   // 4. Nontrigger policy is blocked. For input side, we don't need to check upstream nontrigger policy - presence
   // of data implies upstream needed to (did) process.
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
       gen_topo_get_nontrigger_policy_for_input(&in_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER))
   {
      return TRUE;
   }

   // 5. Trigger is not present (internal check - For example external input module has 1ms threshold, but container
   //    frame length is 5ms. All 5 iterations through the module should not be blocked due to < 5ms of data at the
   //    input).
   //
   // If the trigger is marked present in the scractch ptr it means trigger is present,
   // if not marked it doesnt necessarily
   uint32_t                    port_index       = in_port_ptr->t_base.gu.cmn.index;
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;
   bool_t                      trigger_present  = FALSE;
   if (TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT ==
       proc_context_ptr->in_port_scratch_ptr[port_index].flags.is_trigger_present)
   {
      trigger_present = TRUE;
   }
   else if (TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT ==
            proc_context_ptr->in_port_scratch_ptr[port_index].flags.is_trigger_present)
   {
      trigger_present = FALSE;
   }
   else // TOPO_PROC_CONTEXT_TRIGGER_NOT_EVALUATED
   {
      bool_t *IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR = NULL;
      bool_t  IS_INTERNAL_CHECK_TRUE                 = TRUE;
      trigger_present = spl_topo_in_port_is_trigger_present(topo_ptr,
                                          in_port_ptr,
                                          IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR,
                                          IS_INTERNAL_CHECK_TRUE);
   }

   if (!trigger_present)
   {
      return TRUE;
   }

   return FALSE;
}

// This is used to determine if a port's trigger policy is satisfied on the output side.
bool_t spl_topo_out_port_is_trigger_present(void   *ctx_topo_ptr,
                                            void   *ctx_out_port_ptr,
                                            bool_t *is_ext_trigger_not_satisfied_ptr)

{
   spl_topo_t             *topo_ptr             = (spl_topo_t *)ctx_topo_ptr;
   spl_topo_output_port_t *out_port_ptr         = (spl_topo_output_port_t *)ctx_out_port_ptr;
   spl_topo_module_t      *module_ptr           = (spl_topo_module_t *)out_port_ptr->t_base.gu.cmn.module_ptr;
   bool_t                  has_enough_space     = FALSE;
   uint32_t                out_port_empty_space = 0;
   bool_t                  has_trigger          = FALSE;

   // 1. (int/ext) Needs a valid media format.
   if (spl_topo_media_format_not_received_on_port(topo_ptr, &(out_port_ptr->t_base.common)))
   {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo miid 0x%lx: Output trigger not satisifed no valid media format, output port idx = %ld",
               module_ptr->t_base.gu.module_instance_id,
               out_port_ptr->t_base.gu.cmn.index);
#endif

      return FALSE;
   }

   // 2. (only ext). If there's no held output buffer, we need the trigger. (int we may have to query get out buf)
   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      if (!(topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_has_buffer(
             out_port_ptr->t_base.gu.ext_out_port_ptr)))
      {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo miid 0x%lx: Output trigger not satisifed no held external output buffer, output port idx = %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index);
#endif

         return FALSE;
      }
   }
   else
   {
      // 3. (only int) If there's a pending output media format, we can't write new data to the buffer or else data
      // in the buffer will be split between media formats.
      if (spl_topo_op_port_has_pending_media_fmt(topo_ptr, out_port_ptr))
      {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo miid 0x%lx: Output trigger not satisfied pending output media format, output port idx = %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index);
#endif

         return FALSE;
      }
   }

   // 4. (int/ext) Pending output EOF needs to be drained before new data can be pushed to the buffer.
   if (spl_topo_out_port_has_pending_eof(topo_ptr, out_port_ptr))
   {
      bool_t has_flushing_eos = out_port_ptr->t_base.common.sdata.flags.marker_eos;

      // If the output port also has marker_eos, ignore the eof. We're allowed to push data onto a port
      // with marker_eos - the eos will become nonflushing at that time.
      if (out_port_ptr->t_base.gu.conn_in_port_ptr)
      {
         spl_topo_input_port_t *conn_in_port_ptr = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;
         has_flushing_eos |= conn_in_port_ptr->t_base.common.sdata.flags.marker_eos;
      }

      if (!has_flushing_eos)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo miid 0x%lx: Output trigger not satisifed pending eof, output port idx = %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index);
#endif

         return FALSE;
      }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      else
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo miid 0x%lx: Output trigger not satisifed pending eof, output port idx = %ld - eof and "
                  "marker_eos both present, trigger condition still pending",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index);
      }
#endif
   }

   /*
    * if there is no any trigger policy then don't need to check on output's nblc end.
    * this is because topo process is triggered only when external trigger meets.
    *
    * But if there is any trigger policy then topo-process can be triggered any time an external port meets trigger.
    * In this case we have to check the nblc-end to avoid buffering data within nblc.
    */
   if (topo_ptr->t_base.num_data_tpm)
   {
      // check if nblc end has buffer (in case of external output) or needs data (in case of internal input)
      if (out_port_ptr->t_base.nblc_end_ptr &&
          (out_port_ptr->t_base.nblc_end_ptr != (gen_topo_output_port_t *)out_port_ptr) &&
          out_port_ptr->t_base.nblc_end_ptr->gu.ext_out_port_ptr)
      {
         bool_t has_trigger_on_external_port = spl_topo_out_port_is_trigger_present((void *)topo_ptr,
                                              (void *)out_port_ptr->t_base.nblc_end_ptr,
                                              is_ext_trigger_not_satisfied_ptr);

         if (!has_trigger_on_external_port)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "spl_topo mpd miid 0x%lx output port idx = %ld,"
                     " trigger not present on nblc end external output port.",
                     module_ptr->t_base.gu.module_instance_id,
                     out_port_ptr->t_base.gu.cmn.index);
#endif
            return FALSE;
         }
      }
      else if (out_port_ptr->t_base.gu.conn_in_port_ptr)
      {
         gen_topo_data_need_t rc =
            spl_topo_in_port_needs_data(topo_ptr, (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr);
         if ((GEN_TOPO_DATA_BLOCKED | GEN_TOPO_DATA_NOT_NEEDED) & rc)
         {
            // there can't be any data in the nblc chain so move directly to the nblc-end port.
            spl_topo_input_port_t *nblc_end_in_port_ptr =
               (spl_topo_input_port_t *)((gen_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr)
                  ->nblc_end_ptr;
            spl_topo_output_port_t *nblc_end_out_port_ptr =
               (nblc_end_in_port_ptr) ? (spl_topo_output_port_t *)nblc_end_in_port_ptr->t_base.gu.conn_out_port_ptr
                                      : NULL;

            if ((nblc_end_in_port_ptr) &&        // nblc-end is an internal port
                !(GEN_TOPO_DATA_BLOCKED & rc) && // port returned data not-blocked
                ((gen_topo_module_t *)nblc_end_in_port_ptr->t_base.gu.cmn.module_ptr)
                   ->flags.requires_data_buf && // nblc-end module is requires data buffering (sample slip/MFC)
                spl_topo_get_out_port_empty_space(topo_ptr,
                                                  nblc_end_out_port_ptr)) // there is an empty space in buffer
            {
               // can buffer more data, this logic should be updated based on need_more flag instead, but SC dosn't
               // update need_more flag.
            }
            else if ((nblc_end_in_port_ptr) &&        // nblc-end is an internal port
                     !(GEN_TOPO_DATA_BLOCKED & rc) && // port returned data not-blocked
                     (nblc_end_in_port_ptr->t_base.common.sdata.flags.marker_eos ||
                      nblc_end_out_port_ptr->t_base.common.sdata.flags.marker_eos) && // flushing eos set
                     spl_topo_get_out_port_empty_space(topo_ptr,
                                                       nblc_end_out_port_ptr)) // there is an empty space in buffer
            {
               // can buffer more data if flushing eos is set. flushing eos will change to non-flusing after getting new data.
            }
            else
            {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_MED_PRIO,
                        "spl_topo mpd miid 0x%lx output port idx = %ld, "
                        "connected input does not need data",
                        module_ptr->t_base.gu.module_instance_id,
                        out_port_ptr->t_base.gu.cmn.index);
#endif
               return FALSE;
            }
         }
      }
   }

   // 5. (int/ext) For threshold modules, must have space equal to module's threshold.
   out_port_empty_space = spl_topo_get_out_port_empty_space(topo_ptr, out_port_ptr);
   if (module_ptr->threshold_data.is_threshold_module)
   {
      uint32_t threshold_bytes_per_channel =
         spl_topo_get_module_threshold_bytes_per_channel(&out_port_ptr->t_base.common, module_ptr);
      uint32_t threshold_bytes =
         topo_bytes_per_ch_to_bytes(threshold_bytes_per_channel, out_port_ptr->t_base.common.media_fmt_ptr);

      has_enough_space = spl_topo_length_meets_threshold(out_port_empty_space, threshold_bytes);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!has_enough_space)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx output port idx = %ld threshold %ld bytes, output empty space %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  threshold_bytes,
                  out_port_empty_space);
      }
#endif
    }
    else if (module_ptr->t_base.flags.need_sync_extn)
    {
        uint32_t req_samples = spl_topo_get_scaled_samples(topo_ptr, topo_ptr->cntr_frame_len.frame_len_samples,
                topo_ptr->cntr_frame_len.sample_rate, out_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

        uint32_t required_bytes = topo_samples_to_bytes(req_samples, out_port_ptr->t_base.common.media_fmt_ptr);

        has_enough_space = spl_topo_length_meets_threshold(out_port_empty_space, required_bytes);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      if (!has_enough_space)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd miid 0x%lx output port idx = %ld required %ld bytes, output empty space %ld",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  required_bytes,
                  out_port_empty_space);
      }
#endif
    }
   else if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      uint32_t out_port_delivery_size =
         spl_topo_calc_buf_size(topo_ptr, topo_ptr->cntr_frame_len, out_port_ptr->t_base.common.media_fmt_ptr, TRUE);

      uint32_t out_port_data_max_len    = spl_topo_get_max_buf_len(topo_ptr, &out_port_ptr->t_base.common);
      uint32_t out_port_data_actual_len = spl_topo_get_out_port_actual_data_len(topo_ptr, out_port_ptr);

      // if output  port has already met the delivery size requirement then don't need to buffer additional 1ms
      // allocated for DM path.
      has_enough_space =
         (out_port_delivery_size > out_port_data_actual_len && out_port_data_max_len > out_port_data_actual_len);
   }
   else
   {
      has_enough_space = (0 != out_port_empty_space);
   }

   if (!has_enough_space)
   {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo miid 0x%lx: Output trigger not satisfied not enough space, output port idx = %ld",
               module_ptr->t_base.gu.module_instance_id,
               out_port_ptr->t_base.gu.cmn.index);
#endif

      return FALSE;
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
                 DBG_MED_PRIO,
                 "spl_topo miid 0x%lx: Output trigger satisfied: output port idx = %ld",
                 module_ptr->t_base.gu.module_instance_id,
                 out_port_ptr->t_base.gu.cmn.index);
#endif

   has_trigger = TRUE;

   return has_trigger;
}

/**
 * This is used to determine if a port's trigger policy is satisfied on the output side when the policy is ABSENT.
 * It's just the inverse of is_trigger_present.
 */
bool_t spl_topo_out_port_is_trigger_absent(void *ctx_topo_ptr, void *ctx_out_port_ptr)
{
   bool_t *    IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR = NULL;
   return !spl_topo_out_port_is_trigger_present(ctx_topo_ptr,
                                                             ctx_out_port_ptr,
                                                             IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR);
}

/**
 * During the spl_topo_process loop, this function is used to check if setup/adjust is necessary for this output port.
 * Blocks output if:
 * 1. Media format isn't valid.
 * 2. For external ports, if send_to_topo is FALSE.
 * 3. The port's nontrigger policy is blocked.
 * 4. If output port trigger isn't present.
 */
bool_t spl_topo_out_port_trigger_blocked(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   spl_topo_ext_buf_t *ext_buf_ptr = NULL;

   // 1. Media format isn't valid.
   if (spl_topo_media_format_not_received_on_port(topo_ptr, &(out_port_ptr->t_base.common)))
   {
      return TRUE;
   }

   // 2. For external ports, if send_to_topo is FALSE.
   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      ext_buf_ptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
         out_port_ptr->t_base.gu.ext_out_port_ptr);
      // send_to_topo == FALSE means the fwk did not send down this external port. So set up a dummy in this case.
      if (!ext_buf_ptr->send_to_topo)
      {
         return TRUE;
      }
   }

   // 3. The port's nontrigger policy is blocked.
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == gen_topo_get_nontrigger_policy_for_output(&out_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER))
   {
      return TRUE;
   }

   // 4. Trigger is not present.
   uint32_t                    port_index       = out_port_ptr->t_base.gu.cmn.index;
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;
   bool_t  trigger_present                      = FALSE;
   if (TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT ==
       proc_context_ptr->out_port_scratch_ptr[port_index].flags.is_trigger_present)
   {
      trigger_present = TRUE;
   }
   else if (TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT ==
            proc_context_ptr->out_port_scratch_ptr[port_index].flags.is_trigger_present)
   {
      trigger_present = FALSE;
   }
   else // TOPO_PROC_CONTEXT_TRIGGER_NOT_EVALUATED
   {
      bool_t *IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR = NULL;
      trigger_present = spl_topo_out_port_is_trigger_present(topo_ptr,
                                           out_port_ptr,
                                           IS_EXT_TRIGGGER_NOT_SATISFIED_NULL_PTR);
   }

   if (!trigger_present)
   {
      return TRUE;
   }

   return FALSE;
}

/**
 * Check if input port needs more data.
 */
uint32_t spl_topo_in_port_needs_data(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   gen_topo_data_need_t rc          = GEN_TOPO_DATA_NEEDED;

   // 1. Non-started ports don't need data.
   if (TOPO_PORT_STATE_STARTED != in_port_ptr->t_base.common.state)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_in_port_needs_data: data not needed"
               " for in_port id = 0x%lx, miid = 0x%lx"
               " Port is not Started.",
               in_port_ptr->t_base.gu.cmn.id,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
      return GEN_TOPO_DATA_BLOCKED;
   }

   // 2. Ports with non_trigger_blocked trigger policy don't need data.
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
       gen_topo_get_nontrigger_policy_for_input(&in_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_in_port_needs_data: data not needed"
               " for in_port id = 0x%lx, miid = 0x%lx"
               " Port data trigger policy is blocked.",
               in_port_ptr->t_base.gu.cmn.id,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
      return GEN_TOPO_DATA_BLOCKED;
   }

   {
      bool_t is_internal_check = (in_port_ptr->t_base.gu.ext_in_port_ptr ? FALSE : TRUE);
      bool_t has_trigger = spl_topo_in_port_is_trigger_present(topo_ptr, in_port_ptr, NULL, is_internal_check);

      // 3. If there is enough buffered data to process, don't listen for more input.
      if (has_trigger)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo_in_port_needs_data: data not needed"
                  " for in_port id = 0x%lx, miid = 0x%lx"
                  " Port already has trigger.",
                  in_port_ptr->t_base.gu.cmn.id,
                  in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
         return GEN_TOPO_DATA_NOT_NEEDED;
      }
   }

   //checking on nblc_end
   if ((NULL != in_port_ptr->t_base.nblc_end_ptr) && (&(in_port_ptr->t_base) != in_port_ptr->t_base.nblc_end_ptr))
   {
      rc = spl_topo_in_port_needs_data(topo_ptr, (spl_topo_input_port_t *)in_port_ptr->t_base.nblc_end_ptr);
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_in_port_needs_data:  data_need_at_nblc_end %d (0:blocked, 2:needed, 4:optional)"
               " for in_port id = 0x%lx, miid = 0x%lx",
               rc,
               in_port_ptr->t_base.gu.cmn.id,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
   }

   bool_t is_steady_state = (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->t_base.common.data_flow_state);

   // only when data flow is happening, input is mandatory.
   if (!is_steady_state || (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL ==
                            gen_topo_get_nontrigger_policy_for_input(&in_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER)))
   {
      rc = ((GEN_TOPO_DATA_NEEDED == rc) ? GEN_TOPO_DATA_NEEDED_OPTIONALLY : rc);
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   if (GEN_TOPO_DATA_NEEDED_OPTIONALLY == rc)
   {

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_in_port_needs_data:  data need is optional"
               " for in_port id = 0x%lx, miid = 0x%lx,"
               " is_data_at_gap %d,"
               " is data non trigger policy optional %d",
               in_port_ptr->t_base.gu.cmn.id,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               !is_steady_state,
               (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL ==
                gen_topo_get_nontrigger_policy_for_input(&in_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER)));
   }
#endif
   return rc;
}

/**
 * Check if output port needs buffer to trigger. (only for external port)
 */
uint32_t spl_topo_out_port_needs_trigger(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   gen_topo_port_trigger_need_t rc = GEN_TOPO_PORT_TRIGGER_NEEDED;

   // 1. Non-started ports don't need trigger.
   if (TOPO_PORT_STATE_STARTED != out_port_ptr->t_base.common.state)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_out_port_needs_trigger: trigger not needed"
               " for out_port id = 0x%lx, miid = 0x%lx"
               " Port is not Started.",
               out_port_ptr->t_base.gu.cmn.id,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   fwk_extn_port_nontrigger_policy_t ntp =
      gen_topo_get_nontrigger_policy_for_output(&out_port_ptr->t_base, GEN_TOPO_DATA_TRIGGER);

   // 2. Ports with non_trigger_blocked trigger policy don't need trigger.
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == ntp)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_out_port_needs_trigger: trigger not needed"
               " for out_port id = 0x%lx, miid = 0x%lx"
               " Port data trigger policy is blocked.",
               out_port_ptr->t_base.gu.cmn.id,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   if ((NULL != out_port_ptr->t_base.nblc_start_ptr) &&
       (&(out_port_ptr->t_base) != out_port_ptr->t_base.nblc_start_ptr))
   {
      fwk_extn_port_nontrigger_policy_t nblc_start_ntp =
         gen_topo_get_nontrigger_policy_for_output(out_port_ptr->t_base.nblc_start_ptr, GEN_TOPO_DATA_TRIGGER);

      if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == nblc_start_ntp)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo_out_port_needs_trigger: trigger not needed"
                  " for out_port id = 0x%lx, miid = 0x%lx"
                  " Port data trigger policy is blocked at nblc start.",
                  out_port_ptr->t_base.gu.cmn.id,
                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
         return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
      }

      if (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == nblc_start_ntp)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo_out_port_needs_trigger:  trigger need is optional"
                  " for out_port id = 0x%lx, miid = 0x%lx,"
                  " is data non trigger policy optional %d at nblc start",
                  out_port_ptr->t_base.gu.cmn.id,
                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                  (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == nblc_start_ntp));
#endif
         return GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY;
      }
   }

   rc = (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp) ? GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY
                                                    : GEN_TOPO_PORT_TRIGGER_NEEDED;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   if (GEN_TOPO_DATA_NEEDED_OPTIONALLY == rc)
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo_out_port_needs_trigger:  trigger need is optional"
               " for out_port id = 0x%lx, miid = 0x%lx,"
               " is data non trigger policy optional %d",
               out_port_ptr->t_base.gu.cmn.id,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp));
   }
#endif
   return rc;
}

bool_t spl_topo_is_ipc_module_trigger_satisfied(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   bool_t ext_trigger_not_satisfied = FALSE;

   // Check IPC TX modules trigger
   if ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.num_ipc_output_ports))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;

      gen_topo_output_port_t *ipc_out_port_ptr =
         (gen_topo_output_port_t *)module_ptr->gu.ipc_output_port_list_ptr->op_port_ptr;

      if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state) ||
          (TOPO_PORT_STATE_STARTED != ipc_out_port_ptr->common.state))
      {
         return FALSE;
      }

      bool_t inp_trigger_present =
         spl_topo_in_port_is_trigger_present(topo_ptr, in_port_ptr, NULL, TRUE /* internal port*/);

      fwk_extn_ipc_port_trigger_t ipc_out_trigger =
         gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                   module_ptr,
                                                   FALSE,
                                                   ipc_out_port_ptr->gu.cmn.id,
                                                   &ipc_out_port_ptr->common);

      bool_t is_trigger_satisfied = (inp_trigger_present && (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED == ipc_out_trigger));

      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "IPC TX Module 0x%lX: inp_trigger_present: %lu ipc_out_trigger: %lu is_module_trigger_satisfied %lu",
               module_ptr->gu.module_instance_id,
               inp_trigger_present,
               ipc_out_trigger,
               is_trigger_satisfied);

      return is_trigger_satisfied;
   }
   // Check IPC RX modules trigger
   else if ((1 == module_ptr->gu.num_ipc_input_ports) && (1 == module_ptr->gu.num_output_ports))
   {
      gen_topo_input_port_t *ipc_in_port_ptr =
         (gen_topo_input_port_t *)module_ptr->gu.ipc_input_port_list_ptr->ip_port_ptr;

      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;

      if ((TOPO_PORT_STATE_STARTED != ipc_in_port_ptr->common.state) ||
          (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state))
      {
         return FALSE;
      }

      fwk_extn_ipc_port_trigger_t ipc_in_trigger = gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                                                             module_ptr,
                                                                                             TRUE,
                                                                                             ipc_in_port_ptr->gu.cmn.id,
                                                                                             &ipc_in_port_ptr->common);

      bool_t out_trigger_present =
         spl_topo_out_port_is_trigger_present(topo_ptr, out_port_ptr, &ext_trigger_not_satisfied);

      bool_t is_trigger_satisfied =
         (out_trigger_present &&
          ((FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED | FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY) & ipc_in_trigger));

      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "IPC RX Module 0x%lX: ipc_in_trigger: %lu out_trigger_present: %lu is_module_trigger_satisfied %lu",
               module_ptr->gu.module_instance_id,
               ipc_in_trigger,
               out_trigger_present,
               is_trigger_satisfied);

      return is_trigger_satisfied;
   }
   else
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " IPC Module 0x%lX: Trigger not satisfied, Unsupported Num inputs (%lu, %lu) Num outputs (%lu, %lu ) "
               "not supported",
               module_ptr->gu.module_instance_id,
               module_ptr->gu.num_input_ports,
               module_ptr->gu.num_ipc_input_ports,
               module_ptr->gu.num_output_ports,
               module_ptr->gu.num_ipc_output_ports);
      return FALSE;
   }
   return FALSE;
}
