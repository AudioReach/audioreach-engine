/**
 * \file gen_topo_mod_buf_access_ext_island.c
 * \brief
 *  This file contains island utility functions for INTF_EXTN_MODULE_BUFFER_ACCESS

 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause

/* =======================================================================
Includes
========================================================================== */
#include "gen_topo.h"
#include "gen_topo_mod_buf_access_ext.h"

/* -------------------------------------------------------------
 * Island functions:
 *   gen_topo_handle_assign_buffers_capi_input_buffer_extn_module
 *   gen_topo_clear_and_replace_capi_input_buffers
 *   gen_topo_clear_and_replace_capi_output_buffers
 *
 * Debug prints are emitted when MOD_BUF_ACCESS_DEBUG is defined.
 * ------------------------------------------------------------- */
ar_result_t gen_topo_query_n_assign_capi_input_port_util(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: input port (mod 0x%lX, port 0x%lx) already has a buffer %p origin %lu, "
               "skipping module query",
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id,
               in_port_ptr->common.bufs_ptr[0].data_ptr,
               in_port_ptr->common.flags.buf_origin);
#endif
      return result;
   }

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Found input port (mod 0x%lX, port 0x%lx) querying module for CAPI internal "
            "buffer",
            in_port_ptr->gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->gu.cmn.id);
#endif
   /* ---------------------------------------------------------
    * Obtain the buffer for this input port.
    * --------------------------------------------------------- */
   for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&in_port_ptr->common); b++)
   {
      in_port_ptr->common.bufs_ptr[b].max_data_len = in_port_ptr->common.max_buf_len_per_buf;
   }

   ar_result_t res = gen_topo_mod_buf_mgr_extn_wrapper_get_buf(topo_ptr,
                                                               (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                               TRUE, /* is_input */
                                                               in_port_ptr->gu.cmn.index,
                                                               &in_port_ptr->common);

   if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Failed to get CAPI internal buffer for input port (mod 0x%lX, "
               "port "
               "0x%lx)",
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id);

      // topo buffer will be assigned in the module process context.
      return result;
   }

   /* Mark the buffer origin on the input port. */
   in_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_CAPI_MODULE;

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: queried and assigned CAPI internal buffer 0x%p for input port (mod 0x%lX, "
            "port 0x%lx)",
            in_port_ptr->common.bufs_ptr[0].data_ptr,
            in_port_ptr->gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->gu.cmn.id);
#endif

   return result;
}

ar_result_t gen_topo_handle_assign_buffers_capi_input_buffer_extn_module(gen_topo_t *topo_ptr)
{
   /* Iterate over all modules that are currently STARTED. */
   ar_result_t result = AR_EOK;

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Enter gen_topo_handle_assign_buffers_capi_input_buffer_extn_module()");
#endif

   for (gu_module_list_t *mod_lst = topo_ptr->started_sorted_module_list_ptr; (NULL != mod_lst); LIST_ADVANCE(mod_lst))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)mod_lst->module_ptr;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         /* Check for the buffer‑reuse extension flag. */
         if (GEN_TOPO_MODULE_INPUT_BUF_ACCESS != in_port_ptr->common.flags.supports_buffer_reuse_extn)
         {
            continue;
         }

         result |= gen_topo_query_n_assign_capi_input_port_util(topo_ptr, in_port_ptr);
      }
   }

   return result;
}

