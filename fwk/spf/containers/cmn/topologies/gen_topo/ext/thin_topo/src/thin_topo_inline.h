#ifndef THIN_TOPO_INLINE_H
#define THIN_TOPO_INLINE_H
/**
 * \file thin_topo_inline.h
 * \brief
 *     This file contains utility inline functions for Thin topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "thin_topo.h"
#include "ar_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

static inline bool_t check_if_thin_topo_manager_is_initialized(gen_topo_t *topo_ptr)
{
   return topo_ptr->thin_topo_ptr ? TRUE : FALSE;
}

static inline bool_t check_if_needed_to_exit_thin_topo(gen_topo_t *topo_ptr)
{
   return topo_ptr->exit_flags.word ? TRUE : FALSE;
}

static inline bool_t check_if_cntr_is_processing_with_thin_topo(gen_topo_t *topo_ptr)
{
   return (check_if_thin_topo_manager_is_initialized(topo_ptr) && (THIN_TOPO_ENTERED == topo_ptr->thin_topo_ptr->state))
             ? TRUE
             : FALSE;
}

#define THIN_TOPO_CHECK_IF_EXIT_FLAG_SET(topo_ptr, flag) (TRUE == topo_ptr->exit_flags.flag)

#define THIN_TOPO_SET_EXIT_FLAG(topo_ptr, flag, VALUE)                                                                 \
   {                                                                                                                   \
      topo_ptr->exit_flags.flag = VALUE;                                                                               \
   }

static inline void thin_topo_incr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t *md_list_ptr)
{
   // add the node to the active list
   spf_list_insert_tail((spf_list_node_t **)&topo_ptr->list_of_active_md_node_ptr,
                        md_list_ptr,
                        topo_ptr->heap_id,
                        TRUE /* use pool */);

   uint8_t count = (uint8_t)topo_ptr->exit_flags.num_active_md_nodes_in_cntr;
   if (count < UINT8_MAX)
   {
      count++;
      THIN_TOPO_SET_EXIT_FLAG(topo_ptr, num_active_md_nodes_in_cntr, count);
   }
   else
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Unexpected error in thin topo active metadata counter %lu", count);
      THIN_TOPO_SET_EXIT_FLAG(topo_ptr, num_active_md_nodes_in_cntr, UINT8_MAX);
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Found new metadata list node 0x%lx incremented thin topo's active MD counter to %lu",
            md_list_ptr,
            topo_ptr->exit_flags.num_active_md_nodes_in_cntr);
#endif
}

static inline void thin_topo_count_and_incr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t *md_list_ptr)
{
   while (md_list_ptr)
   {
      thin_topo_incr_active_md_nodes(topo_ptr, md_list_ptr);
      md_list_ptr = md_list_ptr->next_ptr;
   }
}

static inline void thin_topo_decr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t *md_list_ptr)
{
   bool_t is_found =
      spf_list_find_delete_node((spf_list_node_t **)&topo_ptr->list_of_active_md_node_ptr, md_list_ptr, TRUE);

   if (is_found)
   {
      uint8_t count = (uint8_t)topo_ptr->exit_flags.num_active_md_nodes_in_cntr;
      if (count > 0)
      {
         count--;
         THIN_TOPO_SET_EXIT_FLAG(topo_ptr, num_active_md_nodes_in_cntr, count);
      }
      else
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Unexpected error in thin topo active metadata counter %lu",
                  count);
      }
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Destroying metadata list node 0x%lx is_found:%lu decremented thin topo's active MD counter to %lu",
            md_list_ptr,
            is_found,
            topo_ptr->exit_flags.num_active_md_nodes_in_cntr);
#endif
}

static inline void thin_topo_count_and_decr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t *md_list_ptr)
{
   while (md_list_ptr)
   {
      thin_topo_decr_active_md_nodes(topo_ptr, md_list_ptr);
      md_list_ptr = md_list_ptr->next_ptr;
   }
}

static inline void thin_topo_check_get_start_module(gen_topo_t *topo_ptr, gu_module_list_t **start_module_list_pptr)
{
   // get the rest of module proc list ptr if the gen topo is called from the thin topo's exit context
   if (topo_ptr->thin_topo_ptr && (THIN_TOPO_SWITCHED_TO_GEN_TOPO != topo_ptr->thin_topo_ptr->state))
   {
      *start_module_list_pptr = topo_ptr->thin_topo_ptr->rest_of_module_proc_list_ptr;
   }
}

#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* THIN_TOPO_INLINE_H */
