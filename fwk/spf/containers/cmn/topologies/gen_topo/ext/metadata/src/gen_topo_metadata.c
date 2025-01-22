/**
 * \file gen_topo_metadata.c
 *
 * \brief
 *
 *     metadata related implementation.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "spf_ref_counter.h"
#include "thin_topo_inline.h"

static ar_result_t gen_topo_metadata_create_with_tracking(gen_topo_t *               topo_ptr,
                                                          module_cmn_md_list_t **   md_list_pptr,
                                                          uint32_t                  size,
                                                          capi_heap_id_t            heap_id,
                                                          uint32_t                  metadata_id,
                                                          module_cmn_md_flags_t     flags,
                                                          module_cmn_md_tracking_t *tracking_info_ptr,
                                                          module_cmn_md_t **        md_pptr);

/**
 * called for dsp client, and peer-SG stop/flush
 */
ar_result_t gen_topo_create_eos_for_cntr(gen_topo_t *               topo_ptr,
                                         gen_topo_input_port_t *    input_port_ptr,
                                         uint32_t                   input_port_id,
                                         POSAL_HEAP_ID              heap_id,
                                         module_cmn_md_list_t **    eos_md_list_pptr,
                                         module_cmn_md_flags_t *    in_md_flags,
                                         module_cmn_md_tracking_t * eos_tracking_ptr,
                                         module_cmn_md_eos_flags_t *eos_flag_ptr,
                                         uint32_t                   bytes_across_ch,
                                         topo_media_fmt_t *         media_fmt_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                              offset           = 0;
   gen_topo_eos_cargo_t *                cntr_ref_ptr     = NULL;
   module_cmn_md_t *                     new_md_ptr       = NULL;
   gen_topo_module_t *                   module_ptr       = NULL;
   module_cmn_md_eos_t *                 eos_metadata_ptr = NULL;
   intf_extn_param_id_metadata_handler_t handler;
   capi_heap_id_t                        heap_info;
   module_cmn_md_flags_t                 md_flags = { .word = 0 };

   if (input_port_ptr)
   {
      module_ptr = (gen_topo_module_t *)input_port_ptr->gu.cmn.module_ptr;
      gen_topo_populate_metadata_extn_vtable(module_ptr, &handler);
   }
   else
   {
      memset(&handler, 0, sizeof(intf_extn_param_id_metadata_handler_t));
   }

   heap_info.heap_id = heap_id;

   if (in_md_flags)
   {
      md_flags = *in_md_flags;
   }
   else
   {
      md_flags.word                   = 0;
      md_flags.is_out_of_band         = MODULE_CMN_MD_IN_BAND;
      md_flags.buf_sample_association = MODULE_CMN_MD_SAMPLE_ASSOCIATED;
      if (eos_tracking_ptr)
      {
         md_flags.tracking_mode   = MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME;
         md_flags.tracking_policy = MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH;
      }
   }

   /* EOS for data-flow-gap: For STM triggered containers, make flushing EOS as
    * not flushing Note that this is not always true. Imagine a multi-SG Tx HW-EP
    * port container If upstream SG containing STM is stopped, then DS needs to
    * use flushing EOS. However, if unstopped SG has STM, then it's ok not to use
    * flushing EOS. For now, ignore such cases. Firstly, if STM is stopped, whole
    * container doesn't trigger anyway.
    * Usually in Rx path, flushing can be made non-flushing.*/
   /*
    * since EOS causes data-flow-state to change to at-gap where we don't
   underrun if (topo_ptr->flags.is_signal_triggered &&
   eos_flag_ptr->is_flushing_eos)
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: treating EOS as
   nonflushing due to signal triggered mode"); eos_flag_ptr->is_flushing_eos =
   MODULE_CMN_MD_EOS_NON_FLUSHING;
   }
   */
   // only if input port is given, create cntr ref. for ext out port we don't
   // need contr ref.
   if (eos_flag_ptr->is_flushing_eos && input_port_ptr)
   {
      TRY(result, gen_topo_create_eos_cntr_ref(topo_ptr, heap_id, input_port_ptr, input_port_id, &cntr_ref_ptr));
   }

   TRY(result,
       gen_topo_metadata_create_with_tracking(topo_ptr,
                                              eos_md_list_pptr,
                                              sizeof(module_cmn_md_eos_t),
                                              heap_info,
                                              MODULE_CMN_MD_ID_EOS,
                                              md_flags,
                                              eos_tracking_ptr,
                                              &new_md_ptr));

   uint32_t is_out_band = new_md_ptr->metadata_flag.is_out_of_band;
   if (is_out_band)
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)new_md_ptr->metadata_ptr;
   }
   else
   {
      eos_metadata_ptr = (module_cmn_md_eos_t *)&(new_md_ptr->metadata_buf);
   }
   /** EoS metadata payload  */
   eos_metadata_ptr->cntr_ref_ptr = cntr_ref_ptr;
   eos_metadata_ptr->flags        = *eos_flag_ptr;

   // put the EOS at the offset = last byte
   if (media_fmt_ptr)
   {
      gen_topo_do_md_offset_math(topo_ptr->gu.log_id, &offset, bytes_across_ch, media_fmt_ptr, TRUE /* need_to_add */);
   }
   /** metadata payload  */
   new_md_ptr->offset = offset;

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MD_DBG: Created EoS with node_ptr 0x%p, cargo ptr 0x%p, "
            "tracking ptr 0x%p, offset = %lu, eos_flag 0x%lX",
            *eos_md_list_pptr,
            cntr_ref_ptr,
            new_md_ptr->tracking_ptr,
            offset,
            (*eos_flag_ptr).word);

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "MD_DBG: Failed create memory for EoS ");

      if ((NULL == cntr_ref_ptr) && (eos_tracking_ptr))
      {
         gen_topo_drop_md(topo_ptr->gu.log_id,
                          &eos_tracking_ptr->tracking_payload,
                          MODULE_CMN_MD_ID_EOS,
                          md_flags,
                          eos_tracking_ptr->tracking_payload.src_port,
                          FALSE,
                          NULL);
      }
      else
      {
         MFREE_NULLIFY(cntr_ref_ptr);
      }
   }

   return AR_EOK;
}

