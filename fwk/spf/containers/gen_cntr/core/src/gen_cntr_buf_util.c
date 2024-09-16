/**
 * \file gen_cntr_buf_util.c
 * \brief
 *     This file contains utility functions for GEN_CNTR buffer handling
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "apm.h"
#include "pt_cntr.h"


/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* -----------------------------------------------------------------------
 ** Constant / Define Declarations
 ** ----------------------------------------------------------------------- */
/** 200 ms is the largest thresh allowed if LCM is too large.
 *  For AAC, 1024 samples at 8k, frame duration is 128 ms*/
#define GEN_CNTR_MAX_PORT_THRESHOLD_ALLOWED_US (200000)

typedef struct gen_cntr_threshold_prop_t
{
   uint32_t thresh_bytes;
   /* Used only if thresh_samples/thresh_us are zero(due to invalid mf).
       -> thresh_bytes and thresh_samples may not be in sync.
       -> But thresh_samples and thresh_us are always in sync.  */

   uint32_t thresh_samples;
   /* Used only if both ports have same sample rate during propagation. Else thresh_us is used.
    * This is samples per channel. */

   uint32_t sample_rate;
   /* Sampling rate used for calculating thresh_samples. */

   uint32_t thresh_us;
   /* Used only if the thresh_samples cannot be used. */

   spf_data_format_t data_format;
   /* Data format of the threshold module */

} gen_cntr_threshold_prop_t;

/**
 * threshold propagation is possible for SISO, MISO, SIMO
 * and not MIZO, ZIMO, SIZO, ZISO, ZIZO.
 *
 * when thresh propagation is not possible, module must have thresh
 */
static bool_t gen_cntr_is_thresh_propagation_possible(gen_topo_module_t *module_ptr);

static bool_t gen_cntr_is_pseudo_thresh_module(gen_topo_module_t *module_ptr);

static ar_result_t gen_cntr_propagate_threshold_forward(gen_cntr_t *               me_ptr,
                                                        gen_topo_module_t *        out_thresh_module_ptr,
                                                        bool_t *                   thresh_prop_not_complete_ptr,
                                                        gen_cntr_threshold_prop_t *lcm_threshold_ptr,
                                                        uint32_t *                 recurse_depth_ptr);

static ar_result_t gen_cntr_propagate_threshold_backwards(gen_cntr_t *               me_ptr,
                                                          gen_topo_module_t *        in_thresh_module_ptr,
                                                          bool_t *                   thresh_prop_not_complete_ptr,
                                                          gen_cntr_threshold_prop_t *lcm_threshold_ptr,
                                                          uint32_t *                 recurse_depth_ptr);

ar_result_t find_gcd(uint32_t a, uint32_t b, uint32_t *gcd_ptr);
ar_result_t find_lcm(uint32_t a, uint32_t b, uint32_t *lcm_ptr);
/*----------------------------------------------------------------------------
 * Global Data Definitions
 * -------------------------------------------------------------------------*/

uint32_t gen_cntr_get_default_port_threshold(gen_cntr_t        *me_ptr,
                                             gen_topo_module_t *module_ptr,
                                             topo_media_fmt_t  *media_fmt_ptr)
{
   uint32_t buf_size = 0;
   if (SPF_UNKNOWN_DATA_FORMAT == media_fmt_ptr->data_format)
   {
      buf_size = 0;
   }
   else if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {

      static const uint64_t num_us_per_sec     = 1000000;
      uint32_t              frame_size_samples = 0;
      uint32_t              sampling_rate      = media_fmt_ptr->pcm.sample_rate;

      if (0 == me_ptr->cu.conf_frame_len.frame_len_us)
      {
         if (0 == me_ptr->cu.conf_frame_len.frame_len_samples)
         {
            // decide frame size based on the perf mode as container frame len property is not configured.
            frame_size_samples = tu_get_unit_frame_size(sampling_rate) *
                                 TOPO_PERF_MODE_TO_FRAME_DURATION_MS(module_ptr->gu.sg_ptr->perf_mode);
         }
         else
         {
            // Container frame len property is configured for an absolute samples value as frame size
            frame_size_samples = me_ptr->cu.conf_frame_len.frame_len_samples;
         }
      }
      else
      {
         // Container frame len property is configured to determine the frame size wrt configured frame length in time.
         frame_size_samples =
            (uint32_t)(((uint64_t)sampling_rate * (uint64_t)me_ptr->cu.conf_frame_len.frame_len_us) / num_us_per_sec);
      }

      buf_size = topo_samples_to_bytes(frame_size_samples, media_fmt_ptr);
   }
   else
   {
      buf_size = 2048;
   }

   return buf_size;
}

ar_result_t gen_cntr_create_ext_out_bufs(gen_cntr_t *             me_ptr,
                                         gen_cntr_ext_out_port_t *ext_port_ptr,
                                         uint32_t                 num_out_bufs)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_port_ptr->gu.int_out_port_ptr;
   if (!gen_topo_is_ext_peer_container_connected(out_port_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " no external connections yet");
      return AR_EFAILED;
   }

   if (num_out_bufs > CU_MAX_OUT_BUF_Q_ELEMENTS)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Warning: eventhough ICB determined %lu buffers, creating only %lu",
                   num_out_bufs,
                   CU_MAX_OUT_BUF_Q_ELEMENTS);
      num_out_bufs = CU_MAX_OUT_BUF_Q_ELEMENTS;
   }

   if (ext_port_ptr->cu.buf_max_size <= 1)
   {
      return AR_EOK;
   }

   // get heap id for ext out buffer allocations.
   POSAL_HEAP_ID downgraded_heap_id =
      gu_get_downgraded_heap_id(me_ptr->topo.heap_id, ext_port_ptr->gu.downstream_handle.heap_id);

   // peak NBLC start and check if MP buffering extension is supported
   if (out_port_ptr->nblc_start_ptr)
   {
      gen_topo_module_t *nblc_start_module_ptr = (gen_topo_module_t *)out_port_ptr->nblc_start_ptr->gu.cmn.module_ptr;
      if (nblc_start_module_ptr->flags.need_mp_buf_extn)
      {
         downgraded_heap_id = ext_port_ptr->gu.downstream_handle.heap_id;
      }
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                " Creating %lu external buffers of size %lu for (MIID, port_id)=(0x%x, 0x%x) heap_id 0x%lx "
                "downgraded_heap_id 0x%lx",
                (num_out_bufs - ext_port_ptr->cu.num_buf_allocated),
                ext_port_ptr->cu.buf_max_size,
                ext_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_port_ptr->gu.int_out_port_ptr->cmn.id,
                me_ptr->topo.heap_id,
                downgraded_heap_id);

   if (gen_cntr_is_ext_out_v2(ext_port_ptr))
   {
      uint32_t size_per_data_buf = ext_port_ptr->cu.buf_max_size;

      // upper nibble of MSB is used to indicate v2 message - rest 28 bits
      // are available - token is used instead of flag since this is currently
      // only defined and applicable for GCs
      spf_msg_token_t token = {0};
      gen_cntr_set_data_msg_out_buf_token(&token);


      TRY(result,
          spf_svc_create_and_push_buffers_v2_to_buf_queue(ext_port_ptr->gu.this_handle.q_ptr,
                                                          ext_port_ptr->bufs_num,
                                                          size_per_data_buf,
                                                          num_out_bufs,
                                                          ext_port_ptr->gu.downstream_handle.spf_handle_ptr,
                                                          ext_port_ptr->gu.downstream_handle.heap_id,
                                                          &token,
                                                          &ext_port_ptr->cu.num_buf_allocated));
   }
   else
   {
      TRY(result,
          spf_svc_create_and_push_buffers_to_buf_queue(ext_port_ptr->gu.this_handle.q_ptr,
                                                       ext_port_ptr->cu.buf_max_size,
                                                       num_out_bufs,
                                                       ext_port_ptr->gu.downstream_handle.spf_handle_ptr,
                                                       downgraded_heap_id,
                                                       &ext_port_ptr->cu.num_buf_allocated));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_deinit_ext_port_queue(gen_cntr_t *me_ptr, spf_handle_t *hdl_ptr, uint32_t bit_mask)
{
   if (hdl_ptr->q_ptr)
   {
      /*Release mask only in Buffer driven mode*/
      cu_release_bit_in_bit_mask(&me_ptr->cu, bit_mask);

      // We can clear without checking input or output or control because only one bit is set in bit_mask.
      cu_clear_bits_in_x(&me_ptr->cu.all_ext_in_mask, bit_mask);
      cu_clear_bits_in_x(&me_ptr->cu.all_ext_out_mask, bit_mask);

      /*deinit the queue */
      posal_queue_deinit(hdl_ptr->q_ptr);
      hdl_ptr->q_ptr = NULL;
   }

   return AR_EOK;
}

/**
 * destroys (ext_port_ptr->cu.num_buf_allocated - num_bufs_to_keep) num of buffers,
 * where num_bufs_to_keep can be different from num_buf_allocated
 */
void gen_cntr_destroy_ext_buffers(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr, uint32_t num_bufs_to_keep)
{
   uint32_t num_bufs_to_destroy = 0;
   if (num_bufs_to_keep < ext_port_ptr->cu.num_buf_allocated)
   {
      num_bufs_to_destroy = (ext_port_ptr->cu.num_buf_allocated - num_bufs_to_keep);
   }

   // if all buffers must be destroyed then destroy the buffer which is currently held.
   // if some buffers must be kept then first preserve the held buffer.
   if (0 == num_bufs_to_keep)
   {
      gen_cntr_flush_output_data_queue(me_ptr, ext_port_ptr, FALSE); // FALSE - not client cmd.
      MFREE_NULLIFY(ext_port_ptr->bufs_ptr);
      ext_port_ptr->bufs_num = 0;
   }

   if (NULL == ext_port_ptr->gu.this_handle.q_ptr)
   {
      return;
   }

   // if we had allocated buffers, need to destroy now
   if (num_bufs_to_destroy)
   {
      spf_svc_free_buffers_in_buf_queue_nonblocking(ext_port_ptr->gu.this_handle.q_ptr,
                                                    &ext_port_ptr->cu.num_buf_allocated);
   }

   uint32_t num_bufs_destroyed = 0;
   uint32_t num_bufs_kept      = ext_port_ptr->cu.num_buf_allocated;
   if (num_bufs_to_destroy > num_bufs_kept)
   {
      num_bufs_destroyed = num_bufs_to_destroy - num_bufs_kept;
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                " Destroyed %lu external buffers. num_bufs_kept %lu, Module (0x%lX, 0x%lX)",
                num_bufs_destroyed,
                num_bufs_kept,
                ext_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_port_ptr->gu.int_out_port_ptr->cmn.id);
}

/**
 * outbuf_size should be initialized
 */
static inline ar_result_t gen_cntr_get_required_out_buf_size_and_count(gen_cntr_t *             me_ptr,
                                                                       gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                       uint32_t *               num_out_buf,
                                                                       uint32_t *               req_out_buf_size,
                                                                       uint32_t                 specified_out_buf_size,
                                                                       uint32_t                 metadata_size)
{
   /*ar_result_t result = */ cu_determine_ext_out_buffering(&me_ptr->cu, &ext_out_port_ptr->gu);

   *num_out_buf = ext_out_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_out_port_ptr->cu.icb_info.icb.num_reg_prebufs;

   // ignore the error because without ext bufs we cannot trigger topo and send media format.
   // downstream may not be able to send ICB msg without media fmt. To avoid this deadlock, create some bufs by default
   return AR_EOK;
}

/**
 * For signal triggered modules with threshold, we cannot force
 *
 * for raw compressed ports also we cannot force thresholds.
 *
 */
static bool_t gen_cntr_is_forcing_thresh_ok(gen_topo_t *            topo_ptr,
                                            gen_topo_module_t *     module_ptr,
                                            uint32_t                port_id,
                                            uint32_t                is_input,
                                            gen_topo_common_port_t *port_ptr)
{
   if (SPF_IS_PCM_DATA_FORMAT(port_ptr->media_fmt_ptr->data_format))
   {
      if (module_ptr->flags.need_stm_extn && port_ptr->flags.port_has_threshold)
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning: Module 0x%lX, port_id 0x%lx forcing threshold not permitted for signal triggered "
                      "modules "
                      "with threshold",
                      module_ptr->gu.module_instance_id,
                      port_id);
         return FALSE;
      }
      return TRUE;
   }
   else
   {
      return !port_ptr->flags.port_has_threshold;
   }
}

/**
 * Sets ONLY lcm_thresh_us and lcm_thresh_samples which is applicable for all PCM modules
 * This function is called when threshold is determined based on only raw compr module, so only thresh_bytes is set
 */
static void gen_cntr_set_failsafe_pcm_lcm_thresh(gen_topo_t *               topo_ptr,
                                                 gen_topo_input_port_t *    first_in_pcm_port_ptr,
                                                 gen_topo_output_port_t *   first_out_pcm_port_ptr,
                                                 gen_cntr_threshold_prop_t *lcm_thresh_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   gen_topo_module_t *     module_ptr = NULL;
   gen_topo_common_port_t *port_ptr   = NULL;

   if (NULL != first_in_pcm_port_ptr)
   {
      port_ptr   = &first_in_pcm_port_ptr->common;
      module_ptr = (gen_topo_module_t *)first_in_pcm_port_ptr->gu.cmn.module_ptr;
   }
   else // if (NULL != first_out_pcm_port_ptr)
   {
      port_ptr   = &first_out_pcm_port_ptr->common;
      module_ptr = (gen_topo_module_t *)first_out_pcm_port_ptr->gu.cmn.module_ptr;
   }

   uint32_t thresh_bytes    = gen_cntr_get_default_port_threshold(me_ptr, module_ptr, port_ptr->media_fmt_ptr);

   lcm_thresh_ptr->thresh_us      = topo_bytes_to_us(thresh_bytes, port_ptr->media_fmt_ptr, NULL);
   lcm_thresh_ptr->thresh_samples = topo_bytes_to_samples_per_ch(thresh_bytes, port_ptr->media_fmt_ptr);
   lcm_thresh_ptr->sample_rate    = port_ptr->media_fmt_ptr->pcm.sample_rate;

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Only raw compr module found for threshold: thresh_bytes %lu, Assigning PCM LCM thresh_us %lu, "
                "thresh_samples %lu, thresh_SR %d, "
                "based on first module 0x%lx found with valid PCM media format",
                lcm_thresh_ptr->thresh_bytes,
                lcm_thresh_ptr->thresh_us,
                lcm_thresh_ptr->thresh_samples,
                lcm_thresh_ptr->sample_rate,
                module_ptr->gu.module_instance_id);

   return;
}

