/**
 * \file gen_topo_metadata_island.c
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

#define PRINT_MD_PROP_DBG_ISLAND(str1, str2, len_per_ch, str3, ...)                                                    \
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                                \
                   DBG_LOW_PRIO,                                                                                       \
                   "MD_DBG: " str1 ". module 0x%lX, node_ptr 0x%p, md_id 0x%08lX, offset %lu, offset_before %lu," str2 \
                   "_per_ch %lu, " str3,                                                                               \
                   module_ptr->gu.module_instance_id,                                                                  \
                   node_ptr,                                                                                           \
                   md_ptr->metadata_id,                                                                                \
                   md_ptr->offset,                                                                                     \
                   offset_before,                                                                                      \
                   len_per_ch,                                                                                         \
                   ##__VA_ARGS__)

void gen_topo_check_free_md_ptr(void **ptr, bool_t pool_used)
{
   if (pool_used)
   {
      spf_lpi_pool_return_node(*ptr);
      *ptr = NULL;
   }
   else
   {
      MFREE_NULLIFY(*ptr);
   }
}

/**
 * container reference is needed only for flushing EOS.
 */
ar_result_t gen_topo_create_eos_cntr_ref(gen_topo_t *           topo_ptr,
                                         POSAL_HEAP_ID          heap_id,
                                         gen_topo_input_port_t *input_port_ptr,
                                         uint32_t               input_id,
                                         gen_topo_eos_cargo_t **eos_cargo_pptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id         = topo_ptr->gu.log_id;
   bool_t   is_island_heap = POSAL_IS_ISLAND_HEAP_ID(heap_id);

   /*Malloc/Free APIs are not available in LPI - Therefore we need to get/return node from/to the pool instead*/
   gen_topo_eos_cargo_t *cntr_ref_ptr = NULL;
   cntr_ref_ptr =
       is_island_heap ? (gen_topo_eos_cargo_t *)spf_lpi_pool_get_node(sizeof(gen_topo_eos_cargo_t)) : NULL;
   if (NULL == cntr_ref_ptr)
   {
      gen_topo_exit_island_temporarily(topo_ptr);
      cntr_ref_ptr = (gen_topo_eos_cargo_t *)posal_memory_malloc(sizeof(gen_topo_eos_cargo_t), heap_id);
   }

   VERIFY(result, NULL != cntr_ref_ptr);

   // Multiple EOS can come together
   memset(cntr_ref_ptr, 0, sizeof(gen_topo_eos_cargo_t));

   /** Container specific payload : cargo */
   cntr_ref_ptr->inp_ref                  = input_port_ptr;
   cntr_ref_ptr->inp_id                   = input_id;
   cntr_ref_ptr->did_eos_come_from_ext_in = (NULL != input_port_ptr) && (NULL != input_port_ptr->gu.ext_in_port_ptr);
   cntr_ref_ptr->ref_count                = 1;

   *eos_cargo_pptr = cntr_ref_ptr;

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
      MFREE_NULLIFY(cntr_ref_ptr);
   }

   return result;
}

bool_t gen_topo_is_flushing_eos(module_cmn_md_t *md_ptr)
{
   if (NULL == md_ptr)
   {
      return FALSE;
   }
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

      if (MODULE_CMN_MD_EOS_FLUSHING == eos_metadata_ptr->flags.is_flushing_eos)
      {
         return TRUE;
      }
   }
   return FALSE;
}

bool_t gen_topo_does_eos_skip_voting(module_cmn_md_t *md_ptr)
{
   if (NULL == md_ptr)
   {
      return FALSE;
   }
   if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
   {
      module_cmn_md_eos_t *eos_metadata_ptr = NULL;
      uint32_t             is_out_band      = md_ptr->metadata_flag.is_out_of_band;
      if (is_out_band)
      {
         eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
      }
      else
      {
         eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
      }

      if (eos_metadata_ptr->flags.skip_voting_on_dfs_change)
      {
         return TRUE;
      }
   }
   return FALSE;
}

/**
 * need_to_add - TRUE: convert bytes and add to offset
 *               FALSE: convert bytes and subtract from offset
 *
 */
ar_result_t gen_topo_do_md_offset_math(uint32_t          log_id,
                                       uint32_t *        offset_ptr,
                                       uint32_t          bytes_across_ch,
                                       topo_media_fmt_t *med_fmt_ptr,
                                       bool_t            need_to_add)
{
   uint32_t converted_bytes = bytes_across_ch;

   if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format))
   {
      converted_bytes = topo_bytes_to_samples_per_ch(bytes_across_ch, med_fmt_ptr);
   }

   if (need_to_add)
   {
      *offset_ptr += converted_bytes;
   }
   else
   {
      if (*offset_ptr >= converted_bytes)
      {
         *offset_ptr -= converted_bytes;
      }
      else
      {
         TOPO_MSG_ISLAND(log_id,
                  DBG_ERROR_PRIO,
                  "MD_DBG: offset calculation error. offset becoming negative. setting as zero");
         *offset_ptr = 0;
      }
   }

   return AR_EOK;
}

ar_result_t gen_topo_metadata_adj_offset(gen_topo_t *          topo_ptr,
                                         topo_media_fmt_t *    med_fmt_ptr,
                                         module_cmn_md_list_t *md_list_ptr,
                                         uint32_t              bytes_consumed,
                                         bool_t                true_add_false_sub)
{
   ar_result_t result = AR_EOK;

   if (md_list_ptr)
   {
      module_cmn_md_list_t *node_ptr = md_list_ptr;
      while (node_ptr)
      {
         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

#ifdef METADATA_DEBUGGING
         uint32_t offset_before = md_ptr->offset;
#endif

         gen_topo_do_md_offset_math(topo_ptr->gu.log_id,
                                    &md_ptr->offset,
                                    bytes_consumed,
                                    med_fmt_ptr,
                                    true_add_false_sub);
#ifdef METADATA_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                        DBG_LOW_PRIO,
                        "MD_DBG: update offset of md_ptr 0x%x md_id 0x%08lX. offset_before %u. offset %u by bytes_consumed "
                        "%u true_add_false_sub %d, metadata_flag0x%08lX",
                        md_ptr,
                        md_ptr->metadata_id,
                        offset_before,
                        md_ptr->offset,
                        bytes_consumed,
                        true_add_false_sub,
                        md_ptr->metadata_flag.word);
#endif

         node_ptr = node_ptr->next_ptr;
      }
   }

   return result;
}

// Note that fwk does best effor to differ voting whenever this flag is set.
// Race conditions are possible w.r.t to multiple EOS with different flags.
// If an EOS gets generated with skip_voting=TRUE and then a if a new EOS
// gets created with skip_voting=FALSE, the lastest flag is honored even if
// both EOS are still in the container.
static ar_result_t gen_topo_check_eos_md_update_defer_voting_flag( gu_cmn_port_t          *cmn_port_ptr,
                                                                   module_cmn_md_t        *md_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_t *topo_ptr = (gen_topo_t *)((gen_topo_module_t *)cmn_port_ptr->module_ptr)->topo_ptr;
   if (gen_topo_does_eos_skip_voting(md_ptr))
   {
      topo_ptr->flags.defer_voting_on_dfs_change = TRUE;
   }
   else
   {
      topo_ptr->flags.defer_voting_on_dfs_change = FALSE;
   }

   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                  "MD_DBG: Module 0x%08lX, in port id %lu, has an EOS with defer_voting_on_dfs_change:%lu"
                  "offset being copied.",
                  cmn_port_ptr->module_ptr->module_instance_id,
                  cmn_port_ptr->id,
                  topo_ptr->flags.defer_voting_on_dfs_change);

   return result;
}

/**
 * after copying if src or dst still have flushing eos, then dst_received_flush_eos_ptr, src_has_no_more_flush_eos_ptr
 * will be set
 *
 * dst_received_flush_eos_ptr is only updated as TRUE, src_has_no_more_flush_eos_ptr is only updated with FALSE.
 * dst is updated if it gets at least once flushing EOS. src is updated if it looses all flushing EOSes.
 *
 * For both sample associated and buffer associated metadata, the offsets come into play in this function.
 * Till the sample/buffer containing the MD is consumed, the MD info cannot be transferred.
 *
 * Note that for PCM, bytes_already_in_dst, bytes_copied_from_src are byter for all channels
 *
 * all_dfg_or_flush_eos_moved_ptr - set to TRUE if src has no more flushing EOS or DFG left.
 */
static ar_result_t gen_topo_move_md(uint32_t               log_id,
                                    gen_topo_input_port_t *in_port_ptr,
                                    module_cmn_md_list_t **src_md_lst_pptr,
                                    uint32_t               bytes_already_in_dst,
                                    uint32_t               bytes_already_in_src,
                                    uint32_t               bytes_copied_from_src,
                                    topo_media_fmt_t *     src_med_fmt_ptr,
                                    bool_t *               dst_received_flush_eos_ptr,
                                    bool_t *               src_has_no_more_flush_eos_ptr,
                                    bool_t *               dst_received_new_dfg_ptr,
                                    bool_t *               all_dfg_or_flush_eos_moved_ptr)
{
   ar_result_t result = AR_EOK;

   if (!*src_md_lst_pptr)
   {
      return result;
   }

   uint32_t               module_instance_id = in_port_ptr->gu.cmn.module_ptr->module_instance_id;
   uint32_t               port_id            = in_port_ptr->gu.cmn.id;
   topo_media_fmt_t *     dst_med_fmt_ptr    = in_port_ptr->common.media_fmt_ptr;
   module_cmn_md_list_t **dst_md_lst_pptr    = &in_port_ptr->common.sdata.metadata_list_ptr;

   module_cmn_md_list_t *node_ptr                         = *src_md_lst_pptr;
   module_cmn_md_list_t *next_ptr                         = NULL;
   bool_t                any_flush_eos_not_propagated     = FALSE;
   bool_t                any_flush_eos_dfg_not_propagated = FALSE;
   bool_t                any_flush_eos_dfg_existed_in_src = FALSE;
   uint32_t              converted_bytes_copied_from_src  = bytes_copied_from_src;
   uint32_t              converted_bytes_already_in_dst   = bytes_already_in_dst;

   *all_dfg_or_flush_eos_moved_ptr = FALSE;

   if (SPF_IS_PACKETIZED_OR_PCM(src_med_fmt_ptr->data_format))
   {
      converted_bytes_copied_from_src = topo_bytes_to_samples_per_ch(converted_bytes_copied_from_src, src_med_fmt_ptr);
   }
   if (SPF_IS_PACKETIZED_OR_PCM(dst_med_fmt_ptr->data_format))
   {
      converted_bytes_already_in_dst = topo_bytes_to_samples_per_ch(converted_bytes_already_in_dst, dst_med_fmt_ptr);
   }

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      // If offset within data being copied, then first condition for copying is satisfied.
      bool_t move_md = gen_topo_check_move_metadata(md_ptr->offset, bytes_already_in_src, converted_bytes_copied_from_src);

      bool_t is_flush_eos_dfg = (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id);

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         if (gen_topo_is_flushing_eos(md_ptr))
         {
            is_flush_eos_dfg = TRUE;

            if (move_md)
            {
               *dst_received_flush_eos_ptr = TRUE;
            }
            else
            {
               any_flush_eos_not_propagated = TRUE;
            }

            // Check if container voting changes need to be deferred for this Flushing EOS.
            gen_topo_check_eos_md_update_defer_voting_flag(&in_port_ptr->gu.cmn, md_ptr);
         }
      }

      any_flush_eos_dfg_existed_in_src |= is_flush_eos_dfg;

      if (move_md)
      {
         if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
         {
            // set only if we are copying. in GEN_CNTR anyway, process is called as long as MD list is present (prev or
            // next), even if MD is not copied.
            *dst_received_new_dfg_ptr = TRUE;
         }

         // adjust for data already in the next buffer.
         md_ptr->offset += converted_bytes_already_in_dst;

         spf_list_move_node_to_another_list((spf_list_node_t **)dst_md_lst_pptr,
                                            (spf_list_node_t *)node_ptr,
                                            (spf_list_node_t **)src_md_lst_pptr);
      }
      else
      {
         if (is_flush_eos_dfg)
         {
            any_flush_eos_dfg_not_propagated = TRUE;
         }
         // move offset up by the amount of data consumed from the src.
         // if metadata is not copied when it is within the offset, then set offset as zero.
         if (md_ptr->offset >= converted_bytes_copied_from_src)
         {
            md_ptr->offset -= converted_bytes_copied_from_src;
         }
         else
         {
            md_ptr->offset = 0;
            TOPO_MSG_ISLAND(log_id,
                     DBG_ERROR_PRIO,
                     "MD_DBG: Module 0x%08lX, in port id %lu, MD is not copied even though it's within the "
                     "offset being copied.",
                     module_instance_id,
                     port_id);
         }
      }

