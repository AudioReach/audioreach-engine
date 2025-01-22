/**
 * \file pt_cntr_process.c
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "pt_cntr_i.h"
#include "irm_cntr_prof_util.h"

// enable static to get accurate savings.
#define PT_CNTR_STATIC static

#define PT_CNTR_TIME_PROP_ENABLE

PT_CNTR_STATIC ar_result_t pt_cntr_preprocess_setup_ext_output(pt_cntr_t              *me_ptr,
                                                               pt_cntr_module_t       *module_ptr,
                                                               pt_cntr_ext_out_port_t *ext_out_port_ptr);

PT_CNTR_STATIC ar_result_t pt_cntr_preprocess_setup_ext_input(pt_cntr_t             *me_ptr,
                                                              pt_cntr_module_t      *module_ptr,
                                                              pt_cntr_ext_in_port_t *ext_in_port_ptr);

PT_CNTR_STATIC ar_result_t pt_cntr_pop_and_setup_ext_in_buffer_for_pass_thru(pt_cntr_t             *me_ptr,
                                                                             pt_cntr_ext_in_port_t *ext_in_port_ptr);

PT_CNTR_STATIC ar_result_t pt_cntr_setup_ext_in_sdata_for_pass_thru(pt_cntr_t             *me_ptr,
                                                                    pt_cntr_ext_in_port_t *ext_in_port_ptr);

PT_CNTR_STATIC ar_result_t pt_cntr_copy_n_setup_ext_in_sdata_for_non_pass_thru(pt_cntr_t             *me_ptr,
                                                                               pt_cntr_ext_in_port_t *ext_in_port_ptr);

PT_CNTR_STATIC ar_result_t
pt_cntr_pop_and_setup_ext_in_buffer_for_non_pass_thru(pt_cntr_t *me_ptr, pt_cntr_ext_in_port_t *ext_in_port_ptr);

PT_CNTR_STATIC void pt_cntr_propagate_ext_input_buffer_forwards(pt_cntr_t             *me_ptr,
                                                                pt_cntr_input_port_t  *start_in_port_ptr,
                                                                capi_stream_data_v2_t *sdata_ptr,
                                                                uint32_t               num_bufs_to_update);

PT_CNTR_STATIC void pt_cntr_propagate_ext_output_buffer_backwards(pt_cntr_t             *me_ptr,
                                                                  pt_cntr_output_port_t *end_output_port_ptr,
                                                                  capi_stream_data_v2_t *sdata_ptr,
                                                                  uint32_t               num_bufs_to_update);

static inline bool_t pt_cntr_check_if_output_needs_post_process(pt_cntr_module_t *module_ptr)
{
   // todo: optimize the check with preprocess offset macros
   return module_ptr->flags.has_attached_module || module_ptr->flags.has_stopped_port;
}

void pt_cntr_propagate_ext_output_buffer_backwards_non_static(pt_cntr_t             *me_ptr,
                                                              pt_cntr_output_port_t *end_output_port_ptr,
                                                              capi_stream_data_v2_t *sdata_ptr,
                                                              uint32_t               num_bufs_to_update)
{
   pt_cntr_propagate_ext_output_buffer_backwards(me_ptr, end_output_port_ptr, sdata_ptr, num_bufs_to_update);
}

PT_CNTR_STATIC void pt_cntr_propagate_ext_output_buffer_backwards(pt_cntr_t             *me_ptr,
                                                                  pt_cntr_output_port_t *end_output_port_ptr,
                                                                  capi_stream_data_v2_t *sdata_ptr,
                                                                  uint32_t               num_bufs_to_update)
{

#ifdef PT_CNTR_SAFE_MODE
   uint32_t log_id = me_ptr->gc.topo.gu.log_id;
#endif

   pt_cntr_output_port_t *cur_out_port_ptr        = end_output_port_ptr;
   capi_buf_t            *end_output_port_buf_ptr = sdata_ptr->buf_ptr;
   do
   {
      if (!cur_out_port_ptr->gc.gu.cmn.module_ptr->input_port_list_ptr)
      {
         // hit a source module cannot propagate further backwards
         return;
      }

      pt_cntr_input_port_t *cur_in_port_ptr =
         (pt_cntr_input_port_t *)cur_out_port_ptr->gc.gu.cmn.module_ptr->input_port_list_ptr->ip_port_ptr;

      if (FALSE == cur_in_port_ptr->can_assign_ext_out_buffer)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext output buffer propagation: cannot propagate across to input (MIID,Port):(0x%lX,%lx) it uses "
                      "diff buf: 0x%lx origin:%lu",
                      cur_in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gc.gu.cmn.id,
                      cur_in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      cur_in_port_ptr->gc.common.flags.buf_origin);
#endif
         return;
      }

#ifdef PT_CNTR_SAFE_MODE
      /** Note that buf pointers are not cleared from port unless safe mode is enabled.*/
      if (cur_in_port_ptr->sdata_ptr->buf_ptr[0].data_ptr)
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext output buffer propagation: cannot propagate across to input (MIID,Port):(0x%lX,%lx) "
                      "already has an ext buffer assigned 0x%lx origin:%lu",
                      cur_in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gc.gu.cmn.id,
                      cur_in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                      cur_in_port_ptr->gc.common.flags.buf_origin);
      }
#endif

      // assigning buffer to input will automatically assing buffer to connected/previous module's output since
      // they share the same sdata ptr.
      pt_cntr_output_port_t *prev_mod_out_port_ptr = (pt_cntr_output_port_t *)cur_in_port_ptr->gc.gu.conn_out_port_ptr;
      for (uint32_t b = 0; b < num_bufs_to_update; b++)
      {
         cur_in_port_ptr->sdata_ptr->buf_ptr[b].data_ptr     = end_output_port_buf_ptr[b].data_ptr;
         cur_in_port_ptr->sdata_ptr->buf_ptr[b].max_data_len = end_output_port_buf_ptr[b].max_data_len;
      }
#ifdef PT_CNTR_SAFE_MODE_ERR_RECOVERY
      cur_in_port_ptr->gc.common.flags.buf_origin       = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
      prev_mod_out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
#endif

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Propagated ext out (buf:0x%lx, ori:%lu) to cur mod input (0x%lX, %lx) and prev mod output (0x%lX, "
                   "%lx) "
                   "already has an ext buffer assigned",
                   cur_in_port_ptr->gc.common.bufs_ptr[0].data_ptr,
                   cur_in_port_ptr->gc.common.flags.buf_origin,
                   cur_in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   cur_in_port_ptr->gc.gu.cmn.id,
                   prev_mod_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   prev_mod_out_port_ptr->gc.gu.cmn.id);
#endif

      cur_out_port_ptr = prev_mod_out_port_ptr;
   } while (cur_out_port_ptr);

   return;
}

void pt_cntr_propagate_ext_input_buffer_forwards_non_static(pt_cntr_t             *me_ptr,
                                                            pt_cntr_input_port_t  *start_in_port_ptr,
                                                            capi_stream_data_v2_t *sdata_ptr,
                                                            uint32_t               num_bufs_to_update)
{
   pt_cntr_propagate_ext_input_buffer_forwards(me_ptr, start_in_port_ptr, sdata_ptr, num_bufs_to_update);
}

PT_CNTR_STATIC void pt_cntr_propagate_ext_input_buffer_forwards(pt_cntr_t             *me_ptr,
                                                                pt_cntr_input_port_t  *start_in_port_ptr,
                                                                capi_stream_data_v2_t *sdata_ptr,
                                                                uint32_t               num_bufs_to_update)
{
#ifdef PT_CNTR_SAFE_MODE
   uint32_t log_id = me_ptr->gc.topo.gu.log_id;
#endif
   /** propagate the ext buf ptr to NBLC in the FORWARD direction.*/
   capi_buf_t *buf_ptr = sdata_ptr->buf_ptr;

   pt_cntr_input_port_t *cur_in_port_ptr = start_in_port_ptr;
   do
   {
      // current input port is already assigned check if its can be propagted to the across
      // current module to its output port

      if (!cur_in_port_ptr->gc.gu.cmn.module_ptr->output_port_list_ptr)
      {
         // hit a sink module cannot propagate further forwards
         return;
      }

      pt_cntr_output_port_t *cur_out_port_ptr =
         (pt_cntr_output_port_t *)cur_in_port_ptr->gc.gu.cmn.module_ptr->output_port_list_ptr->op_port_ptr;
      if (FALSE == cur_out_port_ptr->can_assign_ext_in_buffer)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext input buffer propagation: cannot propagate across to output (MIID,Port):(0x%lX,%lx) "
                      "can_assign_ext_in_buffer: %lu",
                      cur_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gc.gu.cmn.id,
                      cur_out_port_ptr->can_assign_ext_in_buffer);
#endif
         return;
      }

#ifdef PT_CNTR_SAFE_MODE
      /** Note that buf pointers are not cleared from port unless safe mode is enabled.*/
      if (cur_out_port_ptr->sdata_ptr->buf_ptr[0].data_ptr)
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "ext input buffer propagation: cannot propagate across to output (MIID,Port):(0x%lX,%lx) "
                      "already "
                      "has an ext buffer assigned 0x%lx origin:%lu",
                      cur_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gc.gu.cmn.id,
                      cur_out_port_ptr->gc.common.bufs_ptr[b].data_ptr,
                      cur_out_port_ptr->gc.common.flags.buf_origin);
      }
#endif

      /** set buffer to the connected/next module's input */
      pt_cntr_input_port_t *next_in_port_ptr = (pt_cntr_input_port_t *)cur_out_port_ptr->gc.gu.conn_in_port_ptr;
      for (uint32_t b = 0; b < num_bufs_to_update; b++)
      {
         // assigning buffer to output will automatically assing buffer to connected/next input since
         // they share the same sdata ptr.
         cur_out_port_ptr->sdata_ptr->buf_ptr[b].data_ptr     = buf_ptr[b].data_ptr;
         cur_out_port_ptr->sdata_ptr->buf_ptr[b].max_data_len = buf_ptr[b].max_data_len;
      }

#ifdef PT_CNTR_SAFE_MODE
      cur_out_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
      next_in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
#endif

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Propagated ext in (buf:0x%lx, nbufs: %lu ori:%lu) to cur mod output (0x%lX, %lx) and next mod "
                   "input (0x%lX, "
                   "%lx)",
                   cur_out_port_ptr->sdata_ptr->buf_ptr[0].data_ptr,
                   num_bufs_to_update,
                   cur_out_port_ptr->gc.common.flags.buf_origin,
                   cur_out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   cur_out_port_ptr->gc.gu.cmn.id,
                   next_in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   next_in_port_ptr->gc.gu.cmn.id);
#endif

      /** move the iterator to next module's input*/
      cur_in_port_ptr = next_in_port_ptr;

   } while (cur_in_port_ptr);

   return;
}

static inline ar_result_t pt_cntr_handle_process_events_and_flags(pt_cntr_t               *me_ptr,
                                                                  gen_topo_process_info_t *process_info_ptr,
                                                                  bool_t                  *mf_th_ps_event_ptr,
                                                                  gu_module_list_t        *cur_module_list_ptr)
{
   ar_result_t result = AR_EOK;

   result = gen_cntr_handle_process_events_and_flags((gen_cntr_t *)me_ptr,
                                                     process_info_ptr,
                                                     mf_th_ps_event_ptr,
                                                     cur_module_list_ptr ? cur_module_list_ptr->next_ptr : NULL);

   // attempt to send MF to downstream in the mf event context.
   // since there is not pending data expected on the outputs, container should be always able to send MF downstream
   // immediately.
   if (TRUE == *mf_th_ps_event_ptr)
   {
      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->gc.topo.gu.ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         gen_topo_output_port_t  *out_port_ptr     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

         if (ext_out_port_ptr->flags.out_media_fmt_changed || out_port_ptr->common.flags.media_fmt_event)
         {
#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Module 0x%lX output 0x%lx: Attempting to send ext out media format",
                         out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         out_port_ptr->gu.cmn.id);
#endif
         }

         gen_cntr_ext_out_port_apply_pending_media_fmt(me_ptr, &ext_out_port_ptr->gu);
      }

      // propagate
   }

   return result;
}