/**
 * sets only for PCM/packetized or if media fmt is known.
 * if media fmt is not known, then thresh is returned as is.
 */
static uint32_t gen_cntr_set_thresh_value(gen_topo_t *               topo_ptr,
                                          gen_topo_module_t *        module_ptr,
                                          uint32_t                   port_id,
                                          uint32_t                   is_input,
                                          gen_topo_common_port_t *   port_ptr,
                                          gen_cntr_threshold_prop_t *lcm_threshold_ptr)
{
   uint32_t module_thresh  = gen_topo_get_curr_port_threshold(port_ptr);
   uint32_t overall_thresh = module_thresh;

   // Set thresh value on the port,
   //  1. If mf is valid on the port, lcm_samples can be used to set threshold.
   //  2  If mf is not valid and module has threshold, it will be retained.
   //  3. If mf is not valid and modules doesn't have threshold, set the thresh_bytes.
   //     Necessary in signal trigger cases where mf is not propagated initially, but topo needs buffers to process
   //     Signal trigger modules
   //     before mf is propagated.
   if (port_ptr->flags.is_mf_valid && SPF_IS_PCM_DATA_FORMAT(port_ptr->media_fmt_ptr->data_format))
   {
      if (lcm_threshold_ptr->sample_rate == port_ptr->media_fmt_ptr->pcm.sample_rate)
      {
         overall_thresh = topo_samples_to_bytes(lcm_threshold_ptr->thresh_samples, port_ptr->media_fmt_ptr);
      }
      else
      {
         overall_thresh = topo_us_to_bytes(lcm_threshold_ptr->thresh_us, port_ptr->media_fmt_ptr);
      }
   }
   else if ((0 == module_thresh) || (1 == module_thresh))
   {
      overall_thresh = lcm_threshold_ptr->thresh_bytes;
   }
#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Module 0x%lX, port_id = 0x%lx, is_input = %u, overall_thresh = %lu, module_thresh = %lu",
                module_ptr->gu.module_instance_id,
                port_id,
                is_input,
                overall_thresh,
                module_thresh);
#endif
   if (overall_thresh != module_thresh)
   {
      if ((0 == module_thresh) || gen_cntr_is_forcing_thresh_ok(topo_ptr, module_ptr, port_id, is_input, port_ptr))
      {
         // if module_thresh is one or zero, don't calculate num_proc_loops, let it be one.
         if (port_ptr->flags.port_has_threshold)
         {
            module_ptr->num_proc_loops = MAX(module_ptr->num_proc_loops, overall_thresh / module_thresh);
            if (module_ptr->flags.inplace && (module_ptr->num_proc_loops > 1))
            {
               // when looping old input will be moved up (which actually overwrites output for inplace).
               module_ptr->flags.inplace = FALSE;
               GEN_CNTR_MSG(topo_ptr->gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning: Module 0x%lX, forced to be non-inplace as num-loops %lu > 1",
                            module_ptr->gu.module_instance_id,
                            module_ptr->num_proc_loops);
            }
         }

         port_ptr->port_event_new_threshold = overall_thresh;

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning: Module 0x%lX, port_id 0x%lx (is_input %u): Forcing threshold: old %lu new %lu. SR "
                      "(%lu, %lu). num_proc_loops %lu",
                      module_ptr->gu.module_instance_id,
                      port_id,
                      is_input,
                      module_thresh,
                      overall_thresh,
                      lcm_threshold_ptr->sample_rate,
                      port_ptr->media_fmt_ptr->pcm.sample_rate,
                      module_ptr->num_proc_loops);

         module_thresh = overall_thresh;
      }
   }

   // modules with num proc loops greater than cannot be supported in Pure ST topology.
   if (module_ptr->num_proc_loops > 1)
   {
      topo_ptr->flags.cannot_be_pure_signal_triggered = TRUE;
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Warning: Container cannot be Pure Signal triggered topo since Module 0x%lX has num-loops %lu > 1",
                   module_ptr->gu.module_instance_id,
                   module_ptr->num_proc_loops);
   }

   gen_cntr_rd_ep_num_loops_err_check(topo_ptr, module_ptr);

   return module_thresh;
}

static ar_result_t gen_cntr_prop_thresh_backwards_to_prev_mod_out(gen_cntr_t *            me_ptr,
                                                                  gen_topo_output_port_t *prev_mod_out_port_ptr,
                                                                  uint32_t                threshold,
                                                                  bool_t *                thresh_prop_not_complete_ptr,
                                                                  gen_cntr_threshold_prop_t *lcm_threshold_ptr)
{
   ar_result_t result = AR_EOK;

   bool_t is_derived_thresh = FALSE;

   gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)prev_mod_out_port_ptr->gu.cmn.module_ptr;

   // If prev module is SISO/SIMO and current module has threshold which is in SIMO modules output path.
   // Then iterate through all the output ports of previous modules and get the highest threshold
   // and set it to all the output ports
   uint32_t                max_prev_op_threshold        = threshold;
   gen_topo_output_port_t *max_thresh_prev_out_port_ptr = prev_mod_out_port_ptr;
   for (gu_output_port_list_t *prev_out_port_list_ptr = prev_module_ptr->gu.output_port_list_ptr;
        (NULL != prev_out_port_list_ptr);
        LIST_ADVANCE(prev_out_port_list_ptr))
   {
      gen_topo_output_port_t *temp_prev_out_port_ptr = (gen_topo_output_port_t *)prev_out_port_list_ptr->op_port_ptr;

      if (temp_prev_out_port_ptr->common.flags.port_has_threshold)
      {
         // sets and gets thresh. for PCM/packetized, lcm thresh gets used. Raw/unknown media fmt, return thresh as is.
         // in raw cases, media fmt::data_format may not be known, but thresh needs to be propagated
         uint32_t prev_module_thresh = gen_cntr_set_thresh_value(&me_ptr->topo,
                                                                 prev_module_ptr,
                                                                 temp_prev_out_port_ptr->gu.cmn.id,
                                                                 FALSE, /** is_input */
                                                                 &temp_prev_out_port_ptr->common,
                                                                 lcm_threshold_ptr);

         // this logic would only pick max for raw-compressed, for others, lcm based will be picked.
         if (prev_module_thresh > max_prev_op_threshold)
         {
            max_prev_op_threshold        = prev_module_thresh;
            max_thresh_prev_out_port_ptr = temp_prev_out_port_ptr;
         }
      }
   }

   // Assign max threshold obtained above to the output port of the previous module,
   // Only if the out port doesn't have a threshold.
   for (gu_output_port_list_t *prev_out_port_list_ptr = prev_module_ptr->gu.output_port_list_ptr;
        (NULL != prev_out_port_list_ptr);
        LIST_ADVANCE(prev_out_port_list_ptr))
   {
      gen_topo_output_port_t *temp_prev_out_port_ptr = (gen_topo_output_port_t *)prev_out_port_list_ptr->op_port_ptr;

      // Set threshold only if doesnt exist.
      if (!temp_prev_out_port_ptr->common.flags.port_has_threshold)
      {
         // No need to scale the threshold if max threshold is obtained from the temp_prev_out_port_ptr.
         uint32_t scaled_thresh = max_prev_op_threshold;

         // Media format needs to checked, only if the max threshold has to be scaled while assigning to
         // temp_prev_out_port_ptr. But if temp_prev_out_port_ptr itself is max threshold port then
         // we can propagate threshold without scaling.
         if (max_thresh_prev_out_port_ptr != temp_prev_out_port_ptr)
         {
            if (temp_prev_out_port_ptr->common.flags.is_mf_valid)
            {
               if (SPF_IS_PCM_DATA_FORMAT(prev_mod_out_port_ptr->common.media_fmt_ptr->data_format) &&
                   SPF_IS_PCM_DATA_FORMAT(temp_prev_out_port_ptr->common.media_fmt_ptr->data_format))
               {
                  scaled_thresh =
                     topo_rescale_byte_count_with_media_fmt(max_prev_op_threshold,
                                                            temp_prev_out_port_ptr->common.media_fmt_ptr,
                                                            max_thresh_prev_out_port_ptr->common.media_fmt_ptr);
               }
            }
         }

         if (0 != scaled_thresh)
         {
            if (temp_prev_out_port_ptr->common.port_event_new_threshold != scaled_thresh)
            {
               temp_prev_out_port_ptr->common.port_event_new_threshold = scaled_thresh;
            }

            is_derived_thresh = TRUE;
         }
         else
         {
            *thresh_prop_not_complete_ptr = TRUE;
         }
      }
      else
      {
         /** previous output has thresh and current module's input has thresh. if thresh don't match then there
          * will be multiple topo-processes called.
          * this is ok although not optimal. this is the price for having module's with different thresholds.
          * in this case, thresh prop is based on prev module's out thresh */

         // In case of multi port modules - if the current output ports threshold does not match with the
         // threshold of different module.
         is_derived_thresh = FALSE;
      }

      // Mark the previous output port as visited
      temp_prev_out_port_ptr->gu.cmn.flags.mark = TRUE;

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_backwards: Previous module 0x%lX, out port_id 0x%lx, threshold is "
                   "%lu, is_derived_thresh %u. This module in-threshold %lu",
                   prev_module_ptr->gu.module_instance_id,
                   temp_prev_out_port_ptr->gu.cmn.id,
                   temp_prev_out_port_ptr->common.port_event_new_threshold,
                   is_derived_thresh,
                   threshold);
#endif
   }

   return result;
}

static ar_result_t gen_cntr_prop_thresh_backwards_to_prev_mod_in(gen_cntr_t *            me_ptr,
                                                                 gen_topo_output_port_t *prev_mod_out_port_ptr,
                                                                 bool_t *                thresh_prop_not_complete_ptr,
                                                                 gen_cntr_threshold_prop_t *lcm_threshold_ptr)
{

   ar_result_t result = AR_EOK;

   bool_t is_derived_thresh = FALSE;

   gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)prev_mod_out_port_ptr->gu.cmn.module_ptr;

   for (gu_input_port_list_t *prev_in_port_list_ptr = prev_module_ptr->gu.input_port_list_ptr;
        (NULL != prev_in_port_list_ptr);
        LIST_ADVANCE(prev_in_port_list_ptr))
   {
      gen_topo_input_port_t *prev_mod_in_port_ptr = (gen_topo_input_port_t *)prev_in_port_list_ptr->ip_port_ptr;

      if (!prev_mod_in_port_ptr->common.flags.port_has_threshold)
      {
         uint32_t scaled_thresh = gen_topo_get_curr_port_threshold(&prev_mod_out_port_ptr->common);
         if (prev_mod_in_port_ptr->common.flags.is_mf_valid)
         {
            if (SPF_IS_PCM_DATA_FORMAT(prev_mod_in_port_ptr->common.media_fmt_ptr->data_format) &&
                SPF_IS_PCM_DATA_FORMAT(prev_mod_out_port_ptr->common.media_fmt_ptr->data_format))
            {
               scaled_thresh = topo_rescale_byte_count_with_media_fmt(scaled_thresh,
                                                                      prev_mod_in_port_ptr->common.media_fmt_ptr,
                                                                      prev_mod_out_port_ptr->common.media_fmt_ptr);
            }
         }

         if (0 != scaled_thresh)
         {
            if (scaled_thresh != prev_mod_in_port_ptr->common.port_event_new_threshold)
            {
               prev_mod_in_port_ptr->common.port_event_new_threshold = scaled_thresh;
            }

            is_derived_thresh = TRUE;
         }
         else
         {
            *thresh_prop_not_complete_ptr = TRUE;
         }
      }
      else
      {
         // even if there's threshold, new thresh is assigned to match LCM (only for PCM/packetized)
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         prev_module_ptr,
                                         prev_mod_in_port_ptr->gu.cmn.id,
                                         TRUE, /** is_input */
                                         &prev_mod_in_port_ptr->common,
                                         lcm_threshold_ptr);
      }

      // Mark previous module input port as visited.
      prev_mod_in_port_ptr->gu.cmn.flags.mark = TRUE;

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_backwards: Previous module 0x%lX, in port-id 0x%lx, threshold is %lu, "
                   "is_derived_thresh %u, *thresh_prop_not_complete_ptr %u. Threshold being propagated %lu",
                   prev_module_ptr->gu.module_instance_id,
                   prev_mod_in_port_ptr->gu.cmn.id,
                   prev_mod_in_port_ptr->common.port_event_new_threshold,
                   is_derived_thresh,
                   *thresh_prop_not_complete_ptr,
                   gen_topo_get_curr_port_threshold(&prev_mod_out_port_ptr->common));
#endif
   }

   return result;
}

/**
 * max depth of recursion (max modules)
 */
#define THRESH_PROP_MAX_RECURSE_DEPTH 50

/**
 * Recursive function
 */