/** Replaces the buffer in the port with a new topo buffer, and copies data from old to new buffer.*/
GEN_TOPO_STATIC ar_result_t gen_topo_replace_mod_buf_with_topo_buffer(gen_topo_t             *topo_ptr,
                                                                      gen_topo_module_t      *module_ptr,
                                                                      gen_topo_common_port_t *cmn_port_ptr,
                                                                      uint32_t                port_id,
                                                                      bool_t                  is_input,
                                                                      bool_t                  return_capi_buffer)
{

   // this can happen b4 thresh/MF prop
   if (0 == cmn_port_ptr->max_buf_len || !cmn_port_ptr->bufs_ptr[0].data_ptr ||
       !cmn_port_ptr->bufs_ptr[0].actual_data_len)
   {
      return AR_EOK;
   }

   // internally mem needed for topo_buf_mgr_element_t is counted.
   // Also ref count is initialized to 1.


   int8_t  *new_ptr             = NULL;
   uint32_t bufs_num            = cmn_port_ptr->sdata.bufs_num;
   uint32_t max_buf_len_per_buf = cmn_port_ptr->max_buf_len_per_buf;

   ar_result_t result = topo_buf_manager_get_buf(topo_ptr, &new_ptr, cmn_port_ptr->max_buf_len);
   if (NULL == new_ptr)
   {
      return AR_EFAILED;
   }

   uint32_t bytes_per_ch = MIN(max_buf_len_per_buf, cmn_port_ptr->bufs_ptr[0].actual_data_len);

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX: Port id 0x%lx, replacing prev buf (0x%lx, (%lu of %lu)) with new buf (0x%lx, "
            "(%lu, %lu))",
            module_ptr->gu.module_instance_id,
            port_id,
            cmn_port_ptr->bufs_ptr[0].data_ptr,
            cmn_port_ptr->bufs_ptr[0].actual_data_len,
            cmn_port_ptr->bufs_ptr[0].max_data_len,
            new_ptr,
            bytes_per_ch,
            max_buf_len_per_buf);
#endif

   // first only move data from old to new buffer and assign new buf ptr only after returning the old buffer
   int8_t *buf_ptr = new_ptr;
   for (uint32_t b = 0; b < bufs_num; b++)
   {
      memscpy(buf_ptr, bytes_per_ch, cmn_port_ptr->bufs_ptr[b].data_ptr, bytes_per_ch);
      buf_ptr += max_buf_len_per_buf;

      // clean data len from old sdata
      cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
   }

   // return prev buffer to buffer manager
   if (return_capi_buffer)
   {
      result |= gen_topo_mod_buf_mgr_extn_wrapper_return_buf(topo_ptr,
                                                             module_ptr,
                                                             is_input,
                                                             port_id,
                                                             &bufs_num,
                                                             (capi_buf_t *)cmn_port_ptr->bufs_ptr);
   }

   // update the new buffer ptr in sdata
   buf_ptr = new_ptr;
   for (uint32_t b = 0; b < bufs_num; b++)
   {
      cmn_port_ptr->bufs_ptr[b].data_ptr        = buf_ptr;
      cmn_port_ptr->bufs_ptr[b].actual_data_len = bytes_per_ch;
      cmn_port_ptr->bufs_ptr[b].max_data_len    = max_buf_len_per_buf;

      buf_ptr += max_buf_len_per_buf;
   }
   cmn_port_ptr->flags.buf_origin = GEN_TOPO_BUF_ORIGIN_BUF_MGR;
   return result;
}

GEN_TOPO_STATIC ar_result_t gen_topo_return_replace_capi_borrowed_buf_util_(gen_topo_t             *topo_ptr,
                                                                            gen_topo_module_t      *module_ptr,
                                                                            gen_topo_common_port_t *cmn_port_ptr,
                                                                            uint32_t                port_id,
                                                                            bool_t                  in_input,
                                                                            bool_t                  force_drop_data)
{
   ar_result_t result = AR_EOK;

   if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED != cmn_port_ptr->flags.buf_origin)
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Return, not borrowed CAPI internal buffer on Module 0x%lx port id 0x%lx, "
               "old_buf=0x%p, "
               "len=%lu origin %lu",
               module_ptr->gu.module_instance_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->bufs_ptr[0].actual_data_len,
               cmn_port_ptr->flags.buf_origin);
#endif
      return AR_EOK;
   }

   if (cmn_port_ptr->bufs_ptr[0].actual_data_len && !force_drop_data)
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Replacing Borrowed CAPI internal buffer on Module 0x%lx port id 0x%lx, "
               "old_buf=0x%p, actual_data_len=%lu",
               module_ptr->gu.module_instance_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->bufs_ptr[0].actual_data_len);
#endif

      result |= gen_topo_replace_mod_buf_with_topo_buffer(topo_ptr, module_ptr, cmn_port_ptr, port_id, in_input, FALSE);
   }
   else if (cmn_port_ptr->bufs_ptr[0].data_ptr)
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Clearing Borrowed CAPI internal buffer on Module 0x%lx port id 0x%lx"
               "buffer 0x%p, dropping actual_data_len=%lu ",
               module_ptr->gu.module_instance_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->bufs_ptr[0].actual_data_len);
#endif

      cmn_port_ptr->bufs_ptr[0].data_ptr = NULL;
      cmn_port_ptr->flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_INVALID;
      gen_topo_set_all_bufs_len_to_zero(cmn_port_ptr);
   }
   return result;
}

