/* ======================================================================== */
/**
   @file capi_sal_island.cpp

   Source file to implement the CAPI Interface for
   Simple Accumulator-Limiter (SAL) Module.
*/

/* =========================================================================
   Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
   SPDX-License-Identifier: BSD-3-Clause
   ========================================================================== */

/*==========================================================================
Include files
========================================================================== */
#include "capi_sal.h"
#include "capi_sal_utils.h"
#include "sal_api.h"
#include "spf_list_utils.h"
#include "audio_basic_op.h"

static capi_vtbl_t vtbl = { capi_sal_process,        capi_sal_end,           capi_sal_set_param, capi_sal_get_param,
                            capi_sal_set_properties, capi_sal_get_properties };

/*Processes data in the input stream to do accumulations and saturations */
static capi_err_t capi_sal_process_input_to_scratch(capi_sal_t *        me_ptr,
                                                    capi_stream_data_t *input[],
                                                    uint32_t            input_qf,
                                                    uint32_t            port_idx,
                                                    uint32_t            in_num_samples_per_ch,
                                                    bool_t              first_port);
/*Processes data in scratch to do conversions, limiting, etc - Returns a boolean indicating whether we should return
 * early*/
static bool_t capi_sal_process_scratch_to_output(capi_sal_t *        me_ptr,
                                                 capi_stream_data_t *output[],
                                                 uint32_t            input_qf,
                                                 uint32_t            output_qf,
                                                 uint32_t            max_num_samples_per_ch,
                                                 capi_err_t *        err_code_ptr);

//////////////////////////////////////////////process_utils.cpp//////////////////////////////////////////////////
/*Function to accumulate all 16 bit samples on the input channel and store as 32 bit samples on output buf*/
static void accumulate_bw_16_samples(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch);
/*Function to accumulate all 16 bit samples on the input channel and store as 32 bit samples on output buf with 16 bit
 * sat*/
static void accumulate_bw_16_samples_sat(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch);
/*Function to accumulate all 32 bit Q27 samples on the input channel and store as 32 bit samples on output buf with
 * Q27sat*/
static void accumulate_bw_32_samples_q27_sat(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch);
/*Function to accumulate all 32 bit Q27 samples on the input channel and store as 32 bit samples on output buf*/
static void accumulate_bw_32_samples_no_sat(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch);
/*Function to accumulate all 32 bit Q31 samples on the input channel and store as 32 bit samples on output buf
by saturating them*/
static void accumulate_bw_32_samples_with_sat(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch);
/*Function to convert 32B Q16 or Q27 samples to Q31 or Q27 inplace*/
static void upconvert_ws_32(int8_t *input_ch_buf, uint16_t shift_factor, uint32_t num_samp_per_ch);
/*Function to convert 32B Q15 samples to Q27 or Q31 inplace by expansion or shifts*/
static void upconvert_ws_16_to_32_inplace(int8_t *input_ch_buf, uint16_t shift, uint32_t num_samp_per_ch);

capi_vtbl_t *capi_sal_get_vtbl()
{
   return &vtbl;
}