static inline ar_result_t pt_cntr_mod_buf_mgr_extn_wrapper_get_in_buf(gen_topo_t            *topo_ptr,
                                                                      pt_cntr_module_t      *module_ptr,
                                                                      uint32_t               port_index,
                                                                      capi_stream_data_v2_t *sdata_ptr)
{
   return module_ptr->get_input_buf_fn(module_ptr->buffer_mgr_cb_handle,
                                       port_index,
                                       &sdata_ptr->bufs_num,
                                       sdata_ptr->buf_ptr);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Getting input buffer from Module 0x%lX port_index 0x%lx bufs_num:%lu data_ptr:0x%lx",
                module_ptr->gc.topo.gu.module_instance_id,
                port_index,
                sdata_ptr->bufs_num,
                sdata_ptr->buf_ptr->data_ptr);
#endif
}

static inline ar_result_t pt_cntr_mod_buf_mgr_extn_wrapper_return_out_buf(gen_topo_t            *topo_ptr,
                                                                          pt_cntr_module_t      *module_ptr,
                                                                          uint32_t               port_index,
                                                                          capi_stream_data_v2_t *sdata_ptr)
{

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Returning output port's buffer from Module 0x%lX port_index 0x%lx bufs_num:%lu data_ptr:0x%lx",
                module_ptr->gc.topo.gu.module_instance_id,
                port_index,
                sdata_ptr->bufs_num,
                sdata_ptr->buf_ptr->data_ptr);
#endif

   return module_ptr->return_output_buf_fn(module_ptr->buffer_mgr_cb_handle,
                                           port_index,
                                           &sdata_ptr->bufs_num,
                                           sdata_ptr->buf_ptr);
}

PT_CNTR_STATIC void pt_cntr_underrun_for_ext_in_non_pass_thru(pt_cntr_t             *me_ptr,
                                                              pt_cntr_ext_in_port_t *ext_in_port_ptr,
                                                              pt_cntr_input_port_t  *in_port_ptr)
{
   capi_stream_data_v2_t *sdata_ptr = in_port_ptr->sdata_ptr;

   // Note that for non pass thru, always topo buffer is assigned for the internal input port
   // and data is copied from external to internal input.
   // If external buffer couldnt be polled in the current signal trigger, capi buf will be NULL
   // hence need to assign topo buffer here.
   for (uint32_t b = 0; b < sdata_ptr->bufs_num; b++)
   {
      capi_buf_t *buf_ptr = &sdata_ptr->buf_ptr[b];

      // assign the topo buffer if not assigned yet. possible if ext buffer couldnt be polled.
      memset((buf_ptr->data_ptr + buf_ptr->actual_data_len), 0, (buf_ptr->max_data_len - buf_ptr->actual_data_len));

      buf_ptr->actual_data_len = buf_ptr->max_data_len;
   }
}

PT_CNTR_STATIC void pt_cntr_underrun_for_ext_in_pass_thru(pt_cntr_t             *me_ptr,
                                                          pt_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          pt_cntr_input_port_t  *in_port_ptr)
{
   capi_stream_data_v2_t *sdata_ptr = in_port_ptr->sdata_ptr;

   // underrun only if input media format is available, and topo buffer is allocated for the ext input
   if (!ext_in_port_ptr->topo_in_buf_ptr)
   {
      return;
   }

   // for pass thru upstream buffer is held for processing, if its not available need to assign the topo buffer.
   // if a partial upstream buffer is received, need to copy the data from ext to topo buffer and push zeros
   uint32_t bufs_num = sdata_ptr->bufs_num;
   if (sdata_ptr->buf_ptr[0].data_ptr && ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr)
   {
      // assign topo buffer and copy data from ext to internal input
      int8_t  *dst_buf_ptr     = (int8_t *)ext_in_port_ptr->topo_in_buf_ptr;
      uint32_t dst_len_per_buf = in_port_ptr->gc.common.max_buf_len_per_buf;
      for (uint32_t b = 0; b < bufs_num; b++)
      {
         capi_buf_t *buf_ptr         = &sdata_ptr->buf_ptr[b];
         int8_t     *src_buf_ptr     = (int8_t *)buf_ptr->data_ptr;
         uint32_t    src_len_per_buf = buf_ptr->actual_data_len;

         // copy data from ext to local buffer
         uint32_t len_copied = memscpy(dst_buf_ptr, dst_len_per_buf, src_buf_ptr, src_len_per_buf);

         // fill zeros in the rest of the ch buffer
         memset(dst_buf_ptr + len_copied, 0, dst_len_per_buf - len_copied);

         // update the sdata buffer with the topo buffer
         buf_ptr->data_ptr        = dst_buf_ptr;
         buf_ptr->actual_data_len = dst_len_per_buf;
         buf_ptr->max_data_len    = dst_len_per_buf;

         dst_buf_ptr += dst_len_per_buf;
      }

      // free the external buffer
      spf_msg_ack_msg(&ext_in_port_ptr->gc.cu.input_data_q_msg, AR_EOK);
      ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr = NULL;
   }
   else // ext buffer is not present, hence assign topo buffer and underrun.
   {
      int8_t *data_buf_ptr = (int8_t *)ext_in_port_ptr->topo_in_buf_ptr;

      // fill the topo buffer with zeros at once
      memset(data_buf_ptr, 0, in_port_ptr->gc.common.max_buf_len);

      // assign the underrun buffer to internal port in unpacked format
      uint32_t len_per_buf = in_port_ptr->gc.common.max_buf_len_per_buf;
      for (uint32_t b = 0; b < bufs_num; b++)
      {
         capi_buf_t *buf_ptr      = &sdata_ptr->buf_ptr[b];
         buf_ptr->data_ptr        = data_buf_ptr;
         buf_ptr->actual_data_len = len_per_buf;
         buf_ptr->max_data_len    = len_per_buf;

         data_buf_ptr += len_per_buf;
      }
   }

   // propagated ext buffer in the NBLC
   pt_cntr_propagate_ext_input_buffer_forwards(me_ptr, in_port_ptr, sdata_ptr, bufs_num);
}

PT_CNTR_STATIC void pt_cntr_preprocess_ext_input_underrun(pt_cntr_t             *me_ptr,
                                                          pt_cntr_module_t      *module_ptr,
                                                          pt_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          pt_cntr_input_port_t  *in_port_ptr,
                                                          capi_stream_data_v2_t *sdata_ptr)
{
   // dont underru if ext in is ready to process
   if (ext_in_port_ptr->gc.flags.ready_to_go)
   {
      return;
   }

   // todo: if there are more than one sub graphs do not underrun if the self port state is not started.
   // underrun only if the ext in port is started, should not underrun if the port is opened/stopped, module might crash
   // since it unexpected.
   if ((TOPO_PORT_STATE_STARTED != in_port_ptr->gc.common.state) ||
       (TOPO_PORT_STATE_SUSPENDED == ext_in_port_ptr->gc.cu.connected_port_state))
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Ext input port 0x%lx of Module 0x%lX is stopped/suspended not underrunning lens (%lu of %lu)",
                   in_port_ptr->gc.gu.cmn.id,
                   in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   sdata_ptr->buf_ptr[0].actual_data_len,
                   sdata_ptr->buf_ptr[0].max_data_len);
#endif
      ext_in_port_ptr->gc.flags.ready_to_go = TRUE;
      return;
   }

   // dont push zeros for raw format, just set erasure flag.
   if (!SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->gc.cu.media_fmt.data_format))
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Ext input port 0x%lx of Module 0x%lX is non-pcm fmt setting erasure for underrun (%lu of %lu)",
                   in_port_ptr->gc.gu.cmn.id,
                   in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                   sdata_ptr->buf_ptr[0].actual_data_len,
                   sdata_ptr->buf_ptr[0].max_data_len);

      ext_in_port_ptr->gc.flags.ready_to_go = TRUE;
      sdata_ptr->flags.erasure              = TRUE;
      return;
   }

   // pass thru with partial external buffer
   if (ext_in_port_ptr->pass_thru_upstream_buffer)
   {
      pt_cntr_underrun_for_ext_in_pass_thru(me_ptr, ext_in_port_ptr, in_port_ptr);
   }
   else
   {
      pt_cntr_underrun_for_ext_in_non_pass_thru(me_ptr, ext_in_port_ptr, in_port_ptr);
   }

   in_port_ptr->gc.common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_BUF_MGR;

   sdata_ptr->flags.erasure = TRUE;
   ext_in_port_ptr->gc.err_msg_underrun_err_count++;
   if (gen_cntr_check_for_err_print(&me_ptr->gc.topo))
   {
      GEN_CNTR_MSG_ISLAND(me_ptr->gc.topo.gu.log_id,
                          DBG_ERROR_PRIO,
                          "Ext input port 0x%lx of Module 0x%lX, actual data len %ld max data len %ld "
                          "(both per ch). No of underrun after last print %u.",
                          in_port_ptr->gc.gu.cmn.id,
                          in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                          sdata_ptr->buf_ptr[0].actual_data_len,
                          sdata_ptr->buf_ptr[0].max_data_len,
                          ext_in_port_ptr->gc.err_msg_underrun_err_count);
      ext_in_port_ptr->gc.err_msg_underrun_err_count = 0;
   }

   ext_in_port_ptr->gc.flags.ready_to_go = TRUE;
   return;
}

PT_CNTR_STATIC ar_result_t pt_cntr_preprocess_setup_ext_input(pt_cntr_t             *me_ptr,
                                                              pt_cntr_module_t      *module_ptr,
                                                              pt_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   pt_cntr_input_port_t  *in_port_ptr = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
   capi_stream_data_v2_t *sdata_ptr   = in_port_ptr->sdata_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Setup FEF cntr for Module 0x%lX input 0x%lx",
                module_ptr->gc.topo.gu.module_instance_id,
                in_port_ptr->gc.gu.cmn.id);
   uint32_t num_polled_buffers = 0;
#endif

   // OPTS: Avoiding polling, if buffer is not available cntr underruns
   // todo: check if its possible to pop without polling, and check only ready_to_go to enter the loop.
   while (!ext_in_port_ptr->gc.flags.ready_to_go && posal_queue_poll(ext_in_port_ptr->gc.gu.this_handle.q_ptr))
   {
      if (ext_in_port_ptr->pass_thru_upstream_buffer)
      {
         result = pt_cntr_pop_and_setup_ext_in_buffer_for_pass_thru(me_ptr, ext_in_port_ptr);

         // in pass thru mode, container can only poll one data buffer for an interrupt
         if (ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr)
         {
#ifdef VERBOSE_DEBUGGING
            num_polled_buffers++;
#endif
            break;
         }
      }
      else
      {
         result = pt_cntr_pop_and_setup_ext_in_buffer_for_non_pass_thru(me_ptr, ext_in_port_ptr);
      }

      // note: result can be AR_ENEEDMORE if ext input is not available, in that case input is not expected to be
      // ready and underruns

#ifdef PT_CNTR_SAFE_MODE
      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "process failed for ext input port 0x%lx of Module 0x%lX ",
                      in_port_ptr->gc.gu.cmn.id,
                      in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id);
      }
#endif