GEN_TOPO_STATIC ar_result_t gen_topo_return_replace_capi_mod_buf_util_(gen_topo_t             *topo_ptr,
                                                                       gen_topo_module_t      *module_ptr,
                                                                       gen_topo_common_port_t *cmn_port_ptr,
                                                                       uint32_t                port_id,
                                                                       bool_t                  in_input,
                                                                       bool_t                  force_drop_data)
{
   ar_result_t result = AR_EOK;

   if (cmn_port_ptr->bufs_ptr[0].actual_data_len && !force_drop_data)
   {

#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Replacing buffer on upstream Module 0x%lx port id 0x%lx, old_buf=0x%p, "
               "len=%lu",
               module_ptr->gu.module_instance_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->bufs_ptr[0].actual_data_len);
#endif

      result |= gen_topo_replace_mod_buf_with_topo_buffer(topo_ptr,
                                                          module_ptr,
                                                          cmn_port_ptr,
                                                          port_id,
                                                          in_input,
                                                          TRUE /* return buffer to capi module*/);
   }
   else if (cmn_port_ptr->bufs_ptr[0].data_ptr)
   {

#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Clearing capi buffer Module 0x%lx port id 0x%lx"
               "buffer 0x%p, dropping_data?%lu ",
               module_ptr->gu.module_instance_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->bufs_ptr[0].actual_data_len);
#endif

      result |= gen_topo_mod_buf_mgr_extn_wrapper_return_buf(topo_ptr,
                                                             module_ptr,
                                                             in_input,
                                                             port_id,
                                                             &cmn_port_ptr->sdata.bufs_num,
                                                             (capi_buf_t *)cmn_port_ptr->bufs_ptr);

      cmn_port_ptr->bufs_ptr[0].data_ptr = NULL;
      cmn_port_ptr->flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_INVALID;
      gen_topo_set_all_bufs_len_to_zero(cmn_port_ptr);
   }
   return result;
}

ar_result_t gen_topo_clear_and_replace_capi_input_buffers_util(gen_topo_t            *topo_ptr,
                                                               gen_topo_input_port_t *in_port_ptr,
                                                               bool_t                 force_drop_data)
{
   ar_result_t result = AR_EOK;

   /* -------------------------------------------------------------
    * Propagate the (original or newly created) buffer upstream.
    * ------------------------------------------------------------- */
   /** return CAPI buffers only from the buffer source port context */

   gen_topo_input_port_t *cur_in_port_ptr = in_port_ptr;
   while (cur_in_port_ptr->gu.conn_out_port_ptr)
   {
      gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)cur_in_port_ptr->gu.conn_out_port_ptr;
      result |=
         gen_topo_return_replace_capi_borrowed_buf_util_(topo_ptr,
                                                         (gen_topo_module_t *)prev_out_port_ptr->gu.cmn.module_ptr,
                                                         &prev_out_port_ptr->common,
                                                         prev_out_port_ptr->gu.cmn.id,
                                                         FALSE,
                                                         force_drop_data);

      /* Stop at an NBLc‑start module. */
      gen_topo_module_t *prev_mod_ptr = (gen_topo_module_t *)prev_out_port_ptr->gu.cmn.module_ptr;
      if ((prev_out_port_ptr == prev_out_port_ptr->nblc_start_ptr) &&
          (FALSE == gen_topo_is_inplace_or_disabled_siso(prev_mod_ptr)))
      {
#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Stopping at nblc-start module 0x%lX (out port 0x%lx) due to nblc start input "
                  "? %lu or inplace/disabled module ? %lu",
                  prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  prev_out_port_ptr->gu.cmn.id,
                  (prev_out_port_ptr->nblc_start_ptr == prev_out_port_ptr),
                  gen_topo_is_inplace_or_disabled_siso(prev_mod_ptr));
#endif
         break;
      }

      /* -------------------------------------------------------------
       * After the upstream walk, apply the same drop/replace logic to the
       * original input port (`cur_in_port_ptr`).  This ensures the buffer
       * on the starting module is also updated.
       * ------------------------------------------------------------- */
      gen_topo_input_port_t *prev_mod_in_port_ptr =
         (gen_topo_input_port_t *)prev_mod_ptr->gu.input_port_list_ptr->ip_port_ptr;

      result |=
         gen_topo_return_replace_capi_borrowed_buf_util_(topo_ptr,
                                                         (gen_topo_module_t *)prev_mod_in_port_ptr->gu.cmn.module_ptr,
                                                         &prev_mod_in_port_ptr->common,
                                                         prev_mod_in_port_ptr->gu.cmn.id,
                                                         TRUE,
                                                         force_drop_data);

      cur_in_port_ptr = prev_mod_in_port_ptr;
   }

   // free the input buffer after clearing from the propagated ports since it could be having partial data and needs to
   // be replaced by topo buffer.
   /* ---------------------------------------------------------
    * Buffer is present – decide what to do based on force_drop_data.
    * --------------------------------------------------------- */
   result |= gen_topo_return_replace_capi_mod_buf_util_(topo_ptr,
                                                        (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                        &in_port_ptr->common,
                                                        in_port_ptr->gu.cmn.id,
                                                        TRUE,
                                                        force_drop_data);

   return result;
}