static ar_result_t gen_cntr_propagate_threshold_backwards(gen_cntr_t *               me_ptr,
                                                          gen_topo_module_t *        in_thresh_module_ptr,
                                                          bool_t *                   thresh_prop_not_complete_ptr,
                                                          gen_cntr_threshold_prop_t *lcm_threshold_ptr,
                                                          uint32_t *                 recurse_depth_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   RECURSION_ERROR_CHECK_ON_FN_ENTRY(me_ptr->topo.gu.log_id, recurse_depth_ptr, THRESH_PROP_MAX_RECURSE_DEPTH);
   /**
    * Start from in_thresh_module_ptr and go backwards, while assigning thresholds.
    */
   if (!in_thresh_module_ptr)
   {
      RECURSION_ERROR_CHECK_ON_FN_EXIT(me_ptr->topo.gu.log_id, recurse_depth_ptr);
      return result;
   }

   for (gu_input_port_list_t *in_port_list_ptr = in_thresh_module_ptr->gu.input_port_list_ptr;
        (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (in_port_ptr->common.flags.port_has_threshold && FALSE == in_port_ptr->gu.cmn.flags.mark)
      {
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         in_thresh_module_ptr,
                                         in_port_ptr->gu.cmn.id,
                                         TRUE,
                                         &in_port_ptr->common,
                                         lcm_threshold_ptr);
         in_port_ptr->gu.cmn.flags.mark = TRUE;
      }

      // this is already as per LCM threshold for PCM/packetized
      uint32_t threshold = gen_topo_get_curr_port_threshold(&in_port_ptr->common);

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_backwards: in_thresh_module_ptr(0x%lX). "
                   "In port id 0x%lx. Threshold %lu. port_marker=%u ",
                   in_thresh_module_ptr->gu.module_instance_id,
                   in_port_ptr->gu.cmn.id,
                   threshold,
                   in_port_ptr->gu.cmn.flags.mark);
#endif

      // Skip the propagation on this input port if,
      //      1. Threshold is zero.
      if (0 == threshold)
      {
         *thresh_prop_not_complete_ptr = TRUE;
         continue;
      }

      // assigning thresh backward involves going to previous port based on connections

      gen_topo_output_port_t *prev_mod_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;
      if (prev_mod_out_port_ptr && (FALSE == prev_mod_out_port_ptr->gu.cmn.flags.mark))
      {
         gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)prev_mod_out_port_ptr->gu.cmn.module_ptr;

         TRY(result,
             gen_cntr_prop_thresh_backwards_to_prev_mod_out(me_ptr,
                                                            prev_mod_out_port_ptr,
                                                            threshold,
                                                            thresh_prop_not_complete_ptr,
                                                            lcm_threshold_ptr));

         // if prev_module_ptr is single output and one or many inputs,
         // then derive all input thresholds based on the output.
         // if prev_module_ptr is multi-output, then thresh is already specified
         if (gen_cntr_is_thresh_propagation_possible(prev_module_ptr))
         {
            TRY(result,
                gen_cntr_prop_thresh_backwards_to_prev_mod_in(me_ptr,
                                                              prev_mod_out_port_ptr,
                                                              thresh_prop_not_complete_ptr,
                                                              lcm_threshold_ptr));

            // propgate further backwards with prev_module as the thresh module.
            TRY(result,
                gen_cntr_propagate_threshold_backwards(me_ptr,
                                                       prev_module_ptr,
                                                       thresh_prop_not_complete_ptr,
                                                       lcm_threshold_ptr,
                                                       recurse_depth_ptr));
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(me_ptr->topo.gu.log_id, recurse_depth_ptr);

   return result;
}

static ar_result_t gen_cntr_prop_thresh_forward_to_next_mod_in(gen_cntr_t *               me_ptr,
                                                               gen_topo_input_port_t *    next_mod_in_port_ptr,
                                                               uint32_t                   threshold,
                                                               bool_t *                   thresh_prop_not_complete_ptr,
                                                               gen_cntr_threshold_prop_t *lcm_threshold_ptr)
{

   ar_result_t result = AR_EOK;

   bool_t is_derived_thresh = FALSE;

   gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)next_mod_in_port_ptr->gu.cmn.module_ptr;

   // If next module is MISO get the max threshold of all the inputs ports and
   // assign this max threshold to the input ports which doesn't have threshold.
   uint32_t               max_next_ip_thresh     = threshold;
   gen_topo_input_port_t *max_thresh_in_port_ptr = next_mod_in_port_ptr;
   for (gu_input_port_list_t *in_port_list_ptr = next_module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *temp_next_in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (temp_next_in_port_ptr->common.flags.port_has_threshold)
      {
         // even if there's threshold, new thresh is assigned to match LCM (only for PCM/packetized)
         uint32_t next_module_thresh = gen_cntr_set_thresh_value(&me_ptr->topo,
                                                                 next_module_ptr,
                                                                 temp_next_in_port_ptr->gu.cmn.id,
                                                                 TRUE, /** is_input */
                                                                 &temp_next_in_port_ptr->common,
                                                                 lcm_threshold_ptr);

         if (next_module_thresh > max_next_ip_thresh)
         {
            max_next_ip_thresh     = next_module_thresh;
            max_thresh_in_port_ptr = temp_next_in_port_ptr;
         }
      }
   }

   // Scale and assign the max threshold to the input port, if it doesn't have a threshold.
   for (gu_input_port_list_t *in_port_list_ptr = next_module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *temp_next_in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (!temp_next_in_port_ptr->common.flags.port_has_threshold)
      {
         // Media format needs to checked if the max threshold has to be scaled while assigning to
         // temp_next_in_port_ptr. But if temp_next_in_port_ptr itself is max threshold port then
         // we can propagate threshold without scaling.

         /**
          * next_module_ptr module needs to create extra buffers
          * at its output if the next module has threshold (is not of type GENERIC (enc/dec)).
          * For generic (which don't have threshold), no need to create extra buffers as input buf from next
          * module can be re-used.
          */
         uint32_t scaled_thresh = max_next_ip_thresh;

         if (max_thresh_in_port_ptr != temp_next_in_port_ptr)
         {
            if (temp_next_in_port_ptr->common.flags.is_mf_valid)
            {
               if (SPF_IS_PCM_DATA_FORMAT(next_mod_in_port_ptr->common.media_fmt_ptr->data_format) &&
                   SPF_IS_PCM_DATA_FORMAT(temp_next_in_port_ptr->common.media_fmt_ptr->data_format))
               {
                  scaled_thresh = topo_rescale_byte_count_with_media_fmt(max_next_ip_thresh,
                                                                         temp_next_in_port_ptr->common.media_fmt_ptr,
                                                                         max_thresh_in_port_ptr->common.media_fmt_ptr);
               }
            }
         }

         if (0 != scaled_thresh)
         {
            if (scaled_thresh != temp_next_in_port_ptr->common.port_event_new_threshold)
            {
               temp_next_in_port_ptr->common.port_event_new_threshold = scaled_thresh;
            }
            is_derived_thresh = TRUE;
         }
         else
         {
            *thresh_prop_not_complete_ptr = TRUE;
         }
      }
      else
      {
         is_derived_thresh = FALSE;
      }

      // Mark the input port of next module to visited.
      temp_next_in_port_ptr->gu.cmn.flags.mark = TRUE;

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_forward: Next module 0x%lX, in port-id 0x%lx, threshold is %lu, "
                   "is_derived_thresh %u. This module out-threshold %lu",
                   next_module_ptr->gu.module_instance_id,
                   temp_next_in_port_ptr->gu.cmn.id,
                   temp_next_in_port_ptr->common.port_event_new_threshold,
                   is_derived_thresh,
                   threshold);
#endif
   }

   return result;
}

static ar_result_t gen_cntr_prop_thresh_forward_to_next_mod_out(gen_cntr_t *               me_ptr,
                                                                gen_topo_input_port_t *    next_mod_in_port_ptr,
                                                                bool_t *                   thresh_prop_not_complete_ptr,
                                                                gen_cntr_threshold_prop_t *lcm_threshold_ptr)
{

   ar_result_t result = AR_EOK;

   bool_t is_derived_thresh = FALSE;

   gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)next_mod_in_port_ptr->gu.cmn.module_ptr;

   for (gu_output_port_list_t *next_out_port_list_ptr = next_module_ptr->gu.output_port_list_ptr;
        (NULL != next_out_port_list_ptr);
        LIST_ADVANCE(next_out_port_list_ptr))
   {
      uint32_t                scaled_thresh         = 0;
      gen_topo_output_port_t *next_mod_out_port_ptr = (gen_topo_output_port_t *)next_out_port_list_ptr->op_port_ptr;

      if (!next_mod_out_port_ptr->common.flags.port_has_threshold)
      {
         // input side is linearly related to output side only for generic modules.
         // other modules use same size on input & output.
         scaled_thresh = gen_topo_get_curr_port_threshold(&next_mod_in_port_ptr->common);

         if (next_mod_out_port_ptr->common.flags.is_mf_valid)
         {
            if (SPF_IS_PCM_DATA_FORMAT(next_mod_out_port_ptr->common.media_fmt_ptr->data_format) &&
                SPF_IS_PCM_DATA_FORMAT(next_mod_in_port_ptr->common.media_fmt_ptr->data_format))
            {
               scaled_thresh = topo_rescale_byte_count_with_media_fmt(scaled_thresh,
                                                                      next_mod_out_port_ptr->common.media_fmt_ptr,
                                                                      next_mod_in_port_ptr->common.media_fmt_ptr);
            }
         }

         if (0 != scaled_thresh)
         {
            if (scaled_thresh != next_mod_out_port_ptr->common.port_event_new_threshold)
            {
               next_mod_out_port_ptr->common.port_event_new_threshold = scaled_thresh;
            }

            is_derived_thresh = TRUE;
         }
         else
         {
            *thresh_prop_not_complete_ptr = TRUE;
         }
      }
      else
      {
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         next_module_ptr,
                                         next_mod_out_port_ptr->gu.cmn.id,
                                         TRUE, /** is_input */
                                         &next_mod_out_port_ptr->common,
                                         lcm_threshold_ptr);
         is_derived_thresh = FALSE;
      }

      // Mark the input port of next module to traversed, to avoid redundant propagation along this port.
      next_mod_out_port_ptr->gu.cmn.flags.mark = TRUE;

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_forward: Next module 0x%lX, out port-id 0x%lx, threshold is %lu, "
                   "is_derived_thresh %u, *thresh_prop_not_complete_ptr %u. Threshold being "
                   "propagated %lu",
                   next_module_ptr->gu.module_instance_id,
                   next_mod_out_port_ptr->gu.cmn.id,
                   next_mod_out_port_ptr->common.port_event_new_threshold,
                   is_derived_thresh,
                   *thresh_prop_not_complete_ptr,
                   gen_topo_get_curr_port_threshold(&next_mod_in_port_ptr->common));
#endif
   }

   return result;
}

/**
 * Recursive function
 */
static ar_result_t gen_cntr_propagate_threshold_forward(gen_cntr_t *               me_ptr,
                                                        gen_topo_module_t *        out_thresh_module_ptr,
                                                        bool_t *                   thresh_prop_not_complete_ptr,
                                                        gen_cntr_threshold_prop_t *lcm_threshold_ptr,
                                                        uint32_t *                 recurse_depth_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   RECURSION_ERROR_CHECK_ON_FN_ENTRY(me_ptr->topo.gu.log_id, recurse_depth_ptr, THRESH_PROP_MAX_RECURSE_DEPTH);

   if (!out_thresh_module_ptr)
   {
      RECURSION_ERROR_CHECK_ON_FN_EXIT(me_ptr->topo.gu.log_id, recurse_depth_ptr);
      return result;
   }

   for (gu_output_port_list_t *out_port_list_ptr = out_thresh_module_ptr->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (out_port_ptr->common.flags.port_has_threshold && FALSE == out_port_ptr->gu.cmn.flags.mark)
      {
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         out_thresh_module_ptr,
                                         out_port_ptr->gu.cmn.id,
                                         FALSE,
                                         &out_port_ptr->common,
                                         lcm_threshold_ptr);
         out_port_ptr->gu.cmn.flags.mark = TRUE;
      }

      uint32_t threshold = gen_topo_get_curr_port_threshold(&out_port_ptr->common);

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_propagate_threshold_forward: out_thresh_module_ptr(0x%lX)."
                   " Out port id 0x%lx. Threshold %lu, port_marker=%u ",
                   out_thresh_module_ptr->gu.module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   threshold,
                   out_port_ptr->gu.cmn.flags.mark);
#endif

      // maybe because of media fmt not being propagated.
      if (0 == threshold)
      {
         *thresh_prop_not_complete_ptr = TRUE;
         continue;
      }

      gen_topo_input_port_t *next_mod_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
      // Propagate threshold to the next modules input port on if it hasn't been visited.
      if (next_mod_in_port_ptr && (FALSE == next_mod_in_port_ptr->gu.cmn.flags.mark))
      {
         gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)next_mod_in_port_ptr->gu.cmn.module_ptr;

         TRY(result,
             gen_cntr_prop_thresh_forward_to_next_mod_in(me_ptr,
                                                         next_mod_in_port_ptr,
                                                         threshold,
                                                         thresh_prop_not_complete_ptr,
                                                         lcm_threshold_ptr));

         // if next_module_ptr is single input and one or many outputs,
         // then derive all output thresholds based on the input.
         // if next_module_ptr is multi-input, then thresh is already specified
         if (gen_cntr_is_thresh_propagation_possible(next_module_ptr))
         {
            TRY(result,
                gen_cntr_prop_thresh_forward_to_next_mod_out(me_ptr,
                                                             next_mod_in_port_ptr,
                                                             thresh_prop_not_complete_ptr,
                                                             lcm_threshold_ptr));

            TRY(result,
                gen_cntr_propagate_threshold_forward(me_ptr,
                                                     next_module_ptr,
                                                     thresh_prop_not_complete_ptr,
                                                     lcm_threshold_ptr,
                                                     recurse_depth_ptr));
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(me_ptr->topo.gu.log_id, recurse_depth_ptr);

   return result;
}