#if defined(METADATA_DEBUGGING)
      uint32_t flag = ((uint32_t)any_flush_eos_dfg_not_propagated << 4) |
                      ((uint32_t)any_flush_eos_dfg_existed_in_src << 3) | ((uint32_t)move_md << 2) |
                      (((uint32_t)*dst_received_flush_eos_ptr) << 1) | ((uint32_t)any_flush_eos_not_propagated);
      TOPO_MSG_ISLAND(log_id,
               DBG_LOW_PRIO,
               "MD_DBG: Module 0x%lX, in-port-id %lu, move_md: temp_flag0x%lX, node_ptr 0x%p, md_id 0x%08lX, offset "
               "%lu, converted(copied_from_src %lu, already_in_dst %lu (samples or bytes per ch) )",
               module_instance_id,
               port_id,
               flag,
               node_ptr,
               md_ptr->metadata_id,
               md_ptr->offset,
               converted_bytes_copied_from_src,
               converted_bytes_already_in_dst);
#endif

      node_ptr = next_ptr;
   }

   // Clear marker_eos if all flush-eos got propagated. otherwise, some might have propagated and others might be lying
   // around.
   if (!any_flush_eos_not_propagated)
   {
      *src_has_no_more_flush_eos_ptr = TRUE;
   }

   if (any_flush_eos_dfg_existed_in_src)
   {
      *all_dfg_or_flush_eos_moved_ptr = !any_flush_eos_dfg_not_propagated;
   }

   return result;
}

/**
 * See comments for capi_intf_extn_metadata.h metadata_modify_at_data_flow_start().
 */
capi_err_t gen_topo_capi_metadata_modify_at_data_flow_start(void *                 context_ptr,
                                                            module_cmn_md_list_t * md_node_ptr,
                                                            module_cmn_md_list_t **head_pptr)
{
   capi_err_t         result     = CAPI_EOK;
   ar_result_t        ar_result  = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   if (NULL == md_node_ptr)
   {
      return result;
   }

   module_cmn_md_t *md_ptr = (module_cmn_md_t *)md_node_ptr->obj_ptr;

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

      if (eos_metadata_ptr->flags.is_internal_eos)
      {
    	 //Need to exit island to call memory free operations in nlpi
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                        DBG_LOW_PRIO,
                        "MD_DBG: Module 0x%lX, node_ptr 0x%p, md_ptr 0x%p, destroying internal EOS",
                        module_ptr->gu.module_instance_id,
                        md_node_ptr,
                        md_ptr);

         ar_result = gen_topo_respond_and_free_eos(topo_ptr,
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
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "MD_DBG: Module 0x%lX, modify_md: node_ptr 0x%p, md_ptr 0x%p"
                     "converted flushing EOS to non-flushing.",
                     module_ptr->gu.module_instance_id,
                     md_node_ptr,
                     md_ptr);
         }
      }
   }
   else if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MD_DBG: Module 0x%lX, modify_md: node_ptr 0x%p, md_ptr 0x%p"
               "Destroying DFG.",
               module_ptr->gu.module_instance_id,
               md_node_ptr,
               md_ptr);

      bool_t IS_DROPPED_TRUE = TRUE;
      ar_result = gen_topo_capi_metadata_destroy((void *)module_ptr, md_node_ptr, IS_DROPPED_TRUE, head_pptr, 0, FALSE);
   }

   result = ar_result_to_capi_err(ar_result);

   return result;
}

/**
 * When new data arrives at a buffer, we have to do the following:
 * 1. Remove any DFG not at the end of the buffer
 * 2. Remove any internal EOS not at the end of the buffer
 * 3. Change any flushing EOS not at the end of the buffer to non-flushing
 *    - if as a result of this, there are no more flushing EOS's in the buffer, we have to reset the pending zeros.
 *    - Also if this caused a new flushing EOS to arrive on the buffer, set EOS flushing zeros to it's original amount
 *      (start flushing zeros).
 *
 * This function should be called after data and metadata are combined into the new buffer.
 * This function operates on a single metadata list, although metadata might be spread out among multiple lists.
 *
 * Returns new_marker_eos_ptr -> TRUE if there's any flushing eos remaining in the metadata list, otherwise FALSE.
 *
 * end_offset includes old and new data. The advantage of doing this is, if a module keeps flushing-EOS/DFG in the
 * middle of the data (due to bug), they will be converted to non-flushing EOS.
 */
ar_result_t gen_topo_md_list_modify_md_when_new_data_arrives(gen_topo_t *           topo_ptr,
                                                             gen_topo_module_t *    module_ptr,
                                                             module_cmn_md_list_t **md_list_pptr,
                                                             uint32_t               end_offset,
                                                             bool_t *               new_marker_eos_ptr,
                                                             bool_t *               has_new_dfg_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t           result   = AR_EOK;
   module_cmn_md_list_t *node_ptr = NULL;
   module_cmn_md_list_t *next_ptr = NULL;

   VERIFY(result, topo_ptr && module_ptr && md_list_pptr && new_marker_eos_ptr);
   node_ptr            = *md_list_pptr;
   *new_marker_eos_ptr = FALSE;
   *has_new_dfg_ptr    = FALSE;

   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      //                                                        end offset
      //                                                        |
      //                                                        v
      // buffer: [ <-- old data --> old EOS/DFG <-- new data --> new EOS/DFG ]
      // Convert any old EOS/DFG to non-flushing. internal EOS/DFG can be dropped as they cease to have meanig if data
      // follows them immediately.
      bool_t md_within_offset = (md_ptr->offset < end_offset);

      if (md_within_offset)
      {
#ifdef METADATA_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                         "MD_DBG: Module 0x%lX, metadata within offset node_ptr 0x%p, md_ptr 0x%p md_id 0x%lx, "
                         "end_offset "
                  "%ld, md offset %ld",
                  module_ptr->gu.module_instance_id,
                  node_ptr,
                  md_ptr,
                  md_ptr->metadata_id,
                  end_offset,
                  md_ptr->offset);
#endif

         gen_topo_capi_metadata_modify_at_data_flow_start(module_ptr, node_ptr, md_list_pptr);
      }
      else // Check every remaining node to see if there's any flushing eos left after modifying metadata.
      {
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

            if (MODULE_CMN_MD_EOS_FLUSHING == eos_metadata_ptr->flags.is_flushing_eos)
            {
               *new_marker_eos_ptr = TRUE;
            }
         }
         else if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
         {
            *has_new_dfg_ptr = TRUE;
         }
      }
      node_ptr = next_ptr;
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr ? topo_ptr->gu.log_id : 0)
   {
   }
   return result;
}

/**
 * Modifies the metadata in the module internal metadata list when new data arrives
 * 1. Remove any DFG
 * 2. Remove any internal EOS
 * 3. Change any flushing EOS to non-flushing
 * 4. Reset EOS flushing zeros and EOS/DFG flags
 *
 * There is no offset check here since any metadata part of the internal list already arrived
 * before the new data.
 */
static ar_result_t gen_topo_int_md_list_modify_md_when_new_data_arrives(gen_topo_module_t *module_ptr,
                                                                        uint32_t input_port_id,
                                                                        bool_t *dst_has_flush_eos_ptr,
                                                                        bool_t *dst_has_dfg_ptr)
{
   ar_result_t result = AR_EOK;

   if(!(module_ptr && module_ptr->int_md_list_ptr))
   {
      return result;
   }

   module_cmn_md_list_t **int_md_list_pptr = &module_ptr->int_md_list_ptr;
   module_cmn_md_list_t *node_ptr = *int_md_list_pptr;
   module_cmn_md_list_t *next_ptr = NULL;

   while(node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

#ifdef METADATA_DEBUGGING
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;
      TOPO_MSG_ISLAND(module_ptr->topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "MD_DBG: Module 0x%lX, int_md_list modify_md node_ptr 0x%p, md_ptr 0x%p md_id 0x%lx, "
                      "md offset %ld",
                      module_ptr->gu.module_instance_id,
                      node_ptr,
                      md_ptr,
                      md_ptr->metadata_id,
                      md_ptr->offset);
#endif
      // Directly modify the MD as it is part of the internal list already (old MD)
      gen_topo_capi_metadata_modify_at_data_flow_start(module_ptr, node_ptr, int_md_list_pptr);
      node_ptr = next_ptr;
   }

   // Internal List is not expected have any flushing EOS/DFG anymore. So clear flags & pending_zeros
   *dst_has_flush_eos_ptr = FALSE;
   *dst_has_dfg_ptr = FALSE;

   if (module_ptr->pending_zeros_at_eos)
   {
      TOPO_MSG_ISLAND(module_ptr->topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "MD_DBG: Module 0x%lX, in port id 0x%lx, pending zeros %lu is forcefully made zero as new "
                     "data "
                     "arrived",
                     module_ptr->gu.module_instance_id,
                     input_port_id,
                     module_ptr->pending_zeros_at_eos);

      module_ptr->pending_zeros_at_eos = 0;
   }

   return result;
}

static ar_result_t gen_topo_check_modify_flushing_eos_for_port(gen_topo_input_port_t *in_port_ptr,
                                                               uint32_t               log_id,
                                                               uint32_t               bytes_already_in_dst,
                                                               uint32_t               bytes_copied_from_src,
                                                               bool_t *               dst_has_flush_eos_ptr,
                                                               bool_t *               dst_has_dfg_ptr)
{
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

   // don't reset as we need to preserve values
   //*dst_has_flush_eos_ptr = FALSE;
   //*dst_has_dfg_ptr       = FALSE;

   /**
    *
    * Only if new data is copied we need to change. Metadata corresponding to old data should be already in right shape.
    *
    * if there's flushing EOS at input / internal list, and new data gets copied, change the flushing EOS to
    * non-flushing.
    * don't check for in_port_ptr->common.sdata.flags.marker_eos here. For EP cases, there's a loop
    * in gen_cntr_hw_ep_prepare_input_buffers, and there's also clear marker_eos in gen_cntr_input_data_set_up_peer_cntr
    */
   if ((bytes_copied_from_src > 0) && (module_ptr->int_md_list_ptr || in_port_ptr->common.sdata.metadata_list_ptr))
   {
      /* Handle internal MD list first and modify MD directly without any offset math
       * Consider a scenario with a high algo delay SISO module and flushing EOS in its internal list.
       * Before EOS propagation, data flow resumes on the input. Any MD dependent on data flow state should be
       * updated immediately to avoid unnecessary flushing EOS propagation
       * */
      gen_topo_int_md_list_modify_md_when_new_data_arrives(module_ptr,
                                                           in_port_ptr->gu.cmn.id,
                                                           dst_has_flush_eos_ptr,
                                                           dst_has_dfg_ptr);

      // Process input MD list & modify any old MD when new data is received.
      uint32_t end_offset = 0;

      gen_topo_do_md_offset_math(log_id,
                                 &end_offset,
                                 bytes_already_in_dst + bytes_copied_from_src,
                                 in_port_ptr->common.media_fmt_ptr,
                                 TRUE /* need to add */);


      gen_topo_md_list_modify_md_when_new_data_arrives(module_ptr->topo_ptr,
                                                       module_ptr,
                                                       &in_port_ptr->common.sdata.metadata_list_ptr,
                                                       end_offset,
                                                       dst_has_flush_eos_ptr,
                                                       dst_has_dfg_ptr);

   }

   return result;
}

