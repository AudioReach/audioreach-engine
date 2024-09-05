/**
 * \file capi_mux_demux_island.c
 *
 * \brief
 *        CAPI for mux demux module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_mux_demux.h"
#include "capi_mux_demux_utils.h"

/*------------------------------------------------------------------------
 * Static declarations
 * -----------------------------------------------------------------------*/
capi_vtbl_t mux_demux_vtbl = { capi_mux_demux_process,        capi_mux_demux_end,
                               capi_mux_demux_set_param,      capi_mux_demux_get_param,
                               capi_mux_demux_set_properties, capi_mux_demux_get_properties };

/*----------------------------------------------------------------------------
 * Function Definitions
 * -------------------------------------------------------------------------*/
static uint32_t count_eos_md(module_cmn_md_list_t *md_list)
{
   uint32_t num_eos_md = 0;
   while (md_list)
   {
      if (MODULE_CMN_MD_ID_EOS == md_list->obj_ptr->metadata_id)
      {
         num_eos_md++;
      }
      md_list = md_list->next_ptr;
   }
   return num_eos_md;
}

static capi_err_t capi_mux_demux_clone_metadata_list(capi_mux_demux_t *     me_ptr,
                                                     uint32_t               sample_consumed,
                                                     module_cmn_md_list_t * input_md_list,
                                                     module_cmn_md_list_t **clone_md_list)
{
   capi_err_t     result  = CAPI_EOK;
   capi_heap_id_t heap_id = { .heap_id = me_ptr->heap_id };

   *clone_md_list = NULL;

   if (!input_md_list)
   {
      return result;
   }

   module_cmn_md_list_t *node_ptr = input_md_list;
   module_cmn_md_list_t *next_ptr = NULL;

   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = (module_cmn_md_t *)node_ptr->obj_ptr;

      if (md_ptr->offset <= sample_consumed)
      {
         result = me_ptr->metadata_handler.metadata_clone(me_ptr->metadata_handler.context_ptr,
                                                          node_ptr->obj_ptr,
                                                          clone_md_list,
                                                          heap_id);
         if (CAPI_FAILED(result))
         {
            AR_MSG_ISLAND(DBG_ERROR_PRIO, "cloning metadata failed %lx", result);
            break;
         }
      }

      node_ptr = next_ptr;
   }

   return result;
}

static capi_err_t capi_mux_demux_destroy_metadata_list(capi_mux_demux_t *me_ptr, capi_stream_data_v2_t *input_stream_pt)
{
   capi_err_t result = CAPI_EOK;

   module_cmn_md_list_t *node_ptr = input_stream_pt->metadata_list_ptr;
   module_cmn_md_list_t *next_ptr = NULL;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      result = me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                         node_ptr,
                                                         TRUE,
                                                         &input_stream_pt->metadata_list_ptr);
      if (CAPI_FAILED(result))
      {
         AR_MSG_ISLAND(DBG_ERROR_PRIO, "destroy metadata failed %lx", result);
      }
      node_ptr = next_ptr;
   }

   return result;
}