#ifdef VERBOSE_DEBUGGING
      num_polled_buffers++;
#endif
   }

#ifdef VERBOSE_DEBUGGING

   // if ext input is doing buffer pass thru ext_in_port_ptr->gc.buf.actual_data_len and max len are not set, hence 0
   // will be printed.

   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "Ext input port 0x%lx of Module 0x%lX. num_polled_buffers %lu buffer 0x%p (%lu of %lu ) is_pass_thru "
                "%lu",
                in_port_ptr->gc.gu.cmn.id,
                in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                num_polled_buffers,
                ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->gc.buf.actual_data_len,
                ext_in_port_ptr->gc.buf.max_data_len,
                ext_in_port_ptr->pass_thru_upstream_buffer);
#endif

   // handle underrun
   if (FALSE == ext_in_port_ptr->gc.flags.ready_to_go)
   {
      // todo: underrun few 100sms and stop to cover most of the practical scenarios. ideally need to underruns based on
      // the ext in facing SISO algo delay.
      pt_cntr_preprocess_ext_input_underrun(me_ptr, module_ptr, ext_in_port_ptr, in_port_ptr, sdata_ptr);
   }
   else // if buffer is ready clear earlier set erasure flag
   {
      sdata_ptr->flags.erasure = FALSE;
   }

   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_send_data_to_downstream_peer_cntr(pt_cntr_t              *me_ptr,
                                                                     pt_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                     spf_msg_header_t       *out_buf_header)
{
   ar_result_t result = AR_EOK;

#ifdef VERBOSE_DEBUGGING
   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&out_buf_header->payload_start;
   pt_cntr_module_t      *module_ptr  = (pt_cntr_module_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr->cmn.module_ptr;
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "gen_cntr_send_pcm_to_downstream_peer_cntr: (0x%lx, 0x%lx) timestamp: %lu (0x%lx%lx). size %lu",
                module_ptr->gc.topo.gu.module_instance_id,
                ext_out_port_ptr->gc.gu.int_out_port_ptr->cmn.id,
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->timestamp >> 32),
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->actual_size));
#endif

   // Reinterpret node's buffer as a header.
   posal_bufmgr_node_t out_buf_node = ext_out_port_ptr->gc.cu.out_bufmgr_node;

   // Reinterpret the node itself as a spf_msg_t.
   spf_msg_t *msg_ptr = (spf_msg_t *)&out_buf_node;

   // todo: avoid setting msg opcode by setting on buffer creation
   msg_ptr->msg_opcode  = SPF_MSG_DATA_BUFFER;
   msg_ptr->payload_ptr = out_buf_node.buf_ptr;

   result = posal_queue_push_back(ext_out_port_ptr->gc.gu.downstream_handle.spf_handle_ptr->q_ptr,
                                  (posal_queue_element_t *)msg_ptr);
   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_ERROR_PRIO, "Failed to deliver buffer dowstream. Dropping");
      gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
      goto __bailout;
   }
   else
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "pushed buffer downstream 0x%p. Current bit mask 0x%x",
                   ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr,
                   me_ptr->gc.cu.curr_chan_mask);
#endif
   }

__bailout:
   ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr = NULL;
   return result;
}

/**  For each external out port, postprocess data, send media fmt and data down. */
PT_CNTR_STATIC ar_result_t pt_cntr_post_process_peer_ext_output(pt_cntr_t              *me_ptr,
                                                                pt_cntr_output_port_t  *out_port_ptr,
                                                                pt_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t            result    = AR_EOK;
   capi_stream_data_v2_t *sdata_ptr = out_port_ptr->sdata_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "postprocess output: before module (%lu of %lu), ext out (%lu of %lu), "
                "bytes_produced_by_pp %lu",
                sdata_ptr->buf_ptr[0].actual_data_len,
                sdata_ptr->buf_ptr[0].max_data_len,
                ext_out_port_ptr->gc.buf.actual_data_len,
                ext_out_port_ptr->gc.buf.max_data_len,
                sdata_ptr->buf_ptr[0].actual_data_len);
#endif

   topo_port_state_t ds_downgraded_state = cu_get_external_output_ds_downgraded_port_state_v2(&ext_out_port_ptr->gc.cu);
   if (TOPO_PORT_STATE_STARTED != ds_downgraded_state)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Ext output is stopped: Dropping data %lu and/or metadata 0x%p as there's no external buf",
                   sdata_ptr->buf_ptr->actual_data_len,
                   sdata_ptr->metadata_list_ptr);

      gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
      return result;
   }

   /* Check if there is enough space to the ext out buffer only if,
         1. If the external buffer exist.
         2. And if the ext buffer is not being reused as internal buffer. If its being reused ext buffer
           already has the processed data so we don't have to copy. */
   if (!sdata_ptr->buf_ptr[0].data_ptr)
   {
      gen_cntr_handle_st_overrun_at_post_process((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
      return AR_EOK;
   }

   spf_msg_t media_fmt_msg = { 0 };

   // handles if there is pending mf at ext output, could be pending due to port being started and waiting until process
   // is triggered.
   if (ext_out_port_ptr->gc.flags.out_media_fmt_changed)
   {
      /**
       * copy MF to ext-out even if bytes_produced_by_topo is zero.
       * in gapless use cases, if EOS comes before initial silence is removed, then we send EOS before any data is sent.
       * before sending EOS, we need to send MF (true for any MD)
       * Don't clear the out_port_ptr->gc.common.flags.media_fmt_event if old data is left.
       */
      gen_cntr_get_output_port_media_format_from_topo((gen_cntr_ext_out_port_t *)ext_out_port_ptr,
                                                      TRUE /* updated to unchanged */);

      result = cu_create_media_fmt_msg_for_downstream(&me_ptr->gc.cu,
                                                      &ext_out_port_ptr->gc.gu,
                                                      &media_fmt_msg,
                                                      SPF_MSG_DATA_MEDIA_FORMAT);

      if (result != AR_EOK)
      {
         return result;
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_MED_PRIO,
                   "Sent container output media format from module 0x%x, port 0x%x, flags: media fmt %d frame len %d",
                   ext_out_port_ptr->gc.gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->gc.gu.int_out_port_ptr->cmn.id,
                   out_port_ptr->gc.common.flags.media_fmt_event,
                   ext_out_port_ptr->gc.cu.flags.upstream_frame_len_changed);
#endif

      /** sent media fmt downstream */
      result = cu_send_media_fmt_update_to_downstream(&me_ptr->gc.cu,
                                                      &ext_out_port_ptr->gc.gu,
                                                      &media_fmt_msg,
                                                      ext_out_port_ptr->gc.gu.downstream_handle.spf_handle_ptr->q_ptr);

      if (AR_EOK == result)
      {
         /** reset the media fmt flags*/
         ext_out_port_ptr->gc.flags.out_media_fmt_changed         = FALSE;
         ext_out_port_ptr->gc.cu.flags.upstream_frame_len_changed = FALSE;
      }
   }

   if (sdata_ptr->buf_ptr->actual_data_len || sdata_ptr->metadata_list_ptr)
   {
      /** send buffers downstream*/
      spf_msg_header_t      *header      = (spf_msg_header_t *)(ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr);
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      capi_buf_t *buf_ptr = sdata_ptr->buf_ptr;

      // preparing out buf msg header, similar to gen_cntr_populate_peer_cntr_out_buf()
      out_buf_ptr->actual_size = buf_ptr->actual_data_len * sdata_ptr->bufs_num;
      out_buf_ptr->flags       = 0;

      if (sdata_ptr->metadata_list_ptr)
      {
         // When md lib is in nlpi, we dont need to exit here since by this point we should already be in nlpi
         // due to voting against island when propagating md
         gen_topo_check_realloc_md_list_in_peer_heap_id(me_ptr->gc.topo.gu.log_id,
                                                        &(ext_out_port_ptr->gc.gu),
                                                        &(sdata_ptr->metadata_list_ptr));

         bool_t out_buf_has_flushing_eos = FALSE;
         gen_topo_populate_metadata_for_peer_cntr(&(me_ptr->gc.topo),
                                                  &(ext_out_port_ptr->gc.gu),
                                                  &(sdata_ptr->metadata_list_ptr),
                                                  &out_buf_ptr->metadata_list_ptr,
                                                  &out_buf_has_flushing_eos);
      }

#ifdef PT_CNTR_TIME_PROP_ENABLE
      /** Buffer will send down as soon as ready and reused for internal output
         hence directly copy TS from internal port sdata to external buffer */
      if (sdata_ptr->flags.is_timestamp_valid)
      {
         cu_set_bits(&out_buf_ptr->flags,
                     sdata_ptr->flags.is_timestamp_valid,
                     DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                     DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

         out_buf_ptr->timestamp = sdata_ptr->timestamp;
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Outgoing timestamp: %lu (0x%lx%lx), flag=0x%lx, size=%lu TS_flag = %d",
                   (uint32_t)out_buf_ptr->timestamp,
                   (uint32_t)(out_buf_ptr->timestamp >> 32),
                   (uint32_t)out_buf_ptr->timestamp,
                   out_buf_ptr->flags,
                   out_buf_ptr->actual_size,
                   sdata_ptr->flags.is_timestamp_valid);
#endif
#endif

      // check and send prebuffer
      gen_cntr_check_and_send_prebuffers((gen_cntr_t *)me_ptr,
                                         (gen_cntr_ext_out_port_t *)ext_out_port_ptr,
                                         out_buf_ptr);

      result = pt_cntr_send_data_to_downstream_peer_cntr(me_ptr, ext_out_port_ptr, header);

      // it's enough to reset first buf, as only this is checked against.
      buf_ptr->data_ptr     = NULL;
      buf_ptr->max_data_len = 0;
      pt_cntr_set_bufs_actual_len_to_zero(sdata_ptr);
   }
   else
   {
      /** return the buffer back, if data or metadata is not present*/
      gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
   }

   // clear propagated ext buffer in the NBLC
   pt_cntr_propagate_ext_output_buffer_backwards(me_ptr, out_port_ptr, sdata_ptr, 1);

   return result;
}

PT_CNTR_STATIC void pt_cntr_process_attached_module_to_output(gen_topo_t            *topo_ptr,
                                                              pt_cntr_module_t      *host_module_ptr,
                                                              pt_cntr_output_port_t *out_port_ptr)
{
   capi_err_t             attached_proc_result    = CAPI_EOK;
   pt_cntr_module_t      *out_attached_module_ptr = (pt_cntr_module_t *)out_port_ptr->gc.gu.attached_module_ptr;
   capi_stream_data_v2_t *sdata_ptr               = out_port_ptr->sdata_ptr;

   if (out_attached_module_ptr->gc.topo.flags.disabled || !sdata_ptr->buf_ptr[0].data_ptr)
   {
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "Skipping process on attached elementary module miid 0x%lx for input port idx %ld miid 0x%lx - "
               "disabled %ld, is_mf_valid %ld, no buffer %ld, no data provided %ld, md list ptr NULL %ld",
               out_attached_module_ptr->gc.topo.gu.module_instance_id,
               out_port_ptr->gc.gu.cmn.index,
               host_module_ptr->gc.topo.gu.module_instance_id,
               out_attached_module_ptr->gc.topo.flags.disabled,
               out_port_ptr->gc.common.flags.is_mf_valid,
               (NULL == sdata_ptr->buf_ptr),
               (NULL == sdata_ptr->buf_ptr) ? 0 : (0 == sdata_ptr->buf_ptr[0].actual_data_len),
               (NULL == sdata_ptr->metadata_list_ptr));
#endif
      return;
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_MED_PRIO,
            "Before process attached elementary module miid 0x%lx for output port idx %ld miid 0x%lx",
            out_attached_module_ptr->gc.topo.gu.module_instance_id,
            out_port_ptr->gc.gu.cmn.index,
            host_module_ptr->gc.topo.gu.module_instance_id);
#endif

   uint32_t out_port_idx = out_port_ptr->gc.gu.cmn.index;
   // clang-format off
   IRM_PROFILE_MOD_PROCESS_SECTION(out_attached_module_ptr->gc.topo.prof_info_ptr, topo_ptr->gu.prof_mutex,
   attached_proc_result =
      out_attached_module_ptr->gc.topo.capi_ptr->vtbl_ptr
         ->process(out_attached_module_ptr->gc.topo.capi_ptr,
                   (capi_stream_data_t **)&(host_module_ptr->out_port_sdata_pptr[out_port_idx]),
                   (capi_stream_data_t **)&(host_module_ptr->out_port_sdata_pptr[out_port_idx]));
   );
   // clang-format on

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_MED_PRIO,
            "After process attached elementary module miid 0x%lx for output port idx %ld miid 0x%lx",
            out_attached_module_ptr->gc.topo.gu.module_instance_id,
            out_port_ptr->gc.gu.cmn.index,
            host_module_ptr->gc.topo.gu.module_instance_id);
#endif

#ifdef VERBOSE_DEBUGGING
   // Don't ignore need more for attached modules.
   if (CAPI_FAILED(attached_proc_result))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Attached elementary module miid 0x%lx for output port idx %ld miid 0x%lx returned error 0x%lx "
               "during process",
               out_attached_module_ptr->gc.topo.gu.module_instance_id,
               out_port_ptr->gc.gu.cmn.index,
               host_module_ptr->gc.topo.gu.module_instance_id,
               attached_proc_result);
   }