ar_result_t gen_topo_move_md_from_ext_in_to_int_in_util_(gen_topo_input_port_t *in_port_ptr,
                                                         uint32_t               log_id,
                                                         module_cmn_md_list_t **src_md_lst_pptr,
                                                         uint32_t               bytes_already_in_dst,
                                                         uint32_t               bytes_already_in_src,
                                                         uint32_t               bytes_copied_from_src,
                                                         topo_media_fmt_t *     src_med_fmt_ptr)
{
   ar_result_t result = AR_EOK;

   bool_t dst_received_new_flush_eos = FALSE, src_has_no_flush_eos_anymore = FALSE;
   bool_t dst_received_new_dfg = FALSE, all_dfg_or_flush_eos_moved = FALSE;

   // merge metadata to the new list
   result = gen_topo_move_md(log_id,
                             in_port_ptr,
                             src_md_lst_pptr,
                             bytes_already_in_dst,
                             bytes_already_in_src,
                             bytes_copied_from_src,
                             src_med_fmt_ptr,
                             &dst_received_new_flush_eos,
                             &src_has_no_flush_eos_anymore,
                             &dst_received_new_dfg,
                             &all_dfg_or_flush_eos_moved);

   // if new bytes are copied while in_port had some EOSes in the old md list, then old EOSes should be converted to
   // non-flushing.
   // don't check for marker_eos here see notes inside this func
   // calling after gen_topo_move_md also helps in case any module forgets to convert DFG/EOS in the middle of the
   // buffer.
   // overwriting dst_received_new_flush_eos & dst_received_new_dfg should be ok as below function again determines it
   // correctly.
   //  cases where not overwritten: if bytes_copied_from_src is zero, yet new MD was copied in gen_topo_move_md.
   gen_topo_check_modify_flushing_eos_for_port(in_port_ptr,
                                               log_id,
                                               bytes_already_in_dst,
                                               bytes_copied_from_src,
                                               &dst_received_new_flush_eos,
                                               &dst_received_new_dfg);

   if (dst_received_new_flush_eos)
   {
      // while we are still zero pushing for one flushing EOS, another may come, which needs us to push more zeros
      // if (!in_port_ptr->common.sdata.flags.marker_eos)
      {
    	 gen_topo_module_t * module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
         gen_topo_set_pending_zeros(module_ptr, in_port_ptr);
      }
      in_port_ptr->common.sdata.flags.marker_eos = dst_received_new_flush_eos;
      in_port_ptr->common.sdata.flags.end_of_frame |= dst_received_new_flush_eos;
   }

   // set eof for DFG, otherwise DFG may not move across threshold modules
   in_port_ptr->common.sdata.flags.end_of_frame |= dst_received_new_dfg;

   return result;
}

ar_result_t gen_topo_move_md_from_prev_to_next_util_(gen_topo_module_t *     module_ptr,
                                                     gen_topo_input_port_t * next_in_port_ptr,
                                                     gen_topo_output_port_t *prev_out_port_ptr,
                                                     uint32_t                bytes_to_copy,
                                                     uint32_t                bytes_already_in_prev,
                                                     uint32_t                bytes_already_in_next)
{
   ar_result_t            result         = AR_EOK;
   capi_stream_data_v2_t *next_sdata_ptr = &next_in_port_ptr->common.sdata;
   capi_stream_data_v2_t *prev_sdata_ptr = &prev_out_port_ptr->common.sdata;

   bool_t next_received_new_flush_eos = FALSE, prev_has_no_flush_eos_any_more = FALSE, next_received_new_dfg = FALSE;
   bool_t all_dfg_or_eos_moved = FALSE;

   // see comments in gen_topo_move_md_from_ext_in_to_int_in
   result = gen_topo_move_md(module_ptr->topo_ptr->gu.log_id,
                             next_in_port_ptr,
                             &prev_sdata_ptr->metadata_list_ptr,
                             bytes_already_in_next,
                             bytes_already_in_prev,
                             bytes_to_copy,
                             prev_out_port_ptr->common.media_fmt_ptr,
                             &next_received_new_flush_eos,
                             &prev_has_no_flush_eos_any_more,
                             &next_received_new_dfg,
                             &all_dfg_or_eos_moved);

   // if next_in_port already has any EOS or DFG while new bytes are copied, then old EOSes can be converted to
   // nonflushing.
   gen_topo_check_modify_flushing_eos_for_port(next_in_port_ptr,
                                               module_ptr->topo_ptr->gu.log_id,
                                               bytes_already_in_next,
                                               bytes_to_copy,
                                               &next_received_new_flush_eos,
                                               &next_received_new_dfg);

   // while we are still zero pushing for one flushing EOS, another may come  which needs us to push more zeros to
   // get the second flushing EOS.
   if (next_received_new_flush_eos /*&& !next_sdata_ptr->flags.marker_eos*/)
   {
      gen_topo_set_pending_zeros(module_ptr, next_in_port_ptr);
      next_sdata_ptr->flags.marker_eos = next_received_new_flush_eos;
      next_sdata_ptr->flags.end_of_frame |= next_received_new_flush_eos;
   }

   if (prev_has_no_flush_eos_any_more)
   {
      prev_sdata_ptr->flags.marker_eos = FALSE;
   }

   /* Any DFG MD causes EOF. This is to ensure that threshold modules don't hold DFG.
    * We cannot do this for all buf associated MD because, for other buf associated MD, we might get data later
    * Example TTR MD in voice. TTR could be at 10 ms for a 20 thresh module. setting EOF drops 10 ms.
    * 'OR' is used because we don't want to clear due to EOF set for other reasons. */
   next_sdata_ptr->flags.end_of_frame |= next_received_new_dfg;

   if (all_dfg_or_eos_moved)
   {
      // if flushing eos or DFG moved, reset (data flow gap)
      topo_basic_reset_output_port(module_ptr->topo_ptr, prev_out_port_ptr, TRUE);
   }

   return result;
}

#ifdef ERROR_CHECK_MODULE_PROCESS
/**
 * Validations:
 * offset cannot be > buffer size
 */
ar_result_t gen_topo_validate_metadata_eof(gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      /* For raw compressed and packetized, offset is in bytes.
       * For PCM, offset in samples (per channel).
       * (sample_word_size = bit_width * 8)
       * For deinterleaved data,
       *    metadata is applicable from sample at (channel buffer + (sample_offset * sample_word_size))
       * For interleaved data,
       *    metadata is applicable from sample at (buffer +  (sample_offset * sample_word_size * num channels )
       *
       *    in GEN_CNTR, buf.actual_data_len includes all channels. */

      uint32_t out_size = gen_topo_get_actual_len_for_md_prop(&out_port_ptr->common);
      if (SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format))
      {
         out_size = topo_bytes_to_samples_per_ch(out_size, out_port_ptr->common.media_fmt_ptr);
      }

      module_cmn_md_list_t *node_ptr = out_port_ptr->common.sdata.metadata_list_ptr;
      module_cmn_md_list_t *next_ptr = NULL;
      while (node_ptr)
      {
         next_ptr = node_ptr->next_ptr;

         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

         // check if any offset is updated wrongly (wrap around, not dividing by num channels, using pcm offset
         // for raw compr etc)
         if (md_ptr->offset > out_size)
         {
            TOPO_MSG_ISLAND(module_ptr->topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "MD_DBG: Module 0x%lX, out port 0x%lx, MD 0x%lX, offset error, offset %lu is > out size "
                            "%lu",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            md_ptr->metadata_id,
                            md_ptr->offset,
                            out_size);
         }

         // check if EOF comes out before EOS: it's not possible as we need input EOS, internal list and output
         // list.

         node_ptr = next_ptr;
      } // loop through input_md_list_pptr
   }
   return result;
}
#endif

ar_result_t gen_topo_propagate_metadata_util_(gen_topo_t *       topo_ptr,
                                              gen_topo_module_t *module_ptr,
                                              uint32_t           in_bytes_before,
                                              uint32_t           in_bytes_consumed,
                                              uint32_t           out_bytes_produced,
                                              bool_t             is_one_inp_sink_module)
{
   ar_result_t result = AR_EOK;

   gen_topo_input_port_t * in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
   gen_topo_output_port_t *out_port_ptr =
      module_ptr->gu.output_port_list_ptr ? (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr
                                          : NULL; // NULL for sink
   topo_port_state_t out_port_state = TOPO_PORT_STATE_INVALID;
   topo_media_fmt_t *in_med_fmt_ptr = in_port_ptr->common.media_fmt_ptr;

   uint32_t algo_delay = module_ptr->algo_delay; // disabled module - already handled.

   intf_extn_md_propagation_t input_md_info;
   memset(&input_md_info, 0, sizeof(input_md_info));
   input_md_info.df = gen_topo_convert_spf_data_format_to_capi_data_format(in_med_fmt_ptr->data_format);
   input_md_info.len_per_ch_in_bytes         = BYTES_TO_BYTES_PER_CH(in_bytes_consumed, in_med_fmt_ptr);
   input_md_info.initial_len_per_ch_in_bytes = BYTES_TO_BYTES_PER_CH(in_bytes_before, in_med_fmt_ptr);
   input_md_info.buf_delay_per_ch_in_bytes   = 0; // module does not report buffering delay. so assumed to be zero
   input_md_info.bits_per_sample             = in_med_fmt_ptr->pcm.bits_per_sample;
   input_md_info.sample_rate                 = in_med_fmt_ptr->pcm.sample_rate;

   intf_extn_md_propagation_t output_md_info;
   memset(&output_md_info, 0, sizeof(output_md_info));
   capi_stream_data_v2_t *output_stream_ptr = NULL;
   capi_stream_data_v2_t  output_stream;
   memset(&output_stream, 0, sizeof(output_stream));
   if (is_one_inp_sink_module)
   {
      output_stream_ptr                          = &output_stream; // dummy output for sinks
      output_md_info                             = input_md_info;
      output_md_info.initial_len_per_ch_in_bytes = 0;
   }
   else if (out_port_ptr)
   {
      output_stream_ptr                 = &out_port_ptr->common.sdata;
      topo_media_fmt_t *out_med_fmt_ptr = out_port_ptr->common.media_fmt_ptr;

      output_md_info.df = gen_topo_convert_spf_data_format_to_capi_data_format(out_med_fmt_ptr->data_format);
      output_md_info.len_per_ch_in_bytes       = BYTES_TO_BYTES_PER_CH(out_bytes_produced, out_med_fmt_ptr);
      output_md_info.buf_delay_per_ch_in_bytes = 0; // module does not report buffering delay. so assumed to be zero
      output_md_info.bits_per_sample           = out_med_fmt_ptr->pcm.bits_per_sample;
      output_md_info.sample_rate               = out_med_fmt_ptr->pcm.sample_rate;
      out_port_state                           = out_port_ptr->common.state;
   }

   gen_topo_capi_metadata_propagate((void *)module_ptr,
                                    &in_port_ptr->common.sdata,
                                    output_stream_ptr,
                                    &module_ptr->int_md_list_ptr,
                                    algo_delay,
                                    &input_md_info,
                                    &output_md_info);

   // if EOS is stuck at input, then don't propagate EOF (fir_filter_nt_2)
   // this is undoing the changes done by gen_topo_handle_end_of_frame_after_process
   if (in_port_ptr->common.sdata.flags.marker_eos)
   {
      in_port_ptr->common.sdata.flags.end_of_frame = TRUE;
      if (out_port_ptr)
      {
         out_port_ptr->common.sdata.flags.end_of_frame = FALSE;
      }
   }

   /*
    * 1. drop the metadata for one-input-sink module
    * 2. drop the metadata for siso module if output port is not started.
    * 	this can happens if module sets minimum_output_port as zero and output port is stopped
    */
   if (is_one_inp_sink_module || (TOPO_PORT_STATE_STARTED != out_port_state))
   {
      result = gen_topo_metadata_prop_for_sink(module_ptr, output_stream_ptr);
   }

   return result;
}

ar_result_t gen_topo_drop_all_metadata_within_range(uint32_t                log_id,
                                                    gen_topo_module_t *     module_ptr,
                                                    gen_topo_common_port_t *cmn_port_ptr,
                                                    uint32_t                data_dropped_bytes,
                                                    bool_t                  keep_eos_and_ba_md)
{
   ar_result_t result = AR_EOK;

   if (NULL == cmn_port_ptr->sdata.metadata_list_ptr)
   {
      return result;
   }

   intf_extn_md_propagation_t input_md_info;
   memset(&input_md_info, 0, sizeof(input_md_info));
   topo_media_fmt_t *in_med_fmt_ptr = cmn_port_ptr->media_fmt_ptr;
   input_md_info.df                 = gen_topo_convert_spf_data_format_to_capi_data_format(in_med_fmt_ptr->data_format);
   input_md_info.len_per_ch_in_bytes = BYTES_TO_BYTES_PER_CH(data_dropped_bytes, in_med_fmt_ptr);
   // initial length refers to what was present before process was called, in this case it is size of data that was
   // dropped
   input_md_info.initial_len_per_ch_in_bytes = input_md_info.len_per_ch_in_bytes;
   input_md_info.buf_delay_per_ch_in_bytes   = 0; // module does not report buffering delay. so assumed to be zero
   input_md_info.bits_per_sample             = in_med_fmt_ptr->pcm.bits_per_sample;
   input_md_info.sample_rate                 = in_med_fmt_ptr->pcm.sample_rate;

   intf_extn_md_propagation_t output_md_info;
   memset(&output_md_info, 0, sizeof(output_md_info));

   capi_stream_data_v2_t output_stream;
   memset(&output_stream, 0, sizeof(output_stream));
   output_md_info                             = input_md_info;
   output_md_info.initial_len_per_ch_in_bytes = 0;

   // create a temporary list which stores all buffer associated md and eos
   // and remove that md from input list before calling propagate function
   // This is needed to ensure dfg, eos, start end md are not dropped in certain cases
   module_cmn_md_list_t * temp_list_ptr    = NULL;
   capi_stream_data_v2_t *input_stream_ptr = &cmn_port_ptr->sdata;

   if (keep_eos_and_ba_md)
   {
      module_cmn_md_list_t *node_ptr = input_stream_ptr->metadata_list_ptr;
      module_cmn_md_list_t *next_ptr = NULL;
      while (node_ptr)
      {
         next_ptr                = node_ptr->next_ptr;
         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

         if ((MODULE_CMN_MD_BUFFER_ASSOCIATED == md_ptr->metadata_flag.buf_sample_association) ||
             (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id))
         {
            spf_list_move_node_to_another_list((spf_list_node_t **)&(temp_list_ptr),
                                               (spf_list_node_t *)node_ptr,
                                               (spf_list_node_t **)&(input_stream_ptr->metadata_list_ptr));
#ifdef METADATA_DEBUGGING
            TOPO_MSG_ISLAND(log_id,
                     DBG_MED_PRIO,
                     "MD_DBG: Module 0x%lX, metadata node_ptr 0x%p, md_ptr 0x%p md_id 0x%lx "
                     "keeping md in input list, not dropping",
                     module_ptr->gu.module_instance_id,
                     node_ptr,
                     md_ptr,
                     md_ptr->metadata_id);
#endif
         }
         node_ptr = next_ptr;
      }
   }

   // filters all metadata that's getting dropped.
   gen_topo_capi_metadata_propagate((void *)module_ptr,
                                    &cmn_port_ptr->sdata,
                                    &output_stream,
                                    NULL, /** for zero algo delay we dont need internal MD. */
                                    0 /*algo_delay*/,
                                    &input_md_info,
                                    &output_md_info);

   // Move all the md back to input
   if (keep_eos_and_ba_md)
   {
      spf_list_merge_lists((spf_list_node_t **)&(input_stream_ptr->metadata_list_ptr),
                           (spf_list_node_t **)&(temp_list_ptr));
   }

   gen_topo_destroy_all_metadata(log_id, module_ptr, &output_stream.metadata_list_ptr, TRUE /* is_dropped */);

   return result;
}

ar_result_t gen_topo_destroy_all_metadata(uint32_t               log_id,
                                          void *                 module_ctx_ptr,
                                          module_cmn_md_list_t **md_list_pptr,
                                          bool_t                 is_dropped)
{
   ar_result_t           result     = AR_EOK;
   gen_topo_module_t *   module_ptr = (gen_topo_module_t *)module_ctx_ptr;
   module_cmn_md_list_t *node_ptr   = *md_list_pptr;
   module_cmn_md_list_t *next_ptr;

#if defined(METADATA_DEBUGGING)
   if (*md_list_pptr)
   {
      TOPO_MSG_ISLAND(log_id,
               DBG_LOW_PRIO,
               "MD_DBG: Destroy metadata for module 0x%lX, list ptr 0x%p, is_dropped %u",
               module_ptr->gu.module_instance_id,
               *md_list_pptr,
               is_dropped);
   }
#endif

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;
      // stream associated remains in the list and is not destroyed. hence md_list_pptr cannot be made NULL here.
      gen_topo_capi_metadata_destroy((void *)module_ptr, node_ptr, is_dropped, md_list_pptr, 0, FALSE);
      node_ptr = next_ptr;
   }
   return result;
}

