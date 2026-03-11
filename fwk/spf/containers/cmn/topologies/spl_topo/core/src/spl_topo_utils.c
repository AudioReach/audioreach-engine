/**
 * \file spl_topo_utils.c
 *
 * \brief
 *
 *     Topo 2 utility functions. For querying properties of spl_topo structures and naviating the spl_topo graph.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_topo_i.h"

/* =======================================================================
Public Function Definitions
========================================================================== */

void spl_topo_update_module_info(gu_module_t *gu_module_ptr)
{
   spl_topo_module_t *module_ptr = (spl_topo_module_t *)gu_module_ptr;
   module_ptr->flags.module_type = TOPO_MODULE_TYPE_SINGLE_PORT;
   if ((1 < module_ptr->t_base.gu.num_input_ports) || (1 < module_ptr->t_base.gu.num_output_ports))
   {
      module_ptr->flags.module_type = TOPO_MODULE_TYPE_MULTIPORT;
   }

   if (0 == module_ptr->t_base.gu.num_input_ports)
   {
      module_ptr->flags.module_type = TOPO_MODULE_TYPE_SOURCE;
   }

   if (0 == module_ptr->t_base.gu.num_output_ports)
   {
      module_ptr->flags.module_type = TOPO_MODULE_TYPE_SINK;
   }

   if (!module_ptr->threshold_data.is_threshold_module)
   {
      module_ptr->t_base.num_proc_loops = 1;
   }

   module_ptr->flags.is_skip_process = FALSE;


   // Skip if the module isn't connected. This implies we are waiting on another sg open
   // for the module to be usable.

   //if minimum port info is present then make sure that minimum number of ports are opened.
   if (((uint8_t)CAPI_INVALID_VAL != module_ptr->t_base.gu.min_input_ports) &&
       ((uint8_t)CAPI_INVALID_VAL != module_ptr->t_base.gu.min_output_ports))
   {
      if (module_ptr->t_base.gu.num_input_ports < module_ptr->t_base.gu.min_input_ports ||
          module_ptr->t_base.gu.num_output_ports < module_ptr->t_base.gu.min_output_ports)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(module_ptr->t_base.topo_ptr->gu.log_id,
                  DBG_MED_PRIO,
                  "spl_topo mpd skipping module: miid 0x%lx because minimum number of ports are not opened.",
                  module_ptr->t_base.gu.module_instance_id);
#endif
         module_ptr->flags.is_skip_process = TRUE;
      }
   }
   else if ((module_ptr->t_base.gu.max_input_ports > 0 && module_ptr->t_base.gu.num_input_ports == 0) ||
            (module_ptr->t_base.gu.max_output_ports > 0 && module_ptr->t_base.gu.num_output_ports == 0))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(module_ptr->t_base.topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "spl_topo mpd skipping module: miid 0x%lx due to module not connected.",
               module_ptr->t_base.gu.module_instance_id);
#endif
      module_ptr->flags.is_skip_process = TRUE;
   }

   // Skip if sg state is not started.
   if (!gen_topo_is_module_sg_started(&module_ptr->t_base))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(module_ptr->t_base.topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "spl_topo mpd skipping module: miid 0x%lx due to sg not started.",
               module_ptr->t_base.gu.module_instance_id);
#endif
      module_ptr->flags.is_skip_process = TRUE;
   }

   // Disabled modules which are not bypassed should be skipped.
   if (spl_topo_is_module_disabled(module_ptr) && (!module_ptr->t_base.bypass_ptr))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(module_ptr->t_base.topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "spl_topo mpd skipping module: miid 0x%lx due to disabled module without bypass ptr.",
               module_ptr->t_base.gu.module_instance_id);
#endif
      module_ptr->flags.is_skip_process = TRUE;
   }

   module_ptr->flags.is_any_inp_port_at_gap = FALSE;
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      if (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->t_base.common.data_flow_state)
      {
         module_ptr->flags.is_any_inp_port_at_gap = TRUE;
      }
   }
}

/** update module's info.
 */
void spl_topo_update_all_modules_info(spl_topo_t *topo_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

         spl_topo_update_module_info(&module_ptr->t_base.gu);
      }
   }

   //handle port inactive/active event before updating the simp topo connections
   if (topo_ptr->fwk_extn_info.sync_extn_pending_inactive_handling)
   {
      bool_t is_event_handled = TRUE;
      spl_topo_sync_handle_inactive_port_events(topo_ptr, &is_event_handled);
      topo_ptr->fwk_extn_info.sync_extn_pending_inactive_handling = !is_event_handled;
   }

   spl_topo_update_simp_module_connections(topo_ptr);
}

// Always wrap this function in an if of spl_topo_needs_update_sipt_2_flags().
void spl_topo_update_simp_topo_L2_flags(spl_topo_t *topo_ptr)
{
   // Check if all ports are data_flow_started.
   if (topo_ptr->simpt_event_flags.check_data_flow_state)
   {
      for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr);
           LIST_ADVANCE(sg_list_ptr))
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

            // skip the attached module
            if (module_ptr->t_base.gu.host_output_port_ptr)
            {
               continue;
            }

            for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

               if (!in_port_ptr->flags.port_inactive &&
                   (TOPO_DATA_FLOW_STATE_FLOWING != in_port_ptr->t_base.common.data_flow_state))
               {
                  // Since we know all ports are not flowing, we can clear the event here. We have to wait until
                  // this current port has data flowing state before entering sipt.
                  topo_ptr->simpt2_flags.all_ports_data_flowing  = FALSE;
                  topo_ptr->simpt_event_flags.check_data_flow_state = FALSE;

#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: Input port idx = %ld, miid = 0x%lx data flow state is not flowing, dfs "
                           "0x%lx.",
                           in_port_ptr->t_base.gu.cmn.index,
                           in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->t_base.common.data_flow_state);
