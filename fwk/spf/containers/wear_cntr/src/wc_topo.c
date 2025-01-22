/**
 * \file wc_topo.c
 *
 * \brief
 *
 *     Basic topology implementation for wear container
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "wc_topo.h"
#include "wc_topo_capi.h"

#define WC_BYPASS_BW (100 * 1024)
#define WCNTR_TOPO_BITS_TO_BYTES(x) CAPI_CMN_BITS_TO_BYTES(x)

static inline uint32_t wcntr_tu_get_unit_frame_size(uint32_t sample_rate)
{
   // Returns 1 sample as a minimum value
   if (sample_rate < 1000)
   {
      return 1;
   }
   return (sample_rate / 1000);
}

static inline uint32_t wcntr_topo_get_memscpy_kpps(uint32_t bits_per_sample,
                                                   uint32_t num_channels,
                                                   uint32_t sample_rate)
{
   return (WCNTR_TOPO_BITS_TO_BYTES(bits_per_sample) * num_channels * sample_rate) / 4000;
}

static bool_t wcntr_tu_has_media_format_changed(wcntr_topo_media_fmt_t *a1, wcntr_topo_media_fmt_t *b1)
{
   uint32_t i = 0;

   // if a & b data formats are different - then has changed.
   if ((a1->data_format != b1->data_format))
   {
      return TRUE;
   }

   // if a is not PCM/packetized, then generally marked as changed.
   if (!(SPF_IS_PCM_DATA_FORMAT(a1->data_format)))
   {
      return TRUE;
   }

   wcntr_topo_pcm_pack_med_fmt_t *a = &a1->pcm;
   wcntr_topo_pcm_pack_med_fmt_t *b = &b1->pcm;

   if ((a->endianness != b->endianness) || (a->sample_rate != b->sample_rate) || (a->bit_width != b->bit_width) ||
       (a->bits_per_sample != b->bits_per_sample) || (a->interleaving != b->interleaving) ||
       (a->num_channels != b->num_channels) || (a1->fmt_id != b1->fmt_id) || (a->q_factor != b->q_factor))
   {
      return TRUE;
   }
   for (i = 0; i < a->num_channels; i++)
   {
      if (a->chan_map[i] != b->chan_map[i])
      {
         return TRUE;
      }
   }
   return FALSE;
}

static void wcntr_tu_copy_media_fmt(wcntr_topo_media_fmt_t *dst_ptr, wcntr_topo_media_fmt_t *src_ptr)
{

   // for Raw, simply copy pointers.
   *dst_ptr = *src_ptr;
}

bool_t wcntr_topo_is_valid_media_fmt(wcntr_topo_media_fmt_t *med_fmt_ptr)
{

   if (SPF_UNKNOWN_DATA_FORMAT == med_fmt_ptr->data_format)
   {
      return FALSE;
   }

   if (SPF_FIXED_POINT != med_fmt_ptr->data_format)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "wcntr_topo_is_valid_media_fmt : data format 0x%lX is not supported",
             med_fmt_ptr->data_format);
      return FALSE;
   }

   if ((0 == med_fmt_ptr->pcm.bits_per_sample) || (0 == med_fmt_ptr->pcm.bit_width) || (0 == med_fmt_ptr->pcm.q_factor))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "wcntr_topo_is_valid_media_fmt : Invalid MF bits_per_sample %u ,bit_width %u ,q_factor %u",
             med_fmt_ptr->pcm.bits_per_sample,
             med_fmt_ptr->pcm.bit_width,
             med_fmt_ptr->pcm.q_factor);
      return FALSE;
   }
   uint32_t sample_rate = med_fmt_ptr->pcm.sample_rate;
   if ((sample_rate != ((sample_rate / 1000) * 1000)))
   {

      AR_MSG(DBG_ERROR_PRIO, "wcntr_topo_is_valid_media_fmt : fractional sampling rate is not supported");

      return FALSE;
   }

   return TRUE;
}

bool_t wcntr_topo_is_module_sg_stopped_or_suspended(wcntr_topo_module_t *module_ptr)
{
   wcntr_topo_sg_t *sg_ptr = (wcntr_topo_sg_t *)module_ptr->gu.sg_ptr;
   return ((WCNTR_TOPO_SG_STATE_STOPPED == sg_ptr->state) || (WCNTR_TOPO_SG_STATE_SUSPENDED == sg_ptr->state));
}

bool_t wcntr_topo_is_module_sg_started(wcntr_topo_module_t *module_ptr)
{
   wcntr_topo_sg_t *sg_ptr = (wcntr_topo_sg_t *)module_ptr->gu.sg_ptr;
   return (WCNTR_TOPO_SG_STATE_STARTED == sg_ptr->state);
}

ar_result_t wcntr_topo_fmwk_extn_handle_at_init(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t wcntr_topo_fmwk_extn_handle_at_deinit(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t wcntr_topo_intf_extn_handle_at_init(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   // Perform data ports open operation on modules, if it supports the extension.
   if (TRUE == module_ptr->flags.supports_data_port_ops)
   {
      result |= wcntr_topo_intf_extn_data_ports_hdl_at_init(topo_ptr, module_ptr);
   }

   return result;
}

/* =======================================================================
Public Function Definitions
========================================================================== */
ar_result_t wcntr_topo_init_topo(wcntr_topo_t *topo_ptr, wcntr_topo_init_data_t *init_data_ptr, POSAL_HEAP_ID heap_id)
{

   ar_result_t result                = AR_EOK;
   topo_ptr->topo_to_cntr_vtable_ptr = init_data_ptr->wcntr_topo_to_cntr_vtble_ptr;
   topo_ptr->heap_id                 = heap_id;

   return result;
}

ar_result_t wcntr_topo_create_input_port(wcntr_topo_t *topo, wcntr_topo_input_port_t *input_port_ptr)
{
   return AR_EOK;
}
ar_result_t wcntr_topo_create_output_port(wcntr_topo_t *topo, wcntr_topo_output_port_t *output_port_ptr)
{
   return AR_EOK;
}

ar_result_t wcntr_topo_destroy_cmn_port(wcntr_topo_module_t *     module_ptr,
                                        wcntr_topo_common_port_t *cmn_port_ptr,
                                        uint32_t                  log_id,
                                        uint32_t                  port_id,
                                        bool_t                    is_input)
{


   bool_t non_inplace_module_output_port = FALSE;

   if (cmn_port_ptr->bufs_ptr)
   {
      if (cmn_port_ptr->bufs_ptr[0].actual_data_len)
      {
         WCNTR_TOPO_MSG(log_id,
                        DBG_LOW_PRIO,
                        "Module 0x%lX, port_id 0x%lx, Dropping data %lu in destroy common port.",
                        module_ptr->gu.module_instance_id,
                        port_id,
                        cmn_port_ptr->bufs_ptr[0].actual_data_len);
      }
      wcntr_topo_set_all_bufs_len_to_zero(cmn_port_ptr);

      // buffer belongs to non-inplace module's output port

      non_inplace_module_output_port = !module_ptr->flags.inplace & !is_input;

      WCNTR_TOPO_MSG(log_id,
                     DBG_LOW_PRIO,
                     "Module 0x%lX, port_id 0x%lx, non_inplace_module_output_port %u data_ptr 0x%lx bufs_ptr 0x%lx ",
                     module_ptr->gu.module_instance_id,
                     port_id,
                     non_inplace_module_output_port,
                     cmn_port_ptr->bufs_ptr[0].data_ptr,
                     cmn_port_ptr->bufs_ptr);

      if (non_inplace_module_output_port)
      {
         MFREE_NULLIFY(cmn_port_ptr->bufs_ptr[0].data_ptr);
      }
      // Free bufs_ptr pointer
      MFREE_NULLIFY(cmn_port_ptr->bufs_ptr);
      cmn_port_ptr->sdata.bufs_num = 0;
      cmn_port_ptr->sdata.buf_ptr  = NULL;
   }

   return AR_EOK;
}