ar_result_t gen_topo_clear_and_replace_capi_output_buffers_util(gen_topo_t             *topo_ptr,
                                                                gen_topo_output_port_t *out_port_ptr,
                                                                bool_t                  force_drop_data)
{
   ar_result_t result = AR_EOK;

   /* ---------------------------------------------------------
    * Walk forward until we reach the NBLC end for this input.
    * --------------------------------------------------------- */
   gen_topo_output_port_t *curr_out_port_ptr = out_port_ptr;
   while (curr_out_port_ptr->gu.conn_in_port_ptr)
   {
      gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)curr_out_port_ptr->gu.conn_in_port_ptr;

      result |=
         gen_topo_return_replace_capi_borrowed_buf_util_(topo_ptr,
                                                         (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr,
                                                         &next_in_port_ptr->common,
                                                         next_in_port_ptr->gu.cmn.id,
                                                         TRUE,
                                                         force_drop_data);

      gen_topo_module_t *next_mod_ptr = (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr;
      if ((curr_out_port_ptr == curr_out_port_ptr->nblc_end_ptr) ||
          (FALSE == gen_topo_is_inplace_or_disabled_siso(next_mod_ptr)))
      {
#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Reached NBLC end at Module 0x%lx input port 0x%lx forward walk terminates "
                  "nblc end ? %lu or inplace/disabled ? %lu",
                  next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  next_in_port_ptr->gu.cmn.id,
                  (curr_out_port_ptr->nblc_end_ptr == curr_out_port_ptr),
                  (FALSE == gen_topo_is_inplace_or_disabled_siso(next_mod_ptr)));
#endif
         break;
      }

      gen_topo_output_port_t *next_mod_out_port_ptr =
         (gen_topo_output_port_t *)next_mod_ptr->gu.output_port_list_ptr->op_port_ptr;

      result |=
         gen_topo_return_replace_capi_borrowed_buf_util_(topo_ptr,
                                                         (gen_topo_module_t *)next_mod_out_port_ptr->gu.cmn.module_ptr,
                                                         &next_mod_out_port_ptr->common,
                                                         next_mod_out_port_ptr->gu.cmn.id,
                                                         FALSE,
                                                         force_drop_data);
      curr_out_port_ptr = next_mod_out_port_ptr;
   } /* while forward walk */

   // return buffer to capi only after clearing from propagated ports since there could be partial data in the
   // propagated buffer.
   // drop any partial data held

   result |= gen_topo_return_replace_capi_mod_buf_util_(topo_ptr,
                                                        (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr,
                                                        &out_port_ptr->common,
                                                        out_port_ptr->gu.cmn.id,
                                                        FALSE,
                                                        force_drop_data);
   return result;
}

/**
 * gen_topo_clear_and_replace_capi_output_buffers
 *
 * Walk forward from each started module’s input port through the NBLC
 * segment until the input’s nblc_end_ptr equals the input itself.
 * For every output port that supports the buffer‑reuse extension
 * (GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS), if the port holds data:
 *
 *   • When force_drop_data == TRUE  – clear the buffer, return it to the
 *     module, set the origin to INVALID and zero the length fields.
 *   • When force_drop_data == FALSE – replace the buffer with a fresh
 *     topo buffer (thin_topo_replace_with_topo_buffer()).
 *
 * After handling the output port, the same buffer (original or newly
 * created) is propagated to the downstream input port, which is then
 * processed in the same way. The walk stops when the current input
 * port’s nblc_end_ptr is the same as the current input port (NBLC end).
 *
 * Parameters
 * ----------
 *   me_ptr          – gen_cntr_t instance (used for logging and for the
 *                     thin_topo_replace_with_topo_buffer helper).
 *   topo_ptr        – gen_topo_t instance (used by buffer‑mgr helpers).
 *   force_drop_data – TRUE  → drop data (clear buffer, return to module).
 *                     FALSE → keep data by replacing with a topo buffer.
 *
 * Returns
 * -------
 *   AR_EOK on success, otherwise the first error encountered.
 */
ar_result_t gen_topo_clear_and_replace_capi_output_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data)
{
   ar_result_t result = AR_EOK;

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Enter gen_topo_clear_and_replace_capi_output_buffers, force_drop_data=%u",
            force_drop_data);