/**
 * threshold propagation is possible for SISO, MISO, SIMO
 * and not MIZO, ZIMO, SIZO, ZISO, ZIZO.
 *
 * when thresh propagation is not possible, module must have thresh
 */
static bool_t gen_cntr_is_thresh_propagation_possible(gen_topo_module_t *module_ptr)
{
   if (((module_ptr->gu.num_output_ports == 1) /*&& (module_ptr->gu.num_input_ports >= 0)*/) ||
       (/*(module_ptr->gu.num_output_ports >= 0) &&*/ (module_ptr->gu.num_input_ports == 1)))
   {
      return TRUE;
   }

   return FALSE;
}

static bool_t gen_cntr_is_pseudo_thresh_module(gen_topo_module_t *module_ptr)
{
   return ((module_ptr->gu.module_type == AMDB_MODULE_TYPE_DEPACKETIZER) ||
           (module_ptr->gu.module_type == AMDB_MODULE_TYPE_PACKETIZER));
}

/**
 * Common multiple threshold is applicable only for PCM.
 * For non-PCM, their own threshold will be used. For PCM, overall threshold will be used.
 */
static ar_result_t gen_cntr_calc_lcm_threshold(uint32_t                   module_instance_id,
                                               uint32_t                   port_id,
                                               gen_topo_common_port_t *   cmn_port_ptr,
                                               gen_cntr_threshold_prop_t *lcm_threshold_ptr,
                                               uint32_t                   log_id,
                                               bool_t                     has_valid_mf)
{
   ar_result_t result      = AR_EOK;
   uint32_t    lcm_samples = 0;
   uint32_t    lcm_us      = 0;

   uint32_t thresh_bytes = gen_topo_get_curr_port_threshold(cmn_port_ptr);
#ifdef THRESH_PROP_DEBUG
   GEN_CNTR_MSG(log_id,
                DBG_LOW_PRIO,
                "gen_cntr_calc_lcm_threshold: module_instance_id: 0x%x port_id: 0x%lx has_valid_mf: %d  data_format: "
                "%d",
                module_instance_id,
                port_id,
                has_valid_mf,
                cmn_port_ptr->media_fmt_ptr->data_format);
#endif

   if (0 == lcm_threshold_ptr->thresh_bytes)
   {
      // Cache the first non-zero threshold bytes. During threshold propagation, if the media format is not set
      // then this value can be propagated. If mf is set, lcm samples is used to scale the thresh.
      lcm_threshold_ptr->thresh_bytes = thresh_bytes;
   }

   /**
    * In encoding + COP pack cases, encoder thresh might be worstcase frame size, not actual. They may not be multiple.
    * In COP transmission over slimbus, slimbus has to propagate threshold otherwise, thread priority (based on
    * thresh_us) will be wrong. Even though in encoding cases, thresh prop is not required for COP fmt, in slimbus case
    * it's required.
    *
    * LCM concept is applicable only for PP modules. For enc/dec/pack, LCM concept will not be used.
    *
    * However, lcm_threshold_ptr->thresh_us needs to be populated for priority/ICB.
    *
    * Currently, if there are multiple threshold modules in case of packetized data, then first module's threshold will
    * be used as container threshold. For PCM, LCM is used.
    */

   /* Have to support three scenarios: pcm->pcm , pcm->pack, pack->pcm, Checks below:
     1. First threshold module no need to calculate LCM.
     2. If the second threshold module is PCM format but first module was packetized, then overwrite LCM with PCM
     threshold.
     3. If the second threshold module is packetized but first module was PCM, then PCM threshold itself is considered
     and LCM is not computed./
     4. If the first module is PCM and subsequent modules are PCM, LCM is calculated normally
     5. If there's only a pack module in a container it will be picked for threshold */
   if (0 == lcm_threshold_ptr->thresh_samples || (SPF_IS_PCM_DATA_FORMAT(cmn_port_ptr->media_fmt_ptr->data_format) &&
                                                  SPF_IS_PACKETIZED(lcm_threshold_ptr->data_format)))
   {
      if (has_valid_mf && SPF_IS_PACKETIZED_OR_PCM(cmn_port_ptr->media_fmt_ptr->data_format))
      {
         uint32_t thresh_us      = topo_bytes_to_us(thresh_bytes, cmn_port_ptr->media_fmt_ptr, NULL);
         uint32_t thresh_samples = topo_bytes_to_samples_per_ch(thresh_bytes, cmn_port_ptr->media_fmt_ptr);

         GEN_CNTR_MSG(log_id,
                      DBG_HIGH_PRIO,
                      "Setting initial LCM thresh: module_ptr(0x%lX) thresh %lu us, %lu samples",
                      module_instance_id,
                      thresh_us,
                      thresh_samples);

         // first thresh module sample rate is used as ref.
         lcm_threshold_ptr->sample_rate = cmn_port_ptr->media_fmt_ptr->pcm.sample_rate;

         // thresh_bytes may not be in sync with the thresh_samples/thresh_us.
         // Its only used in cases where mf is not known to scale thresh_samples.
         lcm_threshold_ptr->thresh_bytes   = thresh_bytes;
         lcm_threshold_ptr->thresh_samples = thresh_samples;
         lcm_threshold_ptr->thresh_us      = thresh_us;
         lcm_threshold_ptr->data_format    = cmn_port_ptr->media_fmt_ptr->data_format;
      }
   }
   else // LCM only for PCM
   {
      if (has_valid_mf && SPF_IS_PCM_DATA_FORMAT(cmn_port_ptr->media_fmt_ptr->data_format) &&
          SPF_IS_PCM_DATA_FORMAT(lcm_threshold_ptr->data_format))
      {
         uint32_t thresh_us      = topo_bytes_to_us(thresh_bytes, cmn_port_ptr->media_fmt_ptr, NULL);
         uint32_t thresh_samples = topo_bytes_to_samples_per_ch(thresh_bytes, cmn_port_ptr->media_fmt_ptr);

         uint32_t modulo = 0;
         // if sample rates are equal, then take LCM of samples else LCM of us.
         if (cmn_port_ptr->media_fmt_ptr->pcm.sample_rate == lcm_threshold_ptr->sample_rate)
         {
            result = find_lcm(lcm_threshold_ptr->thresh_samples, thresh_samples, &lcm_samples);
            if (AR_SUCCEEDED(result))
            {
               modulo = lcm_samples % thresh_samples;
               lcm_us = topo_samples_to_us(lcm_samples, lcm_threshold_ptr->sample_rate, NULL);
            }
         }
         else
         {
            result = find_lcm(lcm_threshold_ptr->thresh_us, thresh_us, &lcm_us);
            if (AR_SUCCEEDED(result))
            {
               modulo      = lcm_us % thresh_us;
               lcm_samples = topo_us_to_samples(lcm_us, lcm_threshold_ptr->sample_rate);
            }
         }

         if (AR_DID_FAIL(result) || modulo || (lcm_us > GEN_CNTR_MAX_PORT_THRESHOLD_ALLOWED_US))
         {
            GEN_CNTR_MSG(log_id,
                         DBG_HIGH_PRIO,
                         "Warning: LCM Threshold not multiples or unbounded: module_ptr(0x%lX)."
                         " port id 0x%lx. Module Port Threshold %lu us, %lu samples, LCM thresh before %lu us, %lu "
                         "samples, overall thresh after LCM %lu us, %lu samples.",
                         module_instance_id,
                         port_id,
                         thresh_us,
                         thresh_samples,
                         lcm_threshold_ptr->thresh_us,
                         lcm_threshold_ptr->thresh_samples,
                         lcm_us,
                         lcm_samples);
            lcm_us      = GEN_CNTR_MAX_PORT_THRESHOLD_ALLOWED_US;
            lcm_samples = topo_us_to_samples(lcm_us, lcm_threshold_ptr->sample_rate);
         }
         else
         {
#ifdef THRESH_PROP_DEBUG
            GEN_CNTR_MSG(log_id,
                         DBG_LOW_PRIO,
                         "gen_cntr_calc_lcm_threshold: module_ptr(0x%lX).port id 0x%lx. Module Port Threshold %lu us, "
                         "%lu samples, LCM thresh before %lu us, %lu samples, overall thresh after LCM %lu us, %lu "
                         "samples.",
                         module_instance_id,
                         port_id,
                         thresh_us,
                         thresh_samples,
                         lcm_threshold_ptr->thresh_us,
                         lcm_threshold_ptr->thresh_samples,
                         lcm_us,
                         lcm_samples);
#endif
         }

         lcm_threshold_ptr->thresh_samples = lcm_samples;
         lcm_threshold_ptr->thresh_us      = lcm_us;
      }
   }
#ifdef THRESH_PROP_DEBUG
   GEN_CNTR_MSG(log_id,
                DBG_LOW_PRIO,
                "gen_cntr_calc_lcm_threshold: module_ptr(0x%lX). port id 0x%lx. LCM thresh %lu us, %lu samples, %lu "
                "sample rate, %lu bytes",
                module_instance_id,
                port_id,
                lcm_threshold_ptr->thresh_us,
                lcm_threshold_ptr->thresh_samples,
                lcm_threshold_ptr->sample_rate,
                lcm_threshold_ptr->thresh_bytes);
#endif

   return result;
}

/**
 *
 * Thresholds are determined with the below conditions:
 *    a) create as less buffers as possible.
 *       some things that will help this are:
 *       i)  in-place modules don't need both input and output buffers.
 *       ii) SISO modules that have no threshold (1 byte thresh), can use any arbitrary buffer (a common buffer)
 *    b) call process as less frequently as possible.
 *       E.g. operating at decoder threshold is optimal instead of running some modules at 1 ms, others at 5 ms and
 * dec at dec thresh. If not, process will be called too frequently.
 *
 * - proper thresholds are determined only after media format is known.
 * - thresholds can be propagated only for SISO (or single input, zero output & vice versa) modules.
 * - For MIMO modules (since we don't know which input corresponds to which output),
 *    arbitrary threshold based on SG perf-mode & media format is assigned. If it's MISO and SIMO - only 'multi'
 * thresh is assigned.
 *
 * Non MIMO modules:
 * SISO (Single Input Single Output),
 * SIZO (Single Input Zero Output),
 * ZISO (Zero Input Single Output),
 * ZIZO (Zero Input Zero Output)
 *
 */

static ar_result_t gen_cntr_check_and_assign_thresholds(gen_cntr_t *               me_ptr,
                                                        gen_topo_module_t **       thresh_module_pptr,
                                                        bool_t *                   thresh_prop_not_complete_ptr,
                                                        gen_cntr_threshold_prop_t *lcm_thresh_ptr,
                                                        uint32_t *                 recurse_depth_ptr)

{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         bool_t             is_set     = FALSE;

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
            if (FALSE == out_port_ptr->gu.cmn.flags.mark)
            {
               (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                               module_ptr,
                                               out_port_ptr->gu.cmn.id,
                                               FALSE,
                                               &out_port_ptr->common,
                                               lcm_thresh_ptr);
               out_port_ptr->gu.cmn.flags.mark = TRUE;
               is_set                          = TRUE;
            }
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
            if (FALSE == in_port_ptr->gu.cmn.flags.mark)
            {
               (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                               module_ptr,
                                               in_port_ptr->gu.cmn.id,
                                               TRUE,
                                               &in_port_ptr->common,
                                               lcm_thresh_ptr);
               in_port_ptr->gu.cmn.flags.mark = TRUE;
               is_set                         = TRUE;
            }
         }

         if (is_set)
         {
            *recurse_depth_ptr = 0;
            TRY(result,
                gen_cntr_propagate_threshold_backwards(me_ptr,
                                                       module_ptr,
                                                       thresh_prop_not_complete_ptr,
                                                       lcm_thresh_ptr,
                                                       recurse_depth_ptr));

            *recurse_depth_ptr = 0;
            TRY(result,
                gen_cntr_propagate_threshold_forward(me_ptr,
                                                     module_ptr,
                                                     thresh_prop_not_complete_ptr,
                                                     lcm_thresh_ptr,
                                                     recurse_depth_ptr));

            CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
            {
            }
         }
      }
   }
   return result;
}