ar_result_t wcntr_topo_destroy_input_port(wcntr_topo_t *me_ptr, wcntr_topo_input_port_t *in_port_ptr)
{
   return AR_EOK;
}

ar_result_t wcntr_topo_destroy_output_port(wcntr_topo_t *me_ptr, wcntr_topo_output_port_t *out_port_ptr)
{
   return AR_EOK;
}

ar_result_t wcntr_topo_query_and_create_capi(wcntr_topo_t *           topo_ptr,
                                             wcntr_topo_graph_init_t *graph_init_ptr,
                                             wcntr_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   TRY(result,
       wcntr_topo_capi_create_from_amdb(module_ptr,
                                        topo_ptr,
                                        (void *)module_ptr->gu.amdb_handle,
                                        topo_ptr->heap_id,
                                        graph_init_ptr));

   TRY(result, wcntr_topo_fmwk_extn_handle_at_init(topo_ptr, module_ptr));
   TRY(result, wcntr_topo_intf_extn_handle_at_init(topo_ptr, module_ptr));

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

static ar_result_t wcntr_topo_create_virtual_stub(wcntr_topo_t *           topo_ptr,
                                                  wcntr_topo_graph_init_t *graph_init_ptr,
                                                  wcntr_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX is virtually stubbed",
                  module_ptr->gu.module_instance_id,
                  module_ptr->flags.inplace);

   TRY(result, wcntr_topo_check_create_bypass_module(topo_ptr, module_ptr));

   module_ptr->flags.inplace         = TRUE;
   module_ptr->flags.dynamic_inplace = TRUE;

   // stack size for bypass is not high: graph_init_ptr->max_stack_size = MAX(graph_init_ptr->max_stack_size, 0);
   // port_has_threshold, requires_data_buffering -> default value of 0 works.

   TRY(result,
       __gpr_cmd_register(module_ptr->gu.module_instance_id,
                          graph_init_ptr->gpr_cb_fn,
                          graph_init_ptr->spf_handle_ptr));

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

static ar_result_t wcntr_topo_create_module(wcntr_topo_t *           topo_ptr,
                                            wcntr_topo_graph_init_t *graph_init_ptr,
                                            wcntr_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   module_ptr->serial_num      = topo_ptr->module_count++;
   spf_handle_t *gpr_cb_handle = graph_init_ptr->spf_handle_ptr;

   if (AMDB_INTERFACE_TYPE_STUB == module_ptr->gu.itype)
   {
      TRY(result, wcntr_topo_create_virtual_stub(topo_ptr, graph_init_ptr, module_ptr));
   }
   else // CAPI & fmwk module
   {
      if (AMDB_MODULE_TYPE_GENERIC == module_ptr->gu.module_type)
      {
         TRY(result, wcntr_topo_query_and_create_capi(topo_ptr, graph_init_ptr, module_ptr));

         TRY(result, __gpr_cmd_register(module_ptr->gu.module_instance_id, graph_init_ptr->gpr_cb_fn, gpr_cb_handle));
      }
	  //Since no query from AMDB type will be 0
	  else if(MODULE_ID_RD_SHARED_MEM_EP == module_ptr->gu.module_id)
	  {

	   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "Not creating MODULE_ID_RD_SHARED_MEM_EP module ");

	  }
      else
      {

         WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX is of type %u which is not supported",
                        module_ptr->gu.module_instance_id,
                        module_ptr->gu.module_type);
         THROW(result, AR_EFAILED);
      }
   }

   // initialize to 1
   module_ptr->kpps_scale_factor_q4 = WCNTR_UNITY_Q4;
   module_ptr->num_proc_loops       = 1;

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

ar_result_t wcntr_topo_set_get_data_port_properties(wcntr_topo_module_t *module_ptr,
                                                    wcntr_topo_t *       topo_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, NULL != module_ptr);
   VERIFY(result, NULL != topo_ptr);

   // New ports may be added. need to set it to module.
   for (wcntr_gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      wcntr_topo_input_port_t *in_port_ptr = (wcntr_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (WCNTR_GU_STATUS_NEW == in_port_ptr->gu.cmn.gu_status )
      {
         if (module_ptr->capi_ptr)
         {
            if (WCNTR_GU_STATUS_UPDATED == module_ptr->gu.gu_status)
            {
               // if module is new then wcntr_topo_create_module takes care of this.
               TRY(result,
                   wcntr_topo_capi_set_data_port_op(module_ptr,
                                                    INTF_EXTN_DATA_PORT_OPEN,
                                                    &in_port_ptr->common.last_issued_opcode,
                                                    TRUE, /*is_input*/
                                                    in_port_ptr->gu.cmn.index,
                                                    in_port_ptr->gu.cmn.id));
            }

            // A module implementing frame duration extension must always use 1 byte threshold. See comment at threshold
            // event handling.
            if (!module_ptr->flags.need_cntr_frame_dur_extn)
            {
               wcntr_topo_capi_get_port_thresh(module_ptr->gu.module_instance_id,
                                               topo_ptr->gu.log_id,
                                               module_ptr->capi_ptr,
                                               WCNTR_CAPI_INPUT_PORT,
                                               in_port_ptr->gu.cmn.index,
                                               &in_port_ptr->common.port_event_new_threshold);
            }

            if (in_port_ptr->common.port_event_new_threshold > 1)
            {
               WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                              DBG_HIGH_PRIO,
                              "Module 0x%lX with ip thresh is now non-inplace, prev_inplace= %u ",
                              in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                              module_ptr->flags.inplace);
               // Set has threshold flag on the port and also make the module non inplace.
               // If port has threshold then there could be partial data left in the input buf after module-process.
               // But for inplace the buf belongs to output after process. Hence in this case, the inplace buffer may
               // have both input and output data, which results in corruption. To avoid this complication we don't
               // support inplace for port with thresholds. <Same logic applies to the output port context as well.>
               in_port_ptr->common.flags.port_has_threshold = TRUE;
               module_ptr->flags.inplace                    = FALSE;
            }
            else
            {
               in_port_ptr->common.flags.port_has_threshold = FALSE;
            }
            topo_ptr->capi_event_flag.port_thresh = TRUE;
         }

         TRY(result,
             wcntr_topo_initialize_bufs_sdata(topo_ptr,
                                              &in_port_ptr->common,
                                              module_ptr->gu.module_instance_id,
                                              in_port_ptr->gu.cmn.id,
                                              FALSE,
                                              NULL));
      }
      // port is never updated
      in_port_ptr->gu.cmn.gu_status = WCNTR_GU_STATUS_DEFAULT;
   }

   for (wcntr_gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      wcntr_topo_output_port_t *out_port_ptr = (wcntr_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (WCNTR_GU_STATUS_NEW == out_port_ptr->gu.cmn.gu_status )
      {
         if (module_ptr->capi_ptr)
         {
            // if module is new, then wcntr_topo_create_module takes care of this.
            if (WCNTR_GU_STATUS_UPDATED == module_ptr->gu.gu_status)
            {
               TRY(result,
                   wcntr_topo_capi_set_data_port_op(module_ptr,
                                                    INTF_EXTN_DATA_PORT_OPEN,
                                                    &out_port_ptr->common.last_issued_opcode,
                                                    FALSE, /*is_input*/
                                                    out_port_ptr->gu.cmn.index,
                                                    out_port_ptr->gu.cmn.id));
            }

            wcntr_topo_capi_get_port_thresh(module_ptr->gu.module_instance_id,
                                            topo_ptr->gu.log_id,
                                            module_ptr->capi_ptr,
                                            WCNTR_CAPI_OUTPUT_PORT,
                                            out_port_ptr->gu.cmn.index,
                                            &out_port_ptr->common.port_event_new_threshold);
            if (out_port_ptr->common.port_event_new_threshold > 1)
            {
               WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                              DBG_HIGH_PRIO,
                              "Module 0x%lX with op thresh is now non-inplace, prev_inplace= %u ",
                              out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                              module_ptr->flags.inplace);
               // Set has threshold flag on the port and make the module non-inplace.
               // <Refer input port context comment to understand the reason to make it non-inplace.>
               out_port_ptr->common.flags.port_has_threshold = TRUE;
               module_ptr->flags.inplace                     = FALSE;
            }
            else
            {
               out_port_ptr->common.flags.port_has_threshold = FALSE;
            }
            topo_ptr->capi_event_flag.port_thresh = TRUE;
         }

         // at least one buf_ptr must be always present.
         TRY(result,
             wcntr_topo_initialize_bufs_sdata(topo_ptr,
                                              &out_port_ptr->common,
                                              module_ptr->gu.module_instance_id,
                                              out_port_ptr->gu.cmn.id,
                                              FALSE,
                                              NULL));
      }
      // port is never updated

      out_port_ptr->gu.cmn.gu_status = WCNTR_GU_STATUS_DEFAULT;
   }
   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

