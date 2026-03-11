/**
 * \file spl_cntr_eos_util.c
 * \brief
 *     This file contains spl_cntr utility functions for managing eos (end of stream).
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"

/**
 * Called when eos is buffered into external port local buffer in order to handle arrival of eos on external input port.
 */
ar_result_t spl_cntr_ext_in_port_set_eos(spl_cntr_t *            me_ptr,
                                         spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                         module_cmn_md_list_t *  node_ptr,
                                         module_cmn_md_list_t ** list_head_pptr,
                                         bool_t *                new_flushing_eos_arrived_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t           result           = AR_EOK;
   gen_topo_eos_cargo_t *cargo_ptr        = NULL;
   module_cmn_md_eos_t * eos_metadata_ptr = NULL;
   bool_t                is_out_band      = 0;
   bool_t                is_flushing      = FALSE;
   bool_t                is_internal      = FALSE;
   module_cmn_md_t *     md_ptr           = node_ptr->obj_ptr;

   VERIFY(result, (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id) && (NULL != new_flushing_eos_arrived_ptr));

   *new_flushing_eos_arrived_ptr = FALSE;
   is_out_band                   = md_ptr->metadata_flag.is_out_of_band;

   // If media format is not yet received on this port, then eos cannot be propagated. drop right away.
   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "No media format received on external input port idx %d miid 0x%x. Dropping EOS",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
      THROW(result, AR_EFAILED); // force error to drop eos
   }

   if (is_out_band)
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
   }
   else
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
   }

   is_flushing = eos_metadata_ptr->flags.is_flushing_eos;
   is_internal = eos_metadata_ptr->flags.is_internal_eos;

   if ((TOPO_DATA_FLOW_STATE_AT_GAP == ext_in_port_ptr->topo_buf.data_flow_state ||
        ext_in_port_ptr->topo_buf.first_frame_after_gap) &&
       is_internal)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "MD_DBG: SPL_CNTR dropping internal EOS when received at external input, port index (0x%lX, %lu). "
                   "when "
                   "at_gap.",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index);
      bool_t IS_DROPPED_TRUE = TRUE;
      gen_topo_capi_metadata_destroy((void *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr,
                                     node_ptr,
                                     IS_DROPPED_TRUE,
                                     list_head_pptr,
                                     0,
                                     FALSE);
      return result;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "MD_DBG: SPL_CNTR received EOS from Peer container at Module, port index (0x%lX, %lu). "
                "is_flushing %u, node_ptr 0x%p, offset %lu",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                is_flushing,
                node_ptr,
                md_ptr->offset);

   result = gen_topo_create_eos_cntr_ref(&me_ptr->topo.t_base,
                                         me_ptr->topo.t_base.heap_id,
                                         (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr,
                                         ext_in_port_ptr->cu.id,
                                         &cargo_ptr);
   if (AR_EOK != result)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      gen_topo_capi_metadata_destroy((void *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr,
                                     node_ptr,
                                     IS_DROPPED_TRUE,
                                     list_head_pptr,
                                     0,
                                     FALSE);
   }
   else
   {
      bool_t IS_INPUT = TRUE;

      // Do not change the offset, this is handled as part of general md elsewhere.
      eos_metadata_ptr->cntr_ref_ptr = cargo_ptr;

      // If it is a flushing EOS, increment the number of flushing eos.
      if (is_flushing)
      {
         me_ptr->total_flush_eos_stuck++;
         *new_flushing_eos_arrived_ptr = TRUE;
      }

      // Assign is_flushing on the internal input port.
      spl_topo_assign_marker_eos_on_port(&(me_ptr->topo),
                                         (void *)ext_in_port_ptr->gu.int_in_port_ptr,
                                         IS_INPUT,
                                         is_flushing);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Callback from the topo that is used when a cargo ref count goes to zero, which means that
 * one eos has completely left the container. When this happens we should update the external input
 * port fields accordingly, and re-vote if all flushing eos left the container.
 */
ar_result_t spl_cntr_handle_clear_eos_topo_cb(gen_topo_t *         topo_ptr,
                                              void *               ext_inp_ref /*type: gen_topo_input_port_t*/,
                                              uint32_t             ext_inp_id,
                                              module_cmn_md_eos_t *eos_metadata_ptr)
{
   ar_result_t             result          = AR_EOK;
   spl_cntr_t *            me_ptr          = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)topo_ptr);
   spl_cntr_ext_in_port_t *ext_in_port_ptr = NULL;

   // Check if its valid contianer reference (pointers match and ids match)
   bool_t ext_in_ref_found = FALSE;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr;
        ext_in_port_list_ptr != NULL;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *temp_ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      if (((void *)temp_ext_in_port_ptr->gu.int_in_port_ptr == ext_inp_ref) &&
          (temp_ext_in_port_ptr->cu.id == ext_inp_id))
      {
         ext_in_ref_found = TRUE;
         /** For achieving simple topo for Sync->SAL combination in SC, the external input port is removed from module's port list.
          *  But not from the ext_in_port_list_ptr.
          *  The external port on internal port is set as NULL. Internal port on the external port is still valid.
          *  Therefore obtain the ref to ext-in-port from the external list.*/
         ext_in_port_ptr  = temp_ext_in_port_ptr;
         break;
      }
   }

   // Not found return
   if (FALSE == ext_in_ref_found)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "MD_DBG: ext_inp_ref=%p id %ld not found while clearing EOS",
                   ext_inp_ref,
                   ext_inp_id);
      return result;
   }

   // Check to decrement total flush eos. We need to count only EOS which was flushing when arriving at ext input.
   // 1. ext_ip_ref non null guarantees EOS came from external input
   // 2. spl_cntr_handle_clear_eos_topo_cb only gets called for EOS with cntr reference which was flushing when arriving
   // at ext ip.
   bool_t did_eos_come_from_ext_in = (NULL != ext_inp_ref);
   if (did_eos_come_from_ext_in)
   {
      if (me_ptr->total_flush_eos_stuck > 0)
      {
         me_ptr->total_flush_eos_stuck--;

         TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: Flushing EOS left %lu", me_ptr->total_flush_eos_stuck);

         /* When EOS leaves the container, if all inputs become data-flow-state=at_gap, then
         * we can release votes (for FTRT input and output only).
         * If any flush_eos_cnt exists, then it means that there's an EOS stuck in the middle & even though ext-in is at
         * gap, we cannot release votes .
         * Corner cases:
         *  - same input receiving multiple EOSes
         *  - multiple inputs receiving different EOSes
         *  - flushing EOS becoming non-flushing in the middle.
         *  - EOS followed by data and again EOS before first one goes out.
         *  - some modules are not in data-flow-gap, while ext input is: this is not possible unless there are source
         * modules. */

         if (0 == me_ptr->total_flush_eos_stuck)
         {
            // TOPO_MSG(topo_ptr->gu.log_id,
            //          DBG_HIGH_PRIO,
            //          "MD_DBG: As no more flushing EOSes are left, trying to remove vote from power manager");

            // bool_t is_at_least_one_sg_started = TRUE; // otherwise, we would not get buffer!
            // gen_cntr_vote_pm_conditionally(me_ptr, me_ptr->cu.cntr_frame_len.frame_len_us,
            // is_at_least_one_sg_started);
         }
         else
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "MD_DBG: total_flush_eos_left %lu, hence not trying to remove votes yet",
                     me_ptr->total_flush_eos_stuck);
         }
      }
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "MD_DBG: EoS cleared for miid 0x%lx, input port idx %lu",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index);

   // The internal port's marker_eos would have gotten cleared during topology invokation.
   return result;
}