capi_err_t capi_mux_demux_handle_metadata(capi_mux_demux_t *  me_ptr,
                                          capi_stream_data_t *input[],
                                          capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   // moving metadata from each input port to all the connected output ports.
   for (uint32_t in_port_index = 0; in_port_index < me_ptr->num_of_input_ports; in_port_index++)
   {
      capi_stream_data_v2_t *input_stream_ptr                 = (capi_stream_data_v2_t *)input[in_port_index];
      uint32_t               num_of_connected_output_ports    = 0;
      uint32_t               num_eos_md                       = 0;
      me_ptr->input_port_info_ptr[in_port_index].is_eos_moved = FALSE;

      if (!input_stream_ptr || (CAPI_STREAM_V2 != input[in_port_index]->flags.stream_data_version) ||
          !(input_stream_ptr->metadata_list_ptr))
      {
         continue;
      }

      // num of eos metadata before propagation.
      num_eos_md = count_eos_md(input_stream_ptr->metadata_list_ptr);

      if (TRUE == me_ptr->input_port_info_ptr[in_port_index].fmt.is_valid)
      {
         for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports; out_port_arr_index++)
         {
            uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;
            if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
                NULL == output[out_port_index] || NULL == output[out_port_index]->buf_ptr[0].data_ptr ||
                FALSE == me_ptr->input_port_info_ptr[in_port_index].is_output_connected[out_port_arr_index])
            {
               continue;
            }
            num_of_connected_output_ports++;
         }
      }

      if (0 == num_of_connected_output_ports)
      {
#ifdef MUX_DEMUX_TX_DEBUG_INFO
         AR_MSG_ISLAND(DBG_LOW_PRIO, "Destroying metadata from input stream %lu, as it is not connected.", in_port_index);
#endif
         capi_mux_demux_destroy_metadata_list(me_ptr, input_stream_ptr);
         continue;
      }

      for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports; out_port_arr_index++)
      {
         uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;
         capi_stream_data_v2_t  clone_input_stream;

         if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
             NULL == output[out_port_index] || NULL == output[out_port_index]->buf_ptr[0].data_ptr ||
             FALSE == me_ptr->input_port_info_ptr[in_port_index].is_output_connected[out_port_arr_index])
         {
            continue;
         }
         capi_stream_data_v2_t *output_stream_ptr = (capi_stream_data_v2_t *)output[out_port_index];
         uint32_t               samples_processed = // minimum of input and output actual data len
            min_of_two(bytes_to_samples(output_stream_ptr->buf_ptr[0].actual_data_len,
                                        me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample),
                       bytes_to_samples(input_stream_ptr->buf_ptr[0].actual_data_len,
                                        me_ptr->input_port_info_ptr[in_port_index].fmt.bits_per_sample));


         // make a copy of stream info.
         memscpy(&clone_input_stream,
                 sizeof(capi_stream_data_v2_t),
                 input[in_port_index],
                 sizeof(capi_stream_data_v2_t));

         if (1 < num_of_connected_output_ports)
         { // if it is not the last connection for this input port then clone metadata
#ifdef MUX_DEMUX_TX_DEBUG_INFO
            AR_MSG_ISLAND(DBG_LOW_PRIO, "cloning metadata for input stream %lu.", in_port_index);
#endif
            clone_input_stream.metadata_list_ptr = NULL;
            result                               = capi_mux_demux_clone_metadata_list(me_ptr,
                                                        samples_processed,
                                                        input_stream_ptr->metadata_list_ptr,
                                                        &clone_input_stream.metadata_list_ptr);
         }
#ifdef MUX_DEMUX_TX_DEBUG_INFO
         else
         {
            AR_MSG_ISLAND(DBG_LOW_PRIO, "merging metadata from input stream %lu.", in_port_index);
         }