/**
 *
 */
ar_result_t wcntr_topo_create_modules(wcntr_topo_t *topo_ptr, wcntr_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   uint32_t max_max_input_ports  = 0;
   uint32_t max_max_output_ports = 0;

   for (wcntr_gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      wcntr_gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

      // Normally we would consider only updated or new SGs, but when updating max max input vs output ports,
      // we have to look at all modules even if there is no new handling.
      for (wcntr_gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;

         // Consider only updated or new modules
         if (WCNTR_GU_STATUS_DEFAULT != module_ptr->gu.gu_status)
         {
            // New modules - create from AMDB; also set/get properties
            if (WCNTR_GU_STATUS_NEW == module_ptr->gu.gu_status)
            {
               module_ptr->topo_ptr = topo_ptr;

               // Also sets the ports.
               TRY(result, wcntr_topo_create_module(topo_ptr, graph_init_ptr, module_ptr));
            }

            /* Do data in/out port operations */
            TRY(result, wcntr_topo_set_get_data_port_properties(module_ptr, topo_ptr));

            /* Do control port operations, and get port thresholds for all ports */
            TRY(result, wcntr_topo_set_ctrl_port_properties(module_ptr, topo_ptr, FALSE));

            module_ptr->gu.gu_status = WCNTR_GU_STATUS_DEFAULT;
         }

         // Count the max of all modules, not just modules needing an update.
         max_max_input_ports  = MAX(max_max_input_ports, module_ptr->gu.max_input_ports);
         max_max_output_ports = MAX(max_max_output_ports, module_ptr->gu.max_output_ports);
      }

      // Clearing gu status since we updated all needed modules in the sg. If status is already
      // default, this does nothing.
      sg_ptr->gu_status = WCNTR_GU_STATUS_DEFAULT;
   }

   if ((max_max_input_ports != topo_ptr->proc_context.num_in_ports) && (max_max_input_ports > 0))
   {
      MFREE_REALLOC_MEMSET(topo_ptr->proc_context.in_port_sdata_pptr,
                           capi_stream_data_v2_t *,
                           max_max_input_ports * sizeof(capi_stream_data_v2_t *),
                           topo_ptr->heap_id,
                           result);

      topo_ptr->proc_context.num_in_ports = max_max_input_ports;
   }

   if ((max_max_output_ports != topo_ptr->proc_context.num_out_ports) && (max_max_output_ports > 0))
   {
      MFREE_REALLOC_MEMSET(topo_ptr->proc_context.out_port_sdata_pptr,
                           capi_stream_data_v2_t *,
                           max_max_output_ports * sizeof(capi_stream_data_v2_t *),
                           topo_ptr->heap_id,
                           result);

      topo_ptr->proc_context.num_out_ports = max_max_output_ports;
   }

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

/**
 * reset_capi_dependent_dont_destroy - for placeholder module reset we want to only reset capi stuff and not destroy
 * everything
 */
void wcntr_topo_destroy_module(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   /*Every module is registered at the time of create, hence needs to be registered*/

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
				  DBG_HIGH_PRIO,
				  "wcntr_topo_destroy_module Module 0x%lX START",
				  module_ptr->gu.module_instance_id);

   __gpr_cmd_deregister(module_ptr->gu.module_instance_id);

   wcntr_topo_check_destroy_bypass_module(topo_ptr, module_ptr);

   // uint32_t is_inplace = module_ptr->flags.inplace;

   if (module_ptr->capi_ptr)
   {
      module_ptr->capi_ptr->vtbl_ptr->end(module_ptr->capi_ptr);
      MFREE_NULLIFY(module_ptr->capi_ptr);
   }

   if (topo_ptr->topo_to_cntr_vtable_ptr->destroy_module)
   {
      topo_ptr->topo_to_cntr_vtable_ptr->destroy_module(topo_ptr, module_ptr, FALSE);
   }

   // Destroy module input and output port resources.
   for (wcntr_gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      wcntr_topo_output_port_t *out_port_ptr = (wcntr_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      wcntr_topo_common_port_t *cmn_port_ptr = &out_port_ptr->common;

      wcntr_topo_destroy_cmn_port(module_ptr, cmn_port_ptr, topo_ptr->gu.log_id, out_port_ptr->gu.cmn.id, FALSE);
   }

   for (wcntr_gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      wcntr_topo_input_port_t * in_port_ptr  = (wcntr_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      wcntr_topo_common_port_t *cmn_port_ptr = &in_port_ptr->common;

      wcntr_topo_destroy_cmn_port(module_ptr, cmn_port_ptr, topo_ptr->gu.log_id, in_port_ptr->gu.cmn.id, TRUE);
   }

	   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
				  DBG_HIGH_PRIO,
				  "wcntr_topo_destroy_module Module 0x%lX END",
				  module_ptr->gu.module_instance_id);
}

ar_result_t wcntr_topo_destroy_modules(wcntr_topo_t *topo_ptr, spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   for (wcntr_gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;

         // if gk sg list is given then unless sg id is found in it, we don't need to destroy
         if (spf_sg_list_ptr)
         {
            if (!wcntr_gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, module_ptr->gu.sg_ptr->id))
            {
               continue;
            }
         }
         wcntr_topo_destroy_module(topo_ptr, module_ptr);
      }
   }

   return AR_EOK;
}