static capi_err_t capi_sal_process_inplace(capi_sal_t *        me_ptr,
                                           capi_stream_data_t *input[],
                                           capi_stream_data_t *output[],
                                           int32_t             input_port_index,
                                           bool_t              input_has_md_n_flags)
{
   uint32_t input_word_size_bytes = me_ptr->operating_mf_ptr->format.bits_per_sample >> 3;
   uint32_t count                 = 0;
   // Check for MD and EOF/erasure flags explicitly
   if (input_has_md_n_flags)
   {
      capi_sal_handle_metadata_b4_process(me_ptr, input, output);
   }

   for (uint32_t i = 0; i < input[input_port_index]->bufs_num; i++)
   {
      /* in place module */
      if (input[input_port_index]->buf_ptr[i].data_ptr != output[0]->buf_ptr[i].data_ptr)
      {
	     count++;
         
         memscpy(output[0]->buf_ptr[i].data_ptr,
                 output[0]->buf_ptr[0].max_data_len,
                 input[input_port_index]->buf_ptr[i].data_ptr,
                 input[input_port_index]->buf_ptr[0].actual_data_len);
      }

      // AR_MSG(DBG_HIGH_PRIO,
      //        "CAPI SAL: out buf actual data length: %d ",
      //        output[0]->buf_ptr[i].actual_data_len);
   }
   
   if(0 < count)
   {
      AR_MSG_ISLAND(DBG_LOW_PRIO, "SAL: 0x%lx: input and output data buffers data ptrs are not same for %lu times with total number of channels %lu!", 
                         me_ptr->iid,
                         count,
						 me_ptr->operating_mf_ptr->format.num_channels);
   }

   output[0]->buf_ptr[0].actual_data_len =
      SAL_MIN(input[input_port_index]->buf_ptr[0].actual_data_len, output[0]->buf_ptr[0].max_data_len);

   input[input_port_index]->buf_ptr[0].actual_data_len = output[0]->buf_ptr[0].actual_data_len;

   if (input_has_md_n_flags)
   {
      uint32_t inp_samples_per_ch    = input[input_port_index]->buf_ptr[0].actual_data_len / input_word_size_bytes;
      uint32_t ONLY_ONE_ACTIVE_INPUT = 1; // inplace works only in this case
#ifdef USES_AUDIO_IN_ISLAND
      posal_island_trigger_island_exit();
#else
      posal_island_trigger_island_exit_inline();
#endif // USES_AUDIO_IN_ISLAND
      capi_sal_process_metadata_after_process(me_ptr,
                                              input,
                                              output,
                                              inp_samples_per_ch,
                                              input_word_size_bytes,
                                              ONLY_ONE_ACTIVE_INPUT);
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------
  Function name: capi_sal_process
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_sal_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   POSAL_ASSERT(_pif);
   POSAL_ASSERT(input[0]);

   capi_err_t  capi_result             = CAPI_EOK;
   capi_sal_t *me_ptr                  = (capi_sal_t *)_pif;
   bool_t      raise_mf_at_start       = FALSE;
   bool_t      any_port_has_md_n_flags = FALSE;
   uint32_t    port_index              = 0; // port index being processed

   // TODO: ideally when num_active_in_ports ==1, we need to propagate these flags. Changes are needed in
   // capi_sal_process_metadata_after_process as well.
   if (me_ptr->module_flags.raise_mf_on_next_process)
   {
      bool_t mf_raised = FALSE;
      SAL_MSG_ISLAND(me_ptr->iid,
                     DBG_MED_PRIO,
                     "Raising output media format on this process call due to flag set and returning");
      // raise out mf
      capi_sal_raise_out_mf(me_ptr, me_ptr->operating_mf_ptr, &mf_raised);

      if (mf_raised)
      {
         // will process on next cycle, mark all input as un-consumed and return
         for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
         {
            if ((NULL != input[i]) && (NULL != input[i]->buf_ptr))
            {
               // for unpacked v2 need to update only 1st ch
               input[i]->buf_ptr[0].actual_data_len = 0;
            }
         }
         return CAPI_EOK;
      }
   }

   int32_t active_input_port_index = -1;
   for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
   {
      // initialize the proc_check_pass to false
      me_ptr->in_port_arr[i].port_flags.proc_check_pass = FALSE;

      if (DATA_PORT_CLOSED == me_ptr->in_port_arr[i].state)
      {
         continue;
      }

      if ((NULL == input[i]) || (NULL == input[i]->buf_ptr))
      {
         continue;
      }

      capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[i];
      if (in_stream_ptr->metadata_list_ptr || me_ptr->in_port_arr[i].md_list_ptr || in_stream_ptr->flags.end_of_frame ||
          in_stream_ptr->flags.erasure)
      {
         any_port_has_md_n_flags = TRUE;
      }

      if (((NULL != input[i]->buf_ptr[0].data_ptr) && (0 != input[i]->buf_ptr[0].actual_data_len)) &&
          me_ptr->in_port_arr[i].port_flags.at_gap)
      {
         // if we have data, reset the flag that indicates EOS was propagated
         me_ptr->in_port_arr[i].port_flags.at_gap = FALSE;
         me_ptr->num_ports_at_gap--;

         // here - a port went from at gap to data_flow. Now if all were at gap before this - we need to force this
         // port as the reference port.
#ifdef USES_AUDIO_IN_ISLAND
         posal_island_trigger_island_exit();
#else
         posal_island_trigger_island_exit_inline();
#endif // USES_AUDIO_IN_ISLAND
         if (me_ptr->module_flags.all_ports_at_gap && me_ptr->in_port_arr[i].port_flags.mf_rcvd)
         {
            SAL_MSG_ISLAND(me_ptr->iid, DBG_HIGH_PRIO, "Forcing port idx %lu as ref port", i);
            raise_mf_at_start |= capi_sal_handle_data_flow_start_force_ref(me_ptr, i);
            me_ptr->module_flags.all_ports_at_gap = FALSE;
         }
         else
         {
            // any port that was at gap and now not at gap needs a chance to re-evaluate it's media format - This is
            // equivalent to a port start operation
            SAL_MSG_ISLAND(me_ptr->iid, DBG_MED_PRIO, "Port index %lu was at gap - handling data flow start", i);
            raise_mf_at_start |= capi_sal_handle_data_flow_start(me_ptr, i);
         }
      }
      if (DATA_PORT_STARTED == me_ptr->in_port_arr[i].state)
      {
         me_ptr->in_port_arr[i].port_flags.proc_check_pass = TRUE;
         // input[i] + input[i]->buf_ptr non-null and port started

         // used for inplace case, for inplace it will be written only once.
         active_input_port_index = i;
      }
   }

   // if it already raise mf, will process on next cycle, mark all input as un-consumed and return
   if (raise_mf_at_start)
   {
      SAL_MSG_ISLAND(me_ptr->iid, DBG_MED_PRIO, "Raising mf at start will process on next cycle");
      for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
      {
         if ((NULL != input[i]) && (NULL != input[i]->buf_ptr))
         {
            // update only 1st buf for unpacked v2
            input[i]->buf_ptr[0].actual_data_len = 0;
         }
      }
      return CAPI_EOK;
   }

   if (me_ptr->module_flags.insert_int_eos)
   {
      capi_sal_insert_eos_for_us_gap(me_ptr, output);
   }

   if ((!me_ptr->operating_mf_ptr))
   {
      SAL_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "input omf not set yet");

      // In case EOS with 0 data is sent to SAL before any data flow started (thus omf not set),
      // we don't know how to handle. We can only destroy.
      capi_sal_destroy_all_md(me_ptr, input);

      return CAPI_EOK;
   }

   //  If inplace process and return here itself.
   if (me_ptr->module_flags.is_inplace)
   {
      if (active_input_port_index < 0 || !output[0]->buf_ptr)
      {
         SAL_MSG_ISLAND(me_ptr->iid,
                        DBG_ERROR_PRIO,
                        "CAPI SAL: Failed to operate in inplace mode eventhough marked inplace. "
                        "active_in_port_index: %ld",
                        active_input_port_index);
         return CAPI_EFAILED;
      }
      else
      {
         return capi_sal_process_inplace(me_ptr, input, output, active_input_port_index, any_port_has_md_n_flags);
      }
   }

   capi_err_t err_code               = CAPI_EOK;
   bool_t     EARLY_RETURN_TRUE      = TRUE;
   uint32_t   input_qf               = me_ptr->operating_mf_ptr->format.q_factor;
   uint32_t   output_qf              = me_ptr->out_port_cache_cfg.q_factor;
   uint32_t   in_num_samples_per_ch  = 0;
   uint32_t   max_num_samples_per_ch = 0;
   uint32_t   new_max_data_len       = 0;
   uint32_t   max_data_len           = me_ptr->ref_acc_out_buf_len;
   uint32_t   output_word_size_bytes = me_ptr->out_port_cache_cfg.word_size_bytes;
   uint32_t   input_word_size_bytes  = me_ptr->operating_mf_ptr->format.bits_per_sample >> 3;
   uint32_t   out_max_samples_per_ch = capi_cmn_div_num(output[0]->buf_ptr[0].max_data_len, output_word_size_bytes);
   // When EOSes come on all inputs, EOS still goes out as flushing.
   // last EOS on the output must be flushing, rest non-flushing. test sal_super_3 (2 EOSes with same offset in
   // GEN_CNTR)

   if (any_port_has_md_n_flags)
   {
      capi_result = capi_sal_handle_metadata_b4_process(me_ptr, input, output);
   }

   // Consider a case where capiv2 input max data length changes across process calls.
   // For 16 bit case alone, the calculated scratch buffer length is over-allocated to
   // process the data as 32 bit samples. As a result, the comparison of incoming data
   // length vs the current scratch buffer length needs to be in terms of samples
   // Hence, scaling down the max_data_len value by 2 and check for reallocation
   // based on incoming data length.
   if (BIT_WIDTH_16 == me_ptr->operating_mf_ptr->format.bits_per_sample)
   {
      max_data_len >>= 1; // div by 2
      /* acc buf is over-allocated to hold 32 bit samples. So scale down max length accordingly */
   }

   uint32_t num_active_inputs_to_acc_without_eos = 0;

   for (uint32_t i = 0; i < me_ptr->num_in_ports_started; i++)
   {
      port_index = me_ptr->started_in_port_index_arr[i];
      if (FALSE == me_ptr->in_port_arr[port_index].port_flags.proc_check_pass)
      {
         continue;
      }

      // Cache original input length.
      me_ptr->in_port_arr[port_index].proc_ctx_prev_actual_data_len = input[port_index]->buf_ptr[0].actual_data_len;

      if (me_ptr->in_port_arr[port_index].port_flags.data_drop)
      {

#ifdef SAL_DBG_LOW
         SAL_MSG_ISLAND(me_ptr->iid, DBG_MED_PRIO, "Dropping data on port idx %lu", port_index);
#endif
         continue;
      }

      /* Increment counter for num active inputs sent to accumulator. This does not include the active ports which are
      dropping data due to mf mismatch, and does not include input streams with eos marker.
      There could be ports which are in DATA_PORT_STARTED state but AT_GAP wrt data flow state, these ports should
      not be considered active ports.
      This count is used to determine zero pushing behavior for eos */
      capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[port_index];
      if ((FALSE == me_ptr->in_port_arr[port_index].port_flags.at_gap) && (FALSE == in_stream_ptr->flags.marker_eos))
      {
         num_active_inputs_to_acc_without_eos++;
      }
      else if(in_stream_ptr->flags.marker_eos)
      {
          //flushing zeros will be handled in the scractch buffer if input is empty.
          for (uint32_t j = 0; j < me_ptr->operating_mf_ptr->format.num_channels; j++)
          {
             memset(me_ptr->acc_out_scratch_arr[j].data_ptr, 0, me_ptr->acc_out_scratch_arr[j].max_data_len);
          }
      }

      // If number of samples per channel in capiv2 input > allocated max number of samples
      new_max_data_len = SAL_MAX(new_max_data_len, input[port_index]->buf_ptr[0].max_data_len);

      uint32_t inp_samples_per_ch =
         capi_cmn_div_num(input[port_index]->buf_ptr[0].actual_data_len, input_word_size_bytes);

      max_num_samples_per_ch = SAL_MAX(max_num_samples_per_ch, inp_samples_per_ch);
   }

   // amount of input to be consumed is limited by output space
   max_num_samples_per_ch = SAL_MIN(max_num_samples_per_ch, out_max_samples_per_ch);

   // need to reallocate scratch buffer if the calculated new_max_data_len in this process call
   // is greater than the size of the scratch buffer allocated already.
   if (new_max_data_len > max_data_len)
   {
      max_data_len = new_max_data_len;

      if (BIT_WIDTH_16 == me_ptr->operating_mf_ptr->format.bits_per_sample)
      {
         max_data_len <<= 1; // mul by 2
         /* acc buf should hold 32 bit samples. If the max input bw is 2 bytes, the acc buff should
            be large enough to hold 32 bit samples. */
      }
#ifdef USES_AUDIO_IN_ISLAND
      posal_island_trigger_island_exit();
#else
      posal_island_trigger_island_exit_inline();
#endif // USES_AUDIO_IN_ISLAND
      capi_result = capi_sal_bw_conv_allocate_scratch_ptr_buf(me_ptr, max_data_len);
      if (CAPI_EOK != capi_result)
      {
         SAL_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "Failed to reallocate acc out scratch buf, bailing out");
         return CAPI_EFAILED;
      }
      me_ptr->ref_acc_out_buf_len = max_data_len;
   }

   /* Loop and accumulate input data */
   bool_t first_port = TRUE;
   for (uint32_t index = 0; index < me_ptr->num_in_ports_started; index++)
   {
      port_index = me_ptr->started_in_port_index_arr[index];
      if ((FALSE == me_ptr->in_port_arr[port_index].port_flags.proc_check_pass) ||
          (me_ptr->in_port_arr[port_index].port_flags.data_drop))
      {
         // means that the port is inactive, move on
         continue;
      }
      // mark the port as processed
      me_ptr->in_port_arr[port_index].port_flags.is_algo_proc = TRUE;
      // all buffers assumed to have same data len on one port
      in_num_samples_per_ch = capi_cmn_div_num(input[port_index]->buf_ptr[0].actual_data_len, input_word_size_bytes);

      capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[port_index];

      // for flushing EOS, always zero flush. Even if there are other inputs it's ok to
      // flush zeros because adding zeros to nonzero doesn't change nonzero data.
      //    Always flushing zeros keeps it simple. If zero flushing is done conditionally based on active inputs,
      //    then transitions (new input joins while EOS is stuck inside) are difficult to handle.
      capi_sal_check_push_zeros_for_flushing_eos(me_ptr,
                                                 in_stream_ptr,
                                                 &me_ptr->in_port_arr[port_index],
                                                 &in_num_samples_per_ch,
                                                 &max_num_samples_per_ch,
                                                 out_max_samples_per_ch,
                                                 num_active_inputs_to_acc_without_eos);

      // metadata must be processed if no buf is given
      if ((NULL == input[port_index]->buf_ptr[0].data_ptr) || (0 == input[port_index]->buf_ptr[0].actual_data_len))
      {
         //in case flushing eos is present then zeros in output scratch buffer will ensure flushing.
         continue;
      }

      // no mixing is done when flag is set, unless it is the unmixed channel
      if (me_ptr->module_flags.unmixed_output && (port_index != me_ptr->unmixed_output_port_index))
      {
         continue;
      }
      /*Processes data in the input stream to do accumulations and saturations */
      capi_result |=
         capi_sal_process_input_to_scratch(me_ptr, input, input_qf, port_index, in_num_samples_per_ch, first_port);
      first_port = FALSE;
   } // for port

   output[0]->buf_ptr[0].actual_data_len = max_num_samples_per_ch * output_word_size_bytes;

   /* DTMF Stuff: output is copied from single input port if unmixed output flag is set */
   if (me_ptr->module_flags.unmixed_output && !capi_sal_check_limiting_required(me_ptr))
   {
#ifdef USES_AUDIO_IN_ISLAND
      posal_island_trigger_island_exit();
#else
      posal_island_trigger_island_exit_inline();
#endif // USES_AUDIO_IN_ISLAND
      capi_sal_dtmf(me_ptr, input, output, any_port_has_md_n_flags, max_num_samples_per_ch, input_word_size_bytes);
      return CAPI_EOK;
   }

   /* Update limiter bypass mode */
   if (capi_sal_check_limiting_required(me_ptr))
   {
      for (uint32_t i = 0; i < me_ptr->operating_mf_ptr->format.num_channels; i++)
      {
         me_ptr->lim_in_ptr[i]  = (int32_t *)me_ptr->acc_out_scratch_arr[i].data_ptr;
         me_ptr->lim_out_ptr[i] = (int32_t *)output[0]->buf_ptr[i].data_ptr;
      }
   }

   /* Processes data in scratch to do conversions, limiting, etc - Returns a boolean indicating whether we should
    * return early*/
   if (EARLY_RETURN_TRUE ==
       capi_sal_process_scratch_to_output(me_ptr, output, input_qf, output_qf, max_num_samples_per_ch, &err_code))
   {
      return err_code;
   }

   // For all ports that have (data_drop == TRUE), we need to update the actual_data_len indicating how much
   // data we are dropping/consuming. This should be equal to the amout of data we are consuming on the ref port
   // max_num_samples_per_ch * input_word_size_bytes -> consumed bytes/ch
   for (uint32_t i = 0; i < me_ptr->num_in_ports_started; i++)
   {
      port_index = me_ptr->started_in_port_index_arr[i];
      if ((FALSE == me_ptr->in_port_arr[port_index].port_flags.proc_check_pass) ||
          (FALSE == me_ptr->in_port_arr[port_index].port_flags.mf_rcvd))
      {
         continue;
      }

      if (me_ptr->in_port_arr[port_index].port_flags.data_drop)
      {
         // dont modify input length, if its same it means data is dropped
      }
      else
      {
         input[port_index]->buf_ptr[0].actual_data_len =
            SAL_MIN((max_num_samples_per_ch * input_word_size_bytes),
                    me_ptr->in_port_arr[port_index].proc_ctx_prev_actual_data_len);
      }

#ifdef SAL_DBG_LOW
      SAL_MSG(me_ptr->iid,
              DBG_MED_PRIO,
              "Consumed %lu bytes on input port %lu, channel %lu",
              input[port_index]->buf_ptr[0].actual_data_len,
              port_index,
              j);
#endif
   }

   // Note: num_active_in_ports in this call is not affected by the EOS propagation done in this process call
   // (capi_sal_check_process_metadata).
   // however, if there's data coming in this process call, then it affects (follow the flag: at_gap)
   // This preferential treatment towards marking EOS as flushing helps in making sure EOS always propagated
   // (flushing = zero pushing)
   capi_sal_process_metadata_handler(any_port_has_md_n_flags,
                                     me_ptr,
                                     input,
                                     output,
                                     max_num_samples_per_ch,
                                     input_word_size_bytes);

   return capi_result;
}