#endif

         num_of_connected_output_ports--;

         // propagate from clone to output
         {
            intf_extn_md_propagation_t input_md_info, output_md_info;
            memset(&input_md_info, 0, sizeof(input_md_info));
            memset(&output_md_info, 0, sizeof(output_md_info));
            input_md_info.df = CAPI_FIXED_POINT;
            input_md_info.len_per_ch_in_bytes =
               samples_to_bytes(samples_processed, me_ptr->input_port_info_ptr[in_port_index].fmt.bits_per_sample);
            input_md_info.initial_len_per_ch_in_bytes = clone_input_stream.buf_ptr[0].actual_data_len;
            input_md_info.bits_per_sample             = me_ptr->input_port_info_ptr[in_port_index].fmt.bits_per_sample;
            input_md_info.sample_rate                 = me_ptr->input_port_info_ptr[in_port_index].fmt.sample_rate;

            output_md_info.df = CAPI_FIXED_POINT;
            output_md_info.len_per_ch_in_bytes = output_stream_ptr->buf_ptr[0].actual_data_len;
            output_md_info.initial_len_per_ch_in_bytes = 0;
            output_md_info.bits_per_sample = me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample;
            output_md_info.sample_rate     = me_ptr->operating_sample_rate;

#ifdef MUX_DEMUX_TX_DEBUG_INFO
            AR_MSG_ISLAND(DBG_LOW_PRIO,
                   "propagaing metadata for input stream index %lu to output stream index %lu."
                   "inpupt intial len %lu, input len %lu, output len %lu, num_eos_md %lu",
                   in_port_index,
                   out_port_index,
                   input_md_info.initial_len_per_ch_in_bytes,
                   input_md_info.len_per_ch_in_bytes,
                   output_md_info.len_per_ch_in_bytes,
                   num_eos_md);
#endif

            me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                        &clone_input_stream,
                                                        output_stream_ptr,
                                                        NULL,
                                                        0,
                                                        &input_md_info,
                                                        &output_md_info);
         }

         if (0 == num_of_connected_output_ports)
         {
            memscpy(input[in_port_index],
                    sizeof(capi_stream_data_v2_t),
                    &clone_input_stream,
                    sizeof(capi_stream_data_v2_t));
         }
         else
         {
            // if for some reason metadata is stuck in cloned stream then destroy.
            capi_mux_demux_destroy_metadata_list(me_ptr, &clone_input_stream);
         }
      }

      if (num_eos_md > count_eos_md(input_stream_ptr->metadata_list_ptr))
      {
         me_ptr->input_port_info_ptr[in_port_index].is_eos_moved = TRUE; // It means that eos is moved to the output.
      }
   }

   // make output flushing eos to non-flushing if it got any non-eosed/non-zero data from any input stream.
   for (uint32_t in_port_index = 0; in_port_index < me_ptr->num_of_input_ports; in_port_index++)
   {
      capi_stream_data_v2_t *input_stream_ptr = (capi_stream_data_v2_t *)input[in_port_index];

      if (!input_stream_ptr || (FALSE == me_ptr->input_port_info_ptr[in_port_index].fmt.is_valid) ||
          (me_ptr->input_port_info_ptr[in_port_index].is_eos_moved) || // eos propagated from this input.
          (0 == input_stream_ptr->buf_ptr[0].actual_data_len))         // no contribution from this input
      {
         continue;
      }

      for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports; out_port_arr_index++)
      {
         uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;

         capi_stream_data_v2_t *output_stream_ptr = (capi_stream_data_v2_t *)output[out_port_index];


         if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
             NULL == output[out_port_index] || FALSE == output_stream_ptr->flags.marker_eos || // already non-flushing
             0 == output_stream_ptr->buf_ptr[0].actual_data_len ||                             // no output generated
             NULL == output[out_port_index]->buf_ptr[0].data_ptr ||
             FALSE == me_ptr->input_port_info_ptr[in_port_index].is_output_connected[out_port_arr_index])
         {
            continue;
         }

         /* at this point, this output port is connected to an input port
          * where eos is not moved and a valid data is propagated from input to output.
          */

         // go through each MD and mark as non-flushing
         module_cmn_md_list_t *node_ptr = output_stream_ptr->metadata_list_ptr;

         while (node_ptr)
         {
            module_cmn_md_t *    md_ptr            = (module_cmn_md_t *)node_ptr->obj_ptr;
            module_cmn_md_eos_t *eos_md_ptr        = NULL;
            bool_t               flush_eos_present = FALSE;

            if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
            {
               bool_t out_of_band = md_ptr->metadata_flag.is_out_of_band;
               if (out_of_band)
               {
                  eos_md_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
               }
               else
               {
                  eos_md_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
               }

               flush_eos_present = eos_md_ptr->flags.is_flushing_eos;

               AR_MSG_ISLAND(DBG_LOW_PRIO,
                      "EOS metadata found 0x%p, flush_eos_present %u. Marking as non-flushing for out port index %lu",
                      eos_md_ptr,
                      flush_eos_present,
                      out_port_index);

               if (flush_eos_present)
               {
                  eos_md_ptr->flags.is_flushing_eos = MODULE_CMN_MD_EOS_NON_FLUSHING;
               }
            }

            node_ptr = node_ptr->next_ptr;
         }

         output_stream_ptr->flags.marker_eos = FALSE;
      }
   }

   return result;
}