ar_result_t wcntr_topo_destroy_topo(wcntr_topo_t *topo_ptr)
{
   MFREE_NULLIFY(topo_ptr->proc_context.in_port_sdata_pptr);
   MFREE_NULLIFY(topo_ptr->proc_context.out_port_sdata_pptr);
   return AR_EOK;
}

uint32_t wcntr_topo_get_curr_port_threshold(wcntr_topo_common_port_t *port_ptr)
{
   uint32_t threshold =
      (0 != port_ptr->port_event_new_threshold) ? port_ptr->port_event_new_threshold : port_ptr->max_buf_len;

   return threshold;
}

static ar_result_t wcntr_topo_prop_med_fmt_from_prev_out_to_next_in(wcntr_topo_t *           topo_ptr,
                                                                    wcntr_topo_module_t *    module_ptr,
                                                                    wcntr_topo_input_port_t *in_port_ptr,
                                                                    bool_t                   is_data_path,
                                                                    bool_t *is_further_propagation_possible)
{
   ar_result_t               result       = AR_EOK;
   wcntr_topo_output_port_t *prev_out_ptr = (wcntr_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;
   wcntr_topo_sg_t *         sg_ptr       = (wcntr_topo_sg_t *)in_port_ptr->gu.cmn.module_ptr->sg_ptr;
   if (!sg_ptr->can_mf_be_propagated)
   {
      *is_further_propagation_possible = FALSE;
      WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "MF not propagating now from MID 0x%lX ",
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id);
      return AR_EOK;
   }
   bool_t is_run_time = (WCNTR_TOPO_PORT_STATE_STARTED == in_port_ptr->common.state);

   /* *
    *  propagation is possible only if port states are appropriate & also if there's a media fmt event.
    *
    *  Do not break going through the sorted module list if propagation is not possible.
    *  Modules may be connected nonlinearly.
    *
    */
   bool_t mf_event_present =
      (prev_out_ptr && prev_out_ptr->common.flags.media_fmt_event) || in_port_ptr->common.flags.media_fmt_event;

   bool_t propagation_possible = mf_event_present;

   bool_t is_rt = (topo_ptr->flags.is_signal_triggered);

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "prop_med_fmt_from_prev_out_to_next_in: miid 0x%lX , is_run_time %u mf_event_present %u ",
                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  is_run_time,
                  mf_event_present);

   // Force propagation if the input port hasn't received a media format yet and the previous output port
   // has a valid media format.
   // - (!propagation_possible): No need to force if already true.
   // - (prev_out_ptr): Only needed for internal input port case. External inputs will always receive a new media
   //   format on the data path before data during start.
   if ((!propagation_possible) && (!in_port_ptr->flags.media_fmt_received) && prev_out_ptr)
   {
      if (prev_out_ptr->common.flags.is_mf_valid)
      {
         propagation_possible = TRUE;
      }
   }

   if (propagation_possible) // this check prevents unnecessary warning prints.
   {
      if (is_data_path)
      {
         if (!is_run_time)
         {
            /** Data path media format must be propagated to only those parts of the graph which are started.
             */
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Warning: Data path media format will not be propagated to not started ports (0x%lX, "
                           "0x%lx).",
                           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->gu.cmn.id);
            propagation_possible = FALSE;
         }
      }
      else
      {
         wcntr_topo_port_state_t in_port_sg_state;
         in_port_sg_state =
            wcntr_topo_sg_state_to_port_state(wcntr_topo_get_sg_state(in_port_ptr->gu.cmn.module_ptr->sg_ptr));

         if (WCNTR_TOPO_PORT_STATE_STOPPED == in_port_sg_state)
         {
            /** Control path media format must not be propagated to parts of the graph which are already running or
             * in stop state. Must be in prepare orstarted state.
             * this is just optimization to prevent multiple media fmt from propagating in stop state.
             */
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Warning: Control path media format will not be propagated to "
                           "stopped (%lu) sg ports (sg must be in prepare/start state) (0x%lX, 0x%lx). ",
                           in_port_sg_state,
                           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->gu.cmn.id);
            propagation_possible = FALSE;
         }
      }

      // print warning only if RT & propagation not possible or if > 0 bytes. otherwise, no use printing.
      if (is_rt)
      {

         if (in_port_ptr->common.bufs_ptr && in_port_ptr->common.bufs_ptr[0].actual_data_len > 0)
         {
            // drop RT and propagate media format in case of RT/interrupt driven use cases even if
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_ERROR_PRIO,
                           "Warning: Propagating media format to (0x%lX, 0x%lx) by dropping pending data %lu  for real "
                           "time "
                           "case, pending zeros %lu dropped",
                           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->gu.cmn.id,
                           in_port_ptr->common.bufs_ptr[0].actual_data_len);

            // drop the data and propagate
            wcntr_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
         }

         if (!propagation_possible)
         {
            propagation_possible = TRUE;
            // drop RT and propagate media format in case of RT/interrupt driven use cases even if
            // propagation_possible=FALSE due to prepare (above).
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_ERROR_PRIO,
                           "Warning: Propagating media format to (0x%lX, 0x%lx) forcefully for real time case. "
                           "port_state%u)",
                           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->gu.cmn.id,
                           in_port_ptr->common.state);
         }
      }
      else // NRT use cases:
      {
         // Signal triggered Always RT
      }
   }

   if (!propagation_possible)
   {
      // in case of control path MF sent after start, prop won't be done now. but we need to remember for later.
      if (mf_event_present)
      {
         in_port_ptr->common.flags.media_fmt_event = TRUE;
      }
      return result;
   }

   if (prev_out_ptr && prev_out_ptr->common.flags.media_fmt_event)
   {

      wcntr_tu_copy_media_fmt(&in_port_ptr->common.media_fmt, &prev_out_ptr->common.media_fmt);
      prev_out_ptr->common.flags.media_fmt_event = FALSE;
      in_port_ptr->common.flags.media_fmt_event  = TRUE;
      in_port_ptr->flags.media_fmt_received      = TRUE;
      in_port_ptr->common.flags.is_mf_valid      = prev_out_ptr->common.flags.is_mf_valid;

      WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "prop_med_fmt_from_prev_out_to_next_in: miid 0x%lX , updated port flags %X",
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,in_port_ptr->flags.word);
   }

   // For ext in port this flag is set at container level.
   if (in_port_ptr->common.flags.media_fmt_event)
   {
      // Even if module is being bypassed, we need to set media fmt as setting media fmt may enable module.
      if (module_ptr->capi_ptr)
      {
         if (in_port_ptr->common.flags.is_mf_valid)
         {

            WCNTR_TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                                       module_ptr,
                                       in_port_ptr,
                                       (&in_port_ptr->common.media_fmt),
                                       "module input");

            result = wcntr_topo_capi_set_media_fmt(topo_ptr,
                                                   module_ptr,
                                                   &in_port_ptr->common.media_fmt,
                                                   TRUE, // is input mf?
                                                   in_port_ptr->gu.cmn.index);
         }
         if (AR_DID_FAIL(result))
         {
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Warning: Module 0x%lX: Set Input Media format failed. bypassing module.",
                           module_ptr->gu.module_instance_id);
            result = wcntr_topo_check_create_bypass_module(topo_ptr, module_ptr);
            if (AR_DID_FAIL(result))
            {
               WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                              DBG_ERROR_PRIO,
                              "Module 0x%lX: Error creating bypass module",
                              module_ptr->gu.module_instance_id);
            }
            // no need to set topo_ptr->capi_event_flag.process_state = TRUE;
         }
         else
         {
            if (module_ptr->bypass_ptr && !module_ptr->flags.disabled)
            {
               result = wcntr_topo_check_destroy_bypass_module(topo_ptr, module_ptr);
            }
         }

         if (module_ptr->bypass_ptr)
         {
            uint32_t kpps = wcntr_topo_get_memscpy_kpps(in_port_ptr->common.media_fmt.pcm.bits_per_sample,
                                                        in_port_ptr->common.media_fmt.pcm.num_channels,
                                                        in_port_ptr->common.media_fmt.pcm.sample_rate);
            topo_ptr->capi_event_flag.kpps = (kpps != module_ptr->kpps);
            module_ptr->kpps               = kpps;
         }
      }

      /* no CAPI, only one i/p and o/p port possible
       * propagate input to output for framework modules and bypass modules*/
      if (!module_ptr->capi_ptr || module_ptr->bypass_ptr)
      {
         for (wcntr_gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              NULL != out_port_list_ptr;
              LIST_ADVANCE(out_port_list_ptr))
         {
            wcntr_topo_output_port_t *out_port_ptr = (wcntr_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (in_port_ptr->common.flags.is_mf_valid &&
                wcntr_tu_has_media_format_changed(&out_port_ptr->common.media_fmt, &in_port_ptr->common.media_fmt))
            {
               wcntr_tu_copy_media_fmt(&out_port_ptr->common.media_fmt, &in_port_ptr->common.media_fmt);
               out_port_ptr->common.flags.media_fmt_event = TRUE;
               out_port_ptr->common.flags.is_mf_valid     = TRUE;

               WCNTR_TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                                          module_ptr,
                                          out_port_ptr,
                                          (&out_port_ptr->common.media_fmt),
                                          "module output");
            }
         }
      }

      in_port_ptr->common.flags.media_fmt_event = FALSE;
   }
   return result;
}