////////////////////////////////// Library wrappers //////////////////////////////////

static capi_err_t capi_sal_process_input_to_scratch(capi_sal_t *        me_ptr,
                                                    capi_stream_data_t *input[],
                                                    uint32_t            input_qf,
                                                    uint32_t            port_idx,
                                                    uint32_t            in_num_samples_per_ch,
                                                    bool_t              first_port)
{
   for (uint32_t j = 0; j < me_ptr->operating_mf_ptr->format.num_channels; j++)
   {
      int8_t *in_ptr = input[port_idx]->buf_ptr[j].data_ptr;

      if (first_port)
      {
         memscpy(me_ptr->acc_out_scratch_arr[j].data_ptr,
                 me_ptr->acc_out_scratch_arr[j].max_data_len,
                 in_ptr,
                 input[port_idx]->buf_ptr[0].actual_data_len);

         // now, we need to upconvert to q31 for the scratch buffer
         if (me_ptr->input_process_info.upconvert_flag)
         {
            upconvert_ws_16_to_32_inplace(me_ptr->acc_out_scratch_arr[j].data_ptr, 0, in_num_samples_per_ch);
         }
      }
      else
      {
#if __qdsp6__
         // copying only if it is not aligned
         if (0 != ((uintptr_t)input[port_idx]->buf_ptr[j].data_ptr & me_ptr->input_process_info.alignment))
         {
            memscpy(me_ptr->acc_in_scratch_buf.data_ptr,
                    me_ptr->acc_in_scratch_buf.max_data_len,
                    in_ptr,
                    input[port_idx]->buf_ptr[0].actual_data_len);
            in_ptr = me_ptr->acc_in_scratch_buf.data_ptr;
         }
#endif
         me_ptr->input_process_info.accumulate_func_ptr(in_ptr,
                                                        me_ptr->acc_out_scratch_arr[j].data_ptr,
                                                        in_num_samples_per_ch);
      }
   }

   return CAPI_EOK;
}

