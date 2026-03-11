/**
 * \file pt_cntr_buf_utils.c
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "pt_cntr_i.h"

PT_CNTR_STATIC ar_result_t pt_cntr_assign_buffer_to_module(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr);

PT_CNTR_STATIC inline ar_result_t pt_cntr_buf_mgr_wrapper_get_buf(gen_topo_t             *topo_ptr,
                                                                  gen_topo_common_port_t *cmn_port_ptr);

PT_CNTR_STATIC pt_cntr_ext_in_port_t *pt_cntr_is_inplace_from_cur_in_to_ext_in(gen_topo_t           *topo_ptr,
                                                                               pt_cntr_input_port_t *cur_in_port_ptr);

PT_CNTR_STATIC pt_cntr_ext_out_port_t *pt_cntr_is_inplace_from_cur_out_to_ext_out(
   gen_topo_t            *topo_ptr,
   pt_cntr_output_port_t *cur_out_port_ptr);

PT_CNTR_STATIC void pt_cntr_assign_bufs_ptr(uint32_t                log_id,
                                            gen_topo_common_port_t *dst_cmn_port_ptr,
                                            gen_topo_common_port_t *src_cmn_port_ptr,
                                            gen_topo_module_t      *dst_module_ptr,
                                            uint32_t                dst_port_id)
{
#if defined(SAFE_MODE) || defined(VERBOSE_DEBUGGING)
   /**
    * Without valid MF, bufs_num is not properly assigned. E.g. prev out and next in might have
            diff bufs_num, whereas we assume them to be same. In this case, even though we see below error we can safely
    ignore it as allocated buf is not used for modules. trigger condition won't be satisfied due to invalid MF.
    */
   if (dst_cmn_port_ptr->sdata.bufs_num != src_cmn_port_ptr->sdata.bufs_num)
   {
      TOPO_MSG(log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: Port 0x%lx, bufs num are different %lu, %lu",
               dst_module_ptr->gu.module_instance_id,
               dst_port_id,
               dst_cmn_port_ptr->sdata.bufs_num,
               src_cmn_port_ptr->sdata.bufs_num);
   }
#endif

   for (uint32_t b = 0; b < dst_cmn_port_ptr->sdata.bufs_num; b++)
   {
      dst_cmn_port_ptr->bufs_ptr[b].data_ptr     = src_cmn_port_ptr->bufs_ptr[b].data_ptr;
      dst_cmn_port_ptr->bufs_ptr[b].max_data_len = src_cmn_port_ptr->bufs_ptr[b].max_data_len;
      // dst_cmn_port_ptr->bufs_ptr[b].actual_data_len = src_cmn_port_ptr->bufs_ptr[b].actual_data_len;
   }
}

PT_CNTR_STATIC ar_result_t pt_cntr_free_module_buffer(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr);

/*** PASS THRU CONTAINER BUFFER MANAGEMENT UTILS ***/

/** For all the internal data links share only one buffer for the link. That is input and connected output has the same
   buffer, since there pass thru container doesn't support internal buffering. This function also assigns sdata pointer
   at the end, the sdata pointers are also shared between input and next output, to avoid proapagating actual data
   lengths, metadata, timestamps during steady state from the current module to next.*/
ar_result_t pt_cntr_assign_port_buffers(pt_cntr_t *me_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->gc.topo;
   INIT_EXCEPTION_HANDLING

   GEN_CNTR_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Re-assigning topo buffers to the modules.");

   if (FALSE == me_ptr->flags.processing_data_path_mf)
   {
      // iterate through sorted module list and free currently assigned buffers
      for (gu_module_list_t *sorted_module_list_ptr = me_ptr->gc.topo.gu.sorted_module_list_ptr;
           NULL != sorted_module_list_ptr;
           LIST_ADVANCE(sorted_module_list_ptr))
      {
         pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)sorted_module_list_ptr->module_ptr;
         pt_cntr_free_module_buffer(me_ptr, module_ptr);
      }
   }

   // propagate any pending external output media format
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->gc.topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      pt_cntr_output_port_t   *out_port_ptr     = (pt_cntr_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      if (ext_out_port_ptr->flags.out_media_fmt_changed || out_port_ptr->gc.common.flags.media_fmt_event)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX output 0x%lx: Dropping the buf 0x%lx len %lu of %lu",
                        out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                        out_port_ptr->gc.gu.cmn.id,
                        out_port_ptr->gc.common.sdata.buf_ptr[0].data_ptr,
                        out_port_ptr->gc.common.sdata.buf_ptr[0].actual_data_len,
                        out_port_ptr->gc.common.sdata.buf_ptr[0].max_data_len);