#endif
}

/**
 * after input and output buffers are set-up, topo process is called once on signal triggers.
 * 1. If sufficient input is not present, ext in will underrun at this point.
 * 2. If ext output is ready, it ready_to_deliver_output flag will be set and buffer will be released by the caller.
 * 3. If output is ready, delivers the buffers to downstream containers.
 */
PT_CNTR_STATIC ar_result_t pt_cntr_data_process_one_frame(pt_cntr_t *me_ptr)
{
   ar_result_t result      = AR_EOK;
   capi_err_t  proc_result = CAPI_EOK;
   gen_topo_t *topo_ptr    = &me_ptr->gc.topo;
   uint32_t    log_id      = topo_ptr->gu.log_id;
   INIT_EXCEPTION_HANDLING

   // todo: add safe mode checks for every port before and after process

   /** ------------- MODULE PROCESS ---------------------
    *  prepare input and output sdata and calls the module capi process.
    */

   // setup ext input ports, better to setup external inputs before outputs because if there is any
   // media format propagation it can retrigger ext output port setup.
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->gc.topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      pt_cntr_ext_in_port_t *ext_in_port_ptr = (pt_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      pt_cntr_module_t      *module_ptr = (pt_cntr_module_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr;

      TRY(result, pt_cntr_preprocess_setup_ext_input(me_ptr, module_ptr, ext_in_port_ptr));
   }

   // setup ext output ports
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->gc.topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      pt_cntr_ext_out_port_t *ext_out_port_ptr = (pt_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      pt_cntr_module_t       *module_ptr = (pt_cntr_module_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr->cmn.module_ptr;

      /** setup ext output buffer and setup the internal capi output sdata with the ext buffer pointers.
       *    1. If buffer is not present
       *       option 1: pass NULL buf ptr - no need to alloc topo, module can internally overrun.
       *       option 2: alloc a topo buffer and assign it, post process drop the buffer.
       */
      TRY(result, pt_cntr_preprocess_setup_ext_output(me_ptr, module_ptr, ext_out_port_ptr));
   }

   /** -------- PRE-PROCESS INPUT SIDE of MIMO --------------- */

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(log_id, DBG_HIGH_PRIO, "processing mimo inputs: External inputs & calling source module process...");
#endif

   /** Call source modules and setup the connected inputs */
   gu_module_list_t *module_list_ptr = me_ptr->src_module_list_ptr;
   while (NULL != module_list_ptr)
   {
      // iterate over source module's output ports
      pt_cntr_module_t *src_module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;

      // currently only SISO source modules are supported in the star shaped graph
      pt_cntr_output_port_t *out_port_ptr =
         (pt_cntr_output_port_t *)src_module_ptr->gc.topo.gu.output_port_list_ptr->op_port_ptr;
      capi_stream_data_v2_t *sdata_ptr = out_port_ptr->sdata_ptr;

      if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE == out_port_ptr->gc.common.flags.buf_origin)
      {
         // todo: restrict to interleaved/deintlv unpacked to avoid loops for the extn
         for (uint32_t b = 0; b < sdata_ptr->bufs_num; b++)
         {
            sdata_ptr->buf_ptr[b].data_ptr = NULL;

            // max data len must have been setup during after media format proapagation.
            // sdata_ptr->buf_ptr[b].max_data_len = out_port_ptr->gc.common.max_buf_len_per_buf;
         }
      }
      else if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED == out_port_ptr->gc.common.flags.buf_origin)
      {
         pt_cntr_input_port_t *next_in_port_ptr = (pt_cntr_input_port_t *)out_port_ptr->gc.gu.conn_in_port_ptr;
         pt_cntr_module_t     *next_module_ptr  = (pt_cntr_module_t *)next_in_port_ptr->gc.gu.cmn.module_ptr;

         TRY(result,
             pt_cntr_mod_buf_mgr_extn_wrapper_get_in_buf(topo_ptr,
                                                         next_module_ptr,
                                                         next_in_port_ptr->gc.gu.cmn.index,
                                                         sdata_ptr));
      }
      else
      {
         // topo buffer is would have been assigned at start
      }

      // todo: add safe mode checks for the output buf ptrs and lens

#ifdef VERBOSE_DEBUGGING
      FEF_PRINT_PORT_INFO_AT_PROCESS(src_module_ptr->gc.topo.gu.module_instance_id,
                                     out_port_ptr->gc.gu.cmn.id,
                                     sdata_ptr,
                                     proc_result,
                                     "output",
                                     "before",
                                     out_port_ptr->gc.common.flags.buf_origin);
#endif

      // Note: No need to set "proc_info_ptr->is_in_mod_proc_context". Flag needs to be set only if output can have
      // pending data. which is not possible in case of Pass Thru container.
      // proc_info_ptr->is_in_mod_proc_context = TRUE;

      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(src_module_ptr->gc.topo.prof_info_ptr,
      topo_ptr->gu.prof_mutex,
      proc_result                           = src_module_ptr->process(src_module_ptr->gc.topo.capi_ptr,
                                            NULL, // will be NULL for src module
                                            (capi_stream_data_t **)src_module_ptr->out_port_sdata_pptr);
      );
      // clang-format on

      if (pt_cntr_any_events_raised_by_module(topo_ptr))
      {
         // here MF must propagate from next module, starting from current module overwrites any data that module
         // might have outputed in this call.
         if (pt_cntr_any_process_call_events(topo_ptr))
         {
            bool_t                  mf_th_ps_event = FALSE;
            gen_topo_process_info_t temp;
            memset(&temp, 0, sizeof(gen_topo_process_info_t));

            me_ptr->flags.processing_data_path_mf = TRUE;
            pt_cntr_handle_process_events_and_flags(me_ptr,
                                                    &temp,
                                                    &mf_th_ps_event,
                                                    pt_cntr_get_gu_sorted_list_ptr(topo_ptr, src_module_ptr));
            me_ptr->flags.processing_data_path_mf = FALSE;

            /** continue processing from the same src module if the module raises any mf/threshold event .*/
            GEN_CNTR_MSG(log_id,
                         DBG_HIGH_PRIO,
                         "Looping back to topo process from module 0x%lX result 0x%lx",
                         src_module_ptr->gc.topo.gu.module_instance_id,
                         proc_result);
            continue;
         }
         else
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lx raised unsupported capi events 0x%lx (mask) in process context module result "
                         "0x%lx",
                         src_module_ptr->gc.topo.gu.module_instance_id,
                         ((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word,
                         proc_result);
            THROW(result, AR_EFAILED);
         }
      }

      // Skipping calling the attached module process here, it requires an extra loop hence moved
      // to the subsequence input port loop, where it will called with the conn_out_port_ptr.

#if defined(VERBOSE_DEBUGGING) || defined(PT_CNTR_SAFE_MODE)
      // VERIFY(proc_result, CAPI_EOK == proc_result);
      for (gu_output_port_list_t *op_list_ptr = src_module_ptr->gc.topo.gu.output_port_list_ptr; NULL != op_list_ptr;
           LIST_ADVANCE(op_list_ptr))
      {
         pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)op_list_ptr->op_port_ptr;
         FEF_PRINT_PORT_INFO_AT_PROCESS(src_module_ptr->gc.topo.gu.module_instance_id,
                                        out_port_ptr->gc.gu.cmn.id,
                                        sdata_ptr,
                                        proc_result,
                                        "output",
                                        "after",
                                        out_port_ptr->gc.common.flags.buf_origin);

// todo: add safe mode checks for the output buf ptrs for module buffer access extn,
// and check lengths
#ifdef PT_CNTR_SAFE_MODE
         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE == out_port_ptr->gc.common.flags.buf_origin)
         {
            if (!sdata_ptr->data_ptr)
            {
               // print failure that module didnt provide the buffer
            }

            // todo: check bufs ptrs for all the chs, and lengths
         }
#endif
      }
#endif

      // Call output attached module process. And also if propagated state is stopped need to drop the data since next
      // module cannot consume data.
      if (src_module_ptr->flags.has_attached_module)
      {
         pt_cntr_process_attached_module_to_output(topo_ptr, src_module_ptr, out_port_ptr);
      }

      // print overrun and drop data
      if (src_module_ptr->flags.has_stopped_port)
      {
         pt_cntr_set_bufs_actual_len_to_zero(out_port_ptr->sdata_ptr);
      }

      LIST_ADVANCE(module_list_ptr);
   }

   module_list_ptr = me_ptr->module_proc_list_ptr;
   while (NULL != module_list_ptr)
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(log_id,
               DBG_HIGH_PRIO,
               "MID: 0x%lx preparing mimo inputs and outputs, and cals mimo process...",
               module_ptr->gc.topo.gu.module_instance_id);
#endif

      /** no need to setup input ports, input port buffers and sdata will be setup are mirrored with prev output ports,
       * and should be setup on virtue of calling modules in sorted order. If input is external, external ports and
       * pc->sdata will be setup in the ext input context. */
#if defined(PT_CNTR_SAFE_MODE) || defined(VERBOSE_DEBUGGING)
      /* Iterate through modules inputs and move data from ext input or source modules output */
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gc.topo.gu.input_port_list_ptr;
           (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         pt_cntr_input_port_t  *in_port_ptr = (pt_cntr_input_port_t *)in_port_list_ptr->ip_port_ptr;
         capi_stream_data_v2_t *sdata_ptr   = in_port_ptr->sdata_ptr;

         if (!sdata_ptr->buf_ptr[0].data_ptr)
         {
            // reaches here if input mf is not received yet, and even underrun is not possible.
            TOPO_MSG(log_id,
                     DBG_ERROR_PRIO,
                     "MID: 0x%lx input port 0x%lx buffer is not setup. unexpected!",
                     module_ptr->gc.topo.gu.module_instance_id,
                     in_port_ptr->gc.gu.cmn.id);
         }
         else
         {
            // for internal input ports, sdata is mirrored with the connected/prev output port, and buffer would have
            // been setup in the prev output context, so no need to setup buffer again here
            // 1. for internal inputs with GEN_TOPO_INPUT_BUFFER_ACCESS extn, buffer would have been queried and
            // populated
            //    in the upstream output port context. So no need to query again for this module. Also the sdata is
            // 2. for internal inputs without buffers access extn, topo buffer or module's output buffer would have been
            //     assigned in the prev module output port process context.
         }

         FEF_PRINT_PORT_INFO_AT_PROCESS(module_ptr->gc.topo.gu.module_instance_id,
                                        in_port_ptr->gc.gu.cmn.id,
                                        sdata_ptr,
                                        proc_result,
                                        "input",
                                        "before",
                                        in_port_ptr->gc.common.flags.buf_origin);
      }