static ar_result_t gen_cntr_set_and_propagate_thresholds(gen_cntr_t *               me_ptr,
                                                         gen_topo_module_t **       thresh_module_pptr,
                                                         bool_t *                   thresh_prop_not_complete_ptr,
                                                         gen_cntr_threshold_prop_t *lcm_thresh_ptr,
                                                         uint32_t *                 recurse_depth_ptr)
{
   ar_result_t result = AR_EOK;
   bool_t      is_set = FALSE;
   INIT_EXCEPTION_HANDLING
   // Mark all the input and output ports of threshold module to visited, since the threshold is already
   // set on the threshold module.
   for (gu_input_port_list_t *in_port_list_ptr = (*thresh_module_pptr)->gu.input_port_list_ptr;
        (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (FALSE == in_port_ptr->gu.cmn.flags.mark)
      {
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         (*thresh_module_pptr),
                                         in_port_ptr->gu.cmn.id,
                                         TRUE, /** is_input */
                                         &in_port_ptr->common,
                                         lcm_thresh_ptr);

         in_port_ptr->gu.cmn.flags.mark = TRUE;
         is_set                         = TRUE;
      }
   }

   for (gu_output_port_list_t *out_port_list_ptr = (*thresh_module_pptr)->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (FALSE == out_port_ptr->gu.cmn.flags.mark)
      {
         (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                         (*thresh_module_pptr),
                                         out_port_ptr->gu.cmn.id,
                                         FALSE, /** is_input */
                                         &out_port_ptr->common,
                                         lcm_thresh_ptr);

         out_port_ptr->gu.cmn.flags.mark = TRUE;
         is_set                          = TRUE;
      }
   }

   if (is_set)
   {
      /**  Always propagate from one of the thresh module.
       * If we use in_thresh for backward and out-thresh for forward, then there could be holes between the 2.
       * Doing this way doesn't change the way thresh is assigned.
       *
       * in some cases (Signal trigger) only one thresh is present */

      *recurse_depth_ptr = 0;
      TRY(result,
          gen_cntr_propagate_threshold_backwards(me_ptr,
                                                 *thresh_module_pptr,
                                                 thresh_prop_not_complete_ptr,
                                                 lcm_thresh_ptr,
                                                 recurse_depth_ptr));

      *recurse_depth_ptr = 0;
      TRY(result,
          gen_cntr_propagate_threshold_forward(me_ptr,
                                               *thresh_module_pptr,
                                               thresh_prop_not_complete_ptr,
                                               lcm_thresh_ptr,
                                               recurse_depth_ptr));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

void check_insert_module_ptr(gen_topo_module_t *module_ptr, spf_list_node_t **head_pptr)
{
   spf_list_node_t *node_ptr = *head_pptr;

   for (; NULL != node_ptr; node_ptr = node_ptr->next_ptr)
   {
      gen_topo_module_t *node_obj_ptr = (gen_topo_module_t *)node_ptr->obj_ptr;
      if (NULL != node_obj_ptr)
      {
         if (node_obj_ptr == module_ptr)
         {
            return;
         }
      }
   }
   spf_list_insert_tail(head_pptr, module_ptr, POSAL_HEAP_DEFAULT, FALSE);
}

static ar_result_t gen_cntr_simple_threshold_assignment(gen_cntr_t                *me_ptr,
                                                        bool_t                    *thresh_prop_not_complete_ptr,
                                                        gen_cntr_threshold_prop_t *lcm_thresh_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   gen_topo_module_t      *first_module_with_valid_mf  = NULL;
   gen_topo_common_port_t *first_port_with_valid_mf    = NULL;
   uint32_t                first_port_id_with_valid_mf = 0;

   // by default set it to true
   *thresh_prop_not_complete_ptr = TRUE;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (out_port_ptr->common.flags.is_mf_valid && (!first_port_with_valid_mf))
            { // here assuming it is a PCM media format.
               first_port_with_valid_mf    = &out_port_ptr->common;
               first_module_with_valid_mf  = module_ptr;
               first_port_id_with_valid_mf = out_port_ptr->gu.cmn.id;
            }

            out_port_ptr->common.flags.port_has_threshold = (out_port_ptr->common.threshold_raised) ? TRUE : FALSE;

            if (out_port_ptr->common.flags.port_has_threshold)
            {
               out_port_ptr->common.port_event_new_threshold = out_port_ptr->common.threshold_raised;

               TRY(result,
                   gen_cntr_calc_lcm_threshold(module_ptr->gu.module_instance_id,
                                               out_port_ptr->gu.cmn.id,
                                               (&out_port_ptr->common),
                                               lcm_thresh_ptr,
                                               me_ptr->topo.gu.log_id,
                                               out_port_ptr->common.flags.is_mf_valid));
            }
            else
            {
               // set to 1 so that this is properly initialized during gen_cntr_set_thresh_value
               out_port_ptr->common.port_event_new_threshold = 1;
            }
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            if (in_port_ptr->common.flags.is_mf_valid && (!first_port_with_valid_mf))
            { // here assuming it is a PCM media format.
               first_port_with_valid_mf    = &in_port_ptr->common;
               first_module_with_valid_mf  = module_ptr;
               first_port_id_with_valid_mf = in_port_ptr->gu.cmn.id;
            }

            in_port_ptr->common.flags.port_has_threshold = (in_port_ptr->common.threshold_raised) ? TRUE : FALSE;

            if (in_port_ptr->common.flags.port_has_threshold)
            {
               in_port_ptr->common.port_event_new_threshold = in_port_ptr->common.threshold_raised;
               TRY(result,
                   gen_cntr_calc_lcm_threshold(module_ptr->gu.module_instance_id,
                                               in_port_ptr->gu.cmn.id,
                                               (&in_port_ptr->common),
                                               lcm_thresh_ptr,
                                               me_ptr->topo.gu.log_id,
                                               in_port_ptr->common.flags.is_mf_valid));
            }
            else
            {
               // set to 1 so that this is properly initialized during gen_cntr_set_thresh_value
               in_port_ptr->common.port_event_new_threshold = 1;
            }
         }
      }
   }

   if (0 == lcm_thresh_ptr->thresh_us)
   {
      if (0 == first_port_id_with_valid_mf)
      {
         return result;
      }

      uint32_t temp = gen_cntr_get_default_port_threshold(me_ptr,
                                                          first_module_with_valid_mf,
                                                          first_port_with_valid_mf->media_fmt_ptr);

      first_port_with_valid_mf->port_event_new_threshold = temp;

      TRY(result,
          gen_cntr_calc_lcm_threshold(first_module_with_valid_mf->gu.module_instance_id,
                                      first_port_id_with_valid_mf,
                                      first_port_with_valid_mf,
                                      lcm_thresh_ptr,
                                      me_ptr->topo.gu.log_id,
                                      first_port_with_valid_mf->flags.is_mf_valid));
   }

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                            module_ptr,
                                            out_port_ptr->gu.cmn.id,
                                            FALSE,
                                            &out_port_ptr->common,
                                            lcm_thresh_ptr);
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            (void)gen_cntr_set_thresh_value(&me_ptr->topo,
                                            module_ptr,
                                            in_port_ptr->gu.cmn.id,
                                            TRUE,
                                            &in_port_ptr->common,
                                            lcm_thresh_ptr);
         }
      }
   }

   *thresh_prop_not_complete_ptr = FALSE;

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_find_thresh_module_and_assign_thresholds(gen_cntr_t         *me_ptr,
                                                                     gen_topo_module_t **thresh_module_pptr,
                                                                     bool_t             *thresh_prop_not_complete_ptr,
                                                                     gen_cntr_threshold_prop_t *lcm_thresh_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *in_thresh_module_ptr = NULL, *out_thresh_module_ptr = NULL;

   gen_topo_input_port_t * first_input_port_with_mf  = NULL;
   gen_topo_output_port_t *first_output_port_with_mf = NULL;

   gen_topo_input_port_t * first_input_port_with_pcm_mf  = NULL;
   gen_topo_output_port_t *first_output_port_with_pcm_mf = NULL;

   bool_t   is_mimo_module_present = FALSE;
   uint32_t recurse_depth          = 0;

   spf_list_node_t *mod_list_head_ptr = NULL;

   /* This module is used for threshold in case no other input or output threshold module is present instead of first
    * input or output media format module */
   gen_topo_module_t *pseudo_thresh_module_out_ptr = NULL, *pseudo_thresh_module_in_ptr = NULL;

   /**
    * First, for MIMO modules assign threshold if not already done by the module.
    * All MIMO modules must have thresholds on the 'multiple' side.
    *
    * At the end we get a module with input thresh and a module with out thresh (both could be same modules)
    */

   /* This flag are set under a specific case of raw compressed data transfer across containers*/
   bool_t allow_inheritance_of_upstream_frame_len = FALSE;
   /* Counter which gets incremented when there is any module capable of inheriting upstream frame length*/
   uint32_t can_loop_back_and_inherit_upstream_frame_len = 0;

/* Loop back if no inp/out threshold module is found and we can inherit the upstream threshold, example scenario is conn
 * proxy sink */
loop_over_modules_to_calc_lcm_threshold:
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // reset all module's num_proc_loops since we are taking max based on new thresholds.
         module_ptr->num_proc_loops = 1;

         // Threshold propagation is not possible only if the module is MIMO.
         is_mimo_module_present = !gen_cntr_is_thresh_propagation_possible(module_ptr);

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            bool_t has_valid_mf = out_port_ptr->common.flags.is_mf_valid;

            // Reset threshold info
            out_port_ptr->common.flags.port_has_threshold = (out_port_ptr->common.threshold_raised) ? TRUE : FALSE;
            out_port_ptr->common.port_event_new_threshold = out_port_ptr->common.threshold_raised;

            // cache the first module with media format.
            if (has_valid_mf && (NULL == first_output_port_with_mf))
            {
               first_output_port_with_mf = out_port_ptr;
            }

            if (has_valid_mf && (NULL == first_output_port_with_pcm_mf) &&
                SPF_IS_PCM_DATA_FORMAT(out_port_ptr->common.media_fmt_ptr->data_format))
            {
               first_output_port_with_pcm_mf = out_port_ptr;
            }

            if (is_mimo_module_present)
            {
               if (!out_port_ptr->common.flags.port_has_threshold && has_valid_mf)
               {
                  // For MIMO modules assign thresh forcefully
                  uint32_t temp =
                     gen_cntr_get_default_port_threshold(me_ptr, module_ptr, out_port_ptr->common.media_fmt_ptr);
                  if (temp != gen_topo_get_curr_port_threshold(&out_port_ptr->common))
                  {
                     out_port_ptr->common.port_event_new_threshold = temp;
                  }
                  // Set has threshold flag on the port and make the module non inplace.
                  out_port_ptr->common.flags.port_has_threshold = TRUE;
                  module_ptr->flags.inplace                     = FALSE;
               }
            }

            /* In output port case, if valid mf is not present we avoid only lcm calculation,
               we can still consider module's output threshold and assign output buffers.
               Ex: decoders will not raise output MF until first frame is processed, but for the
               first frame to process module needs to be called with an output buffer, for that
               threshold needs to assigned even though there is no valid MF. Else decoder process
               will never get called.

               Dont consider module for lcm calculation and start module for threshold propagation if
               We should not overwrite hwep threshold because of cop pack or depack. However if only cop
               pack or depack is there in a container , this can still be the threshold pointer

               Cases to cover: cop_pack -> HWEP, HW_EP->cop_depack, 61937depack->RD_EP, WREP->61937_pack.
               if HWEP doesn't provide threshold use pack_depack threshold otherwise, must use HWEP thresh.*/

            if (out_port_ptr->common.flags.port_has_threshold)
            {
               if (!gen_cntr_is_pseudo_thresh_module(module_ptr))
               {
                  TRY(result,
                      gen_cntr_calc_lcm_threshold(module_ptr->gu.module_instance_id,
                                                  out_port_ptr->gu.cmn.id,
                                                  (&out_port_ptr->common),
                                                  lcm_thresh_ptr,
                                                  me_ptr->topo.gu.log_id,
                                                  has_valid_mf));

                  /////
                  check_insert_module_ptr(module_ptr, &mod_list_head_ptr);
                  /////

                  // pick first module with thresh
                  if (NULL == out_thresh_module_ptr)
                  {
                     out_thresh_module_ptr = module_ptr;
#ifdef THRESH_PROP_DEBUG
                     GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                  DBG_LOW_PRIO,
                                  "gen_cntr_find_thresh_module_and_assign_thresholds: out_thresh_module_ptr, port-id "
                                  "(0x%lX, "
                                  "0x%lx). "
                                  "Threshold %lu, requires_data_buffering=%u",
                                  out_thresh_module_ptr->gu.module_instance_id,
                                  out_port_ptr->gu.cmn.id,
                                  gen_topo_get_curr_port_threshold(&out_port_ptr->common),
                                  module_ptr->flags.requires_data_buf);
#endif
                  }
               }
               else
               {
                  pseudo_thresh_module_out_ptr = module_ptr;
               }
            }
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            // input mf should be valid and must have been accepted by module to consider the threshold
            bool_t has_valid_mf =
               in_port_ptr->common.flags.is_mf_valid && !in_port_ptr->common.flags.module_rejected_mf;

            // Reset threshold info
            in_port_ptr->common.flags.port_has_threshold = (in_port_ptr->common.threshold_raised) ? TRUE : FALSE;
            in_port_ptr->common.port_event_new_threshold = in_port_ptr->common.threshold_raised;

            // cache the first module with media format.
            if (has_valid_mf && (NULL == first_input_port_with_mf))
            {
               first_input_port_with_mf = in_port_ptr;
            }

            if (has_valid_mf && (NULL == first_input_port_with_pcm_mf) &&
                SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format))
            {
               first_input_port_with_pcm_mf = in_port_ptr;
            }

            /*If this is an external input port AND no threshold is raised AND there is valid media format which is
            either raw compr or deint raw compr, we must inherit upstream max buf len as threshold and propagate in
            order to size the downstream buffers correctly.This is to ensure we don't break frame boundaries of
            compressed data which has to be copied as a whole and not in parts in XPAN sink usecases.

            In source usecases, since downstream container will have threshold we will not enter this check.
            This condition should also be exercised for rd-ep module getting raw compr data from upstream iec depack to
            avoid breaking frame boundary hence can't check for needs_stm_extn

            Module should inherit this threshold only if no other module in the container raised threshold.
            Otherwise we honor the threshold module. Example scenario: (conn proxy src) -> (Log -> Dec) we need to honor
            decoder threshold and log module should not inherit from upstream.
            */
            if ((in_port_ptr->gu.ext_in_port_ptr) && (!in_port_ptr->common.threshold_raised) && (has_valid_mf) &&
                SPF_IS_RAW_COMPR_DATA_FMT(in_port_ptr->common.media_fmt_ptr->data_format))
            {
               can_loop_back_and_inherit_upstream_frame_len++;

               /* We enter this condition if no other threshold module has been found at the end of the subgraph-for
                * loop*/
               if (allow_inheritance_of_upstream_frame_len)
               {
                  cu_ext_in_port_t *cu_ext_in_port_ptr =
                     (cu_ext_in_port_t *)((uint8_t *)in_port_ptr->gu.ext_in_port_ptr +
                                          me_ptr->cu.ext_in_port_cu_offset);

                  if (0 != cu_ext_in_port_ptr->upstream_frame_len.frame_len_bytes)
                  {
                     // Assigning port event thresh here to imply conn proxy module has raised this thresh
                     in_port_ptr->common.port_event_new_threshold =
                        cu_ext_in_port_ptr->upstream_frame_len.frame_len_bytes;
                     in_port_ptr->common.flags.port_has_threshold = TRUE;

#ifdef THRESH_PROP_DEBUG
                     GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                  DBG_HIGH_PRIO,
                                  "gen_cntr_calc_lcm_threshold: Inherit upstream buf size, module_ptr(0x%lX). port id "
                                  "0x%lx. port event thresh %lu bytes, max buf len %d",
                                  module_ptr->gu.module_instance_id,
                                  in_port_ptr->gu.cmn.id,
                                  in_port_ptr->common.port_event_new_threshold,
                                  in_port_ptr->common.max_buf_len);
#endif
                  }
               }
            }

            if (is_mimo_module_present)
            {
               // For MIMO modules assign thresh forcefully
               if (!in_port_ptr->common.flags.port_has_threshold && has_valid_mf)
               {
                  uint32_t temp =
                     gen_cntr_get_default_port_threshold(me_ptr, module_ptr, in_port_ptr->common.media_fmt_ptr);
                  if (temp != gen_topo_get_curr_port_threshold(&in_port_ptr->common))
                  {
                     in_port_ptr->common.port_event_new_threshold = temp;
                  }
                  // Set has threshold flag on the port and make the module non inplace.
                  in_port_ptr->common.flags.port_has_threshold = TRUE;
                  module_ptr->flags.inplace                    = FALSE;
               }
            }

            /* Dont consider module for lcm calculation and as inp thresh module if the
               input media foramt is not accpeted by the modules.

               In output port case, if valid mf is not present we avoid only lcm calculation,
               we can still consider module's output threshold and assign output buffers.
               Ex: decoders will not raise output MF until first frame is processed, but for the
               first frame to process module needs to be called with an output buffer, for that
               threshold needs to assigned even though there is no valid MF. Else decoder process
               will never get called.

               Ex: test voice_call_amrnb_dtmf_gen_with_pb_2

               We should not overwrite hwep threshold because of cop pack or depack.
               However if only cop pack or depack is there in a container ,
               this can still be the threshold pointer  */
            if (in_port_ptr->common.flags.port_has_threshold && has_valid_mf)
            {
               if (!gen_cntr_is_pseudo_thresh_module(module_ptr))
               {
                  TRY(result,
                      gen_cntr_calc_lcm_threshold(module_ptr->gu.module_instance_id,
                                                  in_port_ptr->gu.cmn.id,
                                                  (&in_port_ptr->common),
                                                  lcm_thresh_ptr,
                                                  me_ptr->topo.gu.log_id,
                                                  has_valid_mf));

                  /////
                  check_insert_module_ptr(module_ptr, &mod_list_head_ptr);
                  /////

                  // pick first module with thresh
                  if (NULL == in_thresh_module_ptr)
                  {
                     in_thresh_module_ptr = module_ptr;
#ifdef THRESH_PROP_DEBUG
                     GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                  DBG_LOW_PRIO,
                                  "gen_cntr_find_thresh_module_and_assign_thresholds: in_thresh_module_ptr, port-index "
                                  "(0x%lX, 0x%lx). Threshold %lu, requires_data_buffering=%u",
                                  in_thresh_module_ptr->gu.module_instance_id,
                                  in_port_ptr->gu.cmn.id,
                                  gen_topo_get_curr_port_threshold(&in_port_ptr->common),
                                  module_ptr->flags.requires_data_buf);
#endif
                  }
               }
               else
               {
                  pseudo_thresh_module_in_ptr = module_ptr;
               }
            }
         }
      }
   }

   // Check if there is atleast one with input or output threshold,
   //       1. If present then assign that module as the threshold module.
   //       2. If there is no such module and there is a module which can inherit upstream frame len, then assign that
   //       module as threshold module - only thresh bytes will be populated, the threshold in us and samples will be
   //       assigned based on perf mode and media format.
   //       3. If there is no such module and there is a MIMO module in the topo then threshold can't be propagated.
   //       4. If there is no such module and there is module with media fmt set, then assign the module with mf as
   //          threshold module. The default threshold is then assigned based on perf mode and media format.
   if ((NULL == out_thresh_module_ptr) && (NULL == in_thresh_module_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   " Warning: Neither a module with input threshold (0x%p) or one with output (0x%p) is found. Trying "
                   "to assign defaults; "
                   "first_input_port_with_mf 0x%p, "
                   "first_output_port_with_mf0x%p, "
                   "can_loop_back_and_inherit_upstream_frame_len %d",
                   in_thresh_module_ptr,
                   out_thresh_module_ptr,
                   first_input_port_with_mf,
                   first_output_port_with_mf,
                   can_loop_back_and_inherit_upstream_frame_len);

      // if there is no module with mf set then threshold can't be propagated.
      // usually input MF will be non-null; but in cases like spkr_prot which act as source modules during calib, out MF
      // can be non-null while input is null.
      if ((NULL == first_input_port_with_mf) && (NULL == first_output_port_with_mf))
      {
         *thresh_prop_not_complete_ptr = TRUE;
         return result;
      }

      /* Case 2: When we find a module that can inherit upstream threshold */
      if (1 == can_loop_back_and_inherit_upstream_frame_len)
      {
         allow_inheritance_of_upstream_frame_len = TRUE;
         goto loop_over_modules_to_calc_lcm_threshold;
      }
      else if (can_loop_back_and_inherit_upstream_frame_len > 1)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Unsupported scenario - multiple input ports cannot inherit upstream frame len");
      }

      gen_topo_module_t *     temp_module_ptr = NULL;
      gen_topo_common_port_t *cmn_port_ptr    = NULL;
      uint32_t                temp_port_id    = 0;

      /* In case neither input nor output threshold module is present then the pseudo module can be used for  threshold.
       * If this is not present then first input media format or ouput media format module can be used */
      if (pseudo_thresh_module_in_ptr || pseudo_thresh_module_out_ptr)
      {
         /* NOTE: For the port that has threshold but has data format raw lcm thresh will not be updated. */
         if (NULL != pseudo_thresh_module_in_ptr)
         {
            temp_module_ptr = pseudo_thresh_module_in_ptr;
            gen_topo_input_port_t *thresh_port_ptr =
               (gen_topo_input_port_t *)temp_module_ptr->gu.input_port_list_ptr->ip_port_ptr;
            cmn_port_ptr = &thresh_port_ptr->common;
            temp_port_id = thresh_port_ptr->gu.cmn.id;

            TRY(result,
                gen_cntr_calc_lcm_threshold(temp_module_ptr->gu.module_instance_id,
                                            temp_port_id,
                                            cmn_port_ptr,
                                            lcm_thresh_ptr,
                                            me_ptr->topo.gu.log_id,
                                            TRUE /*has_valid_mf*/));
         }

         if (NULL != pseudo_thresh_module_out_ptr)
         {
            temp_module_ptr = pseudo_thresh_module_out_ptr;
            gen_topo_output_port_t *thresh_port_ptr =
               (gen_topo_output_port_t *)temp_module_ptr->gu.output_port_list_ptr->op_port_ptr;
            cmn_port_ptr = &thresh_port_ptr->common;
            temp_port_id = thresh_port_ptr->gu.cmn.id;

            TRY(result,
                gen_cntr_calc_lcm_threshold(temp_module_ptr->gu.module_instance_id,
                                            temp_port_id,
                                            cmn_port_ptr,
                                            lcm_thresh_ptr,
                                            me_ptr->topo.gu.log_id,
                                            TRUE /*has_valid_mf*/));
         }
      }
      else
      {
         if (NULL != first_input_port_with_mf)
         {
            // Assign default threshold based on the media format and perf mode.
            // Get the module with first input media format as the threshold module and assign threshold based on
            temp_module_ptr = (gen_topo_module_t *)first_input_port_with_mf->gu.cmn.module_ptr;
            cmn_port_ptr    = &first_input_port_with_mf->common;
            temp_port_id    = first_input_port_with_mf->gu.cmn.id;

            // Check and assign default threshold
            uint32_t temp = gen_cntr_get_default_port_threshold(me_ptr,
                                                                temp_module_ptr,
                                                                first_input_port_with_mf->common.media_fmt_ptr);
            if (temp != gen_topo_get_curr_port_threshold(&first_input_port_with_mf->common))
            {
               first_input_port_with_mf->common.port_event_new_threshold = temp;
            }
         }
         else //(NULL !- first_output_port_with_mf)
         {
            temp_module_ptr = (gen_topo_module_t *)first_output_port_with_mf->gu.cmn.module_ptr;
            cmn_port_ptr    = &first_output_port_with_mf->common;
            temp_port_id    = first_output_port_with_mf->gu.cmn.id;

            uint32_t temp = gen_cntr_get_default_port_threshold(me_ptr,
                                                                temp_module_ptr,
                                                                first_output_port_with_mf->common.media_fmt_ptr);
            if (temp != gen_topo_get_curr_port_threshold(&first_output_port_with_mf->common))
            {
               first_output_port_with_mf->common.port_event_new_threshold = temp;
            }
         }

         TRY(result,
             gen_cntr_calc_lcm_threshold(temp_module_ptr->gu.module_instance_id,
                                         temp_port_id,
                                         cmn_port_ptr,
                                         lcm_thresh_ptr,
                                         me_ptr->topo.gu.log_id,
                                         TRUE /*has_valid_mf*/));
      }

      // Assign the first module with mf as threshold module.
      *thresh_module_pptr = temp_module_ptr;

      /* If thresh us, samples and SR is 0 and only thresh bytes is valid, it indicates that only a module with raw
        compressed data format is found and used for lcm thresh calculation. In such cases, for any PCM modules in
        a parallel path in the same container - 0 threshold would be incorrectly assigned. Example: cyclic graph with
        Conn Proxy sink and SAL in same container, in parallel paths. Conn proxy would be picked as threshold module.

        For these cases, we can assign ONLY the lcm thresh us and lcm thresh samples to perf mode based threshold based
        on first pcm module with valid mf that is present in same container. Pcm thresh can be propagated for all other
        pcm modules. Thresh bytes will be based on raw compr module.
      */ if (((NULL != first_input_port_with_pcm_mf) || (NULL != first_output_port_with_pcm_mf)) &&
             (0 == lcm_thresh_ptr->thresh_us) && (0 == lcm_thresh_ptr->thresh_samples) &&
             (0 == lcm_thresh_ptr->sample_rate) && (0 != lcm_thresh_ptr->thresh_bytes))
      {
         gen_cntr_set_failsafe_pcm_lcm_thresh(&me_ptr->topo,
                                              first_input_port_with_pcm_mf,
                                              first_output_port_with_pcm_mf,
                                              lcm_thresh_ptr);
      }