capi_err_t capi_sal_lim_loop_process(capi_sal_t *me_ptr, uint32_t max_num_samples_per_ch, capi_stream_data_t *output[])
{
   uint32_t num      = max_num_samples_per_ch;
   uint32_t den      = me_ptr->limiter_static_vars.max_block_size;
   uint32_t q        = capi_cmn_div_num(num, den);
   uint32_t rem      = num - (den * q);
   uint32_t lim_samp = (num > den) ? den : num; // just one loop in this case
   uint32_t count    = 0;
#ifdef SAL_DBG_LOW
   SAL_MSG(me_ptr->iid, DBG_ERROR_PRIO, "num = %lu, den = %lu, q = %lu, lim_samp %lu", num, den, q, lim_samp);
#endif

   while ((count <= q) && (lim_samp))
   {
      if (LIMITER_SUCCESS !=
          limiter_process(&me_ptr->lib_mem, (void **)me_ptr->lim_out_ptr, me_ptr->lim_in_ptr, lim_samp))
      {
         SAL_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "limiter_process failed!");
         return CAPI_EFAILED;
      }
      count++;
      if (count > q)
      {
         break;
      }
      // advance pointers
      for (uint32_t i = 0; i < me_ptr->operating_mf_ptr->format.num_channels; i++)
      {
         me_ptr->lim_in_ptr[i] += lim_samp;
         me_ptr->lim_out_ptr[i] =
            (int32_t *)((int8_t *)(me_ptr->lim_out_ptr[i]) + (me_ptr->out_port_cache_cfg.word_size_bytes * lim_samp));
      }
      if (count == q)
      {
         lim_samp = rem; // last run
      }
   }
   return CAPI_EOK;
}