#endif

      /** iterate over the outputs of MIMO module and setup the output buffer,
          1. If its internal output and MIMO output supports extn, buffer is not assigned.
          2. */
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr;
           (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)out_port_list_ptr->op_port_ptr;
         capi_stream_data_v2_t *sdata_ptr    = out_port_ptr->sdata_ptr;

         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE == out_port_ptr->gc.common.flags.buf_origin)
         {
            // todo: restrict to interleaved/deintlv unpacked to avoid loops for the extn
            for (uint32_t b = 0; b < sdata_ptr->bufs_num; b++)
            {
               sdata_ptr->buf_ptr[b].data_ptr = NULL;
            }
         }
         else if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED == out_port_ptr->gc.common.flags.buf_origin)
         {
            pt_cntr_input_port_t *next_in_port_ptr = (pt_cntr_input_port_t *)out_port_ptr->gc.gu.conn_in_port_ptr;
            pt_cntr_module_t     *next_module_ptr  = (pt_cntr_module_t *)next_in_port_ptr->gc.gu.cmn.module_ptr;

            if (AR_EOK != (result = pt_cntr_mod_buf_mgr_extn_wrapper_get_in_buf(topo_ptr,
                                                                                next_module_ptr,
                                                                                next_in_port_ptr->gc.gu.cmn.index,
                                                                                sdata_ptr)))
            {
               GEN_CNTR_MSG(log_id,
                            DBG_HIGH_PRIO,
                            "Error assigning buffer to module 0x%lX. Failed to get input buffer from next module 0x%lX "
                            "result 0x%lx",
                            module_ptr->gc.topo.gu.module_instance_id,
                            next_module_ptr->gc.topo.gu.module_instance_id,
                            result);
            }
         }
         else
         {
            // could be ext input or internal input that uses topo buffers.
            //  1. for internal port buffer is assingned at graph start itself, hence no need to assign the topo buffer
            //  here.
            //  2. for ext input buffer is assigned in a loop before module process loop
            //  3. No need to reset sdata lengths to zero, it must have been reset in the next modules context post
            //  process
         }

#ifdef VERBOSE_DEBUGGING
         FEF_PRINT_PORT_INFO_AT_PROCESS(module_ptr->gc.topo.gu.module_instance_id,
                                        out_port_ptr->gc.gu.cmn.id,
                                        sdata_ptr,
                                        proc_result,
                                        "output",
                                        "before",
                                        out_port_ptr->gc.common.flags.buf_origin);
#endif
      }

      // Note: No need to set "proc_info_ptr->is_in_mod_proc_context". Flag needs to be set only if output can have
      // pending data. which is not possible in case of Pass Thru container.
      // proc_info_ptr->is_in_mod_proc_context = TRUE;

      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->gc.topo.prof_info_ptr, topo_ptr->gu.prof_mutex,
      proc_result = module_ptr->process(module_ptr->gc.topo.capi_ptr,
                                        (capi_stream_data_t **)module_ptr->in_port_sdata_pptr,
                                        (capi_stream_data_t **)module_ptr->out_port_sdata_pptr);
      );
      // clang-format on

#ifdef VERBOSE_DEBUGGING
      // VERIFY(proc_result, CAPI_EOK == proc_result);
      GEN_CNTR_MSG(log_id,
                   DBG_HIGH_PRIO,
                   "capi process done for Module 0x%lX result 0x%lx",
                   module_ptr->gc.topo.gu.module_instance_id,
                   proc_result);
#endif

      if (pt_cntr_any_events_raised_by_module(topo_ptr))
      {
         // here MF must propagate from next module, starting from current module overwrites any data that module
         // might have outputed in this call.
         if (pt_cntr_any_process_call_events(topo_ptr))
         {
            bool_t                  mf_th_ps_event = FALSE;
            gen_topo_process_info_t temp;
            memset(&temp, 0, sizeof(gen_topo_process_info_t));

            me_ptr->flags.processing_data_path_mf = TRUE;
            pt_cntr_handle_process_events_and_flags(me_ptr,
                                                    &temp,
                                                    &mf_th_ps_event,
                                                    pt_cntr_get_gu_sorted_list_ptr(topo_ptr, module_ptr));
            me_ptr->flags.processing_data_path_mf = FALSE;

            /* continue processing from the same module after propagating threshold/mf*/
            GEN_CNTR_MSG(log_id,
                         DBG_HIGH_PRIO,
                         "Looping back to topo process from module 0x%lX result 0x%lx",
                         module_ptr->gc.topo.gu.module_instance_id,
                         proc_result);
            continue;
         }
         else
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lx raised unsupported capi events 0x%lx (mask) in process context module result "
                         "0x%lx",
                         module_ptr->gc.topo.gu.module_instance_id,
                         ((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word,
                         proc_result);
            THROW(result, AR_EFAILED);
         }
      }

      /**------------------- POST PROCESS internal outputs of the Module --------------------------- */

      /* Call output attached module process.*/
      if (pt_cntr_check_if_output_needs_post_process(module_ptr))
      {
         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (out_port_ptr->gc.gu.attached_module_ptr)
            {
               pt_cntr_process_attached_module_to_output(topo_ptr, module_ptr, out_port_ptr);
            }

            // print overrun and drop data
            if (TOPO_PORT_STATE_STARTED != out_port_ptr->gc.common.state)
            {
               pt_cntr_set_bufs_actual_len_to_zero(out_port_ptr->sdata_ptr);
            }
         }
      }

#ifdef VERBOSE_DEBUGGING
      /** Print output sdata after process */
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr;
           (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)out_port_list_ptr->op_port_ptr;
         FEF_PRINT_PORT_INFO_AT_PROCESS(module_ptr->gc.topo.gu.module_instance_id,
                                        out_port_ptr->gc.gu.cmn.id,
                                        out_port_ptr->sdata_ptr,
                                        proc_result,
                                        "output",
                                        "after",
                                        out_port_ptr->gc.common.flags.buf_origin);
      }
#endif

      /**------------------- POST PROCESS internal inputs of the module --------------------------- */
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gc.topo.gu.input_port_list_ptr;
           (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         pt_cntr_input_port_t  *in_port_ptr = (pt_cntr_input_port_t *)in_port_list_ptr->ip_port_ptr;
         capi_stream_data_v2_t *sdata_ptr   = in_port_ptr->sdata_ptr;

#ifdef VERBOSE_DEBUGGING
         FEF_PRINT_PORT_INFO_AT_PROCESS(module_ptr->gc.topo.gu.module_instance_id,
                                        in_port_ptr->gc.gu.cmn.id,
                                        sdata_ptr,
                                        proc_result,
                                        "input",
                                        "after",
                                        in_port_ptr->gc.common.flags.buf_origin);
#endif

         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED == in_port_ptr->gc.common.flags.buf_origin)
         {
            // return the buffer shared by the source module through capi process
            pt_cntr_output_port_t *conn_out_port_ptr = (pt_cntr_output_port_t *)in_port_ptr->gc.gu.conn_out_port_ptr;
            pt_cntr_module_t      *prev_module_ptr   = (pt_cntr_module_t *)conn_out_port_ptr->gc.gu.cmn.module_ptr;
            TRY(result,
                pt_cntr_mod_buf_mgr_extn_wrapper_return_out_buf(topo_ptr,
                                                                prev_module_ptr,
                                                                conn_out_port_ptr->gc.gu.cmn.index,
                                                                sdata_ptr));
         }

         // destroy unhandled metadata
#if defined(PT_CNTR_SAFE_MODE)
         if (sdata_ptr->metadata_list_ptr)
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "MIID 0x%lX input port 0x%lx: Unexpected pending MD found at input after process",
                         module_ptr->gc.topo.gu.module_instance_id,
                         in_port_ptr->gc.gu.cmn.id);

#ifdef PT_CNTR_SAFE_MODE_ERR_RECOVERY
            gen_topo_destroy_all_metadata(log_id,
                                          (void *)in_port_ptr->gc.gu.cmn.module_ptr,
                                          &sdata_ptr->metadata_list_ptr,
                                          TRUE /* is dropped*/);
#elif PT_CNTR_SAFE_MODE_ERR_CRASH
            spf_svc_crash();
#endif
         }

         uint32_t unexpected_flag_mask = topo_sdata_get_flag_mask_other_than_ver_and_is_ts_valid(sdata_ptr);
         if (unexpected_flag_mask)
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "MIID 0x%lX input port 0x%lx unexpected sdata flags are set 0x%lx",
                         module_ptr->gc.topo.gu.module_instance_id,
                         in_port_ptr->gc.gu.cmn.id,
                         sdata_ptr->flags.word);
#ifdef PT_CNTR_SAFE_MODE_ERR_RECOVERY
            // clears unexpected flags from the sdata
            sdata_ptr->flags.word = sdata_ptr->flags.word & (~unexpected_flag_mask);
#elif PT_CNTR_SAFE_MODE_ERR_CRASH
            spf_svc_crash();
#endif
         }