#ifdef THRESH_PROP_DEBUG
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_find_thresh_module_and_assign_thresholds: thresh_module_ptr, port-id (0x%lX, 0x%lx). "
                   "Threshold %lu, requires_data_buffering=%u",
                   temp_module_ptr->gu.module_instance_id,
                   temp_port_id,
                   gen_topo_get_curr_port_threshold(cmn_port_ptr),
                   temp_module_ptr->flags.requires_data_buf);
#endif
      // Set and propagate threshold from threshold module
      result = gen_cntr_set_and_propagate_thresholds(me_ptr,
                                                     thresh_module_pptr,
                                                     thresh_prop_not_complete_ptr,
                                                     lcm_thresh_ptr,
                                                     &recurse_depth);
   }
   else
   {
      //*thresh_module_pptr = (NULL != in_thresh_module_ptr) ? in_thresh_module_ptr : out_thresh_module_ptr;
      spf_list_node_t *  node_ptr = mod_list_head_ptr;
      gen_topo_module_t *mod_ptr  = NULL;

      /* If thresh us, samples and SR is 0 and only thresh bytes is valid, it indicates that only a module with raw
        compressed data format is found and used for lcm thresh calculation. In such cases, for any PCM modules in
        a parallel path in the same container - 0 threshold would be incorrectly assigned. Example: cyclic graph with
        Conn Proxy sink and SAL in same container, in parallel paths. Conn proxy would be picked as threshold module.

        For these cases, we can assign ONLY the lcm thresh us and lcm thresh samples to perf mode based threshold based
        on first pcm module with valid mf that is present in same container. Pcm thresh can be propagated for all other
        pcm modules.Thresh bytes will be based on raw compr module.
      */
      if (((NULL != first_input_port_with_pcm_mf) || (NULL != first_output_port_with_pcm_mf)) &&
          (0 == lcm_thresh_ptr->thresh_us) && (0 == lcm_thresh_ptr->thresh_samples) &&
          (0 == lcm_thresh_ptr->sample_rate) && (0 != lcm_thresh_ptr->thresh_bytes))
      {
         gen_cntr_set_failsafe_pcm_lcm_thresh(&me_ptr->topo,
                                              first_input_port_with_pcm_mf,
                                              first_output_port_with_pcm_mf,
                                              lcm_thresh_ptr);
      }

      for (; NULL != node_ptr; node_ptr = node_ptr->next_ptr)
      {
         mod_ptr = (gen_topo_module_t *)node_ptr->obj_ptr;

         *thresh_module_pptr = mod_ptr;

         // Set and propagate threshold from threshold module
         result = gen_cntr_set_and_propagate_thresholds(me_ptr,
                                                        thresh_module_pptr,
                                                        thresh_prop_not_complete_ptr,
                                                        lcm_thresh_ptr,
                                                        &recurse_depth);
      }
   }

   result = gen_cntr_check_and_assign_thresholds(me_ptr,
                                                 thresh_module_pptr,
                                                 thresh_prop_not_complete_ptr,
                                                 lcm_thresh_ptr,
                                                 &recurse_depth);

   // Clear all the port markers for the next propagation.
   result = gu_reset_graph_port_markers(me_ptr->cu.gu_ptr);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   ar_result_t res = spf_list_delete_list(&mod_list_head_ptr, FALSE);

   if (res != AR_EOK)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "ERROR");
   }

   return result;
}