static bool_t capi_sal_process_scratch_to_output(capi_sal_t *        me_ptr,
                                                 capi_stream_data_t *output[],
                                                 uint32_t            input_qf,
                                                 uint32_t            output_qf,
                                                 uint32_t            max_num_samples_per_ch,
                                                 capi_err_t *        err_code_ptr)
{
   bool_t early_return = FALSE; // indicates if the caller should return
   *err_code_ptr       = CAPI_EOK;

   if (input_qf <= output_qf)
   {
      // for 24b and 16b inputs, we need to call the limiter process
      if (capi_sal_check_limiting_required(me_ptr))
      {
         *err_code_ptr = capi_sal_lim_loop_process(me_ptr, max_num_samples_per_ch, output);
         if (AR_EOK != *err_code_ptr)
         {
            early_return = TRUE;
            return early_return;
         }
      }
      else // lim disabled or not reqd, either way
      {
         for (uint32_t i = 0; i < me_ptr->operating_mf_ptr->format.num_channels; i++)
         {
            memscpy(output[0]->buf_ptr[i].data_ptr,
                    output[0]->buf_ptr[0].actual_data_len,
                    me_ptr->acc_out_scratch_arr[i].data_ptr,
                    output[0]->buf_ptr[0].actual_data_len);
         }
      }
      if ((QF_BPS_32 == output_qf) && (QF_BPS_32 != input_qf))
      {
         for (uint32_t i = 0; i < me_ptr->operating_mf_ptr->format.num_channels; i++)
         {
            // now, we need to upconvert to q31
            if (QF_BPS_16 == input_qf)
            {
               upconvert_ws_16_to_32_inplace(output[0]->buf_ptr[i].data_ptr,
                                             QF_BPS_32 - QF_BPS_16 /* QF_CNV_16 */,
                                             max_num_samples_per_ch);
            }
            else // 24 = input bps
            {
               upconvert_ws_32(output[0]->buf_ptr[i].data_ptr,
                               QF_BPS_32 - QF_BPS_24 /* QF_CNV_4 */,
                               max_num_samples_per_ch);
            }
         }
      }
      if ((QF_BPS_24 == output_qf) && (QF_BPS_24 != input_qf))
      {
         // Q15 to Q27
         for (uint32_t i = 0; i < me_ptr->operating_mf_ptr->format.num_channels; i++)
         {
            upconvert_ws_16_to_32_inplace(output[0]->buf_ptr[i].data_ptr,
                                          QF_BPS_24 - QF_BPS_16 /* QF_CNV_12 */,
                                          max_num_samples_per_ch);
         }
      }
      // if output is bps16, input can't be lesser - so no conversion needed
   }
   else // input_qf > output_qf
   {
      // SAL is required in LPI because of VOLTE LPI use case. For this use case, voice path will always have 16 bit
      // and EP can be higher so we will not hit this case & hence this else case doesn't have to be in island.
#ifdef USES_AUDIO_IN_ISLAND
      posal_island_trigger_island_exit();
#else
      posal_island_trigger_island_exit_inline();
#endif // USES_AUDIO_IN_ISLAND
      early_return =
         capi_sal_inqf_greater_than_outqf(me_ptr, input_qf, output_qf, output, max_num_samples_per_ch, err_code_ptr);
   }
   return early_return;
}

capi_err_t capi_sal_set_input_process_info(capi_sal_t *me_ptr)
{
   if (NULL == me_ptr->operating_mf_ptr)
   {
      return AR_EOK;
   }

   me_ptr->input_process_info.alignment      = 0x7;
   me_ptr->input_process_info.upconvert_flag = FALSE;

   switch (me_ptr->operating_mf_ptr->format.q_factor)
   {
      case QF_BPS_16:
      {
         if (me_ptr->limiter_enabled)
         {
            me_ptr->input_process_info.alignment           = 0x3;
            me_ptr->input_process_info.accumulate_func_ptr = accumulate_bw_16_samples;
            me_ptr->input_process_info.upconvert_flag      = TRUE;
         }
         else
         {
            me_ptr->input_process_info.accumulate_func_ptr = accumulate_bw_16_samples_sat;
         }
         break;
      }
      case QF_BPS_24:
      {
         if (me_ptr->limiter_enabled)
         {
            me_ptr->input_process_info.accumulate_func_ptr = accumulate_bw_32_samples_no_sat;
         }
         else
         {
            me_ptr->input_process_info.accumulate_func_ptr = accumulate_bw_32_samples_q27_sat;
         }
         break;
      }
      case QF_BPS_32:
      {
         me_ptr->input_process_info.accumulate_func_ptr = accumulate_bw_32_samples_with_sat;
         break;
      }
      default:
      {
         SAL_MSG_ISLAND(me_ptr->iid,
                        DBG_ERROR_PRIO,
                        "Unsupported QF %lu at the input",
                        me_ptr->operating_mf_ptr->format.q_factor);
         return CAPI_EBADPARAM;
         break;
      }
   }
   return CAPI_EOK;
}