#endif // PT_CNTR_SAFE_MODE

         // reset actual len to zero
         pt_cntr_set_bufs_actual_len_to_zero(sdata_ptr);
      }

      // move to next module in the proc list
      LIST_ADVANCE(module_list_ptr);
   }
   // end of proc module list iter

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(log_id, DBG_HIGH_PRIO, "PT_CNTR: processing sink modules ...");
#endif

   /** HANDLE SINK MODULE PROCESS */
   module_list_ptr = me_ptr->sink_module_list_ptr;
   while (NULL != module_list_ptr)
   {
      pt_cntr_module_t *sink_module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;

#ifdef VERBOSE_DEBUGGING
      // iterate over sink module's input ports
      for (gu_input_port_list_t *ip_list_ptr = sink_module_ptr->gc.topo.gu.input_port_list_ptr; NULL != ip_list_ptr;
           LIST_ADVANCE(ip_list_ptr))
      {
         pt_cntr_input_port_t *in_port_ptr = (pt_cntr_input_port_t *)ip_list_ptr->ip_port_ptr;

         FEF_PRINT_PORT_INFO_AT_PROCESS(sink_module_ptr->gc.topo.gu.module_instance_id,
                                        in_port_ptr->gc.gu.cmn.id,
                                        in_port_ptr->sdata_ptr,
                                        proc_result,
                                        "SINK input",
                                        "before",
                                        in_port_ptr->gc.common.flags.buf_origin);

         // sdata is mirrored with the connected outputs associated with the MIMO module
         // and hence no need to setup input port sdata again
      }
#endif

      // Note: No need to set "proc_info_ptr->is_in_mod_proc_context". Flag needs to be set only if output can have
      // pending data. which is not possible in case of Pass Thru container.
      // proc_info_ptr->is_in_mod_proc_context = TRUE;

      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(sink_module_ptr->gc.topo.prof_info_ptr, topo_ptr->gu.prof_mutex,
      proc_result = sink_module_ptr->process(sink_module_ptr->gc.topo.capi_ptr,
                                             (capi_stream_data_t **)sink_module_ptr->in_port_sdata_pptr,
                                             (capi_stream_data_t **)NULL);
      );
      // clang-format on

      // iterate over sink module's and reset the actual lengths to mark the data as consumed.
      for (gu_input_port_list_t *ip_list_ptr = sink_module_ptr->gc.topo.gu.input_port_list_ptr; NULL != ip_list_ptr;
           LIST_ADVANCE(ip_list_ptr))
      {
         pt_cntr_input_port_t  *in_port_ptr = (pt_cntr_input_port_t *)ip_list_ptr->ip_port_ptr;
         capi_stream_data_v2_t *sdata_ptr   = in_port_ptr->sdata_ptr;

#ifdef VERBOSE_DEBUGGING
         VERIFY(result, CAPI_EOK == proc_result);
         FEF_PRINT_PORT_INFO_AT_PROCESS(sink_module_ptr->gc.topo.gu.module_instance_id,
                                        in_port_ptr->gc.gu.cmn.id,
                                        sdata_ptr,
                                        proc_result,
                                        "SINK input",
                                        "after",
                                        in_port_ptr->gc.common.flags.buf_origin);
#endif

         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED == in_port_ptr->gc.common.flags.buf_origin)
         {
            // return the buffer shared by the source module through capi process
            pt_cntr_output_port_t *conn_out_port_ptr = (pt_cntr_output_port_t *)in_port_ptr->gc.gu.conn_out_port_ptr;
            pt_cntr_module_t      *prev_module_ptr   = (pt_cntr_module_t *)conn_out_port_ptr->gc.gu.cmn.module_ptr;
            TRY(result,
                pt_cntr_mod_buf_mgr_extn_wrapper_return_out_buf(topo_ptr,
                                                                prev_module_ptr,
                                                                conn_out_port_ptr->gc.gu.cmn.index,
                                                                sdata_ptr));
         }

         // destroy unhandled metadata
#if defined(PT_CNTR_SAFE_MODE)
         if (sdata_ptr->metadata_list_ptr)
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "MIID 0x%lX input port 0x%lx: Unexpected pending MD found at input after process",
                         sink_module_ptr->gc.topo.gu.module_instance_id,
                         in_port_ptr->gc.gu.cmn.id);

#ifdef PT_CNTR_SAFE_MODE_ERR_RECOVERY
            gen_topo_destroy_all_metadata(log_id,
                                          (void *)in_port_ptr->gc.gu.cmn.module_ptr,
                                          &sdata_ptr->metadata_list_ptr,
                                          TRUE /* is dropped*/);
#elif PT_CNTR_SAFE_MODE_ERR_CRASH
            spf_svc_crash();
#endif
         }

         uint32_t unexpected_flag_mask = topo_sdata_get_flag_mask_other_than_ver_and_is_ts_valid(sdata_ptr);
         if (unexpected_flag_mask)
         {
            GEN_CNTR_MSG(log_id,
                         DBG_ERROR_PRIO,
                         "MIID 0x%lX input port 0x%lx unexpected sdata flags are set 0x%lx",
                         sink_module_ptr->gc.topo.gu.module_instance_id,
                         in_port_ptr->gc.gu.cmn.id,
                         sdata_ptr->flags.word);
#ifdef PT_CNTR_SAFE_MODE_ERR_RECOVERY
            // clears unexpected flags from the sdata
            sdata_ptr->flags.word = sdata_ptr->flags.word & (~unexpected_flag_mask);
#elif PT_CNTR_SAFE_MODE_ERR_CRASH
            spf_svc_crash();
#endif
         }

#endif // PT_CNTR_SAFE_MODE

         // reset actual len to zero, done at the end
         pt_cntr_set_bufs_actual_len_to_zero(sdata_ptr);
      }

      LIST_ADVANCE(module_list_ptr);
   }

   /** POST PROCESS EXT OUTPUTS TO THE CONTAINER*/
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->gc.topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      pt_cntr_ext_out_port_t *ext_out_port_ptr = (pt_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      pt_cntr_output_port_t  *out_port_ptr     = (pt_cntr_output_port_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr;

      /** prepare ext output buffer and send it to downstream. and the GEN_TOPO_BUF_ORIGIN_EXT_BUF will be cleared
       *
       * move ptr from internal port to ext port, move Metadata as well. Usually output buffer must be released
       * every interrupt, because module will produce eith data or metadata on the started outuput ports.
       *
       * if module doesnt generate may be just retain the output buffer for the next process and dont send it
       * downstream and return the buffers.
       */
      TRY(result, pt_cntr_post_process_peer_ext_output(me_ptr, out_port_ptr, ext_out_port_ptr));
   }

   /** POST PROCESS EXT INPUTS TO THE CONTAINER*/

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->gc.topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      pt_cntr_ext_in_port_t *ext_in_port_ptr = (pt_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      pt_cntr_input_port_t  *in_port_ptr     = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
      capi_stream_data_v2_t *sdata_ptr       = in_port_ptr->sdata_ptr;

      if (ext_in_port_ptr->pass_thru_upstream_buffer)
      {
         if (ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr)
         {
            pt_cntr_ext_input_return_buffer(log_id, ext_in_port_ptr);

            // note that as an optimization buf lens, and buf ptrs are not cached in ext port for the pass thru
            // scenario. hence no need to reset todo: print warning if ext->actual len/buf/max len is non-zero, since
            // its unexpected
         }

         // for pass thru sdata buffer is assigned every process call
         sdata_ptr->buf_ptr[0].data_ptr     = NULL;
         sdata_ptr->buf_ptr[0].max_data_len = 0;
         // actual len is reset outside if() in pt_cntr_set_bufs_actual_len_to_zero()

         // clear propagated ext buffer in the NBLC
         pt_cntr_propagate_ext_input_buffer_forwards(me_ptr, in_port_ptr, sdata_ptr, 1);
      }
      else
      {
         // note that in non pass thru external input buffer is freed during preprocess
         // once data is copied and ext buffer is empty.

         // for non pass thru since topo buffer is used need to retain buf_ptr and max len
         // actual len is reset outside if() in pt_cntr_set_bufs_actual_len_to_zero()

#ifdef VERBOSE_DEBUGGING
         if (ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr || ext_in_port_ptr->gc.buf.actual_data_len)
         {
            // todo: print debug msg that buffer is being held for non pass thru scenario
         }
#endif
      }

      // reset actual len to zero
      ext_in_port_ptr->gc.flags.ready_to_go = FALSE;
      pt_cntr_set_bufs_actual_len_to_zero(sdata_ptr);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }

   return result;
}

///////////////////////   FEF cntr external output utilities ///////////////////////////////

ar_result_t pt_cntr_preprocess_setup_ext_output_non_static(pt_cntr_t              *me_ptr,
                                                           pt_cntr_module_t       *module_ptr,
                                                           pt_cntr_ext_out_port_t *ext_out_port_ptr)
{
   return pt_cntr_preprocess_setup_ext_output(me_ptr, module_ptr, ext_out_port_ptr);
}

/**
 * sets up output buffers and calls process on topo.
 *
 * any of the output port can trigger this.
 */
PT_CNTR_STATIC ar_result_t pt_cntr_preprocess_setup_ext_output(pt_cntr_t              *me_ptr,
                                                               pt_cntr_module_t       *module_ptr,
                                                               pt_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t            result       = AR_EOK;
   pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr;
   capi_stream_data_v2_t *sdata_ptr    = out_port_ptr->sdata_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Setup FEF cntr for Module 0x%lX output 0x%lx",
                module_ptr->gc.topo.gu.module_instance_id,
                out_port_ptr->gc.gu.cmn.id);
#endif

   topo_port_state_t ds_downgraded_state = cu_get_external_output_ds_downgraded_port_state_v2(&ext_out_port_ptr->gc.cu);
   if (TOPO_PORT_STATE_STARTED != ds_downgraded_state)
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Ext output is stopped - no need to preprocess %lu",
                   ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr,
                   sdata_ptr ? sdata_ptr->buf_ptr[0].data_ptr : NULL);
#endif
      gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
      return result;
   }

   // pop_out_buf() returns early if output buf was already popped and assigned.
   result = gen_cntr_process_pop_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);
   if (NULL == ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr)
   {
      /** if ext output buf is not available print overrun and return */
      gen_cntr_st_check_print_overrun((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);

      // error can be due to buffer reallocation failure, return buffer to make sure
      // process is not called with incorrect sized buffers.
      gen_cntr_return_back_out_buf((gen_cntr_t *)me_ptr, (gen_cntr_ext_out_port_t *)ext_out_port_ptr);

      // todo: need to assign a topo buffer for the last module to overrun ?
      return result;
   }

   spf_msg_header_t      *header      = (spf_msg_header_t *)(ext_out_port_ptr->gc.cu.out_bufmgr_node.buf_ptr);
   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   uint32_t max_len_per_buf = out_port_ptr->gc.common.max_buf_len_per_buf;

   /** assign for the internal output port which is nothing but the end of an NBLC and propagate the same buffer
    * backwards.*/
   int8_t  *data_ptr = (int8_t *)(&(out_buf_ptr->data_buf));
   uint32_t bufs_num = sdata_ptr->bufs_num;
   for (uint32_t b = 0; b < bufs_num; b++)
   {
      sdata_ptr->buf_ptr[b].data_ptr        = data_ptr;
      sdata_ptr->buf_ptr[b].actual_data_len = 0;
      sdata_ptr->buf_ptr[b].max_data_len    = max_len_per_buf;
      data_ptr += max_len_per_buf;
   }

   // handle timestamp
   sdata_ptr->flags.is_timestamp_valid = FALSE;

   pt_cntr_propagate_ext_output_buffer_backwards(me_ptr, out_port_ptr, sdata_ptr, bufs_num);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Module 0x%lX external output 0x%lx int buf: (0x%lx %lu/%lu num bufs: %lu) ",
                module_ptr->gc.topo.gu.module_instance_id,
                out_port_ptr->gc.gu.cmn.id,
                sdata_ptr->buf_ptr[0].data_ptr,
                sdata_ptr->buf_ptr[0].actual_data_len,
                sdata_ptr->buf_ptr[0].max_data_len,
                sdata_ptr->bufs_num);
#endif

   return result;
}

static ar_result_t pt_cntr_modify_eos_md_at_ext_input(pt_cntr_t             *me_ptr,
                                                      pt_cntr_ext_in_port_t *ext_in_port_ptr,
                                                      module_cmn_md_list_t **head_pptr,
                                                      module_cmn_md_list_t  *md_node_ptr,
                                                      module_cmn_md_t       *md_ptr)
{
   ar_result_t           result           = AR_EOK;
   gen_topo_t           *topo_ptr         = &me_ptr->gc.topo;
   pt_cntr_input_port_t *in_port_ptr      = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
   gen_topo_module_t    *module_ptr       = (gen_topo_module_t *)in_port_ptr->gc.gu.cmn.module_ptr;
   module_cmn_md_eos_t  *eos_metadata_ptr = 0;
   uint32_t              is_out_band      = md_ptr->metadata_flag.is_out_of_band;
   if (is_out_band)
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
   }
   else
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
   }

   if (eos_metadata_ptr->flags.is_internal_eos)
   {
      // Need to exit island to call memory free operations in nlpi
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "MD_DBG: Module 0x%lX, node_ptr 0x%p, md_ptr 0x%p, destroying internal EOS",
                   module_ptr->gu.module_instance_id,
                   md_node_ptr,
                   md_ptr);

      result = gen_topo_respond_and_free_eos(topo_ptr,
                                             module_ptr->gu.module_instance_id,
                                             md_node_ptr,
                                             FALSE /*is_eos_rendered*/,
                                             head_pptr,
                                             FALSE);
   }
   else
   {
      if (MODULE_CMN_MD_EOS_FLUSHING == eos_metadata_ptr->flags.is_flushing_eos)
      {
         eos_metadata_ptr->flags.is_flushing_eos = MODULE_CMN_MD_EOS_NON_FLUSHING;
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "MD_DBG: Module 0x%lX, modify_md: node_ptr 0x%p, md_ptr 0x%p"
                      "converted flushing EOS to non-flushing.",
                      module_ptr->gu.module_instance_id,
                      md_node_ptr,
                      md_ptr);
      }
   }
   return result;
}