// repackage metadata. repackaging is done only in NLPI container (at the boundary of NLPI -> LPI)
// tracking doesn't matter as tracked pointers in LPI
ar_result_t gen_topo_check_realloc_md_list_in_peer_heap_id(uint32_t               log_id,
                                                           gu_ext_out_port_t *    ext_out_port_ptr,
                                                           module_cmn_md_list_t **md_list_pptr)
{
   module_cmn_md_list_t *node_ptr                = *md_list_pptr;
   module_cmn_md_list_t *next_ptr                = NULL;
   POSAL_HEAP_ID         downstream_heap_id      = ext_out_port_ptr->downstream_handle.heap_id;
   bool_t                is_downstream_low_power = POSAL_IS_ISLAND_HEAP_ID(downstream_heap_id);

   for (; NULL != node_ptr; (node_ptr = next_ptr))
   {
      next_ptr = node_ptr->next_ptr;

      // if the list node is allocated from the correct heap, the obj/md will be too.
      // so we don't need to check md_ptr->metadata_ptr as well..
      // if node is already from downstream heap, we can continue.
      // IF NOT:
      // 1) ....if downstream heap is LPI heap - we have to repackage.
      // 2) ....if not, we don't have to since we're already in LPI heap
      if (!is_downstream_low_power || spf_list_node_is_addr_from_heap(node_ptr, downstream_heap_id))
      {
         continue;
      }

      //we come here only for NLPI
      gen_topo_realloc_md_list_in_peer_heap_id(log_id, md_list_pptr, node_ptr, downstream_heap_id);
   }
   return AR_EOK;
}

void gen_topo_populate_metadata_for_peer_cntr(gen_topo_t *           gen_topo_ptr,
                                              gu_ext_out_port_t *    ext_out_port_ptr,
                                              module_cmn_md_list_t **md_list_pptr,
                                              module_cmn_md_list_t **out_md_list_pptr,
                                              bool_t *               out_buf_has_flushing_eos_ptr)
{
   *out_buf_has_flushing_eos_ptr = FALSE;

   if ((!md_list_pptr) || (!(*md_list_pptr)))
   {
      return;
   }

   module_cmn_md_list_t *node_ptr = *md_list_pptr;
   module_cmn_md_list_t *next_ptr = NULL;

   if (!ext_out_port_ptr->downstream_handle.spf_handle_ptr)
   {
      // detach, and free all metadata
      TOPO_MSG_ISLAND(gen_topo_ptr->gu.log_id, DBG_HIGH_PRIO, "MD_DBG: downstream not connected. dropping metadata");
      gen_topo_destroy_all_metadata(gen_topo_ptr->gu.log_id,
                                    (void *)ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr,
                                    md_list_pptr,
                                    TRUE /* is_dropped */);
      return;
   }

   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      // do not detach, only free container ref ptr
      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
     	 //Exit island here since we need to do a mem free operation which is in nlpi
         module_cmn_md_eos_t *eos_metadata_ptr = NULL;

         uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;
         if (is_out_band)
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }
         if (eos_metadata_ptr->flags.is_flushing_eos)
         {
            *out_buf_has_flushing_eos_ptr = TRUE;

            // Check if container voting changes need to be deferred for this Flushing EOS.
            gen_topo_check_eos_md_update_defer_voting_flag(&ext_out_port_ptr->int_out_port_ptr->cmn, md_ptr);
         }

         // should free only contr ref ptr. others should stay for next containers use
         gen_topo_free_eos_cargo(gen_topo_ptr, md_ptr, eos_metadata_ptr);
      }

#if defined(METADATA_DEBUGGING)
      TOPO_MSG_ISLAND(gen_topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "MD_DBG: Sending metadata to downstream node_ptr 0x%p, md_id 0x%08lX, offset %lu",
               node_ptr,
               md_ptr->metadata_id,
               md_ptr->offset);
#endif

      node_ptr = next_ptr;
   }

   *out_md_list_pptr = *md_list_pptr;
   *md_list_pptr     = NULL;

   thin_topo_count_and_decr_active_md_nodes(gen_topo_ptr, *out_md_list_pptr);
}

/**
 * return first md by the given Id
 */
static module_cmn_md_t *gen_topo_md_list_find_md_by_id(module_cmn_md_list_t *list_ptr,
                                                       uint32_t              metadata_id1,
                                                       uint32_t              metadata_id2)
{
   module_cmn_md_t *md_ptr = NULL;

   while (list_ptr)
   {
      md_ptr = list_ptr->obj_ptr;

      if ((metadata_id1 == md_ptr->metadata_id) || ((0 != metadata_id2) && (metadata_id2 == md_ptr->metadata_id)))
      {
         return md_ptr;
      }

      list_ptr = list_ptr->next_ptr;
   }

   return NULL;
}

bool_t gen_topo_md_list_has_dfg(module_cmn_md_list_t *list_ptr)
{
   return (NULL != gen_topo_md_list_find_md_by_id(list_ptr, MODULE_CMN_MD_ID_DFG, 0));
}

bool_t gen_topo_md_list_has_flushing_eos(module_cmn_md_list_t *list_ptr)
{
   return gen_topo_is_flushing_eos(gen_topo_md_list_find_md_by_id(list_ptr, MODULE_CMN_MD_ID_EOS, 0));
}

bool_t gen_topo_md_list_has_flushing_eos_or_dfg(module_cmn_md_list_t *list_ptr)
{
   module_cmn_md_t *md_ptr = gen_topo_md_list_find_md_by_id(list_ptr, MODULE_CMN_MD_ID_DFG, MODULE_CMN_MD_ID_EOS);
   if (md_ptr && (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id))
   {
      return gen_topo_is_flushing_eos(md_ptr);
   }
   return (NULL != md_ptr);
}

/**
 * Exposed through gen_topo.h. doesn't populate metadata ID
 */