ar_result_t wcntr_topo_propagate_media_fmt(void *cxt_ptr, bool_t is_data_path)
{
   wcntr_topo_t *topo_ptr = (wcntr_topo_t *)cxt_ptr;
   return wcntr_topo_propagate_media_fmt_from_module(cxt_ptr, is_data_path, topo_ptr->gu.sorted_module_list_ptr);
}

/**
 * before calling this, ext input port media fmt is assigned to input port media fmt.
 * framework extension callback allows different topo implementations to call this function and have
 * different framework extension handling.
 */
ar_result_t wcntr_topo_propagate_media_fmt_from_module(void *                  cxt_ptr,
                                                       bool_t                  is_data_path,
                                                       wcntr_gu_module_list_t *start_module_list_ptr)
{
   ar_result_t   result                          = AR_EOK;
   wcntr_topo_t *topo_ptr                        = (wcntr_topo_t *)cxt_ptr;
   bool_t        atleast_one_mod_mf_prop_blocked = FALSE;


   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_MED_PRIO,
                  "propagate_media_fmt START through topo from miid 0x%lX",
                  (start_module_list_ptr ? start_module_list_ptr->module_ptr->module_instance_id : 0));

   for (wcntr_gu_module_list_t *module_list_ptr = start_module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;

      for (wcntr_gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr;
           (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         wcntr_topo_input_port_t *in_port_ptr = (wcntr_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         // Copy media fmt from prev out port to this input port (link/connection)

         bool_t is_further_propagation_possible = TRUE;
         wcntr_topo_prop_med_fmt_from_prev_out_to_next_in(topo_ptr,
                                                          module_ptr,
                                                          in_port_ptr,
                                                          is_data_path,
                                                          &is_further_propagation_possible);
         if (!is_further_propagation_possible)
         {
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Not propagating media further, continue  ");
            atleast_one_mod_mf_prop_blocked = TRUE;
            topo_ptr->mf_propagation_done   = FALSE;
            continue;
         }
      }

      for (wcntr_gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
           NULL != out_port_list_ptr;
           LIST_ADVANCE(out_port_list_ptr))
      {
         wcntr_topo_output_port_t *out_port_ptr = (wcntr_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         if (SPF_UNKNOWN_DATA_FORMAT == out_port_ptr->common.media_fmt.data_format)
         {

            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_LOW_PRIO,
                           "Trying to get out MF for module 0x%lX as current data_format is unknown",
                           module_ptr->gu.module_instance_id);

            if (module_ptr->capi_ptr)
            {
               (void)wcntr_topo_capi_get_out_media_fmt(topo_ptr, module_ptr, out_port_ptr);
            }
         }
      }

      // if out port got destroyed and recreated while disabled, output needs to be re-initialized with input media
      // format.
      if (module_ptr->bypass_ptr) // SISO
      {
         // If the output/input port doesn't exists then media format cannot be propagated.
         if ((module_ptr->gu.num_input_ports == 0) || (module_ptr->gu.num_output_ports == 0))
         {
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Warning: Module 0x%lX doesnt is bypassed and dangling. Media format cannot be propagated.",
                           module_ptr->gu.module_instance_id);
            return result;
         }
         wcntr_topo_input_port_t *in_port_ptr =
            (wcntr_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
         wcntr_topo_output_port_t *out_port_ptr =
            (wcntr_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;

         if (in_port_ptr->common.flags.is_mf_valid &&
             wcntr_tu_has_media_format_changed(&out_port_ptr->common.media_fmt, &in_port_ptr->common.media_fmt))
         {
            wcntr_tu_copy_media_fmt(&out_port_ptr->common.media_fmt, &in_port_ptr->common.media_fmt);
            out_port_ptr->common.flags.is_mf_valid     = TRUE;
            out_port_ptr->common.flags.media_fmt_event = TRUE;

            WCNTR_TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                                       module_ptr,
                                       out_port_ptr,
                                       (&out_port_ptr->common.media_fmt),
                                       "bypass module output");
         }
      }
   }

   
   topo_ptr->capi_event_flag.media_fmt_event = FALSE;
   if (!atleast_one_mod_mf_prop_blocked)
   {
      topo_ptr->mf_propagation_done = TRUE;
   }
   WCNTR_TOPO_MSG(topo_ptr->gu.log_id, DBG_MED_PRIO, "propagate_media_fmt done with result %u ",result);

   return result;
}

ar_result_t wcntr_topo_set_input_port_media_format(void *ctx_ptr, wcntr_topo_media_fmt_t *media_fmt_ptr)
{
   wcntr_topo_input_port_t *topo_in_port_ptr = (wcntr_topo_input_port_t *)ctx_ptr;

   // This copies ptr for raw & sets ext port media fmt buf ptr as NULL.
   wcntr_tu_copy_media_fmt(&topo_in_port_ptr->common.media_fmt, media_fmt_ptr);
   topo_in_port_ptr->flags.media_fmt_received     = TRUE;
   topo_in_port_ptr->common.flags.media_fmt_event = TRUE;
   topo_in_port_ptr->common.flags.is_mf_valid     = wcntr_topo_is_valid_media_fmt(&topo_in_port_ptr->common.media_fmt);
   return AR_EOK;
}

ar_result_t wcntr_topo_get_output_port_media_format(void *                  v_topo_ptr,
                                                    void *                  ctx_ptr,
                                                    bool_t                  update_to_unchanged,
                                                    wcntr_topo_media_fmt_t *media_fmt_ptr)
{

   wcntr_topo_output_port_t *topo_out_port_ptr = (wcntr_topo_output_port_t *)ctx_ptr;

   wcntr_topo_media_fmt_t *module_out_media_fmt_ptr = &topo_out_port_ptr->common.media_fmt;

   if (topo_out_port_ptr->common.flags.media_fmt_event)
   {
      wcntr_tu_copy_media_fmt(media_fmt_ptr, module_out_media_fmt_ptr);
      if (SPF_RAW_COMPRESSED != module_out_media_fmt_ptr->data_format)
      {
         // even if last module outputs deint-unpacked, container sends out deint-packed
         media_fmt_ptr->pcm.interleaving =
            (module_out_media_fmt_ptr->pcm.interleaving == WCNTR_TOPO_DEINTERLEAVED_UNPACKED)
               ? WCNTR_TOPO_DEINTERLEAVED_PACKED
               : module_out_media_fmt_ptr->pcm.interleaving;
      }

      if (update_to_unchanged)
      {
         topo_out_port_ptr->common.flags.media_fmt_event = FALSE;
      }
   }
   return AR_EOK;
}

bool_t wcntr_topo_is_output_port_media_format_changed(void *ctx_ptr)
{
   wcntr_topo_output_port_t *topo_out_port_ptr = (wcntr_topo_output_port_t *)ctx_ptr;

   return topo_out_port_ptr->common.flags.media_fmt_event;
}

/**
 * this reset func was introduced to avoid calling algo reset
 *  & destroy MD
 */
ar_result_t wcntr_topo_basic_reset_input_port(wcntr_topo_t *me_ptr, wcntr_topo_input_port_t *in_port_ptr)
{

   in_port_ptr->common.sdata.flags.word                = 0;
   in_port_ptr->common.sdata.flags.stream_data_version = CAPI_STREAM_V2; // for capi_stream_data_v2_t
   in_port_ptr->common.sdata.timestamp                 = 0;

   if (in_port_ptr->common.bufs_ptr)
   {
      wcntr_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
   }

   WCNTR_TOPO_MSG(me_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: reset input port 0x%lx",
                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  in_port_ptr->gu.cmn.id);

   return AR_EOK;
}

ar_result_t wcntr_topo_shared_reset_input_port(void *topo_ptr, void *topo_in_port_ptr)
{
   wcntr_topo_t *           me_ptr      = (wcntr_topo_t *)topo_ptr;
   wcntr_topo_input_port_t *in_port_ptr = (wcntr_topo_input_port_t *)topo_in_port_ptr;

   wcntr_topo_basic_reset_input_port(me_ptr, in_port_ptr);
   wcntr_topo_algo_reset((void *)in_port_ptr->gu.cmn.module_ptr,
                         me_ptr->gu.log_id,
                         TRUE,                       /* is_port_valid */
                         TRUE,                       /* is_input */
                         in_port_ptr->gu.cmn.index); /* port_index */

   return AR_EOK;
}

/**
 * doesn't call algo reset & destroy MD
 */
ar_result_t wcntr_topo_basic_reset_output_port(wcntr_topo_t *me_ptr, wcntr_topo_output_port_t *out_port_ptr)
{
   ar_result_t          result     = AR_EOK;
   wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

   out_port_ptr->common.sdata.flags.word                = 0;
   out_port_ptr->common.sdata.flags.stream_data_version = CAPI_STREAM_V2; // for capi_stream_data_v2_t
   out_port_ptr->common.sdata.timestamp                 = 0;

   if (out_port_ptr->common.bufs_ptr)
   {
      wcntr_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
   }

   WCNTR_TOPO_MSG(me_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: reset output port 0x%lx",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.id);

   return result;
}

ar_result_t wcntr_topo_shared_reset_output_port(void *topo_ptr, void *topo_out_port_ptr)
{
   wcntr_topo_t *            me_ptr       = (wcntr_topo_t *)topo_ptr;
   wcntr_topo_output_port_t *out_port_ptr = (wcntr_topo_output_port_t *)topo_out_port_ptr;

   wcntr_topo_basic_reset_output_port(me_ptr, out_port_ptr);
   wcntr_topo_algo_reset((void *)out_port_ptr->gu.cmn.module_ptr,
                         me_ptr->gu.log_id,
                         TRUE,                        /* is_port_valid */
                         FALSE,                       /* is_input */
                         out_port_ptr->gu.cmn.index); /* port_index */

   return AR_EOK;
}

ar_result_t wcntr_topo_reset_all_in_ports(wcntr_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   for (wcntr_gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      wcntr_topo_shared_reset_input_port((void *)module_ptr->topo_ptr, (void *)in_port_list_ptr->ip_port_ptr);
   }
   return result;
}

ar_result_t wcntr_topo_reset_all_out_ports(wcntr_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   for (wcntr_gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      wcntr_topo_shared_reset_output_port((void *)module_ptr->topo_ptr, (void *)out_port_list_ptr->op_port_ptr);
   }
   return result;
}

/**
 * useful when CAPI itself is replaced with another (such as in placeholder)
 */

void wcntr_topo_reset_top_level_flags(wcntr_topo_t *topo_ptr)
{
   topo_ptr->flags.any_data_trigger_policy = FALSE;
   topo_ptr->flags.is_signal_triggered     = FALSE;

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Resetting top flags.");

   for (wcntr_gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->flags.need_stm_extn)
         {
            topo_ptr->flags.is_signal_triggered = TRUE;
         }
      }
   }
}