//////////////////////////////////////ACCUMULATION UTILITIES////////////////////////////////////////
static void accumulate_bw_16_samples(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch)
{

   int16_t *in_buf_ptr_16  = (int16_t *)(input_ch_buf);
   int32_t *out_buf_ptr_32 = (int32_t *)(output_ch_buf);

#ifdef __qdsp6__
   int64_t *dest_ptr             = (int64_t *)output_ch_buf;
   int32_t *src_ptr              = (int32_t *)input_ch_buf;
   uint32_t LoopIndex            = 0;
   uint32_t num_samp_per_ch_Div4 = (num_samp_per_ch >> 2);

   // process 4 samples per iteration with intrinsics
   for (; LoopIndex < num_samp_per_ch_Div4; LoopIndex++)
   {
      (*dest_ptr++) += Q6_P_vsxthw_R(*src_ptr++);
      (*dest_ptr++) += Q6_P_vsxthw_R(*src_ptr++);
   }

   // process left over samples.
   for (uint32_t sampIndex = LoopIndex * 4; sampIndex < num_samp_per_ch; sampIndex++)
   {
      out_buf_ptr_32[sampIndex] += (int32_t)in_buf_ptr_16[sampIndex];
   }
#else
   uint32_t count = 0;
   while (count < num_samp_per_ch)
   {
      *(out_buf_ptr_32++) += *(in_buf_ptr_16++);
      count++;
   }
#endif
}

static void accumulate_bw_16_samples_sat(int8_t *input_ch_buf, int8_t *output_ch_buf, uint32_t num_samp_per_ch)
{
   int16_t *in_buf_ptr_16  = (int16_t *)(input_ch_buf);
   int16_t *out_buf_ptr_16 = (int16_t *)(output_ch_buf);
#ifdef __qdsp6__
   uint32_t i              = 0;
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   int64_t *out_buf_ptr_64 = (int64_t *)output_ch_buf;
   for (i = 0; i < num_samp_per_ch / 4; i++)
   {
      out_buf_ptr_64[i] = Q6_P_vaddh_PP_sat(out_buf_ptr_64[i], in_buf_ptr_64[i]);
   }
   for (i *= 4; i < num_samp_per_ch; i++)
   {
      out_buf_ptr_16[i] = Q6_R_add_RlRl_sat(out_buf_ptr_16[i], in_buf_ptr_16[i]);
   }

#else
   uint32_t count = 0;
   int32_t sum = 0;
   while (count < num_samp_per_ch)
   {
      sum = *out_buf_ptr_16 + *in_buf_ptr_16;
      if (sum >= 0)
      {
         *(out_buf_ptr_16++) = (sum >= MAX_16) ? MAX_16 : sum;
      }
      else
      {
         *(out_buf_ptr_16++) = (sum < MIN_16) ? MIN_16 : sum;
      }
      in_buf_ptr_16++;
      count++;
   }
#endif
}

static void accumulate_bw_32_samples_no_sat(
   int8_t *restrict input_ch_buf,  // using "restrict" speeds up parallel processing.
   int8_t *restrict output_ch_buf, // NOTE:There should be no aliasing between the two pointers.
   uint32_t         num_samp_per_ch)
{
#ifdef __qdsp6__
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   int64_t *out_buf_ptr_64 = (int64_t *)output_ch_buf;
   uint32_t num_iterations = num_samp_per_ch >> 2; // We are adding and accumulating 4 samples in parallel.
                                                   // Hence we iterate over num_samples/4  times only

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      out_buf_ptr_64[2 * i] =
         Q6_P_vaddw_PP(out_buf_ptr_64[2 * i],
                       in_buf_ptr_64[2 * i]); // Intrinsic to add and accumulate two words in parallel

      out_buf_ptr_64[(2 * i) + 1] =
         Q6_P_vaddw_PP(out_buf_ptr_64[(2 * i) + 1],
                       in_buf_ptr_64[(2 * i) + 1]); // Intrinsic to add and accumulate two words in parallel
   }
   uint32_t num_64bit_samples_consumed = num_iterations << 1; // Every iteration we processed two 64 bit words.
                                                              // So multiplying by 2
   int32_t *temp_in_buf  = (int32_t *)(in_buf_ptr_64 + num_64bit_samples_consumed);
   int32_t *temp_out_buf = (int32_t *)(out_buf_ptr_64 + num_64bit_samples_consumed);

   // num_samples_consumed corresponds to 32 bits samples consumed. It will double of 64 bit samples consumed.
   for (uint32_t num_samples_consumed = num_64bit_samples_consumed << 1; num_samples_consumed < num_samp_per_ch;
        num_samples_consumed++)
   {
      *temp_out_buf = Q6_R_add_RR(*temp_out_buf, *temp_in_buf);
      temp_in_buf++;
      temp_out_buf++;
   }
#else
   int32_t *in_buf_ptr_32 = (int32_t *)(input_ch_buf);
   int32_t *out_buf_ptr_32 = (int32_t *)(output_ch_buf);
   uint32_t count = 0;
   while (count < num_samp_per_ch)
   {
      *out_buf_ptr_32 = s32_add_s32_s32(*out_buf_ptr_32, *in_buf_ptr_32);
      out_buf_ptr_32++;
      in_buf_ptr_32++;
      count++;
   }
#endif
}