#endif
                  return;
               }
            }
         }
      }

      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "SIMP_TOPO_L2_FLAGS: All ports data flowing.");

      topo_ptr->simpt2_flags.all_ports_data_flowing  = TRUE;
      topo_ptr->simpt_event_flags.check_data_flow_state = FALSE;
   }
   else if (!(topo_ptr->simpt2_flags.all_ports_data_flowing))
   {
      // If all_ports_data_flowing is FALSE, return as we can't enter sipt.
      return;
   }

   // Check if all ports have valid media format.
   if (topo_ptr->simpt_event_flags.check_valid_mf)
   {
      for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr);
           LIST_ADVANCE(sg_list_ptr))
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

            // skip the attached module
            if (module_ptr->t_base.gu.host_output_port_ptr)
            {
               continue;
            }

            for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

               if (!in_port_ptr->flags.port_inactive && !in_port_ptr->t_base.common.flags.is_mf_valid)
               {
                  // Since we know all ports don't have valid mf, we can clear the event here. We have to wait until
                  // this current port has valid mf before entering sipt.
                  topo_ptr->simpt2_flags.all_ports_valid_mf = FALSE;
                  topo_ptr->simpt_event_flags.check_valid_mf = FALSE;

#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: input port idx = %ld, miid = 0x%lx mf not valid.",
                           in_port_ptr->t_base.gu.cmn.index,
                           in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

                  return;
               }
            }

            for (gu_output_port_list_t *op_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; op_port_list_ptr;
                 LIST_ADVANCE(op_port_list_ptr))
            {
               spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)op_port_list_ptr->op_port_ptr;

               if (!out_port_ptr->flags.port_inactive && !out_port_ptr->t_base.common.flags.is_mf_valid)
               {
                  // Since we know all ports don't have valid mf, we can clear the event here. We have to wait until
                  // this current port has valid mf before entering sipt.
                  topo_ptr->simpt2_flags.all_ports_valid_mf = FALSE;
                  topo_ptr->simpt_event_flags.check_valid_mf = FALSE;

#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: output port idx = %ld, miid = 0x%lx mf not valid.",
                           out_port_ptr->t_base.gu.cmn.index,
                           out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
                  return;
               }
            }
         }
      }

      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "SIMP_TOPO_L2_FLAGS: All ports mf valid.");

      topo_ptr->simpt2_flags.all_ports_valid_mf = TRUE;
      topo_ptr->simpt_event_flags.check_valid_mf = FALSE;
   }
   else if (!(topo_ptr->simpt2_flags.all_ports_valid_mf))
   {
      // If all_ports_valid_mf is FALSE, return as we can't enter sipt.
      return;
   }

   // Check if any port has pending media format.
   if (topo_ptr->simpt_event_flags.check_pending_mf)
   {
      for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr);
           LIST_ADVANCE(sg_list_ptr))
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

            // skip the attached module
            if (module_ptr->t_base.gu.host_output_port_ptr)
            {
               continue;
            }

            for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

               if (!in_port_ptr->flags.port_inactive && in_port_ptr->t_base.common.flags.media_fmt_event)
               {
#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: input port idx = %ld, miid = 0x%lx has pending mf.",
                           in_port_ptr->t_base.gu.cmn.index,
                           in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
                  return;
               }
            }

            for (gu_output_port_list_t *op_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; op_port_list_ptr;
                 LIST_ADVANCE(op_port_list_ptr))
            {
               spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)op_port_list_ptr->op_port_ptr;

               if (!out_port_ptr->flags.port_inactive && out_port_ptr->t_base.common.flags.media_fmt_event)
               {
#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: output port idx = %ld, miid = 0x%lx has pending mf.",
                           out_port_ptr->t_base.gu.cmn.index,
                           out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
                  return;
               }
            }
         }
      }

      topo_ptr->simpt_event_flags.check_pending_mf = FALSE;

      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "SIMP_TOPO_L2_FLAGS: No port has pending mf.");
   }

   // Check if any port has eof.
   if (topo_ptr->simpt_event_flags.check_eof)
   {
      for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr);
           LIST_ADVANCE(sg_list_ptr))
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

            // skip the attached module
            if (module_ptr->t_base.gu.host_output_port_ptr)
            {
               continue;
            }

            for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
                 LIST_ADVANCE(in_port_list_ptr))
            {
               spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
               bool_t                 ext_eof     = FALSE;
               if (in_port_ptr->ext_in_buf_ptr)
               {
                  ext_eof = in_port_ptr->ext_in_buf_ptr->end_of_frame;
               }

               if (!in_port_ptr->flags.port_inactive &&
                   (spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr) || ext_eof))
               {
#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: input port idx = %ld, miid = 0x%lx has eof.",
                           in_port_ptr->t_base.gu.cmn.index,
                           in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
                  return;
               }
            }

            for (gu_output_port_list_t *op_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; op_port_list_ptr;
                 LIST_ADVANCE(op_port_list_ptr))
            {
               spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)op_port_list_ptr->op_port_ptr;

               if (!out_port_ptr->flags.port_inactive && spl_topo_out_port_has_pending_eof(topo_ptr, out_port_ptr))
               {
#ifdef SPL_SIPT_DBG
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "SIMP_TOPO_L2_FLAGS: output port idx = %ld, miid = 0x%lx has eof.",
                           out_port_ptr->t_base.gu.cmn.index,
                           out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
                  return;
               }
            }
         }
      }

      // clearing it here.
      topo_ptr->simpt_event_flags.check_eof = FALSE;

      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "SIMP_TOPO_L2_FLAGS: No port has eof.");
   }
}

/**
 * Get the module's threshold in bytes per channel. Note that amount of bytes can vary by port
 * due to different media format, but duration will be constant.
 */
uint32_t spl_topo_get_module_threshold_bytes_per_channel(gen_topo_common_port_t *port_common_ptr,
                                                         spl_topo_module_t *     module_ptr)
{
   topo_media_fmt_t *media_fmt           = port_common_ptr->media_fmt_ptr;
   uint32_t          thresh_bytes_per_ch = 0;

   if (module_ptr->threshold_data.is_threshold_module)
   {
      // Avoid rounding and overflow errors.
      // Formula: thresh_bytes_per_ch = (thresh_in_samples_per_channel * (cur_port_sr / thresh_sr) * byte_width)
      uint64_t intermediate_1 = module_ptr->threshold_data.thresh_in_samples_per_channel;

      if (media_fmt->pcm.sample_rate != module_ptr->threshold_data.thresh_port_sample_rate)
      {
         uint64_t intermediate_2 =
            module_ptr->threshold_data.thresh_in_samples_per_channel * media_fmt->pcm.sample_rate;
         intermediate_1 = intermediate_2 / module_ptr->threshold_data.thresh_port_sample_rate;
      }
      thresh_bytes_per_ch = ((uint32_t)intermediate_1) * (TOPO_BITS_TO_BYTES(media_fmt->pcm.bits_per_sample));
   }

   return thresh_bytes_per_ch;
}