static ar_result_t pt_cntr_modify_md_at_ext_input(pt_cntr_t             *me_ptr,
                                                  pt_cntr_ext_in_port_t *ext_in_port_ptr,
                                                  module_cmn_md_list_t **md_list_pptr)
{
   ar_result_t           result   = AR_EOK;
   module_cmn_md_list_t *node_ptr = *md_list_pptr;
   module_cmn_md_list_t *next_ptr = NULL;
   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         result |= pt_cntr_modify_eos_md_at_ext_input(me_ptr, ext_in_port_ptr, md_list_pptr, node_ptr, md_ptr);
      }
      node_ptr = next_ptr;
   }
   return result;
}

/** setup input data bufs (pre-process), handles TS and Data*/
PT_CNTR_STATIC ar_result_t pt_cntr_setup_ext_in_sdata_for_pass_thru(pt_cntr_t             *me_ptr,
                                                                    pt_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result        = AR_EOK;
   pt_cntr_input_port_t  *in_port_ptr   = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
   spf_msg_header_t      *header        = (spf_msg_header_t *)(ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;
   capi_stream_data_v2_t *sdata_ptr     = in_port_ptr->sdata_ptr;
   uint32_t               bufs_num      = sdata_ptr->bufs_num;
   uint32_t               actual_len_per_buf = topo_div_num(input_buf_ptr->actual_size, bufs_num);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Setup ext input port 0x%lx of Module 0x%lX ",
                in_port_ptr->gc.gu.cmn.id,
                in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id);
   uint32_t bytes_in_int_inp_md_prop = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->gc.common);
#endif

   // todo: preserve prebuffer

   /** Hold the external buffer for nblc modules processing. expected actual length is already checked hence assuming
    * per buf actual length to be 'max_buf_len_per_buf'*/
   capi_buf_t *buf_ptr;
   int8_t     *data_buf_ptr = (int8_t *)input_buf_ptr->data_buf;
   for (uint32_t b = 0; b < bufs_num; b++)
   {
      buf_ptr                  = &sdata_ptr->buf_ptr[b];
      buf_ptr->data_ptr        = data_buf_ptr;
      buf_ptr->actual_data_len = actual_len_per_buf;
      buf_ptr->max_data_len    = actual_len_per_buf;
      data_buf_ptr += actual_len_per_buf;
   }

   // todo: further optimization - propagate buffer until nblc end, leads to overwriting the ICB buffer. check if thats
   // ok ?

#ifdef PT_CNTR_TIME_PROP_ENABLE
   /** copy external input timestamp and update ts_to_sync to the next expected timestmap if buffer has valid, ts to
    * sync will be updated based on the ext buffer TS so no need to update here. */
   bool_t new_ts_valid =
      cu_get_bits(input_buf_ptr->flags, DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK, DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

   /** updates timestamp validity flag to true/false  */
   sdata_ptr->flags.is_timestamp_valid = new_ts_valid;
   sdata_ptr->timestamp                = new_ts_valid ? input_buf_ptr->timestamp : 0;

#endif // PT_CNTR_TIME_PROP_ENABLE

   // handle meta-data
   // note that since its pass thru internal port is expected to not have any metadata
   if (input_buf_ptr->metadata_list_ptr)
   {
      if (me_ptr->flags.supports_md_propagation)
      {
         sdata_ptr->metadata_list_ptr     = input_buf_ptr->metadata_list_ptr;
         input_buf_ptr->metadata_list_ptr = NULL;

         // converts flushing to non-flushing EOS
         result |= pt_cntr_modify_md_at_ext_input(me_ptr, ext_in_port_ptr, &sdata_ptr->metadata_list_ptr);
      }
      else // destroy metadata since there are modules that doesnt support MD propgatation.
      {
         // todo: add special handling to render EOS

         gen_topo_destroy_all_metadata(me_ptr->gc.topo.gu.log_id,
                                       (void *)in_port_ptr->gc.gu.cmn.module_ptr,
                                       &input_buf_ptr->metadata_list_ptr,
                                       TRUE /* is dropped*/);
      }
   }

   ext_in_port_ptr->gc.flags.ready_to_go = (actual_len_per_buf == in_port_ptr->gc.common.max_buf_len_per_buf);

   pt_cntr_propagate_ext_input_buffer_forwards(me_ptr, in_port_ptr, sdata_ptr, bufs_num);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "preprocess input: prev len %lu module (%lu of %lu) per buf, ext in buf 0x%lx, flags %08lX ready_to_go "
                "%lu ",
                bytes_in_int_inp_md_prop,
                sdata_ptr->buf_ptr[0].actual_data_len,
                sdata_ptr->buf_ptr[0].max_data_len,
                sdata_ptr->buf_ptr[0].data_ptr,
                sdata_ptr->flags.word,
                ext_in_port_ptr->gc.flags.ready_to_go);
#endif
   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_pop_and_setup_ext_in_buffer_for_pass_thru(pt_cntr_t             *me_ptr,
                                                                             pt_cntr_ext_in_port_t *ext_in_port_ptr)

{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   /* pops the front of the queue*/
   result = posal_queue_pop_front(ext_in_port_ptr->gc.gu.this_handle.q_ptr,
                                  (posal_queue_element_t *)&(ext_in_port_ptr->gc.cu.input_data_q_msg));
   if (AR_EOK != result)
   {
      ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr = NULL;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_ERROR_PRIO, "Failed to pop the ext input buffer.");
#endif

      return result;
   }

#ifdef PT_CNTR_SAFE_MODE
   if (ext_in_port_ptr->vtbl_ptr->is_data_buf(me_ptr, ext_in_port_ptr))
   {
      ext_in_port_ptr->gc.cu.buf_recv_cnt++;
   }
#endif

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "(miid,port-id) (0x%lX, 0x%lx) Popped an input msg buffer 0x%lx with opcode 0x%x",
                ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode);
#endif

   //
   // Note: In ext in pass thru scenario, ext in cannot have partial data hence no need to track bytes left in ext in
   // here internal input sdata is directly populated with the ext in and not caching in ext input for optimizations.
   //
   // ext_in_port_ptr->gc.buf.data_ptr        = (int8_t *)&input_buf_ptr->data_buf;
   // ext_in_port_ptr->gc.buf.actual_data_len = input_buf_ptr->actual_size;
   // ext_in_port_ptr->gc.buf.max_data_len    = input_buf_ptr->actual_size;

   // process messages
   switch (ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      {
         TRY(result, pt_cntr_setup_ext_in_sdata_for_pass_thru(me_ptr, ext_in_port_ptr));
         break;
      }
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->gc.topo.gu.log_id,
                             DBG_MED_PRIO,
                             "PT_CNTR input media format update cmd from peer service to module, port index  (0x%lX, "
                             "%lu) ",
                             ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.index);

         // Exit island temporarily for handling media format.If there is any event as part of this commond
         // we will vote against island in that context
         gen_topo_exit_island_temporarily(&me_ptr->gc.topo);

         me_ptr->flags.processing_data_path_mf = TRUE;
         gen_cntr_check_process_input_media_fmt_data_cmd((gen_cntr_t *)me_ptr,
                                                         (gen_cntr_ext_in_port_t *)ext_in_port_ptr);

         me_ptr->flags.processing_data_path_mf = FALSE;
         break;
      }
      default:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->gc.topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "PT_CNTR received unsupported message 0x%lx",
                             ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode);
         result = AR_EUNSUPPORTED;
         result |= gen_cntr_free_input_data_cmd((gen_cntr_t *)me_ptr,
                                                (gen_cntr_ext_in_port_t *)ext_in_port_ptr,
                                                AR_EUNSUPPORTED,
                                                FALSE);
         break;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }
   return (result);
}

PT_CNTR_STATIC ar_result_t pt_cntr_pop_and_setup_ext_in_buffer_for_non_pass_thru(pt_cntr_t             *me_ptr,
                                                                                 pt_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_msg_header_t      *header        = NULL;
   spf_msg_data_buffer_t *input_buf_ptr = NULL;

   if (!ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr)
   {
      /* pops the front of the queue*/
      result = posal_queue_pop_front(ext_in_port_ptr->gc.gu.this_handle.q_ptr,
                                     (posal_queue_element_t *)&(ext_in_port_ptr->gc.cu.input_data_q_msg));
      if (AR_EOK != result)
      {
         ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr = NULL;

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_ERROR_PRIO, "Failed to pop the ext input buffer.");
#endif
         return result;
      }

#ifdef PT_CNTR_SAFE_MODE
      if (ext_in_port_ptr->vtbl_ptr->is_data_buf(me_ptr, ext_in_port_ptr))
      {
         ext_in_port_ptr->gc.cu.buf_recv_cnt++;
      }
#endif

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Popped an input msg buffer 0x%lx with opcode 0x%x from (miid,port-id) (0x%lX, 0x%lx) input queue",
                   ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr,
                   ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode,
                   ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.id);
#endif

      header        = (spf_msg_header_t *)(ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr);
      input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      ext_in_port_ptr->gc.buf.data_ptr        = (int8_t *)&input_buf_ptr->data_buf;
      ext_in_port_ptr->gc.buf.actual_data_len = input_buf_ptr->actual_size;
      ext_in_port_ptr->gc.buf.max_data_len    = input_buf_ptr->actual_size;
   }
   else
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Using earlier popped input msg buffer 0x%lx with opcode 0x%x from (miid,port-id) (0x%lX, 0x%lx) "
                   "input queue",
                   ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr,
                   ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode,
                   ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.id);
#endif
   }

   // process messages
   switch (ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      {
         TRY(result, pt_cntr_copy_n_setup_ext_in_sdata_for_non_pass_thru(me_ptr, ext_in_port_ptr));
         break;
      }
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->gc.topo.gu.log_id,
                             DBG_MED_PRIO,
                             "PT_CNTR input media format update cmd from peer service to module, port index  (0x%lX, "
                             "%lu) ",
                             ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.index);

         // Exit island temporarily for handling media format.If there is any event as part of this commond
         // we will vote against island in that context
         gen_topo_exit_island_temporarily(&me_ptr->gc.topo);
         me_ptr->flags.processing_data_path_mf = TRUE;
         gen_cntr_check_process_input_media_fmt_data_cmd((gen_cntr_t *)me_ptr,
                                                         (gen_cntr_ext_in_port_t *)ext_in_port_ptr);
         me_ptr->flags.processing_data_path_mf = FALSE;
         break;
      }
      default:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->gc.topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "PT_CNTR received unsupported message 0x%lx",
                             ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode);
         result = AR_EUNSUPPORTED;
         result |= gen_cntr_free_input_data_cmd((gen_cntr_t *)me_ptr,
                                                (gen_cntr_ext_in_port_t *)ext_in_port_ptr,
                                                AR_EUNSUPPORTED,
                                                FALSE);
         break;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }
   return (result);
}