ar_result_t gen_topo_metadata_create(gen_topo_t            *topo_ptr,
                                     module_cmn_md_list_t **md_list_pptr,
                                     uint32_t               size,
                                     POSAL_HEAP_ID          heap_id,
                                     bool_t                 is_out_band,
                                     module_cmn_md_t **     md_pptr)
{
   ar_result_t ar_result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t md_size = sizeof(module_cmn_md_t);
   uint32_t log_id  = topo_ptr->gu.log_id;

   if (NULL == md_list_pptr)
   {
      return ar_result;
   }

   if (NULL == md_pptr)
   {
      return CAPI_EBADPARAM;
   }
   *md_pptr = NULL;
   module_cmn_md_t *md_ptr;
   void *           md_payload_ptr = NULL;
   spf_list_node_t *tail_node_ptr  = NULL;
   bool_t           is_island_heap = POSAL_IS_ISLAND_HEAP_ID(heap_id);

   // for in-band do only one malloc
   if (!is_out_band)
   {
      md_size = MODULE_CMN_MD_INBAND_GET_REQ_SIZE(size);
   }

   /*Malloc/Free APIs are not available in LPI - Therefore we need to get/return node from/to the pool instead*/
   md_ptr = is_island_heap ? (module_cmn_md_t *)spf_lpi_pool_get_node(md_size)
                           : (module_cmn_md_t *)posal_memory_malloc(md_size, heap_id);

   VERIFY(ar_result, NULL != md_ptr);

//For inband with size 0, the md_ptr is malloced with sizeof(module_cmn_md_t) – 8 byte
   memset((void *)md_ptr, 0, MIN(md_size, sizeof(module_cmn_md_t))); // memset only top portion as size may be huge

   if (is_out_band)
   {
      if (size)
      {
         md_payload_ptr = is_island_heap ? spf_lpi_pool_get_node(size) : posal_memory_malloc(size, heap_id);
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

   TRY(ar_result, spf_list_insert_tail((spf_list_node_t **)md_list_pptr, md_ptr, heap_id, TRUE /* use pool */));

   spf_list_get_tail_node((spf_list_node_t *)*md_list_pptr, &tail_node_ptr);
#if defined(METADATA_DEBUGGING)
   TOPO_MSG_ISLAND(log_id,
            DBG_MED_PRIO,
            "MD_DBG: Metadata create: spf_list_node_t 0x%p host md_ptr 0x%p",
            tail_node_ptr,
            md_ptr);
#endif

   md_ptr->metadata_flag.is_out_of_band = is_out_band;

   CATCH(ar_result, TOPO_MSG_PREFIX, log_id)
   {
      if (is_out_band)
      {
         gen_topo_check_free_md_ptr(&md_payload_ptr, is_island_heap);
      }
      gen_topo_check_free_md_ptr((void **)&md_ptr, is_island_heap);
      // No errors after inserting to linked list
   }

   *md_pptr = md_ptr;

   // Increment md counter as the last step, so that if there was any error MD counter incremented can be skipped.
   if (ar_result == AR_EOK)
   {
      thin_topo_incr_active_md_nodes(topo_ptr, (module_cmn_md_list_t *)tail_node_ptr);
   }

   return ar_result;
}

/**
 * Moves metadata from src_list_head_pptrmetadata (internal list or input list) to the output list.
 * returns TRUE if md was moved to output list.
 */
static bool_t gen_topo_move_md_to_out_list(gen_topo_module_t *    module_ptr,
                                           module_cmn_md_list_t * node_ptr,
                                           module_cmn_md_list_t **output_md_list_pptr,
                                           module_cmn_md_list_t **src_list_head_pptr,
                                           uint32_t               out_produced_per_ch,
                                           uint32_t               out_initial_len_per_ch,
                                           uint32_t               in_consumed_per_ch,
                                           data_format_t          in_df,
                                           data_format_t          out_df,
                                           bool_t                 flushing_eos)
{
   gen_topo_t *     topo_ptr               = module_ptr->topo_ptr;
   module_cmn_md_t *md_ptr                 = node_ptr->obj_ptr;
   uint32_t         offset_before          = md_ptr->offset;
   bool_t           pcmpack_in_pcmpack_out = CAPI_IS_PCM_PACKETIZED(out_df) && CAPI_IS_PCM_PACKETIZED(in_df);
   bool_t           move_to_out            = TRUE;

   // squeezing behavior for decoder like module only
   // For WR EP or such modules b4 decoder, doing squeezing behavior prevents EOS from reaching decoder separate from
   // buf. this causes gapless to fail (where last chunk of data should be received along with EOS in order to remove
   //   trailing silence)
   if (flushing_eos)
   {
      if ((AMDB_MODULE_TYPE_DECODER == module_ptr->gu.module_type) ||
          (AMDB_MODULE_TYPE_PACKETIZER == module_ptr->gu.module_type) ||
          (AMDB_MODULE_TYPE_DEPACKETIZER == module_ptr->gu.module_type))
      {
         move_to_out = (0 == out_produced_per_ch);
      }
   }

   // md_ptr->offset is input scale

   if (move_to_out && (md_ptr->offset <= in_consumed_per_ch))
   {
      if (pcmpack_in_pcmpack_out)
      {
         // If no input was consumed, move md to start or end of buffer based on is_begin_associated_md flag.
         if(0 == in_consumed_per_ch)
         {
            if(md_ptr->metadata_flag.is_begin_associated_md)
            {
               md_ptr->offset = 0;
            }
            else
            {
               md_ptr->offset = out_produced_per_ch;
            }
         }
         else
         {
            // scale based on out len produced for a given input len, so that duration modifying modules are covered.
            // For non-DM modules, this works the same as using sample rates in place of lengths
            uint32_t offset_in_out_scale = TOPO_CEIL(md_ptr->offset * out_produced_per_ch, in_consumed_per_ch);

            // Due to some precision issues offset shouldn't go outside.
            md_ptr->offset = MIN(out_produced_per_ch, offset_in_out_scale);
         }
      }
      else
      {
         if(md_ptr->metadata_flag.is_begin_associated_md)
         {
            /* If offset points to end of input buf, then after moving data to the output it should point to end of output
             * buf. Interpret no input case as md is at start of input buffer for is_beginning_associated_md = TRUE.
             */
            if ((0 != in_consumed_per_ch) && (md_ptr->offset == in_consumed_per_ch))
            {
               md_ptr->offset = out_produced_per_ch;
            }
            else /* If offset is at the start of input buffer*/
            {
               md_ptr->offset = 0;
            }
         }
         else
         {
            /* If offset points to end of input buf, then after moving data to the output it should point to end of output
             * buf*/
            if (md_ptr->offset == in_consumed_per_ch)
            {
               md_ptr->offset = out_produced_per_ch;
            }
            else /* If offset is at the start of input buffer*/
            {
               md_ptr->offset = 0;
            }
         }
      }

      // Newly generated output goes after data that was initially in the output, so incorporate initial data length
      // into the md offset.
      md_ptr->offset += out_initial_len_per_ch;

      spf_list_move_node_to_another_list((spf_list_node_t **)output_md_list_pptr,
                                         (spf_list_node_t *)node_ptr,
                                         (spf_list_node_t **)src_list_head_pptr);
#ifdef METADATA_DEBUGGING
      PRINT_MD_PROP_DBG_ISLAND("moving to output list",
                               "in_consumed",
                               in_consumed_per_ch,
                               "out_produced_per_ch %lu, out_initial_len_per_ch %lu",
                               out_produced_per_ch,
                               out_initial_len_per_ch);
#else
      (void)offset_before;
#endif
      if (module_ptr->capi_ptr && flushing_eos && gen_topo_fwk_owns_md_prop(module_ptr) &&
          ((AMDB_MODULE_TYPE_DECODER == module_ptr->gu.module_type) ||
           (AMDB_MODULE_TYPE_ENCODER == module_ptr->gu.module_type) ||
           (AMDB_MODULE_TYPE_PACKETIZER == module_ptr->gu.module_type) ||
           (AMDB_MODULE_TYPE_DEPACKETIZER == module_ptr->gu.module_type) ||
           (AMDB_MODULE_TYPE_CONVERTER == module_ptr->gu.module_type)))
      {
         gen_topo_exit_island_temporarily(topo_ptr);
         // in elite, Eos resulted in algo reset for dec/enc/pack/unpack, continue the same behavior for
         // CAPI backward compat
         gen_topo_capi_algorithmic_reset(topo_ptr->gu.log_id,
                                         module_ptr->capi_ptr,
                                         FALSE /*is_port_valid*/,
                                         FALSE /*is_input*/,
                                         0);
      }
      // moved to output list
      return TRUE;
   }
   else
   {
      // didn't move to output list
      return FALSE;
   }
}

/**
 * this is called after MD/EOS comes to output port, hence always free.
 */
ar_result_t gen_topo_metadata_prop_for_sink(gen_topo_module_t *module_ptr, capi_stream_data_v2_t *output_stream_ptr)
{
   ar_result_t result = AR_EOK;

   result = gen_topo_destroy_all_metadata(module_ptr->topo_ptr->gu.log_id,
                                          (void *)module_ptr,
                                          &output_stream_ptr->metadata_list_ptr,
                                          FALSE /* is_dropped */);

   gen_topo_t *topo_ptr                                 = module_ptr->topo_ptr;
   topo_ptr->proc_context.process_info.anything_changed = TRUE;

   return result;
}

/**
 * these functions are exposed to modules through capiv2 interface extensions
 */
/**
 * doesn't populate metadata ID
 */
capi_err_t gen_topo_capi_metadata_create(void *                 context_ptr,
                                         module_cmn_md_list_t **md_list_pptr,
                                         uint32_t               size,
                                         capi_heap_id_t         c_heap_id,
                                         bool_t                 is_out_band,
                                         module_cmn_md_t **     md_pptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   TRY(result,
       ar_result_to_capi_err(gen_topo_metadata_create(topo_ptr,
                                                      md_list_pptr,
                                                      size,
                                                      (POSAL_HEAP_ID)c_heap_id.heap_id,
                                                      is_out_band,
                                                      md_pptr)));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_topo_free_md(gen_topo_t *           topo_ptr,
                             module_cmn_md_list_t * md_list_ptr,
                             module_cmn_md_t *      md_ptr,
                             module_cmn_md_list_t **head_pptr)
{
   ar_result_t result    = AR_EOK;
   bool_t      pool_used = FALSE;

   if (md_ptr)
   {
      // generic metadata is assumed to not require deep cloning
      uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;
      if (is_out_band)
      {
         pool_used = spf_lpi_pool_is_addr_from_md_pool(md_ptr->metadata_ptr);

         //If it is not allocated from island pool, we need to exit island to call mem free
         if(!pool_used)
         {
        	gen_topo_exit_island_temporarily(topo_ptr);
         }
         gen_topo_check_free_md_ptr(&(md_ptr->metadata_ptr), pool_used);
      }
   }

   if (md_list_ptr)
   {
      pool_used = spf_lpi_pool_is_addr_from_md_pool(md_list_ptr->obj_ptr);

      //If it is not allocated from island pool, we need to exit island to call mem free
      if(!pool_used)
      {
     	gen_topo_exit_island_temporarily(topo_ptr);
      }

      gen_topo_check_free_md_ptr((void **)&(md_list_ptr->obj_ptr), pool_used);

      thin_topo_decr_active_md_nodes(topo_ptr, md_list_ptr);

      spf_list_delete_node_update_head((spf_list_node_t **)&md_list_ptr,
                                       (spf_list_node_t **)head_pptr,
                                       TRUE /* pool_used*/);
   }

   return result;
}

/*
 * frees up metadata related memories and for specific ones, also takes care of ref counting.
 * context_ptr - context pointer
 * in_md_list_pptr  - pointer to the pointer to the list node which needs to be terminated.
 *    if this list belongs to a linked list, then it's neighboring nodes are properly updated upon destroy.
 *
 * is_dropped - some metadata (EOS) may result in different behavior for drop vs. consumption (render eos).
 */
capi_err_t gen_topo_capi_metadata_destroy(void *                 context_ptr,
                                          module_cmn_md_list_t * md_list_ptr,
                                          bool_t                 is_dropped,
                                          module_cmn_md_list_t **head_pptr,
                                          uint32_t               md_actual_sink_miid,
										  bool_t                 override_ctrl_to_disable_tracking_event)
{
   capi_err_t         result     = CAPI_EOK;
   ar_result_t        ar_result  = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;
   uint32_t           md_sink_miid = 0;

   if (NULL == md_list_ptr)
   {
      return result;
   }

   module_cmn_md_t *md_ptr = (module_cmn_md_t *)md_list_ptr->obj_ptr;

   // metadata such as EOS and DFG should not be dropped if dropping is due to a stall.
   // but currently no such scenario. Hence always destroy EOS and DFG even though they carry info
   // about data flow state.

   md_sink_miid = (md_actual_sink_miid) ? md_actual_sink_miid : module_ptr->gu.module_instance_id;
   switch (md_ptr->metadata_id)
   {
      case MODULE_CMN_MD_ID_EOS:
      {
    	   //Exit island here since we need to do a mem free operation which is in nlpi
         ar_result = gen_topo_respond_and_free_eos(topo_ptr,
                                                   md_sink_miid,
                                                   md_list_ptr,
                                                   !is_dropped,
                                                   head_pptr,
                                                   override_ctrl_to_disable_tracking_event);
         break;
      }
      default: // For MODULE_CMN_MD_ID_DFG, this is sufficient
      {
#ifdef METADATA_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MD_DBG: destroy, node_ptr 0x%p, metadata_ptr 0x%p, md_id 0x%08lX",
                  md_list_ptr,
                  md_ptr,
                  md_ptr->metadata_id);
#endif

         if (NULL != md_ptr->tracking_ptr)
         {
            // Exit island here since we need to do a mem free operation which is in nlpi
            gen_topo_exit_island_temporarily(topo_ptr);
            gen_topo_raise_tracking_event(topo_ptr,
                                          md_sink_miid,
                                          md_list_ptr,
                                          !is_dropped,
                                          NULL,
                                          override_ctrl_to_disable_tracking_event);
         }

         //Dont need to exit island here since gen_topo_free_md can return nodes in island as well
         //if md node belongs to lpi pool
         ar_result = gen_topo_free_md(topo_ptr, md_list_ptr, md_ptr, head_pptr);
      }
   }

   result = ar_result_to_capi_err(ar_result);

   return result;
}

/**
 * Segregates the input metadata into 2 lists: output list and a list internal to the module
 * by comparing the offsets. internal_md_list_pptr can be given in subsequent calls.
 *
 * There are two types of metadata - offset associated/buffer associated.
 *
 * ---------------------------- SAMPLE ASSOCIATED METADATA ------------------------------------------------------------
 * Works for all combinations input-pcm & output-pcm, input-non-pcm & output-non-pcm.
 *
 * puts metadata on o/p port only when corresponding data is in the o/p buffer, i.e., offset cannot be
 * outside actual_data_len. If outside, then module needs to hold internally.
 *
 * Example1:
 *    7 ms input, 3ms offset of metadata1, 6ms offset of metadata2, 5ms input threshold. 6 ms algo delay. 5 ms out buf.
 *       first process call:
 *          5 ms is consumed, and sent to output. metadata1 is stored in internal list with offset =(3+6)-5=4ms.
 *          metadata2 is left in the input list as it's outside the consumed data.
 *          metadata1 is within input consumed, but module cannot send out as metadata offset
 *          would be outside the buf (9 ms).
 *       next process: input = 2 ms old data + x ms new data. container should've updated the metadata2 offset to 1 ms.
 *          metadata 1 will go to output with offset 4 ms (bcoz 4 < out buf 5 ms).
 *          metadata 2 will go to internal list with offset (1+6)-5=2.
 *
 * Since input and output sample rates may be different offsets need to be scaled.
 * internal_md_list_pptr objects are at the output scale.
 *
 * For non-pcm, algo delay is zero. non-pcm cases, metadata can go internal_md_list_pptr due to squeezing behavior.
 *
 * For PCM, All channels must have same number of samples.
 *
 * EOS has special behavior.
 *  - EOS offset always goes to last byte on the output buffer.
 *  - Squeezing behavior is required for non-generic modules. Flushing EOS is not propagated until module stops
 * producing data.
 *     For other module types, if this behavior is needed, then they need to copy EoS internally inside CAPIv2, &
 *     set to output once squeezing is over.  We don't need do this squeezing behavior in GEN_CNTR (underflow)
 *     because GEN_CNTR always underruns and pushes zeros.
 *  - input EOS flag is cleared & output EOS flag set only after EOS propagates out of input and internal list to output
 * list.
 *     When EOS is stuck inside due to algo delay or squeezing behavior, input-flag marker_eos is not cleared.
 *
 * ---------------------------- BUFFER ASSOCIATED METADATA ------------------------------------------------------------
 *   When the metadata is buffer associated, algo delay does not affect the offset.
 *     E.g :- MODULE_CMN_MD_ID_DFG
 */
capi_err_t gen_topo_capi_metadata_propagate(void *                      context_ptr,
                                            capi_stream_data_v2_t *     input_stream_ptr,
                                            capi_stream_data_v2_t *     output_stream_ptr,
                                            module_cmn_md_list_t **     internal_md_list_pptr,
                                            uint32_t                    algo_delay_us,
                                            intf_extn_md_propagation_t *input_md_info_ptr,
                                            intf_extn_md_propagation_t *output_md_info_ptr)
{
   capi_err_t result = CAPI_EOK;

   if ((NULL == input_stream_ptr) || (NULL == context_ptr) || (NULL == output_stream_ptr))
   {
      return CAPI_EBADPARAM;
   }

   gen_topo_module_t *    module_ptr               = (gen_topo_module_t *)context_ptr;
   gen_topo_t *           topo_ptr                 = module_ptr->topo_ptr;
   module_cmn_md_list_t **input_md_list_pptr       = &input_stream_ptr->metadata_list_ptr;
   module_cmn_md_list_t **output_md_list_pptr      = &output_stream_ptr->metadata_list_ptr;
   uint32_t               in_consumed_per_ch       = input_md_info_ptr->len_per_ch_in_bytes;
   uint32_t               out_produced_per_ch      = output_md_info_ptr->len_per_ch_in_bytes;
   uint32_t               out_initial_len_per_ch   = output_md_info_ptr->initial_len_per_ch_in_bytes;
   uint32_t               buf_in_len_per_ch        = input_md_info_ptr->buf_delay_per_ch_in_bytes;
   bool_t                 any_eos_stuck            = FALSE; // any flushing eos stuck is inside module or at input.
   bool_t                 any_eos_moved            = FALSE; // any flushing eos moved to output port
   bool_t                 any_flushing_eos_present = FALSE; // whether flushing eos is present in the lists.
   uint32_t               algo_delay_len_in        = 0;     // Algo delay in input scale

   /**< Indicates whether data is present or not on input/output.
    When len_per_ch_in_bytes is zero, we have an ambiguity about whether data was present
    and it's not consumed (produced) or whether data was not present at all.
    E.g. if there's no data but metadata was present, then offset is zero, len_per_ch_in_bytes and MD
    propagates ahead, but if there's data but no data is consumed, then metadata must not move ahead
    as data is not consumed. */

   if ((NULL == *input_md_list_pptr) && ((NULL == internal_md_list_pptr) || (NULL == *internal_md_list_pptr)))
   {
#ifdef METADATA_DEBUGGING
//      TOPO_MSG_ISLAND(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Both input and internal metadata list are NULL. returning.");
#endif
      return CAPI_EOK;
   }

   if (input_md_info_ptr->initial_len_per_ch_in_bytes < in_consumed_per_ch)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "MD_DBG: Module 0x%lX, data consumed %lu is more than initial data %lu",
               module_ptr->gu.module_instance_id,
               in_consumed_per_ch,
               input_md_info_ptr->initial_len_per_ch_in_bytes);
   }

   if ((CAPI_MAX_FORMAT_TYPE == input_md_info_ptr->df) || (CAPI_MAX_FORMAT_TYPE == output_md_info_ptr->df))
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "MD_DBG: data formats are not valid for module 0x%lX. Input %d, Output %d",
               module_ptr->gu.module_instance_id,
               input_md_info_ptr->df,
               output_md_info_ptr->df);
      return CAPI_EBADPARAM;
   }

   if (CAPI_IS_PCM_PACKETIZED(input_md_info_ptr->df))
   {
      if ((0 == input_md_info_ptr->bits_per_sample) || (0 == input_md_info_ptr->sample_rate))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "MD_DBG: input formats are not valid for module 0x%lX. bits per sample %lu, sample rate %lu",
                  module_ptr->gu.module_instance_id,
                  input_md_info_ptr->bits_per_sample,
                  input_md_info_ptr->sample_rate);
         return CAPI_EBADPARAM;
      }
      in_consumed_per_ch = capi_cmn_divide(in_consumed_per_ch , (input_md_info_ptr->bits_per_sample >> 3));// in samples
      algo_delay_len_in  = topo_us_to_samples(algo_delay_us, input_md_info_ptr->sample_rate);
      buf_in_len_per_ch  = capi_cmn_divide(buf_in_len_per_ch , (input_md_info_ptr->bits_per_sample >> 3)); // in samples
   }

   if (CAPI_IS_PCM_PACKETIZED(output_md_info_ptr->df))
   {
      if ((0 == output_md_info_ptr->bits_per_sample) || (0 == output_md_info_ptr->sample_rate))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "MD_DBG: input formats are not valid for module 0x%lX. bits per sample %lu, sample rate %lu",
                  module_ptr->gu.module_instance_id,
                  output_md_info_ptr->bits_per_sample,
                  output_md_info_ptr->sample_rate);
         return CAPI_EBADPARAM;
      }

      out_produced_per_ch    = capi_cmn_divide(out_produced_per_ch , (output_md_info_ptr->bits_per_sample >> 3));    // in samples
      out_initial_len_per_ch = capi_cmn_divide(out_initial_len_per_ch , (output_md_info_ptr->bits_per_sample >> 3)); // in samples
   }

   /**
    * Algorithm:
    * first from internal_md_list_pptr to output list if offset is within out's range or has any buffer associated md
    * then go thru input list and copy to output if it exists or to internal if it exists or else,
    * update offset in input list itself.
    */
   if (internal_md_list_pptr)
   {
      module_cmn_md_list_t *node_ptr = (*internal_md_list_pptr);
      module_cmn_md_list_t *next_ptr = NULL;

      while (node_ptr)
      {
         next_ptr = node_ptr->next_ptr;

         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

         bool_t   flushing_eos  = gen_topo_is_flushing_eos(md_ptr);
         uint32_t offset_before = md_ptr->offset;

         bool_t md_moved = FALSE;

         // Try to move MD from internal list to output list.
         md_moved = gen_topo_move_md_to_out_list(module_ptr,
                                                 node_ptr,
                                                 output_md_list_pptr,
                                                 internal_md_list_pptr,
                                                 out_produced_per_ch,
                                                 out_initial_len_per_ch,
                                                 in_consumed_per_ch,
                                                 input_md_info_ptr->df,
                                                 output_md_info_ptr->df,
                                                 flushing_eos);

         any_flushing_eos_present |= flushing_eos;
         any_eos_moved |= (flushing_eos && md_moved);

         // if not moved, then update offset in the intermediate list.
         if (!md_moved)
         {
            any_eos_stuck |= flushing_eos;
            md_ptr->offset -= in_consumed_per_ch;
#ifdef METADATA_DEBUGGING
            PRINT_MD_PROP_DBG_ISLAND("updating offset on internal list", "in_consumed", in_consumed_per_ch, "");
#else
            (void)offset_before;
#endif
         }

         node_ptr = next_ptr;
      }
   }

   module_cmn_md_list_t *node_ptr = (*input_md_list_pptr);
   module_cmn_md_list_t *next_ptr = NULL;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *md_ptr        = node_ptr->obj_ptr;
      uint32_t         offset_before = md_ptr->offset;
      bool_t           flushing_eos  = gen_topo_is_flushing_eos(md_ptr);
      bool_t           md_moved      = FALSE;
      uint32_t         is_ba_md = (MODULE_CMN_MD_BUFFER_ASSOCIATED == md_ptr->metadata_flag.buf_sample_association);

      any_flushing_eos_present |= flushing_eos;

      // If offset of metadata belongs to data that's consumed, then move MD also considered consumed by the module
      // If input is not present, we can propagate only if offset is zero.
      // When input consumed is zero, MD is moved only if input was zero to begin with; otherwise, even if offset is
      // zero, MD is not propagated.
      // Propagated MD will suffer (algo delay +) buffering delay before propagating to output.
      // E.g. in TRM, input is present, no data is consumed, MD shouldn't propagate.
      // However, normally when input is not present, we need to still propagate MD when offset is zero.
      // md_ptr->offset doesn't include zero pushed len. i.e., zero pushing doesn't adjust offset.
      if (gen_topo_check_move_metadata(md_ptr->offset,
                                       input_md_info_ptr->initial_len_per_ch_in_bytes,
                                       in_consumed_per_ch))
      {
         // Add algo delay only for sample associated metadata.
         md_ptr->offset += (is_ba_md ? 0 : algo_delay_len_in);

         // Add buffering delay for all metadata.
         md_ptr->offset += buf_in_len_per_ch;

         md_moved = gen_topo_move_md_to_out_list(module_ptr,
                                                 node_ptr,
                                                 output_md_list_pptr,
                                                 input_md_list_pptr,
                                                 out_produced_per_ch,
                                                 out_initial_len_per_ch,
                                                 in_consumed_per_ch,
                                                 input_md_info_ptr->df,
                                                 output_md_info_ptr->df,
                                                 flushing_eos);

         any_eos_moved |= (flushing_eos && md_moved);

         if (!md_moved)
         {
            any_eos_stuck |= flushing_eos;
            // new offset in the output next time or the next
            md_ptr->offset -= in_consumed_per_ch; // zero for flushing-EOS
            if (NULL == internal_md_list_pptr)
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "intermediate list is NULL. Sending MD to output even "
                        "though it needs to be held in internal list");
               spf_list_move_node_to_another_list((spf_list_node_t **)output_md_list_pptr,
                                                  (spf_list_node_t *)node_ptr,
                                                  (spf_list_node_t **)input_md_list_pptr);
            }
            else
            {
               spf_list_move_node_to_another_list((spf_list_node_t **)internal_md_list_pptr,
                                                  (spf_list_node_t *)node_ptr,
                                                  (spf_list_node_t **)input_md_list_pptr);
#ifdef METADATA_DEBUGGING
               PRINT_MD_PROP_DBG_ISLAND("moving to internal list",
                                        "in_consumed",
                                        in_consumed_per_ch,
                                        ", out_produced_per_ch %lu, algo_delay_len_in %lu",
                                        out_produced_per_ch,
                                        algo_delay_len_in);
#endif
            }
         }
      }
      else
      {
         any_eos_stuck |= flushing_eos;
// keep in input list itself, no need to update offset as container does it in adj_offset_after_process
// md_ptr->offset -= in_consumed_per_ch;

#ifdef METADATA_DEBUGGING
         bool_t                 in_data_present          = (0 != input_md_info_ptr->initial_len_per_ch_in_bytes);
         PRINT_MD_PROP_DBG_ISLAND("keeping in input",
                                  "in_consumed",
                                  in_consumed_per_ch,
                                  "in_data_present %u",
                                  in_data_present);
#else
         (void)offset_before;
#endif
      }
      node_ptr = next_ptr;

   } // loop through input_md_list_pptr

   // if any flushing EOS is stuck inside or at input, keep  input marker_eos set, else
   // clear input marker_eos and if any eos is moved to output, set output marker_eos.
   // marker_eos is set when the EOS is at the input or in the internal list. it's removed only after EOS goes to output
   // list.
   if (any_flushing_eos_present)
   {
      if (any_eos_stuck)
      {
         input_stream_ptr->flags.marker_eos = TRUE;
      }
      else
      {
         input_stream_ptr->flags.marker_eos = FALSE;
      }

      if (any_eos_moved)
      {
         output_stream_ptr->flags.marker_eos = TRUE;
      }
   }
   return result;
}