#define PRINT_MD_PROP_DBG(str1, str2, len_per_ch, str3, ...)                                                           \
   TOPO_MSG(topo_ptr->gu.log_id,                                                                                       \
            DBG_LOW_PRIO,                                                                                              \
            "MD_DBG: " str1 ". module 0x%lX, node_ptr 0x%p, md_id 0x%08lX, offset %lu, "                               \
            "offset_before %lu," str2 "_per_ch %lu, " str3,                                                            \
            module_ptr->gu.module_instance_id,                                                                         \
            node_ptr,                                                                                                  \
            md_ptr->metadata_id,                                                                                       \
            md_ptr->offset,                                                                                            \
            offset_before,                                                                                             \
            len_per_ch,                                                                                                \
            ##__VA_ARGS__)

/**
 * Container can interpret it to move ports to data flow stop.
 * DFG is created,
 *  1. By pause modules.
 *  2. By container if the upstream SG is suspended and self SG is started.
 */
ar_result_t gen_topo_create_dfg_metadata(gen_topo_t            *topo_ptr,
                                         module_cmn_md_list_t **metadata_list_pptr,
                                         POSAL_HEAP_ID          heap_id,
                                         module_cmn_md_t **     dfg_md_pptr,
                                         uint32_t               bytes_in_buf,
                                         topo_media_fmt_t *     media_format_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    log_id = topo_ptr->gu.log_id;

   if (dfg_md_pptr)
   {
      ar_result_t local_result =
         gen_topo_metadata_create(topo_ptr, metadata_list_pptr, 0, heap_id, FALSE /* is_out_band*/, dfg_md_pptr);

      if (AR_SUCCEEDED(local_result))
      {
         (*dfg_md_pptr)->metadata_id                          = MODULE_CMN_MD_ID_DFG;
         (*dfg_md_pptr)->metadata_flag.buf_sample_association = MODULE_CMN_MD_BUFFER_ASSOCIATED;

         if (SPF_IS_PACKETIZED_OR_PCM(media_format_ptr->data_format))
         {
            if (TU_IS_ANY_DEINTERLEAVED_UNPACKED(media_format_ptr->pcm.interleaving))
            {
               (*dfg_md_pptr)->offset += topo_bytes_to_samples_per_ch(bytes_in_buf, media_format_ptr);
            }
            else // interleaved packed and deinterleaved packed
            {
               (*dfg_md_pptr)->offset += topo_bytes_to_samples(bytes_in_buf, media_format_ptr);
            }
         }
         else // for RAW compressed
         {
            (*dfg_md_pptr)->offset += bytes_in_buf;
         }

#ifdef METADATA_DEBUGGING
         TOPO_MSG(log_id,
                  DBG_LOW_PRIO,
                  "Created DFG metadata, bytes_in_buf= %lu, offset= %lu ",
                  bytes_in_buf,
                  (*dfg_md_pptr)->offset);
#endif
      }
      else
      {
         TOPO_MSG(log_id, DBG_ERROR_PRIO, "DFG metadata create failed");
      }
      result |= local_result;
   }

   return result;
}

/**
 * Any buffer associated MD causes EOF. This is to ensure that threshold modules
 * don't hold DFG or such buffer associated MD
 */
bool_t gen_topo_md_list_has_buffer_associated_md(module_cmn_md_list_t *list_ptr)
{
   module_cmn_md_t *md_ptr                       = NULL;
   bool_t           is_buffer_associated_present = FALSE;

   while (list_ptr)
   {
      md_ptr = list_ptr->obj_ptr;

      if (MODULE_CMN_MD_BUFFER_ASSOCIATED == md_ptr->metadata_flag.buf_sample_association)
      {
         return TRUE;
      }
      list_ptr = list_ptr->next_ptr;
   }
   return is_buffer_associated_present;
}

void gen_topo_convert_client_md_flag_to_int_md_flags(uint32_t client_md_flags, module_cmn_md_flags_t *int_md_flags)
{
   int_md_flags->word           = 0;
   int_md_flags->version        = MODULE_CMN_MD_VERSION;
   int_md_flags->is_out_of_band = FALSE;
   int_md_flags->is_client_metadata =
      tu_get_bits(client_md_flags, MD_HEADER_FLAGS_BIT_MASK_CLIENT_INFO, MD_HEADER_FLAGS_SHIFT_CLIENT_INFO);
   int_md_flags->is_client_metadata = ~int_md_flags->is_client_metadata;
   int_md_flags->tracking_mode      = tu_get_bits(client_md_flags,
                                             MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
                                             MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);
   int_md_flags->tracking_policy    = tu_get_bits(client_md_flags,
                                               MD_HEADER_FLAGS_BIT_MASK_TRACKING_EVENT_POLICY,
                                               MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);
   int_md_flags->buf_sample_association =
      tu_get_bits(client_md_flags, MD_HEADER_FLAGS_BIT_MASK_ASSOCIATION, MD_HEADER_FLAGS_SHIFT_ASSOCIATION_FLAG);

   int_md_flags->needs_propagation_to_client_buffer =
      tu_get_bits(client_md_flags,
                  MD_HEADER_FLAGS_BIT_MASK_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER,
                  MD_HEADER_FLAGS_SHIFT_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER_FLAG);

   int_md_flags->needs_propagation_to_client_buffer = ~int_md_flags->needs_propagation_to_client_buffer;
}

void gen_topo_convert_int_md_flags_to_client_md_flag(module_cmn_md_flags_t int_md_flags, uint32_t *client_md_flags)
{
   bool_t temp_client_metadata               = ~int_md_flags.is_client_metadata;
   bool_t needs_propagation_to_client_buffer = ~int_md_flags.needs_propagation_to_client_buffer;
   tu_set_bits(client_md_flags,
               temp_client_metadata,
               MD_HEADER_FLAGS_BIT_MASK_CLIENT_INFO,
               MD_HEADER_FLAGS_SHIFT_CLIENT_INFO);

   tu_set_bits(client_md_flags,
               int_md_flags.tracking_mode,
               MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
               MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);

   tu_set_bits(client_md_flags,
               int_md_flags.tracking_policy,
               MD_HEADER_FLAGS_BIT_MASK_TRACKING_EVENT_POLICY,
               MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);

   tu_set_bits(client_md_flags,
               int_md_flags.buf_sample_association,
               MD_HEADER_FLAGS_BIT_MASK_ASSOCIATION,
               MD_HEADER_FLAGS_SHIFT_ASSOCIATION_FLAG);

   tu_set_bits(client_md_flags,
               needs_propagation_to_client_buffer,
               MD_HEADER_FLAGS_BIT_MASK_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER,
               MD_HEADER_FLAGS_SHIFT_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER_FLAG);
}

ar_result_t gen_topo_raise_md_cloning_event(gen_topo_t *                      topo_ptr,
                                            module_cmn_md_tracking_payload_t *md_tracking_ptr,
                                            uint32_t                          metadata_id)
{
   ar_result_t result = AR_EOK;

   if (NULL == md_tracking_ptr)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "MD_DBG: invalid tracking payload info, failed to raise cloning event");
      return AR_EBADPARAM;
   }

   uint32_t                  opcode        = EVENT_ID_MODULE_CMN_METADATA_CLONE_MD;
   metadata_tracking_event_t md_te_payload = { 0 };

   md_te_payload.metadata_id            = metadata_id;
   md_te_payload.source_module_instance = md_tracking_ptr->src_port;
   md_te_payload.module_instance_id     = 0;
   md_te_payload.token_lsw              = md_tracking_ptr->token_lsw;
   md_te_payload.token_msw              = md_tracking_ptr->token_msw;
   md_te_payload.flags                  = 0;
   md_te_payload.status                 = 0; // ignore

   bool_t is_registered = FALSE;
   (void)__gpr_cmd_is_registered(md_tracking_ptr->src_port, &is_registered);
   // if stream close is done prior to render EOS, then client must not receive render EOS
   if (is_registered)
   {
      gpr_cmd_alloc_send_t args;
      args.src_domain_id = md_tracking_ptr->src_domain_id;
      args.dst_domain_id = md_tracking_ptr->dst_domain_id;
      args.src_port      = md_tracking_ptr->src_port;
      args.dst_port      = md_tracking_ptr->dest_port;
      args.token         = md_tracking_ptr->token_msw;
      args.opcode        = opcode;
      args.payload       = &md_te_payload;
      args.payload_size  = sizeof(metadata_tracking_event_t);
      args.client_data   = 0;
      __gpr_cmd_alloc_send(&args);

      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: raise clone event for MD_ID (0x%lx) "
               "(src port, dst_port) : 0x%lX, 0x%lX), cmd_opcode  0x%lX",
               metadata_id,
               md_tracking_ptr->src_port,
               md_tracking_ptr->dest_port,
               opcode);
   }

   return result;
}

