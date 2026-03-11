/**
 * \file olc_eos_util.c
 * \brief
 *     This file contains olc utility functions for managing eos (end of stream).
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_i.h"
#include "other_metadata.h"

/**
 * We don't need to access ext_inp_ref because all we do is checking if EOS entered the container
 * through ext in port. If so, we simply decrement a global ref counter and remove votes.
 */
ar_result_t olc_clear_eos(gen_topo_t *         topo_ptr,
                               void *               ext_inp_ref,
                               uint32_t             ext_inp_id,
                               module_cmn_md_eos_t *eos_metadata_ptr)
{
   olc_t *               me_ptr       = (olc_t *)GET_BASE_PTR(olc_t, topo, topo_ptr);
   gen_topo_eos_cargo_t *cntr_ref_ptr = (gen_topo_eos_cargo_t *)eos_metadata_ptr->cntr_ref_ptr;
   if (!cntr_ref_ptr)
   {
      return AR_EOK;
   }

   /**
    * did_eos_come_from_ext_in is set only when flushing EOS comes in on the ext in port of container.
    * - If flushing EOS gets converted to non-flushing within the container, then clear_eos is
    *   still called and below code is executed.
    * - If non-flushing EOS enters the container, no container ref (cargo) is created.
    *
    */
   if (!cntr_ref_ptr->did_eos_come_from_ext_in)
   {
      return AR_EOK;
   }

   // Important note: by this time ext-in might be destroyed. so don't access it.

   TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: EoS 0x%p being cleared for ext input port ", eos_metadata_ptr);

   // in intra-container SG case or source module case, this may not be coming from ext-in.
   if (me_ptr->total_flush_eos_stuck > 0)
   {
      me_ptr->total_flush_eos_stuck--;

      TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: Flushing EOS left %lu ", me_ptr->total_flush_eos_stuck);

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
      *     modules.
      *  - ext in getting destroyed before EOS comes out.
      *  */

      if (0 == me_ptr->total_flush_eos_stuck)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: As no more flushing EOSes are left, trying to remove vote from power manager");

         CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, dfs_change);
         olc_handle_fwk_events(me_ptr);
      }
      else
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: total_flush_eos_left %lu, hence not trying to remove votes yet",
                  me_ptr->total_flush_eos_stuck);
      }
   }

   return AR_EOK;
}

ar_result_t olc_process_eos_md_from_peer_cntr(olc_t *                me_ptr,
                                              olc_ext_in_port_t *    ext_in_port_ptr,
                                              module_cmn_md_list_t **md_list_head_pptr)
{
   ar_result_t           result          = AR_EOK;
   bool_t                is_flushing     = FALSE;
   bool_t                is_internal_eos = FALSE;
   module_cmn_md_list_t *md_list_ptr     = *md_list_head_pptr;
   module_cmn_md_list_t *node_ptr        = md_list_ptr;
   module_cmn_md_list_t *next_ptr        = NULL;
   ext_in_port_ptr->input_has_md         = FALSE;

   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         module_cmn_md_eos_t *eos_metadata_ptr = 0;
         uint32_t             is_out_band      = md_ptr->metadata_flag.is_out_of_band;

         if (is_out_band)
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }
         is_flushing     = eos_metadata_ptr->flags.is_flushing_eos;
         is_internal_eos = eos_metadata_ptr->flags.is_internal_eos;

         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_MED_PRIO,
                 "MD_DBG: OLC received EOS cmd from Peer container at Module, "
                 "port index (0x%lX, %lu). is_flushing %u,is_internal %u"
                 " node_ptr 0x%p, offset %lu",
                 ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                 ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                 is_flushing,
                 is_internal_eos,
                 node_ptr,
                 md_ptr->offset);

         // gen_topo_eos_cargo_t *cargo_ptr = NULL;

         if (FALSE == is_internal_eos)
         {
            {
               // do not change the offset as upstream sends with correct offsets

               me_ptr->total_flush_eos_stuck++;

               // eos_metadata_ptr->cntr_ref_ptr = cargo_ptr;
               // We cannot set data flow state we move EOS out of this port.
               // Reason: data and EOS can exist together in the last buf. set
               // port state only after all above are successful. this is call for
               // peer container for which MD is already allocated by upstream
            }
         }

         if (AR_EOK == result)
         {
            ext_in_port_ptr->flags.flushing_eos = is_flushing;
            // do not change the offset as upstream sends with correct offsets
            // eos_metadata_ptr->cntr_ref_ptr = cargo_ptr; // OLC_CA

            ext_in_port_ptr->set_eos_to_sat_wr_ep_pending = TRUE;

            ext_in_port_ptr->sdm_wdp_input_data.eos_flags = eos_metadata_ptr->flags;

            ext_in_port_ptr->input_has_md = TRUE;
         }

         // even though EoS is also input_discontinuity, it's handled separately
         // process any partially processed data
      }
      else if ((MD_ID_TTR == md_ptr->metadata_id) && (NULL == me_ptr->cu.voice_info_ptr))
      {
         gen_topo_capi_metadata_destroy((void *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr,
                                        node_ptr,
                                        TRUE /*is_dropped*/,
                                        md_list_head_pptr,
                                        0,
                                        FALSE);
      }
      else
      {
         ext_in_port_ptr->input_has_md = TRUE;
      }
      node_ptr = next_ptr;
   }

   // this list will be merged later to the ext_in_port's mdlist.

   return result;
}

ar_result_t olc_create_send_eos_md(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t            result = AR_EOK;
   spf_msg_t              msg;
   spf_msg_header_t *     header_ptr  = NULL;
   spf_msg_data_buffer_t *out_buf_ptr = NULL;

   if (ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr)
   {
      uint32_t total_size = GET_SPF_INLINE_DATABUF_REQ_SIZE(0);
      if (AR_DID_FAIL(spf_msg_create_msg(&msg,
                                         &total_size,
                                         SPF_MSG_DATA_BUFFER,
                                         NULL,
                                         NULL,
                                         ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr,
                                         me_ptr->cu.heap_id)))
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "Failed to create a buffer for sending EOS metadata, Module 0x%lx, 0x%lx",
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
         return result;
      }

      header_ptr                     = (spf_msg_header_t *)(msg.payload_ptr);
      out_buf_ptr                    = (spf_msg_data_buffer_t *)&header_ptr->payload_start;
      out_buf_ptr->max_size          = 0;
      out_buf_ptr->actual_size       = 0;
      out_buf_ptr->metadata_list_ptr = ext_out_port_ptr->md_list_ptr;
      out_buf_ptr->timestamp         = 0;
      out_buf_ptr->flags             = 0;
      ext_out_port_ptr->md_list_ptr  = NULL;
      memset(&out_buf_ptr->data_buf[0], 0, out_buf_ptr->max_size);

      result = posal_queue_push_back(ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr,
                                     (posal_queue_element_t *)&msg);
      if (AR_DID_FAIL(result))
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "Failed to send buffer for sending EOS metadata, Module 0x%lx, 0x%lx",
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
         spf_msg_return_msg(&msg);
      }
      else
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_LOW_PRIO,
                 "Sent EOS metadata from Module 0x%lx, 0x%lx",
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                 ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
      }
   }

   return result;
}