static void accumulate_data(int8_t * in_data_ptr,
                            int8_t * out_data_ptr,
                            uint32_t in_q_factor,
                            uint32_t out_q_factor,
                            uint32_t num_samples,
                            int32_t  coef_q15,
                            bool_t   is_copy)
{
   uint32_t input_bits_per_sample  = (PCM_Q_FACTOR_15 < in_q_factor) ? BIT_WIDTH_32 : BIT_WIDTH_16;
   uint32_t output_bits_per_sample = (PCM_Q_FACTOR_15 < out_q_factor) ? BIT_WIDTH_32 : BIT_WIDTH_16;
   uint32_t input_bytes_to_copy    = samples_to_bytes(num_samples, input_bits_per_sample);
   uint32_t output_bytes_to_copy   = samples_to_bytes(num_samples, output_bits_per_sample);
   int32_t  shift_factor           = (in_q_factor - out_q_factor);

   int32_t *in32_buf  = (int32_t *)in_data_ptr;
   int32_t *out32_buf = (int32_t *)out_data_ptr;
   int16_t *in16_buf  = (int16_t *)in_data_ptr;
   int16_t *out16_buf = (int16_t *)out_data_ptr;

   coef_q15 = ONE_Q15_32BIT; // Mixing TODO: remove this to support mixing.

   if (is_copy && 0 == shift_factor && ONE_Q15_32BIT == coef_q15)
   {
      memscpy(out_data_ptr, output_bytes_to_copy, in_data_ptr, input_bytes_to_copy);
      return;
   }

   // Mixing TODO: saturation and rounding when mixing is needed.
   if (in_q_factor == out_q_factor)
   {
      if (BIT_WIDTH_16 == input_bits_per_sample) // q15->q15
      {
          int32_t sum = 0;
         for (uint32_t i = 0; i < num_samples; i++)
         {
            sum = out16_buf[i];
            sum += (in16_buf[i] * coef_q15) >> 15;
            out16_buf[i] = s16_saturate_s32(sum);
         }
      }
      else
      {
          int64_t sum = 0;
         for (uint32_t i = 0; i < num_samples; i++) // q27->q27, q31->q31
         {
            sum = out32_buf[i];
            sum += (in32_buf[i] * (int64_t)coef_q15) >> 15;
            if(PCM_Q_FACTOR_31==out_q_factor)
            {
                out32_buf[i] = s32_saturate_s64(sum);

            }
            else //q27->q27
            {
                if (sum >= 0)
                {
                    out32_buf[i] = (sum >= MAX_28) ? MAX_28 : sum;
                }
                else
                {
                    out32_buf[i] = (sum < MIN_28) ? MIN_28 : sum;
                }
            }
         }
      }
   }
   else
   {
      if (BIT_WIDTH_16 == input_bits_per_sample)
      {
          int64_t sum =0;
         for (uint32_t i = 0; i < num_samples; i++) // q15->q27, q15->q31
         {
             sum = out32_buf[i];
             sum += ((in16_buf[i]*(int64_t)coef_q15) >> (shift_factor + 15)); // total shift 3 or -1
             if(PCM_Q_FACTOR_31==out_q_factor)
             {
                 out32_buf[i] = s32_saturate_s64(sum);

            }
            else //q15->q27
            {
                if (sum >= 0)
                {
                    out32_buf[i] = (sum >= MAX_28) ? MAX_28 : sum;
                }
                else
                {
                    out32_buf[i] = (sum < MIN_28) ? MIN_28 : sum;
                }
            }
         }
      }
      else if (BIT_WIDTH_16 == output_bits_per_sample) // q27->q15, q31->q15
      {
         for (uint32_t i = 0; i < num_samples; i++)
         {
            int64_t temp = (in32_buf[i] *  (int64_t)coef_q15) >> (shift_factor + 15);
            out16_buf[i] = s16_add_s16_s16_sat(out16_buf[i], (int16_t)temp);
         }
      }
      else // q27->q31, q31->q27
      {
          int64_t sum = 0;
         for (uint32_t i = 0; i < num_samples; i++)
         {
            sum = out32_buf[i];
            sum += ((in32_buf[i] *(int64_t)coef_q15) >> (shift_factor + 15)); //total shift 11 or 19
            if(PCM_Q_FACTOR_31==out_q_factor)
            {
                out32_buf[i] = s32_saturate_s64(sum);

            }
            else //q31->q27
            {
                if (sum >= 0)
                {
                    out32_buf[i] = (sum >= MAX_28) ? MAX_28 : sum;
                }
                else
                {
                    out32_buf[i] = (sum < MIN_28) ? MIN_28 : sum;
                }
            }
         }
      }
   }
}