ar_result_t gen_topo_raise_eos_tracking_event(gen_topo_tracking_md_context_t *cb_context_ptr, uint32_t ref_count)
{
   ar_result_t result = AR_EOK;

   if (NULL == cb_context_ptr->tracking_payload_ptr)
   {
      TOPO_MSG(cb_context_ptr->log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: md_tracking_ptr = 0x%X  is NULL in event callback",
               cb_context_ptr->tracking_payload_ptr,
               cb_context_ptr->md_payload_ptr);
      return result;
   }

   uint32_t log_id = cb_context_ptr->log_id;

   module_cmn_md_tracking_payload_t *tracking_ptr = cb_context_ptr->tracking_payload_ptr;

   if ((MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH == cb_context_ptr->flags.tracking_policy) ||
       ((MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST == cb_context_ptr->flags.tracking_policy) && (0 == ref_count)))
   {

      uint32_t                                 opcode       = DATA_CMD_RSP_WR_SH_MEM_EP_EOS_RENDERED;
      void *                                   payload_ptr  = NULL;
      uint32_t                                 payload_size = 0;
      data_cmd_rsp_wr_sh_mem_ep_eos_rendered_t payload      = { 0 };
      payload.module_instance_id                            = cb_context_ptr->module_instance_id;
      payload.render_status = (MD_TRACKING_STATUS_IS_CONSUMED == cb_context_ptr->render_status)
                                 ? WR_SH_MEM_EP_EOS_RENDER_STATUS_RENDERED
                                 : WR_SH_MEM_EP_EOS_RENDER_STATUS_DROPPED;
      payload_ptr           = (void *)&payload;
      payload_size          = sizeof(data_cmd_rsp_wr_sh_mem_ep_eos_rendered_t);

      bool_t is_registered = FALSE;
      (void)__gpr_cmd_is_registered(tracking_ptr->src_port, &is_registered);
      // if stream close is done prior to render EOS, then client must not receive render EOS
      if (is_registered)
      {
         gpr_cmd_alloc_send_t args;
         args.src_domain_id = tracking_ptr->src_domain_id;
         args.dst_domain_id = tracking_ptr->dst_domain_id;
         args.src_port      = tracking_ptr->src_port;
         args.dst_port      = tracking_ptr->dest_port;
         args.token         = tracking_ptr->token_lsw;
         args.opcode        = opcode;
         args.payload       = payload_ptr;
         args.payload_size  = payload_size;
         args.client_data   = 0;
         __gpr_cmd_alloc_send(&args);

         TOPO_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: Raising tracking event for EOS with tracking payload 0x%p (src port 0x%lX), "
                  "render status = %lu, policy 0x%x ref_count = %lu cmd_opcode  0x%lX",
                  tracking_ptr,
                  tracking_ptr->src_port,
                  payload.render_status,
                  cb_context_ptr->flags.tracking_policy,
                  ref_count,
                  opcode);
      }
      else
      {
         TOPO_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: Not raising tracking event for EOS as client has closed the source module, "
                  "tracking payload 0x%p (src port 0x%lX), render status = %lu, policy 0x%x  "
                  "ref_count = %lu cmd_opcode  0x%lX",
                  tracking_ptr,
                  tracking_ptr->src_port,
                  payload.render_status,
                  cb_context_ptr->flags.tracking_policy,
                  ref_count,
                  opcode);
      }
   }
   return result;
}