#endif

         // drop data and propagate
         out_port_ptr->gc.common.bufs_ptr[0].data_ptr = NULL;
         pt_cntr_set_bufs_actual_len_to_zero(&out_port_ptr->gc.common.sdata);

         if (out_port_ptr->gc.common.sdata.metadata_list_ptr)
         {
            gen_topo_destroy_all_metadata(me_ptr->gc.topo.gu.log_id,
                                          (void *)out_port_ptr->gc.gu.cmn.module_ptr,
                                          &out_port_ptr->gc.common.sdata.metadata_list_ptr,
                                          TRUE /* is dropped*/);
         }

         /** return the buffer back */
         gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);

         gen_cntr_ext_out_port_apply_pending_media_fmt(me_ptr, &ext_out_port_ptr->gu);
      }
   }

   // assign buffers only to the ones added pass thru container process list
   for (gu_module_list_t *module_list_ptr = me_ptr->src_module_list_ptr; NULL != module_list_ptr;
        LIST_ADVANCE(module_list_ptr))
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;
      TRY(result, pt_cntr_assign_buffer_to_module(me_ptr, module_ptr));
   }

   // iterate through the NBLC module list
   for (gu_module_list_t *module_list_ptr = me_ptr->module_proc_list_ptr; NULL != module_list_ptr;
        LIST_ADVANCE(module_list_ptr))
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;
      TRY(result, pt_cntr_assign_buffer_to_module(me_ptr, module_ptr));
   }

   // iterate through sink module list
   for (gu_module_list_t *module_list_ptr = me_ptr->sink_module_list_ptr; NULL != module_list_ptr;
        LIST_ADVANCE(module_list_ptr))
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;
      TRY(result, pt_cntr_assign_buffer_to_module(me_ptr, module_ptr));
   }

   // During data path Mf propagation, repropagate the buffers from external ports based on the updated NBLC flgas
   if (TRUE == me_ptr->flags.processing_data_path_mf)
   {
      for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->gc.topo.gu.ext_in_port_list_ptr;
           (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         pt_cntr_ext_in_port_t *ext_in_port_ptr = (pt_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         pt_cntr_input_port_t  *in_port_ptr     = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
         capi_stream_data_v2_t *sdata_ptr       = in_port_ptr->sdata_ptr;

         // if external input buffer is already assigned propagate forwards
         pt_cntr_propagate_ext_input_buffer_forwards_non_static(me_ptr,
                                                                in_port_ptr,
                                                                sdata_ptr,
                                                                in_port_ptr->sdata_ptr->bufs_num);
      }

      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->gc.topo.gu.ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         pt_cntr_ext_out_port_t *ext_out_port_ptr = (pt_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         pt_cntr_output_port_t  *out_port_ptr     = (pt_cntr_output_port_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr;
         capi_stream_data_v2_t  *sdata_ptr        = out_port_ptr->sdata_ptr;

         // if buffer got returned due to MF change, check and setup the ext output with the newly created output
         // buffers if buffer is already allocated it wouldnt pop a new buffer.
         result = pt_cntr_preprocess_setup_ext_output_non_static(me_ptr,
                                                                 (pt_cntr_module_t *)out_port_ptr->gc.gu.cmn.module_ptr,
                                                                 ext_out_port_ptr);

         pt_cntr_propagate_ext_output_buffer_backwards_non_static(me_ptr,
                                                                  out_port_ptr,
                                                                  sdata_ptr,
                                                                  out_port_ptr->sdata_ptr->bufs_num);
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }
   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_assign_buffer_to_module(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->gc.topo;
   INIT_EXCEPTION_HANDLING

   for (gu_input_port_list_t *ip_list_ptr = module_ptr->gc.topo.gu.input_port_list_ptr; NULL != ip_list_ptr;
        LIST_ADVANCE(ip_list_ptr))
   {
      pt_cntr_input_port_t   *in_port_ptr                = (pt_cntr_input_port_t *)ip_list_ptr->ip_port_ptr;
      pt_cntr_ext_in_port_t  *ext_in_port_ptr            = (pt_cntr_ext_in_port_t *)in_port_ptr->gc.gu.ext_in_port_ptr;
      pt_cntr_output_port_t  *prev_out_port_ptr          = pt_cntr_get_connected_output_port(in_port_ptr);
      pt_cntr_ext_in_port_t  *nblc_start_ext_in_port_ptr = NULL;
      pt_cntr_ext_out_port_t *nblc_end_ext_out_port_ptr  = NULL;

      // Assign sdata for the ports and also set pass_thru_upstream_buffer if pass thru container.
      uint32_t upstream_pcm_frame_len_bytes = 0;
      if (ext_in_port_ptr)
      {
         in_port_ptr->sdata_ptr                                       = &in_port_ptr->gc.common.sdata;
         module_ptr->in_port_sdata_pptr[in_port_ptr->gc.gu.cmn.index] = &in_port_ptr->gc.common.sdata;

         // check upstreams's PCM frame length
         if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->gc.cu.media_fmt.data_format))
         {
            upstream_pcm_frame_len_bytes = topo_us_to_bytes(ext_in_port_ptr->gc.cu.upstream_frame_len.frame_len_us,
                                                            in_port_ptr->gc.common.media_fmt_ptr);
         }

         // check if PCM data needs reframing at external inputs
         if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->gc.cu.media_fmt.data_format) &&
             (upstream_pcm_frame_len_bytes != in_port_ptr->gc.common.max_buf_len))
         {
            ext_in_port_ptr->pass_thru_upstream_buffer = FALSE;
         }
         else
         {
            ext_in_port_ptr->pass_thru_upstream_buffer = TRUE;
         }

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Assigned self sdata ptr 0x%lx to module 0x%lx in_port_id 0x%x is_pass_thru ? %lu ",
                  in_port_ptr->sdata_ptr,
                  module_ptr->gc.topo.gu.module_instance_id,
                  in_port_ptr->gc.gu.cmn.id,
                  ext_in_port_ptr->pass_thru_upstream_buffer);
#endif
      }
      else // internal ports
      {
         in_port_ptr->sdata_ptr                                       = &prev_out_port_ptr->gc.common.sdata;
         module_ptr->in_port_sdata_pptr[in_port_ptr->gc.gu.cmn.index] = &prev_out_port_ptr->gc.common.sdata;

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Assigned prev module 0x%lx out_port_id 0x%x sdata ptr 0x%lx to module 0x%lx in_port_id 0x%x",
                  prev_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                  prev_out_port_ptr->gc.gu.cmn.id,
                  in_port_ptr->sdata_ptr,
                  module_ptr->gc.topo.gu.module_instance_id,
                  in_port_ptr->gc.gu.cmn.id);