// using "restrict" speeds up parallel processing
// NOTE:There should be no aliasing between the two pointers.
static void accumulate_bw_32_samples_q27_sat(int8_t *restrict input_ch_buf,
                                             int8_t *restrict output_ch_buf,
                                             uint32_t         num_samp_per_ch)
{
#ifdef __qdsp6__

   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   int64_t *out_buf_ptr_64 = (int64_t *)output_ch_buf;

   uint32_t num_iterations = num_samp_per_ch >> 1; // We are adding and accumulating 2 samples in parallel using vaddw.
   // Hence we iterate over num_samples/2 times only
   int64_t temp_out_q31 = 0, temp_in_q31 = 0;
   for (uint32_t i = 0; i < num_iterations; i++)
   {
      temp_out_q31 = Q6_P_vaslw_PI(out_buf_ptr_64[i], QF_CNV_4); // Doing a left shift to convert from Q27 to Q31 format
      temp_in_q31  = Q6_P_vaslw_PI(in_buf_ptr_64[i], QF_CNV_4);  // Doing a left shift to convert from Q27 to Q31 format
      temp_out_q31 = Q6_P_vaddw_PP_sat(temp_out_q31, temp_in_q31); // Adds 2 words in parallel and saturates them
      out_buf_ptr_64[i] =
         Q6_P_vasrw_PI(temp_out_q31, QF_CNV_4); // Doing a right shift to convert from Q31 to Q27 format
   }

   if (num_samp_per_ch & 0x1) // Handling the last sample if the input samples are odd
   {
      int32_t  sum          = 0;
      int32_t *temp_in_buf  = (int32_t *)(in_buf_ptr_64 + num_iterations); // Getting remaining samples
      int32_t *temp_out_buf = (int32_t *)(out_buf_ptr_64 + num_iterations);
      sum                   = s32_add_s32_s32(*temp_out_buf, *temp_in_buf);
      if (sum >= 0)
      {
         *(temp_out_buf++) = (sum >= MAX_Q27) ? MAX_Q27 : sum;
      }
      else
      {
         *(temp_out_buf++) = (sum < MIN_Q27) ? MIN_Q27 : sum;
      }
   }
#else
   int32_t *in_buf_ptr_32 = (int32_t *)(input_ch_buf);
   int32_t *out_buf_ptr_32 = (int32_t *)(output_ch_buf);
   uint32_t count = 0;
   int32_t sum = 0; // Q27
   while (count < num_samp_per_ch)
   {
      sum = s32_add_s32_s32(*out_buf_ptr_32, *in_buf_ptr_32);
      if (sum >= 0)
      {
         *(out_buf_ptr_32++) = (sum >= MAX_Q27) ? MAX_Q27 : sum;
      }
      else
      {
         *(out_buf_ptr_32++) = (sum < MIN_Q27) ? MIN_Q27 : sum;
      }
      in_buf_ptr_32++;
      count++;
   }
#endif
}

static void accumulate_bw_32_samples_with_sat(int8_t *restrict input_ch_buf,  // NOTE: Input and output pointers
                                              int8_t *restrict output_ch_buf, // should not alias
                                              uint32_t         num_samp_per_ch)
{
#ifdef __qdsp6__
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   int64_t *out_buf_ptr_64 = (int64_t *)output_ch_buf;
   uint32_t num_iterations = num_samp_per_ch >> 2; // We are adding and accumulating 4 samples in parallel.
                                                   // Hence we iterate over num_samples/4  times only

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      out_buf_ptr_64[2 * i] =
         Q6_P_vaddw_PP_sat(out_buf_ptr_64[2 * i],
                           in_buf_ptr_64[2 * i]); // Intrinsic to add and accumulate two words in parallel

      out_buf_ptr_64[(2 * i) + 1] =
         Q6_P_vaddw_PP_sat(out_buf_ptr_64[(2 * i) + 1],
                           in_buf_ptr_64[(2 * i) + 1]); // Intrinsic to add and accumulate two words in parallel
   }

   uint32_t num_64bit_samples_consumed = num_iterations << 1; // Every iteration we processed two 64 bit words
   int32_t *temp_in_buf                = (int32_t *)(in_buf_ptr_64 + num_64bit_samples_consumed);
   int32_t *temp_out_buf               = (int32_t *)(out_buf_ptr_64 + num_64bit_samples_consumed);

   // num_samples_consumed corresponds to 32 bits samples consumed
   for (uint32_t num_samples_consumed = num_64bit_samples_consumed << 1; num_samples_consumed < num_samp_per_ch;
        num_samples_consumed++)
   {
      *temp_out_buf = Q6_R_add_RR_sat(*temp_out_buf, *temp_in_buf);
      temp_in_buf++;
      temp_out_buf++;
   }

#else
   int32_t *in_buf_ptr_32 = (int32_t *)(input_ch_buf);
   int32_t *out_buf_ptr_32 = (int32_t *)(output_ch_buf);
   int32_t count = 0;

   while (count < num_samp_per_ch)
   {
      *out_buf_ptr_32 = s32_add_s32_s32_sat(*out_buf_ptr_32, *in_buf_ptr_32);
      out_buf_ptr_32++;
      in_buf_ptr_32++;
      count++;
   }
#endif
}

///////////////////////////INPLACE BIT WIDTH CONVERSION UTILITIES//////////////////
// this operation should happen on the sal out buf where half the size is filled by
// the limiter. we need to expand from 16 to 32 b and copy inplace
// let's start from the last 16 bits and copy to the end
#ifdef __qdsp6__
static inline void upconvert_ws_16_to_32_inplace_default(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   // Handle last sample if odd
   if (num_samp_per_ch & 0x1)
   {
      num_samp_per_ch += 1;
   }

   uint32_t num_iterations = num_samp_per_ch >> 1;
   int32_t *in_buf_ptr_32  = (int32_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int16_t)); // last 32 bits
   int64_t *out_buf_ptr_64 = (int64_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int32_t)); // last 64 bits

   // Modifying inplace from last sample in buffer
   for (uint32_t i = 0; i < num_iterations; i++)
   {
      *(out_buf_ptr_64--) = Q6_P_vsxthw_R(*(in_buf_ptr_32--));
   }
}
#endif