/**
 * Part of topo_to_cntr callback vtable, informed when all dfg/eos are gone from the external input port. Call
 * spl_cntr_ext_in_port_handle_data_flow_end with create_eos_md FALSE since the md was the dfg/eos which left this port.
 */
ar_result_t spl_cntr_ext_in_port_dfg_eos_left_port(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   bool_t CREATE_EOS_FALSE = FALSE;
   bool_t IS_FLUSHING_TRUE = TRUE;

   spl_cntr_t *            me_ptr          = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)topo_ptr);
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   return spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, CREATE_EOS_FALSE, IS_FLUSHING_TRUE);
}

/**
 * Called when data flow ends. If create_eos_md is TRUE, an internal EOS is created and stored in the
 * external input port.
 */
ar_result_t spl_cntr_ext_in_port_handle_data_flow_end(spl_cntr_t *            me_ptr,
                                                      spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                      bool_t                  create_eos_md,
                                                      bool_t                  is_flushing)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   VERIFY(result, me_ptr && ext_in_port_ptr);

   // If media format is not yet received on this port, then DFG cannot be propagated. Destroy right away.
   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      return result;
   }

   if (TOPO_DATA_FLOW_STATE_AT_GAP == ext_in_port_ptr->topo_buf.data_flow_state)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Setting at-gap when SPL_CNTR is already at-gap - ignoring. Input port idx %lu, miid 0x%lx",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
      return result;
   }

   // Create dfg metadata at the external input port if create_md is TRUE.
   if (create_eos_md)
   {
      spl_topo_input_port_t *int_in_port_ptr = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      if (is_flushing)
      {
         bool_t                    IS_INPUT_TRUE       = TRUE;
         bool_t                    NEW_MARKER_EOS_TRUE = TRUE;
         bool_t                    IS_MAX_FALSE        = FALSE;
         module_cmn_md_eos_flags_t eos_md_flag = {.word = 0 };
         eos_md_flag.is_flushing_eos = TRUE;
         eos_md_flag.is_internal_eos = TRUE;

         result = gen_topo_create_eos_for_cntr(&me_ptr->topo.t_base,
                                               &(int_in_port_ptr->t_base),
                                               ext_in_port_ptr->cu.id,
                                               me_ptr->cu.heap_id,
                                               &(int_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                               NULL,         /* md_flag_ptr */
                                               NULL,         /*tracking_payload_ptr*/
                                               &eos_md_flag, /* eos_payload_flags */
                                               spl_cntr_ext_in_port_get_buf_len((gu_ext_in_port_t *)ext_in_port_ptr,
                                                                                IS_MAX_FALSE),
                                               int_in_port_ptr->t_base.common.media_fmt_ptr);

         // Assign marker eos.
         spl_topo_assign_marker_eos_on_port(&(me_ptr->topo),
                                            (void *)int_in_port_ptr,
                                            IS_INPUT_TRUE,
                                            NEW_MARKER_EOS_TRUE);

         // New flushing EOS arrived, we need to start pushing zeros on the external input port's module
         spl_topo_module_t *first_module_ptr = (spl_topo_module_t *)int_in_port_ptr->t_base.gu.cmn.module_ptr;
         spl_topo_set_eos_zeros_to_flush_bytes(&me_ptr->topo, first_module_ptr);
         TOPO_MSG(me_ptr->topo.t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "SPL_TOPO_EOS_DEBUG: Started flushing eos zeros on miid = 0x%lx. %ld "
                  "bytes per channel of zeros to flush, module algo delay %ld",
                  first_module_ptr->t_base.gu.module_instance_id,
                  first_module_ptr->t_base.pending_zeros_at_eos,
                  first_module_ptr->t_base.algo_delay);

         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Created internal EOS on external input port idx %lu miid 0x%lx",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
      }
      else
      {
         module_cmn_md_t *out_md_ptr   = NULL;
         bool_t           IS_MAX_FALSE = FALSE;
         uint32_t         buf_actual_data_len =
            spl_cntr_ext_in_port_get_buf_len((gu_ext_in_port_t *)ext_in_port_ptr, IS_MAX_FALSE);

         result = gen_topo_create_dfg_metadata(&me_ptr->topo.t_base,
                                               &(int_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                               me_ptr->cu.heap_id,
                                               &out_md_ptr,
                                               buf_actual_data_len,
                                               int_in_port_ptr->t_base.common.media_fmt_ptr);
         if (out_md_ptr)
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "MD_DBG: Inserted DFG at ext in port (0x%lX, 0x%lx) at offset %lu, result %ld",
                         int_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                         int_in_port_ptr->t_base.gu.cmn.id,
                         out_md_ptr->offset,
                         result);

            int_in_port_ptr->t_base.common.sdata.flags.end_of_frame = TRUE;
         }
      }
   }

   if (is_flushing)
   {
      // Timestamps after the data flow gap aren't expected to be continuous. So clear any buffered timestamps.
      TRY(result, spl_cntr_ext_in_port_clear_timestamp_discontinuity(&(me_ptr->topo.t_base), &(ext_in_port_ptr->gu)));
   }

   spl_topo_update_check_data_flow_event_flag(&me_ptr->topo,
                                              &(ext_in_port_ptr->gu.int_in_port_ptr->cmn),
					      TOPO_DATA_FLOW_STATE_AT_GAP);

   ext_in_port_ptr->topo_buf.data_flow_state = TOPO_DATA_FLOW_STATE_AT_GAP;
   spl_cntr_ext_in_port_update_gpd_bit(me_ptr, ext_in_port_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, (me_ptr ? me_ptr->cu.gu_ptr->log_id : 0))
   {
   }

   return result;
}