static ar_result_t gen_topo_raise_md_tracking_event(gen_topo_tracking_md_context_t *cb_context_ptr, uint32_t ref_count)
{
   ar_result_t result = AR_EOK;
   if (NULL == cb_context_ptr->tracking_payload_ptr)
   {
      TOPO_MSG(cb_context_ptr->log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: NULL md_tracking_ptr = 0x%X in callback",
               cb_context_ptr->tracking_payload_ptr);
      return result;
   }

   if ((MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH == cb_context_ptr->flags.tracking_policy) ||
       ((MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST == cb_context_ptr->flags.tracking_policy) && (0 == ref_count)))
   {

      uint32_t                          opcode        = EVENT_ID_MODULE_CMN_METADATA_TRACKING_EVENT;
      metadata_tracking_event_t         md_te_payload = { 0 };
      module_cmn_md_tracking_payload_t *md_tracking_ptr =
         (module_cmn_md_tracking_payload_t *)cb_context_ptr->tracking_payload_ptr;

      md_te_payload.metadata_id            = cb_context_ptr->metadata_id;
      md_te_payload.source_module_instance = md_tracking_ptr->src_port;
      md_te_payload.module_instance_id     = cb_context_ptr->module_instance_id;
      md_te_payload.token_lsw              = md_tracking_ptr->token_lsw;
      md_te_payload.token_msw              = md_tracking_ptr->token_msw;
      md_te_payload.flags                  = (0 == ref_count) ? TRUE : FALSE;
      md_te_payload.status                 = cb_context_ptr->render_status;

      bool_t is_registered = FALSE;
      (void)__gpr_cmd_is_registered(md_tracking_ptr->src_port, &is_registered);
      // if stream close is done prior to render EOS, then client must not receive render EOS
      if (is_registered)
      {
         gpr_cmd_alloc_send_t args;
         args.src_domain_id = md_tracking_ptr->src_domain_id;
         args.dst_domain_id = md_tracking_ptr->dst_domain_id;
         args.src_port      = md_tracking_ptr->src_port;
         args.dst_port      = md_tracking_ptr->dest_port;
         args.token         = md_tracking_ptr->token_msw;
         args.opcode        = opcode;
         args.payload       = &md_te_payload;
         args.payload_size  = sizeof(metadata_tracking_event_t);
         args.client_data   = 0;
         __gpr_cmd_alloc_send(&args);

         TOPO_MSG(cb_context_ptr->log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: Raising tracking event for MD_ID (0x%lx) (src port 0x%lX), render status = %lu, "
                  "policy 0x%x ref_count = %lu cmd_opcode  0x%lX",
                  cb_context_ptr->metadata_id,
                  md_tracking_ptr->src_port,
                  cb_context_ptr->render_status,
                  cb_context_ptr->flags.tracking_policy,
                  ref_count,
                  opcode);
      }
      else
      {
         TOPO_MSG(cb_context_ptr->log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: Not Raising tracking event for MD_ID (0x%lx) as client has closed the source module. "
                  "(src port 0x%lX), render status = %lu, policy 0x%x  ref_count = %lu cmd_opcode  0x%lX",
                  cb_context_ptr->metadata_id,
                  md_tracking_ptr->src_port,
                  cb_context_ptr->render_status,
                  cb_context_ptr->flags.tracking_policy,
                  ref_count,
                  opcode);
      }
   }
   return result;
}