#endif
      }

      // skip assigning a new buffer if the media format is not set
      if ((FALSE == in_port_ptr->gc.common.flags.is_mf_valid) ||
          (TOPO_PORT_STATE_STARTED != in_port_ptr->gc.common.state))
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! (MID:0x%x, port:0x%lx) has invalid media format, not assignig buffer",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id);
         continue;
      }

      // reset the ext buffer propagation flag
      in_port_ptr->can_assign_ext_in_buffer  = FALSE;
      in_port_ptr->can_assign_ext_out_buffer = FALSE;
      if (prev_out_port_ptr && (NULL != (nblc_end_ext_out_port_ptr =
                                            pt_cntr_is_inplace_from_cur_out_to_ext_out(topo_ptr, prev_out_port_ptr))))
      {
         in_port_ptr->can_assign_ext_out_buffer = TRUE;
      }
      else if ((NULL !=
                (nblc_start_ext_in_port_ptr = pt_cntr_is_inplace_from_cur_in_to_ext_in(topo_ptr, in_port_ptr))) &&
               nblc_start_ext_in_port_ptr->pass_thru_upstream_buffer)
      {
         in_port_ptr->can_assign_ext_in_buffer = TRUE;
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "module 0x%lx in_port_id 0x%x can_assign (ext_out_buffer %lu, ext_in_buffer %lu)  ",
               module_ptr->gc.topo.gu.module_instance_id,
               in_port_ptr->gc.gu.cmn.id,
               in_port_ptr->can_assign_ext_out_buffer,
               in_port_ptr->can_assign_ext_in_buffer);