capi_err_t capi_mux_demux_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;
   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= ((NULL == _pif) || (NULL == input) || (NULL == output)) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "process received bad argument.");

   result |= (NULL == me_ptr->input_port_info_ptr || NULL == me_ptr->output_port_info_ptr) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "process failed, port/connection info not present.");

   uint32_t samples_to_process       = 0xFFFFFFFF;
   uint32_t input_samples_to_process = 0;

   // Initialize the output actual data len to zero
   for (uint32_t i = 0; i < me_ptr->num_of_output_ports; i++)
   {
      if (NULL == output[i])
      {
         continue;
      }
      for (uint32_t j = 0; j < output[i]->bufs_num; j++)
      {
         output[i]->buf_ptr[j].actual_data_len = 0;
      }
   }

   if (0 == me_ptr->operating_sample_rate)
   {
      // To drop all the metadata from input streams.
      return capi_mux_demux_handle_metadata(me_ptr, input, output);
   }

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
   if(CAPI_INTERLEAVED == me_ptr->data_interleaving)
   {
      for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports && samples_to_process; out_port_arr_index++)
      {
         uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;
         if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
            NULL == output[out_port_index] || (NULL == output[out_port_index]->buf_ptr[0].data_ptr))
         {
            continue;
         }

         // for interleaving format copy data direcly from input to the connected output
         uint32_t connected_input_arr_index = (uint32_t)-1;
         for (uint32_t j = 0; j < me_ptr->num_of_input_ports; j++)
         {
            if (me_ptr->input_port_info_ptr[j].is_output_connected[out_port_arr_index] && me_ptr->input_port_info_ptr[j].fmt.is_valid)
            {
               connected_input_arr_index = j;
               break;
            }
         }

         if(connected_input_arr_index < me_ptr->num_of_input_ports)
         {
            // copyign data from input to output
            uint32_t conn_inp_port_index = me_ptr->input_port_info_ptr[connected_input_arr_index].port_index;
            if ((NULL == input[conn_inp_port_index]) || (NULL == input[conn_inp_port_index]->buf_ptr[0].data_ptr))
            {
               continue;
            }

            // make a copy of stream info.
            output[out_port_index]->buf_ptr[0].actual_data_len =
               memscpy(output[out_port_index]->buf_ptr[0].data_ptr,
                       output[out_port_index]->buf_ptr[0].max_data_len,
                       input[conn_inp_port_index]->buf_ptr[0].data_ptr,
                       input[conn_inp_port_index]->buf_ptr[0].actual_data_len);
            capi_stream_data_v2_t* op_ptr = (capi_stream_data_v2_t *)output[out_port_index];
            capi_stream_data_v2_t* ip_ptr = (capi_stream_data_v2_t *)input[conn_inp_port_index];
            op_ptr->metadata_list_ptr =  ip_ptr->metadata_list_ptr;
            ip_ptr->metadata_list_ptr = NULL;
            // TODO: print input port id, port index, actual len, max len,  output port id, idx, actual, max length

            output[out_port_index]->flags.word = input[conn_inp_port_index]->flags.word;

            input[conn_inp_port_index]->flags.end_of_frame = FALSE;
            input[conn_inp_port_index]->flags.marker_eos   = FALSE;
            input[conn_inp_port_index]->flags.erasure      = FALSE;
         }
      }

      return result;
   }