ar_result_t gen_topo_clone_md(gen_topo_t *           topo_ptr,
                              module_cmn_md_t *      md_ptr,
                              module_cmn_md_list_t **out_md_list_pptr,
                              POSAL_HEAP_ID          heap_id,
                              bool_t                 disabled_tracking_cloned_md)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (NULL == md_ptr)
   {
      return result;
   }

   void *           in_md_payload_ptr  = NULL;
   void *           new_md_payload_ptr = NULL;
   module_cmn_md_t *new_md_ptr         = NULL;
   bool_t           is_out_band        = FALSE;
   bool_t           is_island_heap     = POSAL_IS_ISLAND_HEAP_ID(heap_id);
   spf_list_node_t *tail_node_ptr      = NULL;

   // generic metadata is assumed to not require deep cloning
   is_out_band          = md_ptr->metadata_flag.is_out_of_band;
   uint32_t new_md_size = sizeof(module_cmn_md_t);
   if (is_out_band)
   {
      in_md_payload_ptr = &md_ptr->metadata_ptr;
      if (is_island_heap)
      {
         new_md_payload_ptr = spf_lpi_pool_get_node(md_ptr->max_size);
         VERIFY(result, NULL != new_md_payload_ptr);
         memset(new_md_payload_ptr, 0, md_ptr->max_size);
      }
      else
      {
         MALLOC_MEMSET(new_md_payload_ptr, void *, md_ptr->max_size, heap_id, result);
      }
   }
   else
   {
      in_md_payload_ptr = &md_ptr->metadata_buf;
      new_md_size += md_ptr->max_size;
   }

   if (is_island_heap)
   {
      new_md_ptr = spf_lpi_pool_get_node(new_md_size);
      VERIFY(result, NULL != new_md_ptr);
      memset((void *)new_md_ptr, 0, new_md_size);
   }
   else
   {
      MALLOC_MEMSET(new_md_ptr, module_cmn_md_t, new_md_size, heap_id, result);
   }
   if (!is_out_band)
   {
      new_md_payload_ptr = (void *)&new_md_ptr->metadata_buf;
   }

   TRY(result, spf_list_insert_tail((spf_list_node_t **)out_md_list_pptr, new_md_ptr, heap_id, TRUE /* use pool */));
   spf_list_get_tail_node((spf_list_node_t *)*out_md_list_pptr, &tail_node_ptr);

   // Copy
   memscpy((void *)new_md_ptr, sizeof(module_cmn_md_t), (void *)md_ptr, sizeof(module_cmn_md_t));
   if (is_out_band)
   {
      new_md_ptr->metadata_ptr = new_md_payload_ptr;
   }
   memscpy(new_md_payload_ptr, md_ptr->max_size, in_md_payload_ptr, md_ptr->max_size);

   if(disabled_tracking_cloned_md)
   {
	   new_md_ptr->tracking_ptr = NULL;
	   new_md_ptr->metadata_flag.tracking_mode = 0;
   }

   /*if the metadata tracking is valid, we should increment the reference counting*/
   if (NULL != new_md_ptr->tracking_ptr)
   {
      spf_ref_counter_add_ref((void *)new_md_ptr->tracking_ptr);
      module_cmn_md_tracking_payload_t *md_tr_ptr = (module_cmn_md_tracking_payload_t *)new_md_ptr->tracking_ptr;
      if (TRUE == md_tr_ptr->flags.enable_cloning_event)
      {
         gen_topo_raise_md_cloning_event(topo_ptr, md_tr_ptr, md_ptr->metadata_id);
      }
   }