void gen_topo_send_md_render_status(void *context_ptr, uint32_t ref_count)
{
   gen_topo_tracking_md_context_t *cb_context_ptr = (gen_topo_tracking_md_context_t *)context_ptr;
   if (NULL == cb_context_ptr)
   {
      return;
   }

   if ((MODULE_CMN_MD_ID_EOS == cb_context_ptr->metadata_id) &&
       (MODULE_CMN_MD_TRACKING_USE_CUSTOM_EVENT == cb_context_ptr->tracking_payload_ptr->flags.requires_custom_event))
   {
      gen_topo_raise_eos_tracking_event(cb_context_ptr, ref_count);
   }
   else
   {
      gen_topo_raise_md_tracking_event(cb_context_ptr, ref_count);
   }
}

void gen_topo_drop_md(uint32_t                          log_id,
                      module_cmn_md_tracking_payload_t *tracking_ptr,
                      uint32_t                          metadata_id,
                      module_cmn_md_flags_t             flags,
                      uint32_t                          module_instance_id,
                      bool_t                            ref_counted_obj_created,
                      void *                            md_event_payload_ptr)
{
   gen_topo_tracking_md_context_t cb_context;
   cb_context.tracking_payload_ptr = tracking_ptr;
   cb_context.module_instance_id   = module_instance_id;
   cb_context.metadata_id          = metadata_id;
   cb_context.flags                = flags;
   cb_context.render_status        = MD_TRACKING_STATUS_IS_DROPPED;
   cb_context.log_id               = log_id;
   cb_context.md_payload_ptr       = md_event_payload_ptr;

   if (ref_counted_obj_created)
   {
      // Internal EOS doesn't have the core - so we should not ref count
      if (NULL != tracking_ptr)
      {
         spf_ref_counter_remove_ref((void *)tracking_ptr, gen_topo_send_md_render_status, &cb_context);
      }
   }
   else
   {
      gen_topo_send_md_render_status((void *)&cb_context, 0 /*ref_count*/);
   }
}

