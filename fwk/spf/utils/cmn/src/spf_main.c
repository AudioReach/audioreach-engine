/**
 * \file spf_main.c
 * \brief
 *     This file contains the initialization routines for the
 *         SPF framework
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal.h"
#include "spf_utils.h"
#ifdef USES_SPF_THREAD_POOL
#include "spf_thread_pool.h"
#endif
#include "spf_watchdog_svc.h"
#include "spf_main.h"
#include "amdb_static.h"
#include "apm.h"
#include "dls.h"
#include "irm.h"
#include "posal_tgt_util.h"
#ifdef ENABLE_MINIDUMP_RODATA_TEXT_REMOVAL
#include "adspservices_qdi_client.h"
#endif

/* =======================================================================
**                          Macro definitions
** ======================================================================= */

/* =======================================================================
**                          Function Definitions
** ======================================================================= */

/* =======================================================================
**                          Global Variable Definitions
** ======================================================================= */


extern void gen_cntr_print_mem_req();
extern void spl_cntr_print_mem_req();
extern void apm_print_mem_req();
/* =======================================================================
**                          Functions
** ======================================================================= */
ar_result_t spf_framework_pre_init(void)
{
   ar_result_t result_spf_mem_init, result = AR_EOK;

#ifdef ENABLE_MINIDUMP_RODATA_TEXT_REMOVAL
   #ifdef MINIDUMP_AUDIOPD_REMOVAL
      unsigned int audiopd_size =  (unsigned int )&audiopd_eaddr - (unsigned int )&audiopd_saddr;
      unsigned int  audiopd_saddrpg = (unsigned int )qurt_lookup_physaddr((unsigned int )&audiopd_saddr);
      md_update_aperture_entry(audiopd_saddrpg,audiopd_size);
   #else
      unsigned int audiopd_rodata_size =  (unsigned int )&audiopd_rodata_eaddr - (unsigned int )&audiopd_rodata_saddr;
      unsigned int  audiopd_rodata_saddrpg = (unsigned int )qurt_lookup_physaddr((unsigned int )&audiopd_rodata_saddr);
      md_update_aperture_entry(audiopd_rodata_saddrpg,audiopd_rodata_size);
      unsigned int audiopd_text_size =  (unsigned int )&audiopd_text_eaddr - (unsigned int )&audiopd_text_saddr;
      unsigned int  audiopd_text_saddrpg = (unsigned int )qurt_lookup_physaddr((unsigned int )&audiopd_text_saddr);
      md_update_aperture_entry(audiopd_text_saddrpg,audiopd_text_size);
   #endif
#endif

   spf_list_buf_pool_init(POSAL_HEAP_DEFAULT, REGULAR_LIST_BUF_POOL_NUM_ARRAYS, REGULAR_LIST_BUF_POOL_NODES_PER_ARR);
#ifdef USES_AUDIO_IN_ISLAND
   spf_list_buf_pool_init(MODIFY_HEAP_ID_FOR_FWK_ALLOC_FOR_MEM_TRACKING(spf_mem_island_heap_id), LPI_LIST_BUF_POOL_NUM_ARRAYS, LPI_LIST_BUF_POOL_NODES_PER_ARR);
   spf_lpi_pool_init(LPI_GENERAL_BUF_POOL_NODE_SIZE, LPI_GENERAL_BUF_POOL_NUM_ARRAYS, LPI_GENERAL_BUF_POOL_NODES_PER_ARR, MODIFY_HEAP_ID_FOR_FWK_ALLOC_FOR_MEM_TRACKING(spf_mem_island_heap_id));
#endif /* USES_AUDIO_IN_ISLAND */

   // TODO: check results for below 2 operations
   result_spf_mem_init = spf_bufmgr_global_init(POSAL_HEAP_DEFAULT);
   if (AR_DID_FAIL(result_spf_mem_init))
   {
      AR_MSG(DBG_FATAL_PRIO, "FAILED to init gk global memory pool with result %d", result_spf_mem_init);
      return AR_EFAILED;
   }

#ifdef POSAL_DBG_HEAP_CONSUMPTION
   AR_MSG(DBG_HIGH_PRIO, "HEAPUSE after spf_bufmgr_global_init %d", posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].curr_heap);
#endif /* POSAL_DBG_HEAP_CONSUMPTION */

   bool_t INIT_CMD_THREAD_TRUE = TRUE;
   amdb_init(POSAL_HEAP_DEFAULT, INIT_CMD_THREAD_TRUE);
#if !defined(AUDIOSSMODE)
   result = irm_init(POSAL_HEAP_DEFAULT);

   AR_MSG(DBG_HIGH_PRIO, "IRM is initialized");
#endif

   result = dls_init();

   return result;
}

ar_result_t spf_framework_post_init(void){
   ar_result_t result = AR_EOK;

   /* create APM service : must be the last one because spf up state is sent by APM upon query from the client. */
   result = apm_create();

#ifdef USES_SPF_THREAD_POOL
   result = spf_thread_pool_init();
#endif

   spf_watchdog_svc_init();

   AR_MSG(DBG_HIGH_PRIO, "spf framework Initialized");

#ifdef USES_DEBUG_DEV_ENV
   gen_cntr_print_mem_req();
   spl_cntr_print_mem_req();
   apm_print_mem_req();
#endif

   return result;
}

ar_result_t spf_framework_start(void)
{

   AR_MSG(DBG_HIGH_PRIO, "spf framework Start");

   return AR_EOK;
}

ar_result_t spf_framework_stop(void)
{

   AR_MSG(DBG_HIGH_PRIO, "spf framework Stop");

   return AR_EOK;
}

ar_result_t spf_framework_pre_deinit(void)
{
#ifndef DISABLE_DEINIT

   spf_watchdog_svc_deinit();

#ifdef USES_SPF_THREAD_POOL
   spf_thread_pool_deinit();
#endif

   /* First destroy the apm service*/
   apm_destroy();

#endif //#ifndef DISABLE_DEINIT

   return AR_EOK;
}

ar_result_t spf_framework_post_deinit(void)
{
#ifndef DISABLE_DEINIT

   dls_deinit();

   /* AMDB deinit (need buf pool for pm voting) */
   bool_t INIT_CMD_THREAD_TRUE = TRUE;
   amdb_deinit(INIT_CMD_THREAD_TRUE);
#if !defined(AUDIOSSMODE)
   irm_deinit();
#endif
   spf_list_buf_pool_deinit(POSAL_HEAP_DEFAULT);

#ifdef USES_AUDIO_IN_ISLAND
   spf_list_buf_pool_deinit(spf_mem_island_heap_id);
   spf_lpi_pool_deinit();
#endif /* USES_AUDIO_IN_ISLAND */

   /* Clean up gk global memory pool */
   spf_bufmgr_global_deinit();

   AR_MSG(DBG_HIGH_PRIO, "spf framework de-inited");
#endif //#ifndef DISABLE_DEINIT

   return AR_EOK;
}