/** any popped buffer recreated */
ar_result_t gen_cntr_ext_out_port_recreate_bufs(void *ctx_ptr, gu_ext_out_port_t *gu_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   cu_base_t *              base_ptr         = (cu_base_t *)ctx_ptr;
   gen_cntr_t *             me_ptr           = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_out_port_ptr;
   gen_topo_output_port_t * out_port_ptr     = (gen_topo_output_port_t *)gu_out_port_ptr->int_out_port_ptr;

   // external bufs have to be created even if this module has no threshold. Due to propagation
   // port_event_new_threshold would be assigned.
   uint32_t buf_size = gen_topo_get_curr_port_threshold(&out_port_ptr->common);

   // num_bufs_per_data_msg_v2 will be 0 if v1 case
   uint32_t num_bufs_per_data_msg_v2 = gen_topo_get_curr_port_bufs_num_v2(&out_port_ptr->common);

   if (buf_size)
   {
      uint32_t metadata_size = 0;
      uint32_t num_data_msg = 0, req_out_buf_size = buf_size;

      gen_cntr_get_required_out_buf_size_and_count(me_ptr,
                                                   ext_out_port_ptr,
                                                   &num_data_msg,
                                                   &req_out_buf_size,
                                                   buf_size,
                                                   metadata_size);

      if (ext_out_port_ptr->vtbl_ptr->recreate_out_buf)
      {
         result =
            ext_out_port_ptr->vtbl_ptr->recreate_out_buf(me_ptr, ext_out_port_ptr, req_out_buf_size, num_data_msg, num_bufs_per_data_msg_v2);
      }
   }

   return result;
}

static ar_result_t gen_cntr_recreate_all_buffers(gen_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   /**
    *
    * external input ports - no need to create any external buffers.
    * iterate through external output ports & create external buffers (bufQ - only for peer-svc client, not for
    * ext-clients)
    *
    * All module input and outputs will have a buffer (including first & final modules)
    *
    */
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            // check if additional bytes are required to accomodate variable samples for DM modules data path.
            // additional bytes are required for a given ip/op based on DM modules position & DM mode in the nblc
            // chain.
            // check funtion: gen_cntr_check_and_find_if_addtional_bytes_required_for_dm for
            uint32_t additional_bytes_req_for_dm_per_ch =
               gen_topo_compute_if_output_needs_addtional_bytes_for_dm(&me_ptr->topo, out_port_ptr);

            uint32_t new_bufs_num        = gen_topo_get_bufs_num_from_med_fmt(out_port_ptr->common.media_fmt_ptr);
            bool_t   did_num_bufs_change = (out_port_ptr->common.sdata.bufs_num != new_bufs_num);

            // if num ch changed in mf and threshold changes, need to reallocate the buffers, so
            // mark it as a new threshold
            if (did_num_bufs_change && (0 == out_port_ptr->common.port_event_new_threshold))
            {
               out_port_ptr->common.port_event_new_threshold = out_port_ptr->common.max_buf_len;
            }

            // Need to check port event threshold and additional_bytes_req_for_dm to reallocate buffers.
            // Note that DM module can dynamically enable/disable DM mode in such cases port_event_new_threshold
            // will be same by additional_bytes_req_for_dm can change
            if (out_port_ptr->common.port_event_new_threshold || additional_bytes_req_for_dm_per_ch || did_num_bufs_change)
            {
               uint32_t req_output_buf_size_bytes =
                  out_port_ptr->common.port_event_new_threshold + (additional_bytes_req_for_dm_per_ch * new_bufs_num);

               if ((SPF_IS_RAW_COMPR_DATA_FMT(out_port_ptr->common.media_fmt_ptr->data_format)) &&
                   (out_port_ptr->common.max_buf_len != req_output_buf_size_bytes))
               {
                  gen_cntr_ext_out_port_t *ext_out_port_ptr =
                     (gen_cntr_ext_out_port_t *)out_port_ptr->gu.ext_out_port_ptr;
                  // If data format is raw compr set the flag if there is any change in the port threshold
                  if (ext_out_port_ptr)
                  {
                     ext_out_port_ptr->cu.flags.upstream_frame_len_changed = TRUE;
                  }
               }

               if ((out_port_ptr->common.max_buf_len != req_output_buf_size_bytes) || did_num_bufs_change)
               {
                  for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
                  {
                     if (out_port_ptr->common.bufs_ptr[b].actual_data_len != 0)
                     {
                        GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                     DBG_HIGH_PRIO,
                                     "Module 0x%lX: out port id 0x%lx, Warning: resetting/dropping buffer when it has "
                                     "valid data %lu",
                                     out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                     out_port_ptr->gu.cmn.id,
                                     out_port_ptr->common.bufs_ptr[b].actual_data_len);
                     }
                     // All buf actual data len must be set to zero
                     // In case of 2 Ch. If the second channel buffer's actual data len is not be set to zero,
                     // It will not be in sync with max len in the other channels and results in corruption
                     out_port_ptr->common.bufs_ptr[b].actual_data_len = 0;
                  }
                  out_port_ptr->common.flags.force_return_buf = TRUE;
                  gen_topo_check_return_one_buf_mgr_buf(&me_ptr->topo,
                                                        &out_port_ptr->common,
                                                        out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                        out_port_ptr->gu.cmn.id);

                  // if one buf is NULL, rest are also assumed to be NULL.
                  out_port_ptr->common.bufs_ptr[0].data_ptr = NULL;

                  out_port_ptr->common.max_buf_len = req_output_buf_size_bytes;
                  // out_port_ptr->common.max_buf_len_per_buf =
                  // topo_div_num(out_port_ptr->common.max_buf_len, out_port_ptr->common.sdata.bufs_num);
                  // don't reset sdata or timestamp as this func may be called while data is being processed.
               }

               out_port_ptr->common.port_event_new_threshold = 0;
            }

            TRY(result,
                gen_topo_initialize_bufs_sdata(&me_ptr->topo,
                                               &out_port_ptr->common,
                                               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                               out_port_ptr->gu.cmn.id));

            // do ext port after internal ports because recreate_ext_out_buffers assigns buffer from ext port & that'll
            // be undone if called in reverse
            gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)out_port_ptr->gu.ext_out_port_ptr;
            if (ext_out_port_ptr)
            {
               result = gen_cntr_ext_out_port_recreate_bufs((void *)&me_ptr->cu, &ext_out_port_ptr->gu);
            }
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            // check if additional bytes are required to accomodate variable samples for DM usecases
            uint32_t additional_bytes_req_for_dm_per_ch =
               gen_topo_compute_if_input_needs_addtional_bytes_for_dm(&me_ptr->topo, in_port_ptr);

            uint32_t new_bufs_num        = gen_topo_get_bufs_num_from_med_fmt(in_port_ptr->common.media_fmt_ptr);
            bool_t   did_num_bufs_change = (in_port_ptr->common.sdata.bufs_num != new_bufs_num);

            // if num ch changed in mf and threshold changes, need to reallocate the buffers, so
            // mark it as a new threshold
            if (did_num_bufs_change && (0 == in_port_ptr->common.port_event_new_threshold))
            {
               in_port_ptr->common.port_event_new_threshold = in_port_ptr->common.max_buf_len;
            }

            // Need to check port event threshold and additional_bytes_req_for_dm to reallocate buffers.
            // Note that DM module can dynamically enable/disable DM mode in such cases port_event_new_threshold
            // will be same by additional_bytes_req_for_dm can change
            if (in_port_ptr->common.port_event_new_threshold || additional_bytes_req_for_dm_per_ch || did_num_bufs_change)
            {
               uint32_t req_input_buf_size_bytes =
                  in_port_ptr->common.port_event_new_threshold + (additional_bytes_req_for_dm_per_ch * new_bufs_num);

               if ((in_port_ptr->common.max_buf_len != req_input_buf_size_bytes) || did_num_bufs_change)
               {
                  for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
                  {
                     if (in_port_ptr->common.bufs_ptr[b].actual_data_len != 0)
                     {
                        GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                     DBG_HIGH_PRIO,
                                     "Module 0x%lX: in port id 0x%lx, Warning: resetting/dropping buffer when it has "
                                     "valid "
                                     "data %lu",
                                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                     in_port_ptr->gu.cmn.id,
                                     in_port_ptr->common.bufs_ptr[b].actual_data_len);
                     }
                     // All buf actual data len must be set to zero
                     // In case of 2 Ch. If the second channel buffer's actual data len is not be set to zero,
                     // It will not be in sync with max len in the other channels and results in corruption
                     in_port_ptr->common.bufs_ptr[b].actual_data_len = 0;
                  }
                  in_port_ptr->flags.need_more_input         = TRUE;
                  in_port_ptr->common.flags.force_return_buf = TRUE;

                  gen_topo_check_return_one_buf_mgr_buf(&me_ptr->topo,
                                                        &in_port_ptr->common,
                                                        in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                        in_port_ptr->gu.cmn.id);

                  in_port_ptr->common.bufs_ptr[0].data_ptr = NULL;

                  in_port_ptr->common.max_buf_len = req_input_buf_size_bytes;
                  // in_port_ptr->common.max_buf_len_per_buf =
                  // topo_div_num(in_port_ptr->common.max_buf_len, in_port_ptr->common.sdata.bufs_num);
                  // don't reset sdata or timestamp as this func may be called while data is being processed.
               }

               in_port_ptr->common.port_event_new_threshold = 0;
            }

            TRY(result,
                gen_topo_initialize_bufs_sdata(&me_ptr->topo,
                                               &in_port_ptr->common,
                                               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                               in_port_ptr->gu.cmn.id));
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "failure creating buffers");
   }

   return result;
}

/**
 * any module with smaller thresh is ok, but it has to be a int factor of ep threshold
 */

/**
 * Note that threshold changing at run time is valid scenario.
 */
ar_result_t gen_cntr_handle_port_data_thresh_change(void *ctx_ptr)
{
   gen_cntr_t                 *me_ptr              = (gen_cntr_t *)ctx_ptr;
   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   INIT_EXCEPTION_HANDLING

   // This function is an event-handler function
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT

   // threshold event handler can be called directly from different contexts
   // therefore need to reconcile the event flags.
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, TRUE /*do reconcile*/);

   if (!capi_event_flag_ptr->port_thresh)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "Skipping threshold propagation as threshold event is not set.");
      return result;
   }

   gen_cntr_threshold_prop_t lcm_threshold = { 0 };

   /** Threshold prop is by default complete.
    * When it's not possible to complete due to absence of media fmt,
    * callees of this func will set it to TRUE.
    * In I2S, thresh event is raised one time, but media fmt comes later on.
    * if we clear port_thresh event flag, then buffers will never get created.*/
   bool_t thresh_prop_not_complete = FALSE;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_MED_PRIO,
                " in gen_cntr_handle_port_data_thresh_change. thresh event %u, media_fmt_event %u, simple threshold "
                "proapgation %u",
                capi_event_flag_ptr->port_thresh,
                capi_event_flag_ptr->media_fmt_event,
                me_ptr->topo.flags.simple_threshold_propagation_enabled);

   if (me_ptr->topo.flags.simple_threshold_propagation_enabled)
   {
      TRY(result, gen_cntr_simple_threshold_assignment(me_ptr, &thresh_prop_not_complete, &lcm_threshold));
   }
   else
   {
      gen_topo_module_t *thresh_module_ptr = NULL; // A module with threshold for all input or all output ports

      TRY(result,
          gen_cntr_find_thresh_module_and_assign_thresholds(me_ptr,
                                                            &thresh_module_ptr,
                                                            &thresh_prop_not_complete,
                                                            &lcm_threshold));
   }

   // if thresh prop is complete,
   if (!thresh_prop_not_complete)
   {
      bool_t is_threshold_valid =
         (((0 == lcm_threshold.sample_rate) && (0 == lcm_threshold.thresh_samples)) || (0 == lcm_threshold.thresh_us))
            ? FALSE
            : TRUE;

      // nblc should be assigned before dm modes. DM mode assignment depends on updated nblc chain.
      gen_topo_assign_non_buf_lin_chains(&me_ptr->topo);

      // modules should be informed about container frame len first so that they can update their DM configuration.
      if (is_threshold_valid)
      {
         gen_topo_fwk_ext_set_cntr_frame_dur(&me_ptr->topo, lcm_threshold.thresh_us);
      }

      // dm should be updated before handling the frame-len change because DM update can change variable input/output
      // configuration.
      gen_topo_update_dm_modes(&me_ptr->topo);

      if (is_threshold_valid)
      {
         icb_frame_length_t fm = {
            .sample_rate       = lcm_threshold.sample_rate,
            .frame_len_samples = lcm_threshold.thresh_samples,
            .frame_len_us      = lcm_threshold.thresh_us,
         };

         cu_handle_frame_len_change(&me_ptr->cu, &fm, fm.frame_len_us);
      }

      gen_cntr_offload_send_opfs_event_to_wr_client(me_ptr);
      gen_cntr_recreate_all_buffers(me_ptr);

      if (FALSE == check_if_pass_thru_container(me_ptr))
      {
         gen_cntr_check_for_multiple_thresh_modules(me_ptr);
      }
   }

   if (check_if_pass_thru_container(me_ptr))
   {
      TRY(result, pt_cntr_validate_media_fmt_thresh((pt_cntr_t *)me_ptr));

      /** Assign topo buffers to the modules in the proc list*/
      TRY(result, pt_cntr_update_module_process_list((pt_cntr_t *)me_ptr));
      TRY(result, pt_cntr_assign_port_buffers((pt_cntr_t *)me_ptr));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if (!thresh_prop_not_complete)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " gen_cntr_handle_port_data_thresh_change complete");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " gen_cntr_handle_port_data_thresh_change not complete");
   }

   // clear anyway as media fmt will call this func again. keeping it on triggers repeated calls from data_process.
   capi_event_flag_ptr->port_thresh = FALSE;

   return result;
}