/**
 * Checks if there is a pending media format in any of the module's input ports or
 * at their connected output ports.
 */
bool_t spl_topo_module_has_pending_in_media_fmt(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      spl_topo_input_port_t * in_port_ptr       = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      spl_topo_output_port_t *conn_out_port_ptr = NULL;
      spl_topo_input_port_t * conn_in_port_ptr  = NULL;

      if (spl_topo_ip_port_has_pending_media_fmt(topo_ptr, in_port_ptr))
      {
         return TRUE;
      }

      // Look to the connected port to check its media format flag.
      spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &conn_out_port_ptr, &conn_in_port_ptr);

      if (conn_out_port_ptr)
      {
         if (spl_topo_op_port_has_pending_media_fmt(topo_ptr, conn_out_port_ptr))
         {
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * Checks if there is a pending flushing eos in the input port or at its connected output port.
 */
bool_t spl_topo_input_port_has_pending_flushing_eos(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   spl_topo_output_port_t *conn_out_port_ptr = NULL;
   spl_topo_input_port_t * conn_in_port_ptr  = NULL;

   if (spl_topo_port_has_flushing_eos(topo_ptr, &(in_port_ptr->t_base.common)))
   {
      return TRUE;
   }

   // Look to the connected port to check its media format flag.
   spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &conn_out_port_ptr, &conn_in_port_ptr);

   if (conn_out_port_ptr)
   {
      if (spl_topo_port_has_flushing_eos(topo_ptr, &(conn_out_port_ptr->t_base.common)))
      {
         return TRUE;
      }
   }

   return FALSE;
}

ar_result_t spl_topo_input_port_clear_pending_eof(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result            = AR_EOK;
   spl_topo_output_port_t *conn_out_port_ptr = NULL;
   spl_topo_input_port_t * conn_in_port_ptr  = NULL;

   VERIFY(result, topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_in_port_clear_timestamp_discontinuity);

   if (in_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      in_port_ptr->t_base.common.sdata.flags.end_of_frame = FALSE;
   }

   spl_topo_get_connected_int_or_ext_op_port(topo_ptr, in_port_ptr, &conn_out_port_ptr, &conn_in_port_ptr);

   if (conn_out_port_ptr && conn_out_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      conn_out_port_ptr->t_base.common.sdata.flags.end_of_frame = FALSE;
      spl_topo_clear_output_timestamp_discontinuity(topo_ptr, conn_out_port_ptr);
   }

   if (conn_in_port_ptr)
   {
      if (conn_in_port_ptr->ext_in_buf_ptr)
      {
         if (conn_in_port_ptr->ext_in_buf_ptr->end_of_frame)
         {
            conn_in_port_ptr->ext_in_buf_ptr->end_of_frame = FALSE;
            TRY(result,
                topo_ptr->t_base.topo_to_cntr_vtable_ptr
                   ->ext_in_port_clear_timestamp_discontinuity(&(topo_ptr->t_base),
                                                               conn_in_port_ptr->t_base.gu.ext_in_port_ptr));
         }
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Return the external input structure reached by following the upstream
 * connections of the module. This function only works when the path from
 * the current module to the external input port contains only SISO modules.
 * Otherwise it is hard to define such behavior, so in that case we return
 * EBADPARAM.
 */
ar_result_t spl_topo_find_ext_inp_port(spl_topo_t *       topo_ptr,
                                       spl_topo_module_t *start_module_ptr,
                                       gu_ext_in_port_t **ext_inp_port_pptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t  result     = AR_EOK;
   gu_module_t *module_ptr = (gu_module_t *)start_module_ptr;

   VERIFY(result, ext_inp_port_pptr);

   for (;;)
   {
      if (TOPO_MODULE_TYPE_SINGLE_PORT != spl_topo_get_module_port_type((spl_topo_module_t *)module_ptr))
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Only single port modules supported");
         return AR_EBADPARAM;
      }

      if (NULL != module_ptr->input_port_list_ptr)
      {
         gu_input_port_t *inp_port_ptr = module_ptr->input_port_list_ptr->ip_port_ptr;
         if (NULL != inp_port_ptr->ext_in_port_ptr)
         {
            *ext_inp_port_pptr = inp_port_ptr->ext_in_port_ptr;
            break;
         }
         module_ptr = inp_port_ptr->conn_out_port_ptr->cmn.module_ptr;
      }
      else
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "input port pointer is NULL");
         return AR_EFAILED;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Returns the external buffer corresponding to this output port. If the output port
 * is not external, returns with *found_external_op_ptr as FALSE since there is no external buffer.
 */
ar_result_t spl_topo_get_ext_op_buf(spl_topo_t *            topo_ptr,
                                    spl_topo_output_port_t *out_port_ptr,
                                    spl_topo_ext_buf_t **   ext_out_buf_pptr,
                                    bool_t *                found_external_op_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   VERIFY(result,
          ext_out_buf_pptr && found_external_op_ptr &&
             topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf);

   // Null for internal ports. Search for external port ptr, if not found then assumed to be internal.
   *ext_out_buf_pptr      = NULL;
   *found_external_op_ptr = FALSE;
   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      *ext_out_buf_pptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
         out_port_ptr->t_base.gu.ext_out_port_ptr);
      *found_external_op_ptr = TRUE;
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Check for unconsumed data on this input port. This would be either in the input port or connected
 * output port's metadata list.
 */
bool_t spl_topo_ip_contains_metadata(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   bool_t has_md = FALSE;

   if (in_port_ptr->t_base.common.sdata.metadata_list_ptr || in_port_ptr->t_base.common.sdata.flags.marker_eos ||
       spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr))
   {
      return TRUE;
   }

   spl_topo_output_port_t *conn_out_port_ptr = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;
   if (conn_out_port_ptr)
   {
      has_md = ((NULL != conn_out_port_ptr->md_list_ptr) || (conn_out_port_ptr->t_base.common.sdata.flags.marker_eos));
   }

   return has_md;
}

/**
 * Check for unconsumed data on this input port. This is true if the input port's actual data len is nonzero or
 * The module has pending zeros to flush.
 */
bool_t spl_topo_ip_port_contains_unconsumed_data(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   spl_topo_module_t *module_ptr   = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   return (0 != spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr)) ||
          (0 != module_ptr->t_base.pending_zeros_at_eos);
}

//Returns how much data in bytes is present at this input port (bytes across channels).
uint32_t spl_topo_get_in_port_actual_data_len(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   uint32_t in_data_len = 0;

   if (in_port_ptr->t_base.gu.ext_in_port_ptr)
   {
      uint32_t num_channels = in_port_ptr->t_base.common.media_fmt_ptr->pcm.num_channels;

      in_data_len =
         topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_in_port_get_buf_len(in_port_ptr->t_base.gu.ext_in_port_ptr,
                                                                           FALSE);
      if (in_port_ptr->ext_in_buf_ptr)
      {
         in_data_len -= (in_port_ptr->ext_in_buf_ptr->bytes_consumed_per_ch * num_channels);
      }
   }
   else
   {
      spl_topo_output_port_t *conn_out_port_ptr = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;
      if (conn_out_port_ptr)
      {
         in_data_len = spl_topo_int_out_port_actual_data_len(topo_ptr, conn_out_port_ptr);
      }
   }

   return in_data_len;
}

/*
 * Returns the capacity of input port buffer (bytes across channels).
 */
uint32_t spl_topo_get_in_port_max_data_len(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   uint32_t in_data_len = 0;

   if (in_port_ptr->t_base.gu.ext_in_port_ptr)
   {
      in_data_len =
         topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_in_port_get_buf_len(in_port_ptr->t_base.gu.ext_in_port_ptr,
                                                                           TRUE);
   }
   else
   {
      spl_topo_output_port_t *conn_out_port_ptr = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;
      if (conn_out_port_ptr)
      {
         // Check each output port for a non-NULL buf_ptr (from topo_buf_manager).
         in_data_len = spl_topo_get_max_buf_len(topo_ptr, &conn_out_port_ptr->t_base.common);
      }
   }

   return in_data_len;
}

/**
 * Check for unconsumed data on this output port. This is true if the output port's actual data len is nonzero.
 */
bool_t spl_topo_op_port_contains_unconsumed_data(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   return (0 != spl_topo_get_out_port_actual_data_len(topo_ptr, out_port_ptr));
}

/*
 * Returns how much data in bytes is present at this output port (bytes across channels).
 */
uint32_t spl_topo_get_out_port_actual_data_len(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   uint32_t            out_data_len = 0;
   spl_topo_ext_buf_t *ext_buf_ptr  = NULL;

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      ext_buf_ptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
         out_port_ptr->t_base.gu.ext_out_port_ptr);

      // External port case. Return 0 for actual or max cases if the buffer isn't created.
      if (ext_buf_ptr && ext_buf_ptr->buf_ptr)
      {
         out_data_len = (ext_buf_ptr->buf_ptr[0].actual_data_len * ext_buf_ptr->num_bufs);
      }
   }
   else
   {
      // Internal port case.
      out_data_len = spl_topo_int_out_port_actual_data_len(topo_ptr, out_port_ptr);
   }

   return out_data_len;
}

