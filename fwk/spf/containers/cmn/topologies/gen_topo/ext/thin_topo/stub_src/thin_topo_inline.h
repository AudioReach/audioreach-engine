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

// clang-format off

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

#define THIN_TOPO_SET_EXIT_FLAG(topo_ptr, flag, VALUE)

#define THIN_TOPO_CHECK_IF_EXIT_FLAG_SET(topo_ptr, flag)

static inline bool_t check_if_cntr_is_processing_with_thin_topo(gen_topo_t *topo_ptr)
{
   return FALSE;
}

static inline bool_t check_if_thin_topo_manager_is_initialized(gen_topo_t *topo_ptr)
{
   return FALSE;
}

static inline bool_t check_if_needed_to_exit_thin_topo(gen_topo_t *topo_ptr)
{
   return TRUE;
}

static inline void thin_topo_incr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t* md_list_ptr)
{
   return;
}

static inline void thin_topo_count_and_incr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t* md_list_ptr)
{
   return;
}

static inline void thin_topo_decr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t* md_list_ptr)
{
   return;
}

static inline void thin_topo_count_and_decr_active_md_nodes(gen_topo_t *topo_ptr, module_cmn_md_list_t *md_list_ptr)
{
   return;
}

static inline void thin_topo_check_get_gen_topo_next_proc_module(gen_topo_t *topo_ptr, gu_module_list_t **start_module_list_pptr)
{
   return;
}


#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* THIN_TOPO_INLINE_H */