ar_result_t gen_cntr_update_icb_info(gen_topo_t *topo_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);

   // check if a DM module is present
   /* go over input ports and set variable size flag in icb info */
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      // if the external inputs nblc end has a DM module with fixed output mode then its variable input.
      bool_t is_variable_input =
         gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr,
                                                 (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);

      if (is_variable_input != ext_in_port_ptr->cu.icb_info.flags.variable_input)
      {
         ext_in_port_ptr->cu.icb_info.flags.variable_input = is_variable_input;

         // mark this flag inorder to inform upstream of any change in varaiable input for the ext port.
         ext_in_port_ptr->cu.prop_info.did_inform_us_of_frame_len_and_var_ip = FALSE;

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "External input (port-id,port-idx) (0x%lx,%lu) chagned variable_input flag from %lu to %lu",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->cu.icb_info.flags.variable_input,
                      is_variable_input);
      }
   }

   /* go over output ports and set variable size flag in icb info */
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      // if the external outupts nblc start has a DM module with fixed input mode then the external op can have variable
      // output samples.
      uint32_t is_variable_output =
         gen_topo_is_out_port_in_dm_variable_nblc(topo_ptr,
                                                  (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr);

      if (is_variable_output != ext_out_port_ptr->cu.icb_info.flags.variable_output)
      {
         ext_out_port_ptr->cu.icb_info.flags.variable_output = is_variable_output;

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "External output (port-id,port-idx) (0x%lx,%lu) updated variable_output flag to %lu",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      is_variable_output);
      }
   }

   return result;
}

/*
 * Init an external input port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
static ar_result_t gen_cntr_init_ext_in_queue(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_cntr_t *            me_ptr       = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_in_port_t *ext_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_port_ptr;
   cu_queue_handler_t      q_handler;
   uint32_t                bit_mask = 0;

   char data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "D", "GEN_CNTR", me_ptr->topo.gu.log_id);

   uint32_t max_num_elements = CU_MAX_INP_DATA_Q_ELEMENTS;

   // for buffer driven mode we add the data queues to the container channel and mask
   bit_mask  = cu_request_bit_in_bit_mask(&me_ptr->cu.available_bit_mask);
   q_handler = gen_cntr_input_dataQ_trigger;

   if (0 == bit_mask)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Bit mask has no bits available 0x%lx",
                   me_ptr->cu.available_bit_mask);
      return result;
   }

   ext_port_ptr->cu.bit_mask = bit_mask;
   me_ptr->cu.all_ext_in_mask |= bit_mask;

   void *q_mem_ptr = GEN_CNTR_GET_EXT_IN_PORT_Q_ADDR(gu_ext_port_ptr);

   if (check_if_pass_thru_container(me_ptr))
   {
      q_mem_ptr = PT_CNTR_GET_EXT_IN_PORT_Q_ADDR(gu_ext_port_ptr);
   }

   result = cu_init_queue(&me_ptr->cu,
                          data_q_name,
                          max_num_elements,
                          bit_mask,
                          q_handler,
                          me_ptr->cu.channel_ptr,
                          &gu_ext_port_ptr->this_handle.q_ptr,
                          q_mem_ptr,
                          gu_get_downgraded_heap_id(me_ptr->topo.heap_id, gu_ext_port_ptr->upstream_handle.heap_id));

   return result;
}

/*
 * Init an external output port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
static ar_result_t gen_cntr_init_ext_out_queue(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t              result       = AR_EOK;
   gen_cntr_t *             me_ptr       = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_port_ptr = (gen_cntr_ext_out_port_t *)gu_ext_port_ptr;
   cu_queue_handler_t       q_handler;
   uint32_t                 bit_mask = 0;
   char                     data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "B", "GEN_CNTR", me_ptr->topo.gu.log_id);

   uint32_t max_num_elements = CU_MAX_OUT_BUF_Q_ELEMENTS;

   // for buffer driven mode we add the data queues to the container channel and mask
   bit_mask  = cu_request_bit_in_bit_mask(&me_ptr->cu.available_bit_mask);
   q_handler = gen_cntr_output_bufQ_trigger;

   if (0 == bit_mask)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Bit mask has no bits available 0x%lx",
                   me_ptr->cu.available_bit_mask);
      return result;
   }

   ext_port_ptr->cu.bit_mask = bit_mask;
   me_ptr->cu.all_ext_out_mask |= bit_mask;

   void *q_mem_ptr = GEN_CNTR_GET_EXT_OUT_PORT_Q_ADDR(gu_ext_port_ptr);

   if (check_if_pass_thru_container(me_ptr))
   {
      q_mem_ptr = PT_CNTR_GET_EXT_OUT_PORT_Q_ADDR(gu_ext_port_ptr);
   }

   result = cu_init_queue(&me_ptr->cu,
                          data_q_name,
                          max_num_elements,
                          bit_mask,
                          q_handler,
                          me_ptr->cu.channel_ptr,
                          &gu_ext_port_ptr->this_handle.q_ptr,
                          q_mem_ptr,
                          gu_get_downgraded_heap_id(me_ptr->topo.heap_id, gu_ext_port_ptr->downstream_handle.heap_id));

   return result;
}

/*
 * Initialize an external input port.
 */
ar_result_t gen_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_cntr_t *            me_ptr       = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_in_port_t *ext_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_port_ptr;
   ext_port_ptr->cu.id                  = cu_get_next_unique_id(&(me_ptr->cu));

   ext_port_ptr->gu.this_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;
   ext_port_ptr->client_config.cid.client_id   = CLIENT_ID_HLOS;
   ext_port_ptr->client_config.cid.gpr_port_id = 0; // setting default to invalid for HLOS

   switch (gu_ext_port_ptr->int_in_port_ptr->cmn.module_ptr->module_id)
   {
      case MODULE_ID_WR_SHARED_MEM_EP:
      {
         gen_cntr_init_gpr_client_ext_in_port(me_ptr, ext_port_ptr);
         break;
      }
      default: // for all other modules, it's considered peer cntr
      {
         gen_cntr_init_peer_cntr_ext_in_port(me_ptr, ext_port_ptr);
         break;
      }
   }

   result = gen_cntr_init_ext_in_queue(base_ptr, gu_ext_port_ptr);
   return result;
}

/*
 * Initialize an external output port.
 */
ar_result_t gen_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t              result           = AR_EOK;
   gen_cntr_t *             me_ptr           = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_ext_port_ptr;

   ext_out_port_ptr->gu.this_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;
   ext_out_port_ptr->client_config.cid.client_id   = CLIENT_ID_HLOS;
   ext_out_port_ptr->client_config.cid.gpr_port_id = 0; // setting to invalid value for HLOS

   switch (gu_ext_port_ptr->int_out_port_ptr->cmn.module_ptr->module_id)
   {
      case MODULE_ID_RD_SHARED_MEM_EP:
      {
         gen_cntr_init_gpr_client_ext_out_port(me_ptr, ext_out_port_ptr);
         break;
      }
      default: // for all other modules, it's considered peer cntr
      {
         gen_cntr_init_peer_cntr_ext_out_port(me_ptr, ext_out_port_ptr);
         break;
      }
   }

   // Initialize external output data queues and handler
   result = gen_cntr_init_ext_out_queue(base_ptr, gu_ext_port_ptr);
   return result;
}

/*
 * Deinitialize an external input port.
 */
ar_result_t gen_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_t *            me_ptr          = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_port_ptr;

   if (SPF_RAW_COMPRESSED == ext_in_port_ptr->cu.media_fmt.data_format)
   {
      tu_capi_destroy_raw_compr_med_fmt(&ext_in_port_ptr->cu.media_fmt.raw);
   }

   if (ext_in_port_ptr->gu.this_handle.q_ptr)
   {
      gen_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, FALSE /* keep data msg */);
   }

   gen_cntr_ext_in_port_reset(me_ptr, ext_in_port_ptr);

   result |= gen_cntr_deinit_ext_port_queue(me_ptr, &ext_in_port_ptr->gu.this_handle, ext_in_port_ptr->cu.bit_mask);

   MFREE_NULLIFY(ext_in_port_ptr->bufs_ptr);
   ext_in_port_ptr->bufs_num = 0;

   // Destroy the internal port.
   result |= gen_topo_destroy_input_port(&me_ptr->topo, (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);

   MFREE_NULLIFY(ext_in_port_ptr->buf.md_buf_ptr);

   // invalidate the association with internal port, so that dangling link can be destroyed first
   gu_deinit_ext_in_port(&ext_in_port_ptr->gu);

   return result;
}

/*
 * Deinitialize an external output port.
 */
ar_result_t gen_cntr_deinit_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t              result           = AR_EOK;
   gen_cntr_t *             me_ptr           = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_ext_port_ptr;
   gen_topo_output_port_t * int_out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (ext_out_port_ptr->cu.num_buf_allocated)
   {
      gen_cntr_flush_output_data_queue(me_ptr, ext_out_port_ptr, FALSE); // not a client cmd.
      // Destroy all the allocated buffers. num_buf_allocated = 0 for DSP_CLIENT
      while (0 < ext_out_port_ptr->cu.num_buf_allocated)
      {
         // no need to wait on the buffer queue, all the buffers should have been returned by now.
         // anyway two threads can not wait on the same channel ptr therefore channel_wait can not be done in the
         // command handling context which may be running in worker thread
         //(void)posal_channel_wait(me_ptr->cu.channel_ptr, ext_out_port_ptr->cu.bit_mask);

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      "external output port idx = %ld, miid = 0x%lx is being closed, destroying external buffer %lu.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_out_port_ptr->cu.num_buf_allocated);

         gen_cntr_destroy_ext_buffers(me_ptr, ext_out_port_ptr, 0);
      }
   }
   else
   {
      gen_cntr_destroy_ext_buffers(me_ptr, ext_out_port_ptr, 0);
   }

   gen_cntr_ext_out_port_reset(me_ptr, ext_out_port_ptr);

   result |= gen_cntr_deinit_ext_port_queue(me_ptr, &ext_out_port_ptr->gu.this_handle, ext_out_port_ptr->cu.bit_mask);

   // Destroy the internal port.
   if (int_out_port_ptr->gu.attached_module_ptr)
   {
      gen_topo_module_t *     attached_module_ptr = (gen_topo_module_t *)int_out_port_ptr->gu.attached_module_ptr;
      gen_topo_output_port_t *attached_out_port_ptr =
         (gen_topo_output_port_t *)(attached_module_ptr->gu.output_port_list_ptr
                                       ? attached_module_ptr->gu.output_port_list_ptr->op_port_ptr
                                       : NULL);

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "external output port idx = %ld, miid = 0x%lx is being closed, destroying tap module output port "
                   "%ld, miid 0x%lx's internal port structure.",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   attached_out_port_ptr->gu.cmn.index,
                   attached_out_port_ptr->gu.cmn.module_ptr->module_instance_id);
#endif
      gen_topo_destroy_output_port(&me_ptr->topo, attached_out_port_ptr);
   }
   else
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "external output port idx = %ld, miid = 0x%lx is being closed, destroying internal port structure.",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

      result |= gen_topo_destroy_output_port(&me_ptr->topo, int_out_port_ptr);
   }

   if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
   {
      tu_capi_destroy_raw_compr_med_fmt(&ext_out_port_ptr->cu.media_fmt.raw);
   }

   if (ext_out_port_ptr->buf.md_buf_ptr)
   {
      MFREE_NULLIFY(ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr);
      MFREE_NULLIFY(ext_out_port_ptr->buf.md_buf_ptr);
   }

   // invalidate the association with internal port, so that dangling link can be destroyed first
   gu_deinit_ext_out_port(&ext_out_port_ptr->gu);

   return result;
}

ar_result_t find_gcd(uint32_t a, uint32_t b, uint32_t *gcd_ptr)
{
   ar_result_t result = AR_EOK;
   *gcd_ptr           = 0;

   uint32_t t;
   while (b)
   {
      t = b;
      b = a % b;
      a = t;
   }

   *gcd_ptr = a;

   return result;
}

ar_result_t find_lcm(uint32_t a, uint32_t b, uint32_t *lcm_ptr)
{
   uint32_t gcd = 0;
   *lcm_ptr     = 0;

   ar_result_t result = find_gcd(a, b, &gcd);
   if (AR_DID_FAIL(result) || (0 == gcd))
   {
      return result;
   }

   uint64_t ab = a * (uint64_t)b;

   uint64_t lcm = ab / gcd;

   if (lcm > UINT32_MAX)
   {
      return AR_EFAILED;
   }

   *lcm_ptr = (uint32_t)lcm;

   return AR_EOK;
}

void gen_cntr_check_and_send_prebuffers_util_(gen_cntr_t *             me_ptr,
                                              gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                              spf_msg_data_buffer_t *  out_buf_ptr)
{
   // Exit and handle prebuffers only if port has requirement
   if (cu_check_if_port_requires_prebuffers(&ext_out_port_ptr->cu))
   {
      gen_topo_exit_island_temporarily(&me_ptr->topo);
      cu_handle_prebuffer(&me_ptr->cu,
                          &ext_out_port_ptr->gu,
                          out_buf_ptr,
                          ext_out_port_ptr->cu.buf_max_size -
                             (gen_topo_compute_if_output_needs_addtional_bytes_for_dm(&(me_ptr->topo),
                                                                                      (gen_topo_output_port_t *)
                                                                                         ext_out_port_ptr->gu
                                                                                            .int_out_port_ptr)));
   }
   else
   {
      // if port didnt have prebuf requirement at data flow start, then we can mark sent= TRUE,
      // If prebuf requriement changes after data flow start, no need to insert prebuffer since its going cause
      // a glitch, if we need to handle scenario there we need to consider if media format changed
      ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = TRUE;
   }
   return;
}