/*
 * Returns how much data in bytes is present at this output port (bytes across channels).
 */
uint32_t spl_topo_get_out_port_data_len(void *topo_ctx_ptr, void *out_port_ctx_ptr, bool_t is_max)
{
   spl_topo_t *            topo_ptr     = (spl_topo_t *)topo_ctx_ptr;
   spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_ctx_ptr;
   uint32_t                out_data_len = 0;

   if (is_max)
   {
      out_data_len = spl_topo_get_max_buf_len(topo_ptr, &out_port_ptr->t_base.common);
   }
   else
   {
      out_data_len = spl_topo_get_out_port_actual_data_len(topo_ptr, out_port_ptr);
   }

   return out_data_len;
}

/**
* Checks how much empty space is available at this input port.
* returns bytes per channel.
*/
uint32_t spl_topo_get_in_port_required_data(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   spl_topo_module_t *module_ptr              = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   uint32_t           in_port_actual_data_len = 0;
   uint32_t           required_bytes          = 0;

   if (spl_topo_is_module_disabled(module_ptr) && (TOPO_MODULE_TYPE_SINK == spl_topo_get_module_port_type(module_ptr)))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo module miid 0x%lx input port id = 0x%x; "
               "data not needed as this is a sink module and disabled.",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.gu.cmn.id);
#endif
      return 0;
   }

   if (spl_topo_input_port_has_pending_eof(topo_ptr, in_port_ptr))
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "spl_topo module miid 0x%lx input port id = 0x%x; "
               "data not needed as dfg or flushing eos is present.",
               module_ptr->t_base.gu.module_instance_id,
               in_port_ptr->t_base.gu.cmn.id);
#endif
      return 0;
   }

   in_port_actual_data_len = topo_bytes_to_bytes_per_ch(spl_topo_get_in_port_actual_data_len(topo_ptr, in_port_ptr),
                                                        in_port_ptr->t_base.common.media_fmt_ptr);

   if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (GEN_TOPO_DM_FIXED_OUTPUT_MODE == module_ptr->t_base.flags.dm_mode))
   {
      required_bytes = topo_samples_to_bytes_per_ch(in_port_ptr->req_samples_info.samples_in,
                                                    in_port_ptr->t_base.common.media_fmt_ptr);
   }
   else if (module_ptr->threshold_data.is_threshold_module)
   {
      required_bytes = spl_topo_get_module_threshold_bytes_per_channel(&in_port_ptr->t_base.common, module_ptr);

      //to avoid multiple topo loop.
      required_bytes *= module_ptr->t_base.num_proc_loops;
   }
   else
   {
      uint32_t req_samples = spl_topo_get_scaled_samples(topo_ptr,
                                                         topo_ptr->cntr_frame_len.frame_len_samples,
                                                         topo_ptr->cntr_frame_len.sample_rate,
                                                         in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

      required_bytes = topo_samples_to_bytes_per_ch(req_samples, in_port_ptr->t_base.common.media_fmt_ptr);
   }

   if (required_bytes > in_port_actual_data_len)
   {
      required_bytes = required_bytes - in_port_actual_data_len;
   }
   else
   {
      required_bytes = 0;
   }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "spl_topo module miid 0x%lx input port id = 0x%x, "
            "already has %ld bytes per ch, required more %ld bytes per ch",
            module_ptr->t_base.gu.module_instance_id,
            in_port_ptr->t_base.gu.cmn.id,
            in_port_actual_data_len,
			required_bytes);