#endif

   // get the bytes to process, based on the in-out buffer size of started and connected ports.
   for (uint32_t in_port_index = 0; in_port_index < me_ptr->num_of_input_ports; in_port_index++)
   {
      if (NULL == input[in_port_index] || FALSE == me_ptr->input_port_info_ptr[in_port_index].fmt.is_valid)
      {
         continue;
      }

#ifdef MUX_DEMUX_TX_DEBUG_INFO
      for (uint32_t j = 0; j < input[in_port_index]->bufs_num; j++)
      {
         AR_MSG_ISLAND(DBG_LOW_PRIO,
                       "before process [0x%lx]  input[%lu][%lu] = %lu of %lu",
                       me_ptr->miid,
                       in_port_index,
                       j,
                       input[in_port_index]->buf_ptr[j].actual_data_len,
                       input[in_port_index]->buf_ptr[j].max_data_len);
      }
#endif

      for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports; out_port_arr_index++)
      {
         uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;

         // performing null check for output data_ptr
         // this was added because of corner case in fwk where it can't communicate port state to the module, if the state changes from started to prepare
         if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
             NULL == output[out_port_index] || NULL == output[out_port_index]->buf_ptr[0].data_ptr ||
             FALSE == me_ptr->input_port_info_ptr[in_port_index].is_output_connected[out_port_arr_index])
         {
            continue;
         }
         // maximum of all the input ports. Need to pad zeros for the ports which have less data.
         input_samples_to_process =
            max_of_two(input_samples_to_process,
                       bytes_to_samples(input[in_port_index]->buf_ptr[0].actual_data_len,
                                        me_ptr->input_port_info_ptr[in_port_index].fmt.bits_per_sample));
         // minimum of all the output ports because all the output ports should have same data.
         samples_to_process =
            min_of_two(samples_to_process,
                       bytes_to_samples(output[out_port_index]->buf_ptr[0].max_data_len,
                                        me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample));
#ifdef MUX_DEMUX_TX_DEBUG_INFO
         AR_MSG_ISLAND(DBG_LOW_PRIO,
                       "[0x%lx] %lu -> %lu   input actual len %lu, output max len %lu",
                       me_ptr->miid,
                       in_port_index,
                       out_port_index,
                       input[in_port_index]->buf_ptr[0].actual_data_len,
                       output[out_port_index]->buf_ptr[0].max_data_len);
#endif
      }
   }
   samples_to_process = min_of_two(samples_to_process, input_samples_to_process);

#ifdef MUX_DEMUX_TX_DEBUG_INFO
   AR_MSG_ISLAND(DBG_LOW_PRIO, "[0x%lx] samples to process %lu.", me_ptr->miid, samples_to_process);
#endif

   // route input to output stream
   for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports && samples_to_process;
        out_port_arr_index++)
   {
      uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;
      if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
          NULL == output[out_port_index])
      {
         continue;
      }
#ifdef SIM
      if (NULL == output[out_port_index]->buf_ptr[0].data_ptr)
      {
         continue;
      }