#if defined(METADATA_DEBUGGING)
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MD_DBG: cloned, original md_ptr 0x%p, new md_ptr 0x%p",
                  md_ptr,
                  new_md_ptr);
#endif

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      // Insert would fail if the node creation failed (memory allocation failures).
      // So we can safely assume its not pushed to the list.
      // If in future there is a possibility for any failure after inserting
      // the node to the tail, then we will need to remove the node from the list as well.
      if (is_out_band)
      {
         gen_topo_check_free_md_ptr(&new_md_payload_ptr, is_island_heap);
      }
      gen_topo_check_free_md_ptr((void **)&new_md_ptr, is_island_heap);
   }

   // Increment md counter as the last step, so that if there was any error MD counter incremented can be skipped.
   if (result == AR_EOK)
   {
      thin_topo_incr_active_md_nodes(topo_ptr, (module_cmn_md_list_t *)tail_node_ptr);
   }

   return result;
}

/**
 *
 *
 * default case is for seamlessly copying metadata across CAPI modules. it doesn't have too-deep copies.
 *
 * context_ptr - context pointer
 * in_md_list_ptr  - pointer to the list node which needs to be cloned
 * out_md_list_pptr - pointer to the pointer to the list node. new node will be inserted at the tail-end.
 *
 */
capi_err_t gen_topo_capi_metadata_clone(void *                 context_ptr,
                                        module_cmn_md_t *      md_ptr,
                                        module_cmn_md_list_t **out_md_list_pptr,
                                        capi_heap_id_t         c_heap_id)
{
   capi_err_t  result    = CAPI_EOK;
   ar_result_t ar_result = AR_EOK;
   if ((NULL == md_ptr) || (NULL == context_ptr))
   {
      return CAPI_EFAILED;
   }
   if (NULL == out_md_list_pptr)
   {
      return CAPI_EFAILED;
   }

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)context_ptr;
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;
   POSAL_HEAP_ID      heap_id    = (POSAL_HEAP_ID)c_heap_id.heap_id;

#if defined(METADATA_DEBUGGING)
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id, DBG_LOW_PRIO, "MD_DBG: clone, metadata_ptr 0x%p", md_ptr);
#endif

   switch (md_ptr->metadata_id)
   {
      case MODULE_CMN_MD_ID_EOS:
      {
         ar_result = gen_topo_clone_eos(topo_ptr, md_ptr, out_md_list_pptr, heap_id);
         if (AR_DID_FAIL(ar_result))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "MD_DBG: Cloning EoS failed");
         }

         break;
      }
      default:
      {
         ar_result = gen_topo_clone_md(topo_ptr, md_ptr, out_md_list_pptr, heap_id, FALSE);
         if (AR_DID_FAIL(ar_result))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "MD_DBG: Cloning MD_ID (0x%lx) failed with 0x%x",
                     md_ptr->metadata_id,
                     ar_result);
         }
         break;
      }
   }

   return result;
}

/**
 * zero pushing is only for flushing EOS
 * when zeros are pushed the offset of the EOS has to be pushed to after the zeros.
 *
 * Always use gen_topo_push_zeros_at_eos and never use this func directly.
 */
ar_result_t gen_topo_push_zeros_at_eos_util_(gen_topo_t *           topo_ptr,
                                             gen_topo_module_t *    module_ptr,
                                             gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // Note: Even for packetized formats this pushes zeros instead of null bursts.

   uint32_t    amount_of_zero_pushed_per_ch = 0; // bytes
   topo_buf_t *bufs_ptr                     = in_port_ptr->common.bufs_ptr;
   uint32_t    ch                           = in_port_ptr->common.media_fmt_ptr->pcm.num_channels;
   uint32_t    amount_zero_push_per_ch      = module_ptr->pending_zeros_at_eos; // bytes
   switch (in_port_ptr->common.media_fmt_ptr->pcm.interleaving)
   {
      case TOPO_DEINTERLEAVED_PACKED:
      {
         uint32_t ch_offset              = capi_cmn_divide( bufs_ptr[0].actual_data_len , ch);
         uint32_t max_empty_space_per_ch = (capi_cmn_divide(bufs_ptr[0].max_data_len , ch)) - ch_offset;
         //(bufs_ptr[0].max_data_len - bufs_ptr[0].actual_data_len) / ch;
         amount_zero_push_per_ch  = MIN(amount_zero_push_per_ch, max_empty_space_per_ch);
         uint32_t curr_ch_spacing = bufs_ptr[0].actual_data_len / ch;
         // uint32_t new_ch_spacing  = curr_ch_spacing + amount_zero_push_per_ch;

         // move channels away to make space for zeros.
         for (uint32_t c = ch; c > 0; c--)
         {
            TOPO_MEMSMOV_NO_RET(bufs_ptr[0].data_ptr + (ch - 1) * (curr_ch_spacing + amount_zero_push_per_ch),
                                amount_zero_push_per_ch,
                                bufs_ptr[0].data_ptr + (ch - 1) * curr_ch_spacing,
                                amount_zero_push_per_ch,
                                topo_ptr->gu.log_id,
                                "ZEROS: (0x%lX, 0x%lX)",
                                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                in_port_ptr->gu.cmn.id);
         }

         for (uint32_t c = 0; c < ch; c++)
         {
            memset(bufs_ptr[0].data_ptr + ch_offset + (c * curr_ch_spacing), 0, amount_zero_push_per_ch);
         }

         bufs_ptr[0].actual_data_len += (amount_zero_push_per_ch * ch);
         amount_of_zero_pushed_per_ch = amount_zero_push_per_ch;

         break;
      }
      default:
      {
         uint32_t amount_zeros_push_per_buf =
            gen_topo_convert_len_per_ch_to_len_per_buf(in_port_ptr->common.media_fmt_ptr, amount_zero_push_per_ch);
         //(buf_ptr->max_data_len - buf_ptr->actual_data_len) / ch;
         amount_zeros_push_per_buf =
            MIN(amount_zeros_push_per_buf,
                (in_port_ptr->common.bufs_ptr[0].max_data_len - in_port_ptr->common.bufs_ptr[0].actual_data_len));

         // Optimize and not update all channel's actual data lengths for unpacked v1/v2.
         if (in_port_ptr->common.flags.is_pcm_unpacked)
         {
            uint32_t actual_data_len_per_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;
            for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
            {
               memset(in_port_ptr->common.bufs_ptr[b].data_ptr + actual_data_len_per_buf, 0, amount_zeros_push_per_buf);
            }
            in_port_ptr->common.bufs_ptr[0].actual_data_len += amount_zeros_push_per_buf;
         }
         else
         {
            for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
            {
               memset(in_port_ptr->common.bufs_ptr[b].data_ptr + in_port_ptr->common.bufs_ptr[b].actual_data_len,
                      0,
                      amount_zeros_push_per_buf);
               in_port_ptr->common.bufs_ptr[b].actual_data_len += amount_zeros_push_per_buf;
            }
         }

         amount_of_zero_pushed_per_ch =
            gen_topo_convert_len_per_buf_to_len_per_ch(in_port_ptr->common.media_fmt_ptr, amount_zeros_push_per_buf);

         break;
      }
   }

   module_ptr->pending_zeros_at_eos -= amount_of_zero_pushed_per_ch;

   // uint32_t samples_per_ch = amount_of_zero_pushed_per_ch / (in_port_ptr->common.media_fmt.pcm.bits_per_sample >> 3);

   // gen_topo_metadata_adj_eos_offset_by_zeros_pushed(topo_ptr, module_ptr, in_port_ptr, samples_per_ch);

#if defined(METADATA_DEBUGGING)
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "MD_DBG: Module 0x%lX, pending zeros %lu, this time pushed %lu bytes per ch",
                   module_ptr->gu.module_instance_id,
                   module_ptr->pending_zeros_at_eos,
                   amount_of_zero_pushed_per_ch);
#endif

   return result;
}

/**
 * Modules that handle metadata must handle EOF by themselves.
 * They must propagate EOF to output and drop any remaining input.
 *
 * EOF achieves force-process. i.e., process with existing data if possible.
 * for threshold modules, they can function if they can work with existing data.
 * for decoders squeezing behavior (call process until no more output) is done for EOF.
 *
 * Container propagation of EOF involves:
 *  - the container calls with input EOF,
 *  - checks that module didn't propagate the EOF to output
 *  - when module produces no more output, propagates the EOF to the output.
 *  - At this time if there's any pending data (and need-more) the data is dropped.
 *
 * Buffering modules must override such that we don't call those modules indefinitely when they don't stop
 * producing output
 */