#endif

   return required_bytes;
}

/**
 * Checks how much empty space is available at this output port.
 * returns bytes (for all channels)
 */
uint32_t spl_topo_get_out_port_empty_space(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   uint32_t out_port_data_max_len    = spl_topo_get_max_buf_len(topo_ptr, &out_port_ptr->t_base.common);
   uint32_t out_port_data_actual_len = spl_topo_get_out_port_actual_data_len(topo_ptr, out_port_ptr);
   return out_port_data_max_len - out_port_data_actual_len;
}

/**
 * Sets number of zeros to flush in bytes per channel at eos for this module. sets zero for modules which
 * handle flushing eos themselves. sets zero for bypassed modules.
 */
void spl_topo_set_eos_zeros_to_flush_bytes(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   uint32_t zeros_bytes = 0;

   if ((!spl_topo_is_module_disabled(module_ptr)) && spl_topo_fwk_handles_flushing_eos(topo_ptr, module_ptr))
   {
      // Can assume single input port since fwk handles flushing eos.
      spl_topo_input_port_t *in_port_ptr =
         (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
      zeros_bytes = topo_us_to_bytes_per_ch(module_ptr->t_base.algo_delay, in_port_ptr->t_base.common.media_fmt_ptr);
   }

   module_ptr->t_base.pending_zeros_at_eos = zeros_bytes;
}


// Checks if there is a pending eof on the output port. Checks both internal and external port structure.
bool_t spl_topo_out_port_has_pending_eof(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   bool_t              eof         = FALSE;
   spl_topo_ext_buf_t *ext_buf_ptr = NULL;

   eof = out_port_ptr->t_base.common.sdata.flags.end_of_frame;

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      ext_buf_ptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
         out_port_ptr->t_base.gu.ext_out_port_ptr);

      eof |= ext_buf_ptr->end_of_frame;
   }

   return eof;
}

/**
 * Transfers data after the timestamp discontinuity from the external output port buffer into the
 * "timestamp discontinuity temporary buffer". This allows us to deliver data before the timestamp
 * discontinuity in one buffer and data after the timestamp discontinuity in another buffer.
 * The ts_temp_buf is stored in the actual output port's internal buffer and is querried from the
 * buffer manager rather than malloced.
 */
ar_result_t spl_topo_transfer_data_to_ts_temp_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t         result            = AR_EOK;
   topo_buf_t *        write_buf_ptr     = NULL;
   spl_topo_ext_buf_t *ext_buf_ptr       = NULL;

   uint32_t read_offset_per_ch = 0;
   uint32_t read_amt_per_ch    = 0;
   uint32_t max_len_per_ch     = 0;

   // Nothing to do if there is no ts_disc.
   if (!out_port_ptr->flags.ts_disc)
   {
      return result;
   }

   VERIFY(result, out_port_ptr->t_base.gu.ext_out_port_ptr);

   ext_buf_ptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
      out_port_ptr->t_base.gu.ext_out_port_ptr);

   // At this point we should never be holding an internal output buffer since this is an external buffer.
   VERIFY(result, (out_port_ptr->t_base.gu.ext_out_port_ptr) && (!(out_port_ptr->t_base.common.bufs_ptr[0].data_ptr)));

   spl_topo_get_output_port_buffer(topo_ptr, out_port_ptr);

   write_buf_ptr      = out_port_ptr->t_base.common.bufs_ptr;
   read_offset_per_ch = out_port_ptr->ts_disc_pos_bytes / ext_buf_ptr->num_bufs;
   read_amt_per_ch    = ext_buf_ptr->buf_ptr[0].actual_data_len - read_offset_per_ch;
   max_len_per_ch     = out_port_ptr->t_base.common.bufs_ptr[0].max_data_len;

   for (uint32_t ch_idx = 0; ch_idx < ext_buf_ptr->num_bufs; ch_idx++)
   {
      uint32_t copy_size;
      TOPO_MEMSCPY(copy_size,
                   write_buf_ptr[ch_idx].data_ptr,
                   max_len_per_ch,
                   ext_buf_ptr->buf_ptr[ch_idx].data_ptr + read_offset_per_ch,
                   read_amt_per_ch,
                   topo_ptr->t_base.gu.log_id,
                   "TS: (0x%lX, 0x%lX)",
                   out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                   out_port_ptr->t_base.gu.cmn.id);

      VERIFY(result, read_amt_per_ch == copy_size);
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      write_buf_ptr[i].actual_data_len += read_amt_per_ch;
      ext_buf_ptr->buf_ptr[i].actual_data_len -= read_amt_per_ch;
   }
#else
   }
   // optimization: update only first buf outside the for loop
   write_buf_ptr[0].actual_data_len += read_amt_per_ch;
   ext_buf_ptr->buf_ptr[0].actual_data_len -= read_amt_per_ch;
#endif

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Transfers data from the ts_temp_buf into the external output port buffer. This is called
 * when popping the next output buffer after delivering the data before the timestamp discontinuity.
 * After this data is transferred, it is expected to be delivered immediately. Thus there is no longer
 * a timestamp discontinuity so we clear the discontinuity from the external port structure.
 */