#endif

      // maximum number of bytes copied from an input buffer to an output buffer for this output port
      uint32_t maximum_bytes_copied_from_input_port_bufs = 0;

      output[out_port_index]->flags.is_timestamp_valid = FALSE;

      for (uint32_t out_buf_index = 0; out_buf_index < output[out_port_index]->bufs_num; out_buf_index++)
      {
         bool_t is_copy = TRUE;

         // min op is done because it is possible that this port is not connected to any input port and has not
         // participated in samples_to_process calculation.
         output[out_port_index]->buf_ptr[out_buf_index].actual_data_len =
            min_of_two(output[out_port_index]->buf_ptr[0].max_data_len,
                       samples_to_bytes(samples_to_process,
                                        me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample));

         /* set buffer with silence, Needed when
          * 1. no input channel connected or
          * 2. an input channel is connected but has data less than output actual data len.
          */
         memset(output[out_port_index]->buf_ptr[out_buf_index].data_ptr,
                0,
                output[out_port_index]->buf_ptr[0].actual_data_len);

         // num of buffers can be less than num_channels
         // num of channels are derived based on the connection index
         // num of buffers are based on the output media format.
         if (out_buf_index < me_ptr->output_port_info_ptr[out_port_arr_index].fmt.num_channels)
         {
            for (uint32_t k = 0; k < me_ptr->output_port_info_ptr[out_port_arr_index]
                                        .channel_connection_ptr[out_buf_index]
                                        .num_of_connected_input_channels;
                 k++)
            {
               uint32_t input_port_index = me_ptr->output_port_info_ptr[out_port_arr_index]
                                              .channel_connection_ptr[out_buf_index]
                                              .input_connections_ptr[k]
                                              .input_port_index;
               uint32_t input_buf_index = me_ptr->output_port_info_ptr[out_port_arr_index]
                                             .channel_connection_ptr[out_buf_index]
                                             .input_connections_ptr[k]
                                             .input_channel_index;
               int32_t coeff_q15 = me_ptr->output_port_info_ptr[out_port_arr_index]
                                      .channel_connection_ptr[out_buf_index]
                                      .input_connections_ptr[k]
                                      .coeff_q15;

               if ((TRUE == me_ptr->input_port_info_ptr[input_port_index].fmt.is_valid) &&
                   (NULL != input[input_port_index]) && input_buf_index < input[input_port_index]->bufs_num)
               {
                  // samples to copy should be minimum of input and output. remaining will be zeros in output buffer.
                  uint32_t num_samples =
                     min_of_two(samples_to_process,
                                bytes_to_samples(input[input_port_index]->buf_ptr[0].actual_data_len,
                                                 me_ptr->input_port_info_ptr[input_port_index].fmt.bits_per_sample));

                  accumulate_data(input[input_port_index]->buf_ptr[input_buf_index].data_ptr,
                                  output[out_port_index]->buf_ptr[out_buf_index].data_ptr,
                                  me_ptr->input_port_info_ptr[input_port_index].fmt.q_factor,
                                  me_ptr->output_port_info_ptr[out_port_arr_index].fmt.q_factor,
                                  num_samples,
                                  coeff_q15,
                                  is_copy);
                  is_copy = FALSE;

                  maximum_bytes_copied_from_input_port_bufs =
                     max_of_two(maximum_bytes_copied_from_input_port_bufs,
                                samples_to_bytes(num_samples,
                                                 me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample));

                  if (TRUE == me_ptr->enable_ts_propagation && input[input_port_index]->flags.is_timestamp_valid &&
                      !output[out_port_index]->flags.is_timestamp_valid)
                  {
                     output[out_port_index]->timestamp                = input[input_port_index]->timestamp;
                     output[out_port_index]->flags.is_timestamp_valid = TRUE;
                  }
               }
            }
         }
      }

      // loop again and update actual data len.
      for (uint32_t out_buf_index = 0; out_buf_index < output[out_port_index]->bufs_num; out_buf_index++)
      {
         output[out_port_index]->buf_ptr[out_buf_index].actual_data_len =
            min_of_two(output[out_port_index]->buf_ptr[0].actual_data_len,
                       maximum_bytes_copied_from_input_port_bufs);

#ifdef MUX_DEMUX_TX_DEBUG_INFO
         AR_MSG_ISLAND(DBG_LOW_PRIO,
                       "[0x%lx]  output[%lu][%lu] = %lu of %lu",
                       me_ptr->miid,
                       out_port_index,
                       out_buf_index,
                       output[out_port_index]->buf_ptr[out_buf_index].actual_data_len,
                       output[out_port_index]->buf_ptr[out_buf_index].max_data_len);
#endif
      }
   }

   result = capi_mux_demux_handle_metadata(me_ptr, input, output);

   // update the buffer size of input streams
   for (uint32_t i = 0; i < me_ptr->num_of_input_ports; i++)
   {
      bool_t is_input_fully_consumed = FALSE;
      if (NULL == input[i] || NULL == input[i]->buf_ptr)
      {
         continue;
      }
      if (TRUE == me_ptr->input_port_info_ptr[i].fmt.is_valid)
      {
         uint32_t bytes_copied =
            samples_to_bytes(samples_to_process, me_ptr->input_port_info_ptr[i].fmt.bits_per_sample);
         for (uint32_t j = 0; j < input[i]->bufs_num; j++)
         {
            is_input_fully_consumed = (bytes_copied >= input[i]->buf_ptr[0].actual_data_len) ? TRUE : FALSE;
            input[i]->buf_ptr[j].actual_data_len = min_of_two(bytes_copied, input[i]->buf_ptr[0].actual_data_len);

#ifdef MUX_DEMUX_TX_DEBUG_INFO
            AR_MSG_ISLAND(DBG_LOW_PRIO,
                          "[0x%lx]  input[%lu][%lu] = %lu of %lu",
                          me_ptr->miid,
                          i,
                          j,
                          input[i]->buf_ptr[j].actual_data_len,
                          input[i]->buf_ptr[j].max_data_len);
#endif
         }
      }
      else
      {
         is_input_fully_consumed = TRUE;
         AR_MSG_ISLAND(DBG_LOW_PRIO,
                       "[0x%lx] Dropping %lu bytes from input port index %lu. this port is not connected.",
                       me_ptr->miid,
                       input[i]->buf_ptr[0].actual_data_len,
                       i);
      }

      //propagate end of frame from input to output due to non dfg/eos.
      if (is_input_fully_consumed && input[i]->flags.end_of_frame)
      {
         for (uint32_t out_port_arr_index = 0; out_port_arr_index < me_ptr->num_of_output_ports; out_port_arr_index++)
         {
            uint32_t out_port_index = me_ptr->output_port_info_ptr[out_port_arr_index].port_index;
            if (DATA_PORT_STATE_STARTED != me_ptr->output_port_info_ptr[out_port_arr_index].port_state ||
                NULL == output[out_port_index] || NULL == output[out_port_index]->buf_ptr[0].data_ptr || NULL == output[out_port_index]->buf_ptr ||
                FALSE == me_ptr->input_port_info_ptr[i].is_output_connected[out_port_arr_index])
            {
               continue;
            }
            uint32_t samples_consumed_from_input = bytes_to_samples(input[i]->buf_ptr[0].actual_data_len,
                                                                    me_ptr->input_port_info_ptr[i].fmt.bits_per_sample);
            uint32_t samples_generated_in_output =
               bytes_to_samples(output[out_port_index]->buf_ptr[0].actual_data_len,
                                me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample);

            // if all data is copied from input to output then propagate EOF.
            // if output has more data then it means that this output port is connected to other input port so don't
            // propagate EOF.
            if (samples_consumed_from_input == samples_generated_in_output)
            {
               output[out_port_index]->flags.end_of_frame = TRUE;
            }
         }
         input[i]->flags.end_of_frame = FALSE;
      }
   }

   return result;
}