static void gen_topo_render_md(uint32_t         log_id,
                               module_cmn_md_t *metadata_ptr,
                               uint32_t         module_instance_id,
                               void *           md_payload_ptr)
{
   gen_topo_tracking_md_context_t cb_context;
   cb_context.tracking_payload_ptr = (module_cmn_md_tracking_payload_t *)metadata_ptr->tracking_ptr;
   cb_context.module_instance_id   = module_instance_id;
   cb_context.metadata_id          = metadata_ptr->metadata_id;
   cb_context.flags                = metadata_ptr->metadata_flag;
   cb_context.render_status        = MD_TRACKING_STATUS_IS_CONSUMED;
   cb_context.log_id               = log_id;
   cb_context.md_payload_ptr       = md_payload_ptr;

   if (NULL != metadata_ptr->tracking_ptr)
   {
      spf_ref_counter_remove_ref((void *)metadata_ptr->tracking_ptr, gen_topo_send_md_render_status, &cb_context);
   }
}

ar_result_t gen_topo_raise_tracking_event(gen_topo_t *          topo_ptr,
                                          uint32_t              sink_miid,
                                          module_cmn_md_list_t *md_list_ptr,
                                          bool_t                is_md_rendered,
                                          void *                md_payload_ptr,
                                          bool_t                override_ctrl_to_disable_tracking_event)
{
   ar_result_t result = AR_EOK;

   if (NULL == md_list_ptr)
   {
      return result;
   }
   module_cmn_md_t *metadata_ptr = NULL;
   metadata_ptr                  = md_list_ptr->obj_ptr;

   if (metadata_ptr->tracking_ptr)
   {
      if (override_ctrl_to_disable_tracking_event)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "MD_DBG: Not raising tracking event for MD_ID (0x%lx) as the caller set the override flag to disable "
                  "tracking event render status = %lu (0/1 : drop/render), policy 0x%x  ",
                  metadata_ptr->metadata_id,
                  is_md_rendered,
                  metadata_ptr->metadata_flag.tracking_policy);
         spf_ref_counter_remove_ref((void *)metadata_ptr->tracking_ptr, NULL, NULL);
      }
      else
      {
      if (is_md_rendered)
      {
         if (MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME == metadata_ptr->metadata_flag.tracking_mode)
         {
               gen_topo_render_md(topo_ptr->gu.log_id, metadata_ptr, sink_miid, md_payload_ptr);
         }
         else
         {
            spf_ref_counter_remove_ref((void *)metadata_ptr->tracking_ptr, NULL, NULL);
         }
      }
      else if (!is_md_rendered) // dropped
      {
         gen_topo_drop_md(topo_ptr->gu.log_id,
                          metadata_ptr->tracking_ptr,
                          metadata_ptr->metadata_id,
                          metadata_ptr->metadata_flag,
                             sink_miid,
                          TRUE,
                          md_payload_ptr);
         }
      }
   }

   return result;
}

/**
 * function to create meta-data with tracking feature.
 */
static ar_result_t gen_topo_metadata_create_with_tracking(gen_topo_t               *topo_ptr,
                                                          module_cmn_md_list_t **   md_list_pptr,
                                                          uint32_t                  size,
                                                          capi_heap_id_t            heap_id,
                                                          uint32_t                  metadata_id,
                                                          module_cmn_md_flags_t     flags,
                                                          module_cmn_md_tracking_t *tracking_info_ptr,
                                                          module_cmn_md_t **        md_pptr)
{
   ar_result_t ar_result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t md_size = sizeof(module_cmn_md_t);
   uint32_t md_tracking_heap_id;
   uint32_t log_id = topo_ptr->gu.log_id;

   bool_t tracking_ref_created = FALSE;
   bool_t tracking_mode        = FALSE;

   module_cmn_md_t *md_ptr         = NULL;
   void *           md_payload_ptr = NULL;
   spf_list_node_t *tail_node_ptr  = NULL;

   if ((NULL == md_list_pptr) || (NULL == md_pptr))
   {
      THROW(ar_result, AR_EBADPARAM)
   }

   *md_pptr = NULL;

   if ((MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME == flags.tracking_mode) ||
       (MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY == flags.tracking_mode))
   {
      tracking_mode = TRUE;
   }

   // for in-band do only one malloc
   if (!flags.is_out_of_band)
   {
      md_size = MODULE_CMN_MD_INBAND_GET_REQ_SIZE(size);
   }

   md_ptr = (module_cmn_md_t *)posal_memory_malloc(md_size, (POSAL_HEAP_ID)heap_id.heap_id);
   VERIFY(ar_result, NULL != md_ptr);
   memset(md_ptr, 0, sizeof(module_cmn_md_t)); // memset only top portion as size may be huge

   if (flags.is_out_of_band)
   {
      if (size)
      {
         md_payload_ptr = posal_memory_malloc(size, (POSAL_HEAP_ID)heap_id.heap_id);
         VERIFY(ar_result, NULL != md_payload_ptr);
      }

      md_ptr->actual_size  = size;
      md_ptr->max_size     = size;
      md_ptr->metadata_ptr = md_payload_ptr;
   }
   else
   {
      md_ptr->actual_size = size;
      md_ptr->max_size    = size;
      // md_payload_ptr NULL for in-band
   }

   if (tracking_mode)
   {
      VERIFY(ar_result, NULL != tracking_info_ptr);

      //Always allocate tracking md in LPI. MD may propagate beyond NLPI-LPI boundary.
      //Also different modules may access tracking payload when in island.

      module_cmn_md_tracking_payload_t *md_tracking_ref_ptr = NULL;
      md_tracking_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, spf_mem_island_heap_id);

      ar_result_t result = spf_ref_counter_create_ref((uint32_t)sizeof(module_cmn_md_tracking_payload_t),
                                                      (POSAL_HEAP_ID)md_tracking_heap_id,
                                                      (void **)&md_tracking_ref_ptr);
      if (AR_EOK != result)
      {
         TOPO_MSG(log_id, DBG_ERROR_PRIO, "MD_DBG: Failed to create a metadata tracking pointer");
         THROW(ar_result, result);
      }
      md_ptr->tracking_ptr = md_tracking_ref_ptr;
      memscpy(md_ptr->tracking_ptr,
              sizeof(module_cmn_md_tracking_payload_t),
              &tracking_info_ptr->tracking_payload,
              sizeof(module_cmn_md_tracking_payload_t));
      tracking_ref_created = TRUE;

      TOPO_MSG(log_id, DBG_HIGH_PRIO, "MD_DBG: created a metadata tracking pointer for MD_ID 0x%lx", metadata_id);
   }

   TRY(ar_result,
       spf_list_insert_tail((spf_list_node_t **)md_list_pptr,
                            md_ptr,
                            (POSAL_HEAP_ID)heap_id.heap_id,
                            TRUE /* use pool */));

   spf_list_get_tail_node((spf_list_node_t *)*md_list_pptr, &tail_node_ptr);