ar_result_t gen_topo_handle_end_of_frame_after_process(gen_topo_t *           topo_ptr,
                                                       gen_topo_module_t *    module_ptr,
                                                       gen_topo_input_port_t *in_port_ptr,
                                                       uint32_t *             in_bytes_given_per_buf,
                                                       bool_t                 need_more)
{
   ar_result_t result = AR_EOK;

   if (module_ptr->gu.flags.is_siso)
   {
      // skip modules that want EOF propagated as controlled by them. E.g. threshold buffering module may not
      // want to propagate like this.
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
      gen_topo_input_port_t * in_port_ptr  = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
      bool_t                  pcm_in_pcm_out = SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format) &&
                              SPF_IS_PCM_DATA_FORMAT(out_port_ptr->common.media_fmt_ptr->data_format);

      // if need-more and no output produced. this behavior helps squeezing (needed for decoders) at EOS.
      // EOF is propagated only once module stops producing anything.
      bool_t is_decoder_like_module =
         in_port_ptr->common.flags.port_has_threshold && module_ptr->flags.requires_data_buf && !pcm_in_pcm_out;

      // for module not like decoder, always pass EOF right away. For decoder like modules, pass EOF right away if
      // it's
      // not due to EOS, but otherwise, squeeze
      if (!is_decoder_like_module || !in_port_ptr->common.sdata.flags.marker_eos ||
          ((0 == out_port_ptr->common.bufs_ptr[0].actual_data_len) && need_more))
      {
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         "Propagating EOF (is_decoder_like_module%u, eos%u) at Module 0x%lX",
                         is_decoder_like_module,
                         in_port_ptr->common.sdata.flags.marker_eos,
                         module_ptr->gu.module_instance_id);
#endif

         // actual len here means what's consumed by CAPI.
         uint32_t pending_data = (in_bytes_given_per_buf[0] - in_port_ptr->common.bufs_ptr[0].actual_data_len);
         if (pending_data && (0 == out_port_ptr->common.bufs_ptr[0].actual_data_len))
         {
            // no output produced => cannot squeeze anymore. hence drop any input.
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: in-port-id 0x%lx: process after: dropping input data %lu as no output got "
                            "produced for EOF",
                            module_ptr->gu.module_instance_id,
                            in_port_ptr->gu.cmn.id,
                            pending_data);

            // before gen_topo_move_data_to_beginning_after_process actual length stands for what's consumed
            // Optimize and not update all channel's actual data lengths for unpacked v1/v2
            for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&in_port_ptr->common); b++)
            {
               in_port_ptr->common.bufs_ptr[b].actual_data_len = in_bytes_given_per_buf[b];
            }
            pending_data = 0;
         }

         // if pending data is zero, move EOF (even if output got produced). If output got produced while there
         // was
         // pending input data then we don't move EOF.
         // wmastd_dec_nt_9 - where EOF is set by client.
         if (0 == pending_data)
         {
            // do not clear need more here. clearing can cause loss of triggers - causes inf inner loop.
            in_port_ptr->common.sdata.flags.end_of_frame  = FALSE;
            out_port_ptr->common.sdata.flags.end_of_frame = TRUE;
         }
      }
   }
   // for sink modules, clear input discontinuity
   else if (module_ptr->gu.flags.is_sink)
   {
      in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
   }

   return result;
}

/* EOS island utilties */

static ar_result_t gen_topo_free_eos(gen_topo_t *           topo_ptr,
                                     module_cmn_md_list_t * eos_node_ptr,
                                     gen_topo_eos_cargo_t * cntr_ref_ptr,
                                     module_cmn_md_eos_t *  eos_metadata_ptr,
                                     module_cmn_md_t *      metadata_ptr,
                                     module_cmn_md_list_t **head_pptr);

/**
 * is_eos_rendered TRUE = render, FALSE = drop
 *
 * is_last - should be relevant only when it's known that one EOS is preset in
 * the md_list_pptr
 */
ar_result_t gen_topo_respond_and_free_eos(gen_topo_t *           topo_ptr,
                                          uint32_t               sink_miid,
                                          module_cmn_md_list_t * md_list_ptr,
                                          bool_t                 is_eos_rendered,
                                          module_cmn_md_list_t **head_pptr,
                                          bool_t                 override_ctrl_to_disable_tracking_event)
{
   ar_result_t result = AR_EOK;

   if (NULL == md_list_ptr)
   {
      return result;
   }

   module_cmn_md_eos_t *eos_metadata_ptr = NULL;
   module_cmn_md_t *    metadata_ptr     = NULL;
   metadata_ptr                          = md_list_ptr->obj_ptr;

   if (MODULE_CMN_MD_ID_EOS == metadata_ptr->metadata_id)
   {
      uint32_t is_out_band = metadata_ptr->metadata_flag.is_out_of_band;
      if (is_out_band)
      {
         eos_metadata_ptr = (module_cmn_md_eos_t *)metadata_ptr->metadata_ptr;
      }
      else
      {
         eos_metadata_ptr = (module_cmn_md_eos_t *)&(metadata_ptr->metadata_buf);
      }

      if (metadata_ptr->tracking_ptr)
      {
         gen_topo_eos_event_payload_t eos_event_payload;
         eos_event_payload.is_flushing_eos = eos_metadata_ptr->flags.is_flushing_eos;
         if (AR_EOK == gen_topo_raise_tracking_event(topo_ptr,
                                                     sink_miid,
                                                     md_list_ptr,
                                                     is_eos_rendered,
                                                     &eos_event_payload,
                                                     override_ctrl_to_disable_tracking_event))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_MED_PRIO,
                            "MD_DBG: Raise EOS called with is_eos_rendered %u for end point "
                            "module (0x%lX), "
                            "tracking payload 0x%p",
                            is_eos_rendered,
                            sink_miid,
                            metadata_ptr->tracking_ptr);
         }
      }
      // Even for non-flushing EOS below function needs to be called (to remove
      // reference to ext in port in case flushing EOS gets converted to
      // non-flushing)
      gen_topo_free_eos(topo_ptr,
                        md_list_ptr,
                        (gen_topo_eos_cargo_t *)eos_metadata_ptr->cntr_ref_ptr,
                        eos_metadata_ptr,
                        metadata_ptr,
                        head_pptr);
   }
   return result;
}

ar_result_t gen_topo_set_pending_zeros(gen_topo_module_t *module_ptr, gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t result = AR_EOK;
   if (!gen_topo_fwk_owns_md_prop(module_ptr))
   {
      return result;
   }

   // only generic PP modules & EP with Single input port (SISO or sink) and
   // pcm/packetized fmt need zero pushing. also need to have a buf. some
   // encoders also have algo delay. to flush it out, only way is to push
   // zeros.otherwise EOS gets stuck. zero flushing only for flush-eos. also zero
   // pushing not necessary if underflow is happening (underflow = zeros) Note
   // that EP modules with 60958 config can also be zero pushed.
   if (/*(AMDB_MODULE_TYPE_GENERIC != module_ptr->gu.module_type) ||*/
       (!SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->common.media_fmt_ptr->data_format)))
   {
      return result;
   }

   // if flushing EOS comes back to back, we need to re-init the value of
   // pending_zeros_at_eos so that second EOS has enough zeros to push it
   // through. if (0 == module_ptr->pending_zeros_at_eos)
   {
      // no need to rescale pending_zeros_at_eos as we don't listen to more input
      // with EoS pending. consider algo delay only if module is not disabled.
      if (!module_ptr->flags.disabled)
      {
         module_ptr->pending_zeros_at_eos =
            topo_us_to_bytes_per_ch(module_ptr->algo_delay, in_port_ptr->common.media_fmt_ptr);
         // algo delay is already accounted in metadata prop
      }

      TOPO_MSG_ISLAND(module_ptr->topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "MD_DBG: flushing EoS at module,port 0x%lX, 0x%lx, total pending zeros (bytes per ch) %lu",
                      module_ptr->gu.module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      module_ptr->pending_zeros_at_eos);
   }

   return result;
}

/**
 * this is called for EOS transfer between peer-container as well,
 * where there's no need to clear spf_payload ref counter.
 */
static ar_result_t gen_topo_free_eos(gen_topo_t *           topo_ptr,
                                     module_cmn_md_list_t * eos_node_ptr,
                                     gen_topo_eos_cargo_t * cntr_ref_ptr,
                                     module_cmn_md_eos_t *  eos_metadata_ptr,
                                     module_cmn_md_t *      metadata_ptr,
                                     module_cmn_md_list_t **head_pptr)
{
   ar_result_t result = AR_EOK;

#ifdef METADATA_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MD_DBG: Free: eos_node_ptr 0x%p cntr_ref_ptr 0x%p eos_metadata_ptr "
            "0x%p, 0x%p",
            eos_node_ptr,
            cntr_ref_ptr,
            eos_metadata_ptr,
            metadata_ptr);
#endif

   /** even if EoS splits, we can destroy metadata_ptr, eos_metadata_ptr and
    eos_node_ptr
    * as these will be unique per path. Only cntr_ref_ptr is ref counted.
    * ext input port lists & flags must be cleared only when cargo ref count
    reaches zero.

    Even for non-flushing EOS below function needs to be called (to remove
    reference to ext in port)*/
   gen_topo_free_eos_cargo(topo_ptr, metadata_ptr, eos_metadata_ptr);

   gen_topo_free_md(topo_ptr, eos_node_ptr, metadata_ptr, head_pptr);

   return result;
}

void gen_topo_free_eos_cargo(gen_topo_t *topo_ptr, module_cmn_md_t *md_ptr, module_cmn_md_eos_t *eos_metadata_ptr)
{
   if (eos_metadata_ptr && eos_metadata_ptr->cntr_ref_ptr)
   {
      gen_topo_eos_cargo_t *cntr_ref_ptr = (gen_topo_eos_cargo_t *)eos_metadata_ptr->cntr_ref_ptr;

      cntr_ref_ptr->ref_count--;
      if (0 == cntr_ref_ptr->ref_count)
      {
         void *temp_ptr = (void *)md_ptr->tracking_ptr;
         // call back container to clear input EoS flag.
         // Even for non-flushing EOS below function needs to be called (to remove
         // reference to ext in port)
         if (topo_ptr->topo_to_cntr_vtable_ptr->clear_eos)
         {
            topo_ptr->topo_to_cntr_vtable_ptr->clear_eos(topo_ptr,
                                                         cntr_ref_ptr->inp_ref,
                                                         cntr_ref_ptr->inp_id,
                                                         eos_metadata_ptr);
            /*(NULL != cntr_ref_ptr->inp_ref) ||
               (cntr_ref_ptr->did_eos_come_from_ext_in)*/
         }


         //If it is not allocated from island pool, we need to exit island to call mem free
         bool_t pool_used = spf_lpi_pool_is_addr_from_md_pool(eos_metadata_ptr->cntr_ref_ptr);
         gen_topo_check_free_md_ptr((void **)&eos_metadata_ptr->cntr_ref_ptr, pool_used);

         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                        DBG_LOW_PRIO,
                        "MD_DBG: Freed EoS (in this container) with metadata ptr 0x%p "
                        "gk payload 0x%p. offset %lu",
                        eos_metadata_ptr,
                        temp_ptr,
                        md_ptr->offset);

         // the list will move along the chain and be freed when EoS is terminated
         // (at sink module or at container boundary)
      }
      eos_metadata_ptr->cntr_ref_ptr = NULL;
   }
   else if (eos_metadata_ptr && !eos_metadata_ptr->cntr_ref_ptr)
   {
      // EOS may not have container reference if its been created by a module
      // Ex: DTMF gen can create EOS at the end of tone generation.
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "MD_DBG: Eos reached external output port without container "
                      "reference. is_flushing %u",
                      eos_metadata_ptr->flags.is_flushing_eos);

      if (eos_metadata_ptr->flags.is_flushing_eos && topo_ptr->topo_to_cntr_vtable_ptr->clear_eos)
      {
         topo_ptr->topo_to_cntr_vtable_ptr->clear_eos(topo_ptr, NULL, 0, eos_metadata_ptr);
      }
   }
}

ar_result_t gen_topo_clone_eos(gen_topo_t *           topo_ptr,
                               module_cmn_md_t *      in_metadata_ptr,
                               module_cmn_md_list_t **out_md_list_pptr,
                               POSAL_HEAP_ID          heap_id)
{
   ar_result_t result = AR_EOK;

   if (NULL == in_metadata_ptr)
   {
      return result;
   }

   module_cmn_md_eos_t *in_eos_metadata_ptr = NULL;
   uint32_t             is_out_band         = in_metadata_ptr->metadata_flag.is_out_of_band;
   if (is_out_band)
   {
      in_eos_metadata_ptr = (module_cmn_md_eos_t *)in_metadata_ptr->metadata_ptr;
   }
   else
   {
      in_eos_metadata_ptr = (module_cmn_md_eos_t *)&(in_metadata_ptr->metadata_buf);
   }

   bool_t disable_tracking_cloned_md = in_eos_metadata_ptr->flags.is_internal_eos ? TRUE : FALSE;

   result = gen_topo_clone_md(topo_ptr, in_metadata_ptr, out_md_list_pptr, heap_id, disable_tracking_cloned_md);
   if (AR_DID_FAIL(result))
   {
      return result;
   }

   gen_topo_eos_cargo_t *cargo_ptr = (gen_topo_eos_cargo_t *)in_eos_metadata_ptr->cntr_ref_ptr;
   if (cargo_ptr)
   {
      cargo_ptr->ref_count++;
   }

   return result;
}