ar_result_t spl_topo_transfer_data_from_ts_temp_buf(spl_topo_t *            topo_ptr,
                                                    spl_topo_output_port_t *out_port_ptr,
                                                    spl_topo_ext_buf_t *    ext_buf_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result       = AR_EOK;
   topo_buf_t *read_buf_ptr = NULL;

   uint32_t read_amt_per_ch     = 0;

   // Nothing to do if there is no ts_disc.
   if (!ext_buf_ptr->timestamp_discontinuity)
   {
      return result;
   }

   VERIFY(result, 0 != ext_buf_ptr->ts_disc_pos_bytes);

   // We should be writing into an empty buffer.
   VERIFY(result, 0 == ext_buf_ptr->buf_ptr[0].actual_data_len);

   read_buf_ptr    = out_port_ptr->t_base.common.bufs_ptr;
   read_amt_per_ch = read_buf_ptr->actual_data_len;

   uint32_t max_data_len_per_ch = ext_buf_ptr->buf_ptr[0].max_data_len;
   for (uint32_t ch_idx = 0; ch_idx < ext_buf_ptr->num_bufs; ch_idx++)
   {
      uint32_t copy_size;
      TOPO_MEMSCPY(copy_size,
                   ext_buf_ptr->buf_ptr[ch_idx].data_ptr,
                   max_data_len_per_ch,
                   read_buf_ptr[ch_idx].data_ptr,
                   read_amt_per_ch,
                   topo_ptr->t_base.gu.log_id,
                   "TS: (0x%lX, 0x%lX)",
                   out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                   out_port_ptr->t_base.gu.cmn.id);

      VERIFY(result, read_amt_per_ch == copy_size);

// Reduce size of read buffer, increase size of write buffer.
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      read_buf_ptr[ch_idx].actual_data_len -= read_amt_per_ch;
      ext_buf_ptr->buf_ptr[ch_idx].actual_data_len += read_amt_per_ch;
   }
#else
   }
   // optimization: update only first buf outside the for loop
   read_buf_ptr[0].actual_data_len -= read_amt_per_ch;
   ext_buf_ptr->buf_ptr[0].actual_data_len += read_amt_per_ch;
#endif

   // We should have emptied the ts temp buf.
   VERIFY(result, 0 == read_buf_ptr->actual_data_len);

   // Return the ts_temp buf.
   spl_topo_return_output_buf(topo_ptr, out_port_ptr);

   // Assign the ts_disc timestamp to the external output buffer.
   ext_buf_ptr->timestamp          = ext_buf_ptr->disc_timestamp;
   ext_buf_ptr->timestamp_is_valid = TRUE;

   // At this point there is no more special state due to ts disc handling, so clear the ts disc
   // from the port.
   ext_buf_ptr->disc_timestamp          = 0;
   ext_buf_ptr->timestamp_discontinuity = FALSE;
   ext_buf_ptr->ts_disc_pos_bytes       = 0;

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * For modules that don't support metadata, checks whether eof should be propagated to the output or kept at the input.
 *
 * Propagate EOF if all input was consumed. For fwk-propagating modules in the SPL_CNTR, this is equivalent to checking
 * that
 * all output data was squeezed out, and lets us propagate EOF on the same call the last data goes out.
 *
 * EOF+EOS cases: EOF should go along with EOS. So only propagate once EOS leaves the input port.
 * EOF+DFG cases: EOF should go along with the DFG. This should happen naturally since DFG propagate at the same time
 *                that input is consumed.
 */
ar_result_t spl_topo_ip_check_prop_eof(spl_topo_t *           topo_ptr,
                                       spl_topo_input_port_t *in_port_ptr,
                                       uint32_t               consumed_data_per_ch,
                                       uint32_t               prev_in_len_per_ch,
                                       bool_t                 has_int_ts_disc,
                                       uint32_t               ts_disc_pos_bytes)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result          = AR_EOK;
   spl_topo_module_t *module_ptr      = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   bool_t             prop_eof        = FALSE;
   bool_t             prev_eos_marker = FALSE;

   // Modules that support metadata will handle eof themselves. If the module is disabled we have to handle eof.
   if (module_ptr->t_base.flags.supports_metadata && (!spl_topo_is_module_disabled(module_ptr)))
   {
      return result;
   }

   // At this point we can assume the module is SISO, source, or sink.
   VERIFY(result, (1 >= module_ptr->t_base.gu.num_output_ports) && (1 >= module_ptr->t_base.gu.num_input_ports));

   // Nothing needed if EOF wasn't set.
   if (!in_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      return result;
   }

   prev_eos_marker =
      topo_ptr->t_base.proc_context.in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].flags.prev_marker_eos;
   if (prev_eos_marker)
   {
      // For eos cases, propagate eof only if eos got propagated (got cleared from input)
      prop_eof = !(in_port_ptr->t_base.common.sdata.flags.marker_eos);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "EOF+EOS case, prop_eof = %ld", prop_eof);
#endif
   }
   else
   {
      // For non-eos cases - if full input was consumed, propagate eof.
      prop_eof = (consumed_data_per_ch == prev_in_len_per_ch);

      // If there's an internal timestamp discontinuity, the eof should correspond to the offset of the discontinuity,
      // not the end of the input buffer. So only check up to that point.
      if (has_int_ts_disc)
      {
         uint32_t consumed_data_all_ch = consumed_data_per_ch * in_port_ptr->t_base.common.media_fmt_ptr->pcm.num_channels;
         prop_eof |= (consumed_data_all_ch == ts_disc_pos_bytes);
      }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "EOF normal case for in port idx %ld miid 0x%lx, prop_eof = %ld, consumed_data_per_ch = %ld, "
               "prev_in_len_per_ch = %ld, has_int_ts_disc = %ld, ts_disc_pos_bytes = %ld",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               prop_eof,
               consumed_data_per_ch,
               prev_in_len_per_ch,
               has_int_ts_disc,
               ts_disc_pos_bytes);