#if defined(METADATA_DEBUGGING)
   TOPO_MSG(log_id,
            DBG_MED_PRIO,
            "MD_DBG: Metadata create: spf_list_node_t 0x%p host md_ptr 0x%p",
            tail_node_ptr,
            md_ptr);
#endif

   md_ptr->metadata_flag = flags;
   md_ptr->metadata_id   = metadata_id;
   *md_pptr              = md_ptr;

   CATCH(ar_result, TOPO_MSG_PREFIX, log_id)
   {
      if (flags.is_out_of_band)
      {
         MFREE_NULLIFY(md_payload_ptr);
      }
      if ((tracking_mode) && (tracking_info_ptr))
      {
         gen_topo_drop_md(log_id,
                          &tracking_info_ptr->tracking_payload,
                          metadata_id,
                          flags,
                          tracking_info_ptr->tracking_payload.src_port,
                          tracking_ref_created,
                          NULL);
      }
      MFREE_NULLIFY(md_ptr);
      // No errors after inserting to linked list
   }

   // Increment md counter as the last step, so that if there was any error MD counter incremented can be skipped.
   if (ar_result == AR_EOK)
   {
      thin_topo_incr_active_md_nodes(topo_ptr, (module_cmn_md_list_t *)tail_node_ptr);
   }

   return ar_result;
}

/**
 * function to create meta-data with tracking feature.
 */
capi_err_t gen_topo_capi_metadata_create_with_tracking(void *                    context_ptr, // context cannot be NULL
                                                       module_cmn_md_list_t **   md_list_pptr,
                                                       uint32_t                  size,
                                                       capi_heap_id_t            heap_id,
                                                       uint32_t                  metadata_id,
                                                       module_cmn_md_flags_t     flags,
                                                       module_cmn_md_tracking_t *tracking_info_ptr,
                                                       module_cmn_md_t **        md_pptr)
{
   ar_result_t ar_result = AR_EOK;
   uint32_t    log_id    = 0;
   INIT_EXCEPTION_HANDLING

   if ((NULL == md_list_pptr) || (NULL == md_pptr) || (NULL == context_ptr))
   {
      THROW(ar_result, AR_EBADPARAM)
   }

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

#if defined(METADATA_DEBUGGING)
   TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "MD_DBG: create metadata 0x%lx", metadata_id);
#endif

   ar_result = gen_topo_metadata_create_with_tracking(topo_ptr,
                                                      md_list_pptr,
                                                      size,
                                                      heap_id,
                                                      metadata_id,
                                                      flags,
                                                      tracking_info_ptr,
                                                      md_pptr);
   if (AR_DID_FAIL(ar_result))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "MD_DBG: create MD_ID (0x%lx) failed with 0x%x",
               metadata_id,
               ar_result);
   }
   else
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: create MD_ID (0x%lx) sucessfully", metadata_id);
   }

   CATCH(ar_result, TOPO_MSG_PREFIX, log_id)
   {
   }
   return ar_result_to_capi_err(ar_result);
}