#endif

   /* -------------------------------------------------------------
    * Iterate over all modules that are currently STARTED.
    * ------------------------------------------------------------- */
   for (gu_module_list_t *mod_lst = topo_ptr->started_sorted_module_list_ptr; (NULL != mod_lst); LIST_ADVANCE(mod_lst))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)mod_lst->module_ptr;

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         /* Assume a single input port per module (typical for this container). */
         gen_topo_output_port_t *curr_out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         // check if output port got the capi buffer
         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE != curr_out_port_ptr->common.flags.buf_origin)
         {
            continue;
         }

#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Module 0x%lx output port 0x%lx has capi internal buffer.",
                  curr_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  curr_out_port_ptr->gu.cmn.id);
#endif

         result = gen_topo_clear_and_replace_capi_output_buffers_util(topo_ptr, curr_out_port_ptr, force_drop_data);
      }
   } /* for each started module */

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Exit gen_topo_clear_and_replace_capi_output_buffers");
#endif

   return result;
}

/**
 * gen_topo_clear_and_replace_capi_input_buffers
 *
 * Walk upstream from every input port that advertises the
 * GEN_TOPO_MODULE_INPUT_BUF_ACCESS flag.  The walk assumes each module
 * in the chain has a single input port (the usual NBLC case).  For each
 * port in the chain:
 *
 *   • If the buffer pointer is NULL – nothing to do.
 *   • If the buffer contains data (actual_data_len > 0)
 *        – when force_drop_data == TRUE  : clear the buffer and return it to the
 *          module (origin set to INVALID).
 *        – when force_drop_data == FALSE : replace the buffer with a fresh topo
 *          buffer while preserving the data (gen_topo_replace_mod_buf_with_topo_buffer()).
 *   • Iterate and clear the buffer from mdoules input to upstream till nblc start.
 *   • apply the same drop/replace logic to the original input port (`cur_in_port_ptr`) so that its buffer is also
 *     updated.
 *
 * Parameters
 * ----------
 *   me_ptr          – gen_cntr_t instance (used for logging).
 *   topo_ptr        – gen_topo_t instance (used by helper functions).
 *   force_drop_data – TRUE  → drop data (clear buffer, return to module).
 *                     FALSE → keep data by replacing with a topo buffer.
 *
 * Returns
 * -------
 *   AR_EOK on success, otherwise an error code from the helper functions.
 */
ar_result_t gen_topo_clear_and_replace_capi_input_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data)
{
   ar_result_t result = AR_EOK;

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Enter gen_topo_clear_and_replace_capi_input_buffers, force_drop_data=%u",
            force_drop_data);
#endif

   /* -------------------------------------------------------------
    * Iterate over all modules that are currently STARTED.
    * ------------------------------------------------------------- */
   for (gu_module_list_t *mod_lst = topo_ptr->started_sorted_module_list_ptr; (NULL != mod_lst); LIST_ADVANCE(mod_lst))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)mod_lst->module_ptr;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         /* Assume a single input port per module (typical for this container). */
         gen_topo_input_port_t *cur_in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         // if buffer is still assigned to capi input, it means capi module process was not called or module
         // left some partial data at input which needs to be replaced by topo buffer.
         // if port still has buffer, it means the borrowed buffer is probably still in use in the NBLC upsteeam
         // which needs to cleared/replaced.
         // If input buf origin is CAPI_MODULE and buffer ptr is NULL, then buffer is successfully propagated and
         // cleared in the upstream already
         if (GEN_TOPO_BUF_ORIGIN_CAPI_MODULE != cur_in_port_ptr->common.flags.buf_origin ||
             (NULL == cur_in_port_ptr->common.sdata.buf_ptr[0].data_ptr))
         {
            continue;
         }

#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Module 0x%lx input port 0x%lx has capi internal buffer",
                  cur_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  cur_in_port_ptr->gu.cmn.id);
#endif
         result = gen_topo_clear_and_replace_capi_input_buffers_util(topo_ptr, cur_in_port_ptr, force_drop_data);
      }
   }

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Exit gen_topo_clear_and_replace_capi_input_buffers");
#endif

   return result;
}