#ifdef __qdsp6__
static inline void upconvert_ws_16_to_32_inplace_q15_q31(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   // Handle last sample if odd
   if (num_samp_per_ch & 0x1)
   {
      num_samp_per_ch += 1;
   }

   uint32_t num_iterations = num_samp_per_ch >> 1;
   int32_t *in_buf_ptr_32  = (int32_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int16_t)); // last 32 bits
   int64_t *out_buf_ptr_64 = (int64_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int32_t)); // last 64 bits

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      int64_t temp        = Q6_P_vsxthw_R(*(in_buf_ptr_32--));
      *(out_buf_ptr_64--) = Q6_P_vaslw_PI(temp, QF_CNV_16);
   }
}
#endif

#ifdef __qdsp6__
static inline void upconvert_ws_16_to_32_inplace_q15_q27(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   // Handle last sample if odd
   if (num_samp_per_ch & 0x1)
   {
      num_samp_per_ch += 1;
   }

   uint32_t num_iterations = num_samp_per_ch >> 1;
   int32_t *in_buf_ptr_32  = (int32_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int16_t)); // last 32 bits
   int64_t *out_buf_ptr_64 = (int64_t *)((input_ch_buf) + (num_samp_per_ch - 2) * sizeof(int32_t)); // last 64 bits

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      int64_t temp        = Q6_P_vsxthw_R(*(in_buf_ptr_32--));
      *(out_buf_ptr_64--) = Q6_P_vaslw_PI(temp, QF_CNV_12);
   }
}
#endif

static void upconvert_ws_16_to_32_inplace(int8_t *input_ch_buf, uint16_t shift_factor, uint32_t num_samp_per_ch)
{
#ifdef __qdsp6__
   switch (shift_factor)
   {
      case QF_CNV_12:
      {
         upconvert_ws_16_to_32_inplace_q15_q27(input_ch_buf, num_samp_per_ch);
         break;
      }
      case QF_CNV_16:
      {
         upconvert_ws_16_to_32_inplace_q15_q31(input_ch_buf, num_samp_per_ch);
         break;
      }
      default:
      {
         upconvert_ws_16_to_32_inplace_default(input_ch_buf, num_samp_per_ch);
         break;
      }
   }
#else
   uint32_t count = 0;

   int16_t *in_buf_ptr_16 = (int16_t *)((input_ch_buf) + (num_samp_per_ch - 1) * sizeof(int16_t));  // last 16 bits
   int32_t *out_buf_ptr_32 = (int32_t *)((input_ch_buf) + (num_samp_per_ch - 1) * sizeof(int32_t)); // last 32 bits

   while (count < num_samp_per_ch)
   {
      int32_t temp = *(in_buf_ptr_16--);
      *(out_buf_ptr_32--) = temp << shift_factor;
      count++;
   }
#endif
}

#ifdef __qdsp6__
static inline void upconvert_ws_q27_q31(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   uint32_t num_iterations = num_samp_per_ch >> 1;

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      in_buf_ptr_64[i] = Q6_P_vaslw_PI(in_buf_ptr_64[i], QF_CNV_4);
   }

   if (num_samp_per_ch & 0x1) // Handling the last sample if the input samples are odd
   {
      int32_t *temp_in_buf = (int32_t *)(in_buf_ptr_64 + num_iterations); // Getting remaining samples
      *(temp_in_buf)       = (*(temp_in_buf)) << QF_CNV_4;
   }
}
#endif

static void upconvert_ws_32(int8_t *input_ch_buf, uint16_t shift_factor, uint32_t num_samp_per_ch)
{
#ifdef __qdsp6__
   switch (shift_factor)
   {
      case QF_CNV_4:
      {
         upconvert_ws_q27_q31(input_ch_buf, num_samp_per_ch);
         break;
      }
      default:
      {
         break;
      }
   }
#else
   int32_t *in_buf_ptr_32 = (int32_t *)(input_ch_buf);
   uint32_t count = 0;
   while (count < num_samp_per_ch)
   {
      int32_t temp = *in_buf_ptr_32;
      *(in_buf_ptr_32++) = temp << shift_factor;
      count++;
   }
#endif
}

#ifdef __qdsp6__
static void inline downconvert_ws_q27_q15(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   uint32_t num_iterations = num_samp_per_ch >> 1;

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      in_buf_ptr_64[i] = Q6_P_vasrw_PI(in_buf_ptr_64[i], QF_CNV_12);
   }

   if (num_samp_per_ch & 0x1) // Handling the last sample if the input samples are odd
   {
      int32_t *temp_in_buf = (int32_t *)(in_buf_ptr_64 + num_iterations); // Getting remaining samples
      *(temp_in_buf)       = (*(temp_in_buf)) >> QF_CNV_12;
   }
}
#endif

#ifdef __qdsp6__
static void inline downconvert_ws_q31_q27(int8_t *input_ch_buf, uint32_t num_samp_per_ch)
{
   int64_t *in_buf_ptr_64  = (int64_t *)input_ch_buf;
   uint32_t num_iterations = num_samp_per_ch >> 1;

   for (uint32_t i = 0; i < num_iterations; i++)
   {
      in_buf_ptr_64[i] = Q6_P_vasrw_PI(in_buf_ptr_64[i], QF_CNV_4);
   }

   if (num_samp_per_ch & 0x1) // Handling the last sample if the input samples are odd
   {
      int32_t *temp_in_buf = (int32_t *)(in_buf_ptr_64 + num_iterations); // Getting remaining samples
      *(temp_in_buf)       = (*(temp_in_buf)) >> QF_CNV_4;
   }
}
#endif

void downconvert_ws_32(int8_t *input_ch_buf, uint16_t shift_factor, uint32_t num_samp_per_ch)
{
#ifdef __qdsp6__
   switch (shift_factor)
   {
      case QF_CNV_4:
      {
         downconvert_ws_q31_q27(input_ch_buf, num_samp_per_ch);
         break;
      }
      case QF_CNV_12:
      {
         downconvert_ws_q27_q15(input_ch_buf, num_samp_per_ch);
         break;
      }
      default:
      {
         break;
      }
   }
#else
   int32_t *in_buf_ptr_32 = (int32_t *)(input_ch_buf);
   uint32_t count = 0;
   while (count < num_samp_per_ch)
   {
      int32_t temp = (*in_buf_ptr_32);
      *(in_buf_ptr_32++) = temp >> shift_factor;
      count++;
   }
#endif
}