void gen_topo_populate_metadata_extn_vtable(gen_topo_module_t *                    module_ptr,
                                            intf_extn_param_id_metadata_handler_t *handler_ptr)
{
   memset(handler_ptr, 0, sizeof(*handler_ptr));

   handler_ptr->version                            = (uint32_t)INTF_EXTN_METADATA_HANDLER_VERSION;
   handler_ptr->context_ptr                        = (void *)module_ptr;
   handler_ptr->metadata_create                    = gen_topo_capi_exit_island_metadata_create;
   handler_ptr->metadata_clone                     = gen_topo_capi_exit_island_metadata_clone;
   handler_ptr->metadata_destroy                   = gen_topo_capi_exit_island_metadata_destroy;
   handler_ptr->metadata_propagate                 = gen_topo_capi_exit_island_metadata_propagate;
   handler_ptr->metadata_modify_at_data_flow_start = gen_topo_capi_exit_island_metadata_modify_at_data_flow_start;
   handler_ptr->metadata_create_with_tracking      = gen_topo_capi_exit_island_metadata_create_with_tracking;
}

// Repackage: must be called only from gen_topo_check_realloc_md_list_in_peer_heap_id.
ar_result_t gen_topo_realloc_md_list_in_peer_heap_id(uint32_t               log_id,
                                                     module_cmn_md_list_t **md_list_pptr,
                                                     module_cmn_md_list_t * node_ptr,
                                                     POSAL_HEAP_ID          downstream_heap_id)
{
   uint32_t md_size        = sizeof(module_cmn_md_t);
   bool_t   POOL_USED_TRUE = TRUE, USE_POOL_TRUE = TRUE;
   void *   new_obj_ptr = NULL;

   // Repackage
   module_cmn_md_t *md_ptr = (module_cmn_md_t *)node_ptr->obj_ptr;

#ifdef METADATA_DEBUGGING
   TOPO_MSG(log_id, DBG_LOW_PRIO, "MD_DBG: Repackage MD ID 0x%lx to LPI heap", md_ptr->metadata_id);
#endif
   if (md_ptr->metadata_flag.is_out_of_band)
   {
      new_obj_ptr = spf_lpi_pool_get_node(md_size);
      if (NULL == new_obj_ptr)
      {
         TOPO_MSG_ISLAND(log_id,
                         DBG_ERROR_PRIO,
                         "MD_DBG: Repackage MD ID %lu to LPI heap: Failed to get node of size %lu from MD "
                         "pool",
                         md_ptr->metadata_id,
                         md_size);
         return AR_EFAILED;
      }

      module_cmn_md_t *new_md_ptr = (module_cmn_md_t *)new_obj_ptr;
      new_md_ptr->metadata_ptr    = spf_lpi_pool_get_node(md_ptr->actual_size);

      if (NULL == new_md_ptr->metadata_ptr)
      {
         TOPO_MSG_ISLAND(log_id,
                         DBG_ERROR_PRIO,
                         "MD_DBG: Repackage MD ID %lu to LPI heap: Failed to get node of size %lu from MD "
                         "pool",
                         md_ptr->metadata_id,
                         md_ptr->actual_size);
         spf_lpi_pool_return_node(new_obj_ptr);
         new_obj_ptr = NULL;
         return AR_EFAILED;
      }
      // copy the md header
      memscpy(new_obj_ptr, md_size, (void *)md_ptr, md_size);
      // then copy the oob payload
      memscpy(new_md_ptr->metadata_ptr, new_md_ptr->actual_size, md_ptr->metadata_ptr, md_ptr->actual_size);

      // free the old metadata
      MFREE_NULLIFY(md_ptr->metadata_ptr);
   }
   else
   {
      uint32_t inband_size = MODULE_CMN_MD_INBAND_GET_REQ_SIZE(md_ptr->actual_size);
      new_obj_ptr          = spf_lpi_pool_get_node(inband_size);
      if (NULL == new_obj_ptr)
      {
         TOPO_MSG_ISLAND(log_id,
                         DBG_ERROR_PRIO,
                         "MD_DBG: Repackage MD ID %lu to LPI heap: Failed to get node of size %lu from MD "
                         "pool",
                         md_ptr->metadata_id,
                         inband_size);
         return AR_EFAILED;
      }
      memscpy(new_obj_ptr, inband_size, (void *)md_ptr, inband_size);
   }
   MFREE_NULLIFY(md_ptr);

   // this api will free/return the old list node, replace it with new node and new object ptr.
   spf_list_realloc_replace_node((spf_list_node_t **)md_list_pptr,
                                 (spf_list_node_t **)&node_ptr,
                                 new_obj_ptr,
                                 POOL_USED_TRUE,
                                 USE_POOL_TRUE,
                                 downstream_heap_id);
   return AR_EOK;
}