#endif

      // if data path media format change, no need to assign a buffer on the port already assigned.
      if (me_ptr->flags.processing_data_path_mf && in_port_ptr->gc.common.bufs_ptr[0].data_ptr)
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! (MID:0x%x, port:0x%lx) already has a buffer 0x%lx origin %lu skipping assignment"
                      "during data path MF prop",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.flags.buf_origin);
         continue;
      }

      // if external input check if it can be pass thru, else assign topo buffer
      if ((NULL == prev_out_port_ptr) && ext_in_port_ptr)
      {
         // max length will be updated later when the ext buffer is populated in the port.
         // length must be set here for the underrun scenarios
         for (uint32_t i = 0; i < in_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            in_port_ptr->gc.common.bufs_ptr[i].max_data_len = in_port_ptr->gc.common.max_buf_len_per_buf;
         }

         if (ext_in_port_ptr->topo_in_buf_ptr)
         {
            // return buffer
            topo_buf_manager_return_buf(&me_ptr->gc.topo, ext_in_port_ptr->topo_in_buf_ptr);
            ext_in_port_ptr->topo_in_buf_ptr = NULL;
         }

         // this can happen b4 thresh/MF prop
         if (in_port_ptr->gc.common.max_buf_len)
         {
            // check if PCM data needs reframing at external inputs
            if (FALSE == ext_in_port_ptr->pass_thru_upstream_buffer)
            {
               TRY(result, pt_cntr_buf_mgr_wrapper_get_buf(topo_ptr, &in_port_ptr->gc.common));
            }
            else // do pass thru
            {
               in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

               // for non pass thru if external output is not present we need a topo buffer to underrun
               TRY(result,
                   topo_buf_manager_get_buf(topo_ptr,
                                            &ext_in_port_ptr->topo_in_buf_ptr,
                                            in_port_ptr->gc.common.max_buf_len));
            }
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) external input US frame len (%luus, %lu bytes) DS frame len (%lu bytes) "
                      "is pass thru mode ? %lu  ",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      ext_in_port_ptr->gc.cu.upstream_frame_len.frame_len_us,
                      upstream_pcm_frame_len_bytes,
                      in_port_ptr->gc.common.max_buf_len,
                      ext_in_port_ptr->pass_thru_upstream_buffer);

         if (ext_in_port_ptr->topo_in_buf_ptr)
         {
            GEN_CNTR_MSG(topo_ptr->gu.log_id,
                         DBG_HIGH_PRIO,
                         "(MID:0x%x, port:0x%lx) uses external input buffer for processing, allocated "
                         "underrun buf ptr: 0x%lx size: %lu origin: %lu",
                         module_ptr->gc.topo.gu.module_instance_id,
                         in_port_ptr->gc.gu.cmn.id,
                         ext_in_port_ptr->topo_in_buf_ptr,
                         in_port_ptr->gc.common.max_buf_len,
                         in_port_ptr->gc.common.flags.buf_origin);
         }
      }
      // check if current input is in the ext output facing NBLC path
      // note: as long as current input is in the NBLC path of the external output port there is no
      // need to check the frame lengths/thresholds, because the external buffers are assgined based
      // on the container threshold and must be sufficient to process the NBLC modules.
      else if (in_port_ptr->can_assign_ext_out_buffer)
      {
         // initialize the buffer origin, buffer ptr will be propagated to this port
         // during process
         in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses external output buffer for processing ptr: 0x%lx size: %lu origin: "
                      "%lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
      /** check if NBLC start is an external input that module can borrow buffer from */
      else if (in_port_ptr->can_assign_ext_in_buffer)
      {
         // in this case buffer will be propagated during the process
         in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses external input buffer for processing ptr: 0x%lx size: %lu origin: "
                      "%lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
      else if (prev_out_port_ptr && (GEN_TOPO_BUF_ORIGIN_BUF_MGR == prev_out_port_ptr->gc.common.flags.buf_origin))
      {
         // borrow buffer from prev module's output port
         pt_cntr_assign_bufs_ptr(topo_ptr->gu.log_id,
                                 &in_port_ptr->gc.common,
                                 &prev_out_port_ptr->gc.common,
                                 (gen_topo_module_t *)in_port_ptr->gc.gu.cmn.module_ptr,
                                 in_port_ptr->gc.gu.cmn.id);
         in_port_ptr->gc.common.flags.buf_origin = prev_out_port_ptr->gc.common.flags.buf_origin;
         gen_topo_buf_mgr_wrapper_inc_ref_count(&in_port_ptr->gc.common);

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses previous output buffer for processing ptr: 0x%lx size: %lu origin: "
                      "%lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
      else if (prev_out_port_ptr &&
               (GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS == prev_out_port_ptr->gc.common.flags.supports_buffer_resuse_extn))
      {
         // gets buffer from prev module during process
         in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED;
         for (uint32_t i = 0; i < in_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            in_port_ptr->gc.common.bufs_ptr[i].max_data_len = in_port_ptr->gc.common.max_buf_len_per_buf;
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses Previous Module's internal buffer for processing ptr: 0x%lx size: "
                      "%lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
      else if (GEN_TOPO_MODULE_INPUT_BUF_ACCESS == in_port_ptr->gc.common.flags.supports_buffer_resuse_extn)
      {
         // gets buffer from the current module during process
         in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_CAPI_MODULE;
         for (uint32_t i = 0; i < in_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            in_port_ptr->gc.common.bufs_ptr[i].max_data_len = in_port_ptr->gc.common.max_buf_len_per_buf;
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses Current Module's internal buffer for processing ptr: 0x%lx size: "
                      "%lu "
                      "origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
      else
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      "Warning! (MID:0x%x, port:0x%lx) buffer is not assigned buf ptr: 0x%lx size: %lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      in_port_ptr->gc.common.max_buf_len,
                      in_port_ptr->gc.common.flags.buf_origin);
      }
   }

   for (gu_output_port_list_t *op_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr; NULL != op_list_ptr;
        LIST_ADVANCE(op_list_ptr))
   {
      pt_cntr_output_port_t  *out_port_ptr              = (pt_cntr_output_port_t *)op_list_ptr->op_port_ptr;
      pt_cntr_input_port_t   *next_in_port_ptr          = (pt_cntr_input_port_t *)out_port_ptr->gc.gu.conn_in_port_ptr;
      pt_cntr_ext_out_port_t *nblc_end_ext_out_port_ptr = NULL;

      /** Firstly assign sdata pointer to output, outputs sdata will be it own sdata */
      out_port_ptr->sdata_ptr                                        = &out_port_ptr->gc.common.sdata;
      module_ptr->out_port_sdata_pptr[out_port_ptr->gc.gu.cmn.index] = &out_port_ptr->gc.common.sdata;

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Assigned self sdata ptr 0x%lx to module 0x%lx out_port_id 0x%x",
               out_port_ptr->sdata_ptr,
               module_ptr->gc.topo.gu.module_instance_id,
               out_port_ptr->gc.gu.cmn.id);
#endif

      // check if the current input is for inplace module, so that the topo buffer assigned to modules input can be
      // assigned to the output as well.
      //
      // important :module having an external input or modules next to the external input attached module's will use
      // external input buffers for processing hence input buffer cannot be propagated inplace at this point,
      // possible to propagate the external input buffer only in the process context.
      //
      // also since modules are iterated through sorted order, prev output is expected to have a topo buffer assigned
      // already if conditions were met.
      pt_cntr_input_port_t *cur_mod_inplace_in_port_ptr = NULL;
      if (pt_cntr_is_inplace_or_disabled_siso(module_ptr))
      {
         cur_mod_inplace_in_port_ptr = (pt_cntr_input_port_t *)module_ptr->gc.topo.gu.input_port_list_ptr->ip_port_ptr;
      }

      out_port_ptr->can_assign_ext_in_buffer  = FALSE;
      out_port_ptr->can_assign_ext_out_buffer = FALSE;
      if (NULL == next_in_port_ptr)
      {
         // if ext output port can assign ext buffer to cur output port
         out_port_ptr->can_assign_ext_out_buffer = TRUE;
      }
      if (NULL != (nblc_end_ext_out_port_ptr = pt_cntr_is_inplace_from_cur_out_to_ext_out(topo_ptr, out_port_ptr)))
      {
         //  can assign ext buffer to the cur output if NBLC end is ext output
         out_port_ptr->can_assign_ext_out_buffer = TRUE;
      }
      else if (cur_mod_inplace_in_port_ptr && cur_mod_inplace_in_port_ptr->can_assign_ext_in_buffer)
      {
         // can assign ext buffer to cur output if cur module is inplace and cur module's input can reuse ext in buffer
         out_port_ptr->can_assign_ext_in_buffer = TRUE;
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "module 0x%lx out_port_id 0x%x can_assign (ext_out_buffer %lu, ext_in_buffer %lu)  ",
               module_ptr->gc.topo.gu.module_instance_id,
               out_port_ptr->gc.gu.cmn.id,
               out_port_ptr->can_assign_ext_out_buffer,
               out_port_ptr->can_assign_ext_in_buffer);
#endif

      // skip assigning a new buffer if the media format is not set
      if ((FALSE == out_port_ptr->gc.common.flags.is_mf_valid) ||
          (TOPO_PORT_STATE_STARTED != out_port_ptr->gc.common.state))
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! (MID:0x%x, port:0x%lx) has invalid media format, not assignig buffer",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id);
         continue;
      }

      // if data path media format no need to assign buffer if already assigned, if there was a threshold propagation
      // buffer would have been reassigned already
      if (me_ptr->flags.processing_data_path_mf && out_port_ptr->gc.common.bufs_ptr[0].data_ptr)
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! (MID:0x%x, port:0x%lx) already has a buffer 0x%lx origin %lu skipping assignment "
                      "during data path MF prop",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.flags.buf_origin);
         continue;
      }

      // if external output
      if (NULL == next_in_port_ptr)
      {
         out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

         // must be set here, will be updated later when ext out buffer is resused.
         // if output buffer is not available at start, it will overrun based on the max length provided
         for (uint32_t i = 0; i < out_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            out_port_ptr->gc.common.bufs_ptr[i].max_data_len = out_port_ptr->gc.common.max_buf_len_per_buf;
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses external output buffer for processing ptr: 0x%lx size: %lu "
                      "origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      else if (out_port_ptr->can_assign_ext_out_buffer)
      {
         // in this case buffer will be propagated during the process
         out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses NBLC external output buffer for processing ptr: 0x%lx size: %lu "
                      "origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      else if (cur_mod_inplace_in_port_ptr && cur_mod_inplace_in_port_ptr->can_assign_ext_in_buffer)
      {
         // in this case buffer will be propagated during the process
         out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses NBLC external input buffer for processing ptr: 0x%lx size: %lu "
                      "origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      // if is inplace internal module assign module
      else if (cur_mod_inplace_in_port_ptr &&
               (GEN_TOPO_BUF_ORIGIN_BUF_MGR == cur_mod_inplace_in_port_ptr->gc.common.flags.buf_origin))
      {
         pt_cntr_assign_bufs_ptr(topo_ptr->gu.log_id,
                                 &out_port_ptr->gc.common,
                                 &cur_mod_inplace_in_port_ptr->gc.common,
                                 (gen_topo_module_t *)out_port_ptr->gc.gu.cmn.module_ptr,
                                 out_port_ptr->gc.gu.cmn.id);
         out_port_ptr->gc.common.flags.buf_origin = cur_mod_inplace_in_port_ptr->gc.common.flags.buf_origin;
         gen_topo_buf_mgr_wrapper_inc_ref_count(&out_port_ptr->gc.common);

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses current inplace module's input buffer for processing ptr: 0x%lx "
                      "size: %lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      else if (GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS == out_port_ptr->gc.common.flags.supports_buffer_resuse_extn)
      {
         // gets buffer from the current module during process
         out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_CAPI_MODULE;
         for (uint32_t i = 0; i < out_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            out_port_ptr->gc.common.bufs_ptr[i].max_data_len = out_port_ptr->gc.common.max_buf_len_per_buf;
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses current modules internal output buffer for processing ptr: 0x%lx "
                      "size: %lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      else if (GEN_TOPO_MODULE_INPUT_BUF_ACCESS == next_in_port_ptr->gc.common.flags.supports_buffer_resuse_extn &&
               (TOPO_PORT_STATE_STARTED == next_in_port_ptr->gc.common.state))
      {
         // gets buffer from next module's input during process
         out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED;
         for (uint32_t i = 0; i < out_port_ptr->gc.common.sdata.bufs_num; i++)
         {
            out_port_ptr->gc.common.bufs_ptr[i].max_data_len = out_port_ptr->gc.common.max_buf_len_per_buf;
         }

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses next modules internal input buffer for processing ptr: 0x%lx "
                      "size: %lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
      else // if module is not inplace and is an internal output port assign topo buffer
      {
         TRY(result, pt_cntr_buf_mgr_wrapper_get_buf(topo_ptr, &out_port_ptr->gc.common));

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "(MID:0x%x, port:0x%lx) uses topo buf mgr buffer for processing ptr: 0x%lx "
                      "size: %lu origin: %lu",
                      module_ptr->gc.topo.gu.module_instance_id,
                      out_port_ptr->gc.gu.cmn.id,
                      out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      out_port_ptr->gc.common.max_buf_len,
                      out_port_ptr->gc.common.flags.buf_origin);
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_free_module_buffer(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->gc.topo;

   for (gu_input_port_list_t *ip_list_ptr = module_ptr->gc.topo.gu.input_port_list_ptr; NULL != ip_list_ptr;
        LIST_ADVANCE(ip_list_ptr))
   {
      pt_cntr_input_port_t *in_port_ptr = (pt_cntr_input_port_t *)ip_list_ptr->ip_port_ptr;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "Freering Module 0x%lx port id 0x%lx buf 0x%lx origin %lu",
                   module_ptr->gc.topo.gu.module_instance_id,
                   in_port_ptr->gc.gu.cmn.id,
                   in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                   in_port_ptr->gc.common.flags.buf_origin);
#endif

      if (in_port_ptr->gc.common.bufs_ptr[0].data_ptr &&
          (GEN_TOPO_BUF_ORIGIN_BUF_MGR == in_port_ptr->gc.common.flags.buf_origin))
      {
         gen_topo_buf_mgr_wrapper_dec_ref_count_return(topo_ptr,
                                                       in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                                                       in_port_ptr->gc.gu.cmn.id,
                                                       &in_port_ptr->gc.common);
      }

      // clear existing buffer pointer, could be stale ptr provided by module through extension.
      in_port_ptr->gc.common.flags.buf_origin = 0;
      for (uint32_t b = 0; b < in_port_ptr->gc.common.sdata.bufs_num; b++)
      {
         in_port_ptr->gc.common.bufs_ptr[b].data_ptr        = NULL;
         in_port_ptr->gc.common.bufs_ptr[b].actual_data_len = 0;
         in_port_ptr->gc.common.bufs_ptr[b].max_data_len    = 0;
      }

      if (in_port_ptr->gc.common.sdata.metadata_list_ptr)
      {
         gen_topo_destroy_all_metadata(me_ptr->gc.topo.gu.log_id,
                                       (void *)in_port_ptr->gc.gu.cmn.module_ptr,
                                       &in_port_ptr->gc.common.sdata.metadata_list_ptr,
                                       TRUE /* is dropped*/);
      }

      in_port_ptr->can_assign_ext_in_buffer  = FALSE;
      in_port_ptr->can_assign_ext_out_buffer = FALSE;

      if (in_port_ptr->gc.gu.ext_in_port_ptr)
      {
         ((pt_cntr_ext_in_port_t *)in_port_ptr->gc.gu.ext_in_port_ptr)->pass_thru_upstream_buffer = FALSE;
      }
   }

   for (gu_output_port_list_t *op_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr; NULL != op_list_ptr;
        LIST_ADVANCE(op_list_ptr))
   {
      pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)op_list_ptr->op_port_ptr;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "Freering Module 0x%lx port id 0x%lx buf 0x%lx origin %lu",
                   module_ptr->gc.topo.gu.module_instance_id,
                   out_port_ptr->gc.gu.cmn.id,
                   out_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                   out_port_ptr->gc.common.flags.buf_origin);
#endif

      // return previous assigned topo buffer
      if (out_port_ptr->gc.common.bufs_ptr[0].data_ptr &&
          (GEN_TOPO_BUF_ORIGIN_BUF_MGR == out_port_ptr->gc.common.flags.buf_origin))
      {
         gen_topo_buf_mgr_wrapper_dec_ref_count_return(topo_ptr,
                                                       out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                                                       out_port_ptr->gc.gu.cmn.id,
                                                       &out_port_ptr->gc.common);
      }

      out_port_ptr->gc.common.flags.buf_origin = 0;
      for (uint32_t b = 0; b < out_port_ptr->gc.common.sdata.bufs_num; b++)
      {
         out_port_ptr->gc.common.bufs_ptr[b].data_ptr        = NULL;
         out_port_ptr->gc.common.bufs_ptr[b].actual_data_len = 0;
         out_port_ptr->gc.common.bufs_ptr[b].max_data_len    = 0;
      }

      if (out_port_ptr->gc.common.sdata.metadata_list_ptr)
      {
         gen_topo_destroy_all_metadata(me_ptr->gc.topo.gu.log_id,
                                       (void *)out_port_ptr->gc.gu.cmn.module_ptr,
                                       &out_port_ptr->gc.common.sdata.metadata_list_ptr,
                                       TRUE /* is dropped*/);
      }

      out_port_ptr->can_assign_ext_in_buffer  = FALSE;
      out_port_ptr->can_assign_ext_out_buffer = FALSE;
   }
   return result;
}

PT_CNTR_STATIC inline ar_result_t pt_cntr_buf_mgr_wrapper_get_buf(gen_topo_t             *topo_ptr,
                                                                  gen_topo_common_port_t *cmn_port_ptr)
{
   int8_t *ptr = NULL;

   // this can happen b4 thresh/MF prop
   if (0 == cmn_port_ptr->max_buf_len)
   {
      return AR_EOK;
   }

   // internally mem needed for topo_buf_mgr_element_t is counted.
   // Also ref count is initialized to 1.
   ar_result_t result = topo_buf_manager_get_buf(topo_ptr, &ptr, cmn_port_ptr->max_buf_len);

   if (ptr)
   {
      cmn_port_ptr->bufs_ptr[0].data_ptr = ptr;
      cmn_port_ptr->flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_BUF_MGR;

      for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
      {
         cmn_port_ptr->bufs_ptr[b].data_ptr =
            cmn_port_ptr->bufs_ptr[0].data_ptr + b * cmn_port_ptr->max_buf_len_per_buf;
         // cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
         cmn_port_ptr->bufs_ptr[b].max_data_len = cmn_port_ptr->max_buf_len_per_buf;
      }
   }

   return result;
}

/** Iterates forwards from the given output till the external output port, and checks if the modules are inplace. */
PT_CNTR_STATIC pt_cntr_ext_out_port_t *pt_cntr_is_inplace_from_cur_out_to_ext_out(
   gen_topo_t            *topo_ptr,
   pt_cntr_output_port_t *cur_out_port_ptr)
{
   /** If current output iterator reached external output terminate the loop */
   while (!cur_out_port_ptr->gc.gu.ext_out_port_ptr)
   {
      /** check if the next module is inplace*/
      pt_cntr_module_t *next_module_ptr = (pt_cntr_module_t *)cur_out_port_ptr->gc.gu.conn_in_port_ptr->cmn.module_ptr;
      if (!pt_cntr_is_inplace_or_disabled_siso(next_module_ptr))
      {
         return NULL;
      }

      if (next_module_ptr->gc.topo.gu.output_port_list_ptr)
      {
         pt_cntr_output_port_t *next_out_port_ptr =
            (pt_cntr_output_port_t *)next_module_ptr->gc.topo.gu.output_port_list_ptr->op_port_ptr;

         /** update iterator and continue the loop*/
         cur_out_port_ptr = next_out_port_ptr;
         continue;
      }
      else
      {
         /** unexpected, didnt reach the external output portential issuet */
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with NBLC assignment, reached unexpected sink module"
                      "(MIID)(0x%lx)",
                      cur_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gc.gu.cmn.id);
         return NULL;
      }
   }

   return (pt_cntr_ext_out_port_t *)cur_out_port_ptr->gc.gu.ext_out_port_ptr;
}

/** Iterate backwards from the given input till the external input port and checks if modules are inplace. */
PT_CNTR_STATIC pt_cntr_ext_in_port_t *pt_cntr_is_inplace_from_cur_in_to_ext_in(gen_topo_t           *topo_ptr,
                                                                               pt_cntr_input_port_t *cur_in_port_ptr)
{
   /** If current input iterator reached external input terminate the loop */
   while (!cur_in_port_ptr->gc.gu.ext_in_port_ptr)
   {
      /** check if the prev module is inplace. */
      pt_cntr_module_t *prev_module_ptr = (pt_cntr_module_t *)cur_in_port_ptr->gc.gu.conn_out_port_ptr->cmn.module_ptr;
      if (FALSE == pt_cntr_is_inplace_or_disabled_siso(prev_module_ptr))
      {
         return NULL;
      }

      if (prev_module_ptr->gc.topo.gu.input_port_list_ptr)
      {
         pt_cntr_input_port_t *prev_in_port_ptr =
            (pt_cntr_input_port_t *)prev_module_ptr->gc.topo.gu.input_port_list_ptr->ip_port_ptr;

         /** update iterator and continue the loop*/
         cur_in_port_ptr = prev_in_port_ptr;
         continue;
      }
      else
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with ext input NBLC check, reached unexpected source module "
                      "(MIID 0xx%lx)",
                      prev_module_ptr->gc.topo.gu.module_instance_id);
         return NULL;
      }
   }

   return (pt_cntr_ext_in_port_t *)cur_in_port_ptr->gc.gu.ext_in_port_ptr;
}