ar_result_t wcntr_topo_algo_reset(void *   topo_module_ptr,
                                  uint32_t log_id,
                                  bool_t   is_port_valid,
                                  bool_t   is_input,
                                  uint16_t port_index)
{
   wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)topo_module_ptr;

   if (module_ptr->capi_ptr)
   {
      AR_MSG(DBG_LOW_PRIO,
             "Module 0x%lX: wcntr_topo_algo_reset  is_input %u ,port_index %u",
             module_ptr->gu.module_instance_id,
             is_input,
             port_index);

      return wcntr_topo_capi_algorithmic_reset(log_id, module_ptr->capi_ptr, is_port_valid, is_input, port_index);
   }
   return AR_EOK;
}

wcntr_topo_sg_state_t wcntr_topo_get_sg_state(wcntr_gu_sg_t *sg_ptr)
{
   return ((wcntr_topo_sg_t *)sg_ptr)->state;
}

void wcntr_topo_set_sg_state(wcntr_gu_sg_t *sg_ptr, wcntr_topo_sg_state_t state)
{
   ((wcntr_topo_sg_t *)sg_ptr)->state = state;
}

ar_result_t wcntr_topo_update_ctrl_port_state(void *                 vtopo_ptr,
                                              wcntr_topo_port_type_t port_type,
                                              void *                 port_ptr,
                                              wcntr_topo_sg_state_t  state)
{
   wcntr_topo_t *topo_ptr = (wcntr_topo_t *)vtopo_ptr;
   ar_result_t   result   = AR_EOK;

   if (port_type == WCNTR_TOPO_CONTROL_PORT_TYPE)
   {
      wcntr_topo_from_sg_state_set_ctrl_port_state(port_ptr, state);
   }
   else
   {
      WCNTR_TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Invalid port type %d", port_type);
   }

   return result;
}