#endif
   }

   if (prop_eof)
   {
      // Send eof through by clearing eof from input and leaving eof on output.
      in_port_ptr->t_base.common.sdata.flags.end_of_frame = FALSE;
      // We know the module has at most one output port. This case is only needed for non-sink modules.
      if (1 == module_ptr->t_base.gu.num_output_ports)
      {
         spl_topo_output_port_t *first_out_port_ptr =
            (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;
         first_out_port_ptr->t_base.common.sdata.flags.end_of_frame = TRUE;
      }
   }
   else
   {
      // If not, keep eof on input by clearing eof from output and leaving eof on input.
      // We know the module has at most one output port. This case is only needed for non-sink modules.
      if (1 == module_ptr->t_base.gu.num_output_ports)
      {
         spl_topo_output_port_t *first_out_port_ptr =
            (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;
         first_out_port_ptr->t_base.common.sdata.flags.end_of_frame = FALSE;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Helper function to call either check_create_bypass_module or check_destroy_bypass_module based on passed-in disable
 * state.
 */
ar_result_t spl_topo_check_update_bypass_module(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_disabled)
{
   ar_result_t result = AR_EOK;

   if (is_disabled)
   {
      result = gen_topo_check_create_bypass_module(&(topo_ptr->t_base), &(module_ptr->t_base));
   }
   else
   {
      result = gen_topo_check_destroy_bypass_module(&(topo_ptr->t_base), &(module_ptr->t_base), FALSE);
   }

   if (1 == module_ptr->t_base.gu.num_output_ports)
   {
      spl_topo_output_port_t *output_port_ptr =
         (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

      spl_topo_update_check_valid_mf_event_flag(topo_ptr,
                                                 &output_port_ptr->t_base.gu.cmn,
                                                 output_port_ptr->t_base.common.flags.is_mf_valid);
   }

   // update max buf len in case if output medai format is updated due to bypass enable/disable.
   spl_topo_update_max_buf_len_for_single_module(topo_ptr, module_ptr);

   spl_topo_update_module_info(&module_ptr->t_base.gu);

   return result;
}

// updates module connections for simplified topo processing in steady state (skips bypass modules)
// also update preliminary flags to enter simplified topo processing.
ar_result_t spl_topo_update_simp_module_connections(spl_topo_t *topo_ptr)
{
   ar_result_t result = AR_EOK;

   // if async open or close is going on then disable the simplified topo and return early.
   if (topo_ptr->t_base.gu.async_gu_ptr)
   {
      topo_ptr->simpt1_flags.any_modules_skip_process = TRUE;
      return AR_EOK;
   }

   // can't update connection if sorting didn't happen yet.
   if (!topo_ptr->t_base.gu.sorted_module_list_ptr)
   {
      return result;
   }

   // flags will be updated based on the modules
   topo_ptr->flags.is_bypass_container = (topo_ptr->t_base.gu.num_parallel_paths == 1)? TRUE: FALSE;

   topo_ptr->simpt1_flags.any_modules_skip_process = FALSE;
   topo_ptr->simpt1_flags.any_state_not_started    = FALSE;
   topo_ptr->simpt1_flags.any_source_module        = FALSE;

   // delete the simplified topo list
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->simpt_sorted_module_list_ptr, TRUE);

   // reset the simplified topo connections
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->t_base.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->flags.is_skip_process)
         {
            topo_ptr->simpt1_flags.any_modules_skip_process = TRUE;
         }

         if (0 == module_ptr->t_base.gu.max_input_ports)
         {
            topo_ptr->simpt1_flags.any_source_module = TRUE;
         }

         // reset the simplified topo ports connection to the gu connection.
         for (gu_input_port_list_t *in_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; (NULL != in_list_ptr);
              LIST_ADVANCE(in_list_ptr))
         {
            spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_list_ptr->ip_port_ptr;
            in_port_ptr->simp_conn_out_port_t  = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;

            if (!in_port_ptr->flags.port_inactive && (TOPO_PORT_STATE_STARTED != in_port_ptr->t_base.common.state))
            {
               topo_ptr->simpt1_flags.any_state_not_started = TRUE;
            }
         }

         for (gu_output_port_list_t *out_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; (NULL != out_list_ptr);
              LIST_ADVANCE(out_list_ptr))
         {
            spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_list_ptr->op_port_ptr;
            out_port_ptr->simp_conn_in_port_t    = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;

            spf_list_delete_list((spf_list_node_t **)&out_port_ptr->simp_attached_module_list_ptr, TRUE);

            //ignore the inactive port, also ignore their attached modules.
            if (out_port_ptr->flags.port_inactive)
            {
              continue;
            }

            if (TOPO_PORT_STATE_STARTED != out_port_ptr->t_base.common.state)
            {
               topo_ptr->simpt1_flags.any_state_not_started = TRUE;
            }

            // if attached module is enabled then add it to the simplified topo port's attached module list.
            if (out_port_ptr->t_base.gu.attached_module_ptr &&
                (!((gen_topo_module_t *)out_port_ptr->t_base.gu.attached_module_ptr)->flags.disabled))
            {
               spf_list_insert_tail((spf_list_node_t **)&out_port_ptr->simp_attached_module_list_ptr,
                                    out_port_ptr->t_base.gu.attached_module_ptr,
                                    topo_ptr->t_base.heap_id,
                                    TRUE);

               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "attaching module 0x%x.",
                        out_port_ptr->t_base.gu.attached_module_ptr->module_instance_id);
            }
         }
      }
   }

   // update simplified topo connection and container bypass flag.
   // container can be bypassed only if all the enabled modules are elementary.
   for (gu_module_list_t *module_list_ptr = topo_ptr->t_base.gu.sorted_module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

      // if a module is blocking data then don't remove.
      if (module_ptr->flags.is_skip_process)
      {
         // if module is blocking data then container can't be bypassed.
         // actually in this case simplified topo can't be used.
         topo_ptr->flags.is_bypass_container = FALSE;

         spf_list_insert_tail((spf_list_node_t **)&topo_ptr->simpt_sorted_module_list_ptr,
                              module_ptr,
                              topo_ptr->t_base.heap_id,
                              TRUE);

         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "adding skipped module 0x%x.",
                  module_ptr->t_base.gu.module_instance_id);
         continue;
      }

      // if module is elementary (which is not attached by gu) then try to attach it to previous output port.
      if (module_ptr->t_base.gu.flags.is_elementary)
      {
         spl_topo_input_port_t *in_port_ptr =
            (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;

         //elementary module can be sink also.
         spl_topo_output_port_t *out_port_ptr =
            (module_ptr->t_base.gu.output_port_list_ptr)
               ? (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr
               : NULL;

         spl_topo_output_port_t *prev_out_port_ptr = in_port_ptr->simp_conn_out_port_t;
         spl_topo_input_port_t * next_in_port_ptr  = (out_port_ptr) ? out_port_ptr->simp_conn_in_port_t : NULL;

         // a module can not be removed if it is connected at external port.
         if (prev_out_port_ptr && next_in_port_ptr)
         {
            // remove the current module from the simplified topo and attach to the previous output port.
            prev_out_port_ptr->simp_conn_in_port_t = next_in_port_ptr;
            next_in_port_ptr->simp_conn_out_port_t = prev_out_port_ptr;

            if (!module_ptr->t_base.flags.disabled)
            {
               spf_list_insert_tail((spf_list_node_t **)&prev_out_port_ptr->simp_attached_module_list_ptr,
                                    module_ptr,
                                    topo_ptr->t_base.heap_id,
                                    TRUE);

               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "attaching module 0x%x to the module 0x%x at port id 0x%x.",
                        module_ptr->t_base.gu.module_instance_id,
                        prev_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                        prev_out_port_ptr->t_base.gu.cmn.id);
            }
            else
            {
               TOPO_MSG(topo_ptr->t_base.gu.log_id,
                        DBG_HIGH_PRIO,
                        "Removing module 0x%x from the simplified topo sorted module list.",
                        module_ptr->t_base.gu.module_instance_id);
            }

            // modules which are already attached at this output port should also be moved to the previous output port.
            spf_list_merge_lists((spf_list_node_t **)&prev_out_port_ptr->simp_attached_module_list_ptr,
                                 (spf_list_node_t **)&out_port_ptr->simp_attached_module_list_ptr);

            continue;
         }
      }

      spl_topo_input_port_t *in_port_ptr =
         (module_ptr->t_base.gu.input_port_list_ptr)
            ? (spl_topo_input_port_t *)module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr
            : NULL;
      spl_topo_output_port_t *out_port_ptr =
         (module_ptr->t_base.gu.output_port_list_ptr)
            ? (spl_topo_output_port_t *)module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr
            : NULL;

      // if a module is enabled then don't remove.
      if (!(module_ptr->t_base.bypass_ptr ||
            spl_topo_intf_extn_mimo_module_process_state_is_module_bypassable(topo_ptr,
                                                                              module_ptr,
                                                                              &in_port_ptr,
                                                                              &out_port_ptr)))
      {
         // if module is not elementary then container can't be bypassed.
         topo_ptr->flags.is_bypass_container =
            (!module_ptr->t_base.gu.flags.is_elementary) ? FALSE : topo_ptr->flags.is_bypass_container;

         spf_list_insert_tail((spf_list_node_t **)&topo_ptr->simpt_sorted_module_list_ptr,
                              module_ptr,
                              topo_ptr->t_base.heap_id,
                              TRUE);

         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "adding enabled module 0x%x.",
                  module_ptr->t_base.gu.module_instance_id);
         continue;
      }

      spl_topo_output_port_t *prev_out_port_ptr = in_port_ptr  ? in_port_ptr->simp_conn_out_port_t : NULL ;
      spl_topo_input_port_t * next_in_port_ptr  = out_port_ptr ? out_port_ptr->simp_conn_in_port_t : NULL ;

      // a module can not be removed if it is connected at external port.
      if (!prev_out_port_ptr || !next_in_port_ptr)
      {
         spf_list_insert_tail((spf_list_node_t **)&topo_ptr->simpt_sorted_module_list_ptr,
                              module_ptr,
                              topo_ptr->t_base.heap_id,
                              TRUE);

         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "adding boundary module 0x%x.",
                  module_ptr->t_base.gu.module_instance_id);
         continue;
      }

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Removing module 0x%x from the simplified topo sorted module list.",
               module_ptr->t_base.gu.module_instance_id);

      // remove the current module from the simplified topo.
      prev_out_port_ptr->simp_conn_in_port_t = next_in_port_ptr;
      next_in_port_ptr->simp_conn_out_port_t = prev_out_port_ptr;

      spf_list_merge_lists((spf_list_node_t **)&prev_out_port_ptr->simp_attached_module_list_ptr,
                           (spf_list_node_t **)&out_port_ptr->simp_attached_module_list_ptr);
   }

   //sanity check for container bypass
   if (topo_ptr->flags.is_bypass_container)
   {
      if (1 != spf_list_count_elements((spf_list_node_t *)topo_ptr->t_base.gu.ext_in_port_list_ptr))
      {
         topo_ptr->flags.is_bypass_container = FALSE;
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "can't bypass container, non-unity external input ports present.");
      }

      if (1 != spf_list_count_elements((spf_list_node_t *)topo_ptr->t_base.gu.ext_out_port_list_ptr))
      {
         topo_ptr->flags.is_bypass_container = FALSE;
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "can't bypass container, non-unity external input ports present.");
      }

      if (1 != topo_ptr->t_base.gu.num_parallel_paths)
      {
         topo_ptr->flags.is_bypass_container = FALSE;
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "can't bypass container, more than one path in the topo.");
      }
   }

   // if container is bypassed then get the enabled elementary module list and attach it to the first module's output port
   if (topo_ptr->flags.is_bypass_container)
   {
      // get the output port of the module connected on the external output.
      // all the elementary modules will be hosted by this output port.
      spl_topo_output_port_t *ext_out_module_ptr_out_ptr =
         (spl_topo_output_port_t *)topo_ptr->t_base.gu.ext_out_port_list_ptr->ext_out_port_ptr->int_out_port_ptr;

      //list of all the enabled elementary modules
      gu_module_list_t *attached_module_list_ptr = NULL;

      for (gu_module_list_t *module_list_ptr = topo_ptr->simpt_sorted_module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->t_base.gu.flags.is_elementary && !module_ptr->t_base.flags.disabled)
         {
            spf_list_insert_tail((spf_list_node_t **)&attached_module_list_ptr,
                                 module_ptr,
                                 topo_ptr->t_base.heap_id,
                                 TRUE);

            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_HIGH_PRIO,
                     "attaching enabled elementary module 0x%x.",
                     module_ptr->t_base.gu.module_instance_id);
         }

         for (gu_output_port_list_t *out_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; (NULL != out_list_ptr);
              LIST_ADVANCE(out_list_ptr))
         {
            spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_list_ptr->op_port_ptr;

            // if output port is active then merge the attached module list and start with next module.
            if (!out_port_ptr->flags.port_inactive)
            {
               spf_list_merge_lists((spf_list_node_t **)&attached_module_list_ptr,
                                    (spf_list_node_t **)&out_port_ptr->simp_attached_module_list_ptr);
               break;
            }
         }
      }

      // external output port will host all the elementary modules including self.
      ext_out_module_ptr_out_ptr->simp_attached_module_list_ptr = attached_module_list_ptr;

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Container bypass enabled with %d elementary modules",
               spf_list_count_elements((spf_list_node_t *)attached_module_list_ptr));
   }

   return result;
}