/** setup input data bufs if ext input is operating with PCM, and US frame duration is not same as DS frame duration.*/
PT_CNTR_STATIC ar_result_t pt_cntr_copy_n_setup_ext_in_sdata_for_non_pass_thru(pt_cntr_t             *me_ptr,
                                                                               pt_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result        = AR_EOK;
   spf_msg_header_t      *header        = (spf_msg_header_t *)(ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   pt_cntr_input_port_t  *in_port_ptr = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
   capi_stream_data_v2_t *sdata_ptr   = in_port_ptr->sdata_ptr;

   uint32_t bytes_in_int_inp_before_copy_for_md_prop = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->gc.common);
   uint32_t bytes_in_ext_inp_before_copy_for_md_prop = ext_in_port_ptr->gc.buf.actual_data_len;

#ifdef PT_CNTR_TIME_PROP_ENABLE

   bool_t new_ts_valid =
      cu_get_bits(input_buf_ptr->flags, DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK, DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

   if (0 == sdata_ptr->buf_ptr[0].actual_data_len)
   {
      /** updates timestamp validity flag to true/false  */
      sdata_ptr->flags.is_timestamp_valid = new_ts_valid;
      sdata_ptr->timestamp                = new_ts_valid ? input_buf_ptr->timestamp : 0;
   }
   else if (new_ts_valid && sdata_ptr->flags.is_timestamp_valid)
   {
      // check TS discontinuity and print error if disc is observed
      int64_t next_data_expected_ts = sdata_ptr->timestamp;
      if (in_port_ptr->gc.common.flags.is_pcm_unpacked)
      {
         next_data_expected_ts += (int64_t)topo_bytes_per_ch_to_us(sdata_ptr->buf_ptr[0].actual_data_len,
                                                                   in_port_ptr->gc.common.media_fmt_ptr,
                                                                   NULL);
      }
      else // interleaved
      {
         next_data_expected_ts += (int64_t)topo_bytes_to_us(sdata_ptr->buf_ptr[0].actual_data_len,
                                                            in_port_ptr->gc.common.media_fmt_ptr,
                                                            NULL);
      }

      bool_t ts_disc = gen_topo_is_timestamp_discontinuous(sdata_ptr->flags.is_timestamp_valid,
                                                           next_data_expected_ts,
                                                           new_ts_valid,
                                                           input_buf_ptr->timestamp);

      if (ts_disc)
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Discontinuity ignored for signal triggerred mode for (MIID, ext in) (0x%lx, 0x%lx) ",
                      in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gc.gu.cmn.id);
      }
   }

#endif // PT_CNTR_TIME_PROP_ENABLE

   // todo: preserve prebuffer

   int8_t  *src_buf_ptr = ext_in_port_ptr->gc.buf.data_ptr;
   uint32_t src_buf_spacing_bytes;
   uint32_t src_read_offset = ext_in_port_ptr->gc.buf.max_data_len - ext_in_port_ptr->gc.buf.actual_data_len;

   uint32_t bytes_available_in_src_per_buf;

   topo_div_three_nums(ext_in_port_ptr->gc.buf.max_data_len,
                       &src_buf_spacing_bytes,
                       ext_in_port_ptr->gc.buf.actual_data_len,
                       &bytes_available_in_src_per_buf,
                       src_read_offset,
                       &src_read_offset,
                       sdata_ptr->bufs_num);

   uint32_t bytes_required_to_fill_per_buf = sdata_ptr->buf_ptr[0].max_data_len - sdata_ptr->buf_ptr[0].actual_data_len;
   uint32_t bytes_to_copy_per_buf          = MIN(bytes_available_in_src_per_buf, bytes_required_to_fill_per_buf);

   // get first ch's read offset ptr, subsequent chs are calculated by incrementing with "src_buf_spacing_bytes"
   src_buf_ptr += src_read_offset;

   capi_buf_t *buf_ptr;
   for (uint32_t b = 0; b < sdata_ptr->bufs_num; b++)
   {
      buf_ptr = &sdata_ptr->buf_ptr[b];

      memscpy((buf_ptr->data_ptr + buf_ptr->actual_data_len),
              bytes_to_copy_per_buf,
              src_buf_ptr,
              bytes_to_copy_per_buf);

      buf_ptr->actual_data_len += bytes_to_copy_per_buf;

      src_buf_ptr += src_buf_spacing_bytes;

      // decrement bytes copied per buf from the external buffer
      ext_in_port_ptr->gc.buf.actual_data_len -= bytes_to_copy_per_buf;
   }

   // handle if metadata is present in ext/int buffer
   if (input_buf_ptr->metadata_list_ptr || sdata_ptr->metadata_list_ptr)
   {
      if (me_ptr->flags.supports_md_propagation)
      {
         // Move metadata from ext in to int input.
         uint32_t total_bytes_copied =
            (bytes_in_ext_inp_before_copy_for_md_prop - ext_in_port_ptr->gc.buf.actual_data_len);
         gen_topo_move_md_from_ext_in_to_int_in((gen_topo_input_port_t *)in_port_ptr,
                                                me_ptr->gc.topo.gu.log_id,
                                                &input_buf_ptr->metadata_list_ptr,
                                                bytes_in_int_inp_before_copy_for_md_prop,
                                                bytes_in_ext_inp_before_copy_for_md_prop,
                                                total_bytes_copied,
                                                in_port_ptr->gc.common.media_fmt_ptr);

         // gen_topo_move_md_from_ext_in_to_int_in() moves md to in_port_ptr->gc.common.sdata.metadata_list_ptr
         // but the sdata ptr assigned to the current input can be different, it could be assigned from the connected
         // output port hence move MD from gen topo sdata to pass thru cntr sdata ptr
         if (sdata_ptr != (&in_port_ptr->gc.common.sdata))
         {
            spf_list_merge_lists((spf_list_node_t **)&sdata_ptr->metadata_list_ptr,
                                 (spf_list_node_t **)in_port_ptr->gc.common.sdata.metadata_list_ptr);
         }

         // converts flushing to non-flushing EOS
         result |= pt_cntr_modify_md_at_ext_input(me_ptr, ext_in_port_ptr, &sdata_ptr->metadata_list_ptr);
      }
      else // destroy metadata since there are modules that doesnt support MD propgatation.
      {
         // todo: add special handling to render EOS

         gen_topo_destroy_all_metadata(me_ptr->gc.topo.gu.log_id,
                                       (void *)in_port_ptr->gc.gu.cmn.module_ptr,
                                       &input_buf_ptr->metadata_list_ptr,
                                       TRUE /* is dropped*/);
      }
   }

   ext_in_port_ptr->gc.flags.ready_to_go =
      (sdata_ptr->buf_ptr[0].actual_data_len == sdata_ptr->buf_ptr[0].max_data_len);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "preprocess input: prev len %lu module (%lu of %lu) per buf, ext in buf 0x%lx, flags %08lX ready_to_go "
                "%lu",
                bytes_in_int_inp_before_copy_for_md_prop,
                sdata_ptr->buf_ptr[0].actual_data_len,
                sdata_ptr->buf_ptr[0].max_data_len,
                sdata_ptr->buf_ptr[0].data_ptr,
                sdata_ptr->flags.word,
                ext_in_port_ptr->gc.flags.ready_to_go);
#endif

   // free the buffer and reset buf info if buffer is processed
   if (0 == ext_in_port_ptr->gc.buf.actual_data_len)
   {
      ext_in_port_ptr->gc.buf.max_data_len = 0;
      ext_in_port_ptr->gc.buf.data_ptr     = NULL;

      pt_cntr_ext_input_return_buffer(me_ptr->gc.topo.gu.log_id, ext_in_port_ptr);
   }

   return result;
}

/* thn container's callback for the interrupt signal.*/
ar_result_t pt_cntr_signal_trigger(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   pt_cntr_t  *me_ptr = (pt_cntr_t *)cu_ptr;

#ifdef VERBOSE_DEBUGGING
   uint64_t proc_ts_before = posal_timer_get_time();
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_LOW_PRIO, "pt_cntr_signal_trigger: Received signal trigger");
#endif

   // Increment in process context
   me_ptr->gc.st_module.processed_interrupt_counter++;
   me_ptr->gc.st_module.steady_state_interrupt_counter++;

   /* Check for signal miss, if raised_interrupt_counter is greater than process counter,
    * one or more interrupts have not been serviced.
    * This check will be skipped for timer modules eg:spr, rat since raised_interrupt_counter is always 0
    * This checks kind of signal miss because of the container thread being busy and one or more interrupts have not
    * been serviced. */
   if (me_ptr->gc.st_module.raised_interrupt_counter > me_ptr->gc.st_module.processed_interrupt_counter)
   {
      // todo: check if anything needs to be done on signal miss ?
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_ERROR_PRIO, "pt_cntr_signal_trigger: Signal Miss");
   }

   /*clear the trigger signal */
   posal_signal_clear_inline(me_ptr->gc.st_module.trigger_signal_ptr);

   /** Process one frame per signal trigger. */
   result = pt_cntr_data_process_one_frame(me_ptr);

   /** Poll control channel and check for incoming ctrl msgs.
    * If any present, do set param and return the msgs. */
   result = cu_poll_and_process_ctrl_msgs(&me_ptr->gc.cu);

   /**
    * TODO: implement post process lite weight signal miss detection. if data process takes more than interrupt duration
    *
    * For signal miss detection,
    *    1. increment "me_ptr->st_module.signal_miss_counter" if there is a signal miss
    *    2. increment me_ptr->st_module.processed_interrupt_counter++
    *    3. me_ptr->gc.topo.flags.need_to_ignore_signal_miss = FALSE;
    *    4. me_ptr->prev_err_print_time_ms = me_ptr->gc.topo.proc_context.err_print_time_in_this_process_ms;
    *
    *    1. option 1: may be put this under debug no need to enable signal miss detection always.
    *    2. option 2: just print error and do nothing
    *
    */

#ifdef VERBOSE_DEBUGGING
   int64_t diff = posal_timer_get_time() - proc_ts_before;
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "pt_cntr_signal_trigger: signal trigger processing done! proc_time:%lu",
                diff);
#endif

   return result;
}

capi_err_t pt_cntr_bypass_module_process(capi_t *_pif, capi_stream_data_t *inputs[], capi_stream_data_t *outputs[])
{
   capi_err_t result = CAPI_EOK;

   uint32_t ip_idx = 0;
   uint32_t op_idx = 0;

#ifdef VERBOSE_DEBUGGING
   if (inputs[ip_idx]->bufs_num != outputs[op_idx]->bufs_num)
   {
      GEN_CNTR_MSG(0,
                   DBG_ERROR_PRIO,
                   "bufs_num must match between input[0x%lx] (%lu) and output[0x%lx]",
                   inputs[ip_idx]->bufs_num,
                   outputs[op_idx]->bufs_num);
   }
#endif

   for (uint32_t i = 0; i < MIN(inputs[ip_idx]->bufs_num, outputs[op_idx]->bufs_num); i++)
   {
      if (!inputs[ip_idx]->buf_ptr[i].data_ptr || !outputs[op_idx]->buf_ptr[i].data_ptr)
      {
         continue;
      }

      uint32_t  out_max_data_len    = outputs[op_idx]->buf_ptr[i].max_data_len;
      uint32_t  in_actual_data_len  = inputs[ip_idx]->buf_ptr[i].actual_data_len;
      uint32_t *out_actual_data_len = &(outputs[op_idx]->buf_ptr[i].actual_data_len);

      if (inputs[ip_idx]->buf_ptr[i].data_ptr != outputs[op_idx]->buf_ptr[i].data_ptr)
      {
         (*out_actual_data_len) = memsmove(outputs[op_idx]->buf_ptr[i].data_ptr,
                                           out_max_data_len,
                                           inputs[ip_idx]->buf_ptr[i].data_ptr,
                                           in_actual_data_len);
      }
      else
      {
         *out_actual_data_len = MIN(out_max_data_len, in_actual_data_len);
      }
   }

   return result;
}