bool_t wcntr_topo_is_module_active(wcntr_topo_module_t *module_ptr, bool_t need_to_ignore_state)
{
   return module_ptr->can_process_be_called;
}

static uint32_t wcntr_topo_aggregate_bandwidth(wcntr_topo_t *topo_ptr, bool_t only_aggregate)
{
   uint32_t aggregated_bw        = 0;
   bool_t   need_to_ignore_state = only_aggregate;

   for (wcntr_gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         uint32_t             bw         = 0;
         wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;
         // Why not checking data flow state? see gen_cntr_pm gen_cntr_vote_pm_conditionally
         if (wcntr_topo_is_module_active(module_ptr, need_to_ignore_state))
         {
            if (module_ptr->capi_ptr)
            {
               bw = module_ptr->data_bw + module_ptr->code_bw;
            }
         }
         else
            aggregated_bw += bw;
      }
   }

   return aggregated_bw;
}

/** gives for all CAPIs. but not per IO streams. */
static uint32_t wcntr_topo_aggregate_kpps(wcntr_topo_t *topo_ptr,
                                          bool_t        only_aggregate,
                                          uint32_t *    scaled_kpps_agg_q4_ptr)
{
   uint32_t aggregate_kpps       = 0;
   bool_t   need_to_ignore_state = only_aggregate;

   *scaled_kpps_agg_q4_ptr = 0;
   for (wcntr_gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         wcntr_topo_module_t *module_ptr = (wcntr_topo_module_t *)module_list_ptr->module_ptr;

         // Why not checking data flow state? see gen_cntr_pm gen_cntr_vote_pm_conditionally
         if (wcntr_topo_is_module_active(module_ptr, need_to_ignore_state))
         {
            uint32_t kpps = 0, scaled_kpps_q4 = 0;

            kpps           = module_ptr->kpps;
            scaled_kpps_q4 = (module_ptr->kpps_scale_factor_q4 * kpps);

            *scaled_kpps_agg_q4_ptr += scaled_kpps_q4;
            aggregate_kpps += kpps;
         }
      }
   }

   return aggregate_kpps;
}

/* =======================================================================
Public Functions

BW is in bytes per sec
========================================================================== */

ar_result_t wcntr_topo_aggregate_kpps_bandwidth(wcntr_topo_t *topo_ptr,
                                                bool_t        only_aggregate,
                                                uint32_t *    aggregate_kpps_ptr,
                                                uint32_t *    aggregate_bw_ptr,
                                                uint32_t *    scaled_kpps_q4_agg_ptr)
{
   ar_result_t result = AR_EOK;

   // Aggregate kpps/bw, then send to common processing.
   {
      *aggregate_kpps_ptr = wcntr_topo_aggregate_kpps(topo_ptr, only_aggregate, scaled_kpps_q4_agg_ptr);
   }

   {
      *aggregate_bw_ptr = wcntr_topo_aggregate_bandwidth(topo_ptr, only_aggregate);
   }

   return result;
}

static bool_t wcntr_topo_is_module_bypassable(wcntr_topo_module_t *module_ptr)
{
   /**
    * checking max port because, if we check num_ports, then while disabled, new ports may be created.
    * bypass not possible to achieve for MIMO.
    *
    * bypass possible for SISO only.
    *
    */
   if ((module_ptr->gu.max_input_ports != 1) || (module_ptr->gu.max_output_ports != 1))
   {
      return FALSE;
   }

   /**
    * Only generic may be disabled
    */
   if (!((AMDB_MODULE_TYPE_GENERIC == module_ptr->gu.module_type) ||
         (AMDB_MODULE_TYPE_FRAMEWORK == module_ptr->gu.module_type)))
   {
      return FALSE;
   }

   return TRUE;
}

static ar_result_t wcntr_topo_switch_bypass_module(wcntr_topo_t *       topo_ptr,
                                                   wcntr_topo_module_t *module_ptr,
                                                   bool_t               bypass_enable)
{
   //  INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // VERIFY(result, topo_ptr->gen_topo_vtable_ptr->get_out_port_data_len);

   if ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.num_output_ports))
   {
      wcntr_topo_output_port_t *output_port_ptr =
         (wcntr_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
      wcntr_topo_input_port_t *input_port_ptr =
         (wcntr_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;

      wcntr_topo_media_fmt_t *src_media_fmt_ptr = NULL;

      if (bypass_enable)
      {
         // back-up output media fmt
         memscpy(&module_ptr->bypass_ptr->media_fmt,
                 sizeof(module_ptr->bypass_ptr->media_fmt),
                 &output_port_ptr->common.media_fmt,
                 sizeof(output_port_ptr->common.media_fmt));

         // use input media fmt as output
         src_media_fmt_ptr = &input_port_ptr->common.media_fmt;
      }
      else
      {
         // else restore output media fmt
         src_media_fmt_ptr = &module_ptr->bypass_ptr->media_fmt;
      }

      // Create memory only if media format is different between this module's input and output
      if (wcntr_topo_is_valid_media_fmt(src_media_fmt_ptr) &&
          wcntr_tu_has_media_format_changed(src_media_fmt_ptr, &output_port_ptr->common.media_fmt))
      {
         bool_t IS_MAX_FALSE = FALSE;
         if (0 != wcntr_topo_get_out_port_data_len(topo_ptr, output_port_ptr, IS_MAX_FALSE))
         {
            WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Warning: Module 0x%lX has data at the output while being bypass disabled/enabled (%u)",
                           module_ptr->gu.module_instance_id,
                           bypass_enable);
            // TODO: in this case, we need to propagate media fmt after this data is sent down.
         }

         memscpy(&output_port_ptr->common.media_fmt,
                 sizeof(wcntr_topo_media_fmt_t),
                 src_media_fmt_ptr,
                 sizeof(wcntr_topo_media_fmt_t));

         output_port_ptr->common.flags.media_fmt_event = TRUE;
         topo_ptr->capi_event_flag.media_fmt_event     = TRUE;
         output_port_ptr->common.flags.is_mf_valid     = TRUE;

         WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_LOW_PRIO,
                        "Module 0x%lX has different media format between input or bypass and output while bypass "
                        "enable/disable (%u)",
                        module_ptr->gu.module_instance_id,
                        bypass_enable);
      }

      bool_t   kpps_changed = FALSE, bw_changed = FALSE;
      uint32_t bypass_kpps = 0;
      // KPPS/BW events are handled by container after looking into wcntr_topo_capi_event_flag_t::process_state flag
      if (bypass_enable)
      {
         module_ptr->bypass_ptr->kpps       = module_ptr->kpps;
         module_ptr->bypass_ptr->algo_delay = module_ptr->algo_delay;
         module_ptr->bypass_ptr->code_bw    = module_ptr->code_bw;
         module_ptr->bypass_ptr->data_bw    = module_ptr->data_bw;

         /*In 1 sec we transfer SR*bytes*ch bytes using memcpy. memcpy usually copies 8-bytes at a time,
         where it does 1 8-byte read and 1 8-byte write = 2 instructions for every 8-byte copy
         Therefore pps = 2*SR*bytes*ch/8 and kpps = pps/1000.*/
         bypass_kpps = wcntr_topo_get_memscpy_kpps(module_ptr->bypass_ptr->media_fmt.pcm.bits_per_sample,
                                                   module_ptr->bypass_ptr->media_fmt.pcm.num_channels,
                                                   module_ptr->bypass_ptr->media_fmt.pcm.sample_rate);

         kpps_changed           = (module_ptr->kpps != bypass_kpps);
         bw_changed             = ((module_ptr->data_bw != WC_BYPASS_BW) || (module_ptr->code_bw != 0));
         module_ptr->kpps       = bypass_kpps;
         module_ptr->algo_delay = 0;
         module_ptr->code_bw    = 0;
         module_ptr->data_bw    = WC_BYPASS_BW;

         // Initialize bypassed thresholds to 0. If a module raises a threshold, this will become set at that time,
         // indicating threshold was raised while module was bypassed.
         module_ptr->bypass_ptr->in_thresh_bytes_all_ch  = 0;
         module_ptr->bypass_ptr->out_thresh_bytes_all_ch = 0;

         // Note on Metadata propagation: we set algo delay to zero above. Internal MD offsets which are based on
         // earlier algo delay must be set to zero.
         // to ensure EOS goes out, need to make pending zeros also zero. We are not moving internal MD to out port
         // because
         // that's taken care already in metadata propagation func. This avoids code duplication.
      }
      else
      {
         // bypass_disable case (module is getting enabled)
         kpps_changed = (module_ptr->kpps != module_ptr->bypass_ptr->kpps);
         bw_changed   = ((module_ptr->data_bw != module_ptr->bypass_ptr->code_bw) ||
                       (module_ptr->code_bw != module_ptr->bypass_ptr->data_bw));

         module_ptr->kpps       = module_ptr->bypass_ptr->kpps;
         module_ptr->algo_delay = module_ptr->bypass_ptr->algo_delay;
         module_ptr->code_bw    = module_ptr->bypass_ptr->code_bw;
         module_ptr->data_bw    = module_ptr->bypass_ptr->data_bw;

         // If thresholds are nonzero, set on ports and set event flag.
         if (0 != module_ptr->bypass_ptr->in_thresh_bytes_all_ch)
         {
            // Ignore redundant threshold events.
            if (input_port_ptr->common.port_event_new_threshold != module_ptr->bypass_ptr->in_thresh_bytes_all_ch)
            {
               input_port_ptr->common.port_event_new_threshold = module_ptr->bypass_ptr->in_thresh_bytes_all_ch;
               if (input_port_ptr->common.port_event_new_threshold > 1)
               {
                  input_port_ptr->common.flags.port_has_threshold = TRUE;
               }
               else
               {
                  input_port_ptr->common.flags.port_has_threshold = FALSE;
               }
               topo_ptr->capi_event_flag.port_thresh = TRUE;
            }
         }

         if (0 != module_ptr->bypass_ptr->out_thresh_bytes_all_ch)
         {
            // Ignore redundant threshold events.
            if (output_port_ptr->common.port_event_new_threshold != module_ptr->bypass_ptr->out_thresh_bytes_all_ch)
            {
               output_port_ptr->common.port_event_new_threshold = module_ptr->bypass_ptr->out_thresh_bytes_all_ch;
               if (output_port_ptr->common.port_event_new_threshold > 1)
               {
                  output_port_ptr->common.flags.port_has_threshold = TRUE;
               }
               else
               {
                  output_port_ptr->common.flags.port_has_threshold = FALSE;
               }
               topo_ptr->capi_event_flag.port_thresh = TRUE;
            }
         }
      }

      topo_ptr->capi_event_flag.kpps = kpps_changed;
      topo_ptr->capi_event_flag.bw   = bw_changed;
   }

   return result;
}

ar_result_t wcntr_topo_check_create_bypass_module(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "check_create_bypass_module for (miid,mid) (0x%lX,0x%lX) ",
                  module_ptr->gu.module_instance_id,
                  module_ptr->gu.module_id);

   if (module_ptr->bypass_ptr)
   {
      WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "Module 0x%lX (0x%lX) is already in bypass state",
                     module_ptr->gu.module_instance_id,
                     module_ptr->gu.module_id);
      return result;
   }

   if (!wcntr_topo_is_module_bypassable(module_ptr))
   {
      WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "Module 0x%lX (0x%lX) is not bypassable",
                     module_ptr->gu.module_instance_id,
                     module_ptr->gu.module_id);
      return AR_EFAILED;
   }

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  SPF_LOG_PREFIX " (miid,mid) (0x%lX,0x%lX) can be bypassed. Begin bypass now",
                  module_ptr->gu.module_instance_id,
                  module_ptr->gu.module_id);

   MALLOC_MEMSET(module_ptr->bypass_ptr,
                 wcntr_topo_module_bypass_t,
                 sizeof(wcntr_topo_module_bypass_t),
                 topo_ptr->heap_id,
                 result);

   if (AR_DID_FAIL(result = wcntr_topo_switch_bypass_module(topo_ptr, module_ptr, TRUE /*bypass_enable*/)))
   {
      WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX (0x%lX) failed to switch bypass",
                     module_ptr->gu.module_instance_id,
                     module_ptr->gu.module_id);
      MFREE_NULLIFY(module_ptr->bypass_ptr);
   }

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

ar_result_t wcntr_topo_check_destroy_bypass_module(wcntr_topo_t *topo_ptr, wcntr_topo_module_t *module_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!module_ptr->bypass_ptr)
   {
      return result;
   }

   TRY(result, wcntr_topo_switch_bypass_module(topo_ptr, module_ptr, FALSE /*bypass_enable*/));

   MFREE_NULLIFY(module_ptr->bypass_ptr);

   WCNTR_TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  SPF_LOG_PREFIX "Module 0x%lX (0x%lX) end bypass",
                  module_ptr->gu.module_instance_id,
                  module_ptr->gu.module_id);

   CATCH(result, WCNTR_TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}
