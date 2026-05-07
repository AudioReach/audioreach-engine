/**
 * \file posal_linux_stubs.c
 *
 * \brief
 *  	This file contains the stub implementation for the APIs that are not yet
 *      implemented for Linux SPF
 *
 * \copyright
 *      Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *      SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal.h"
#include "platform_cfg.h"

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
/* Global island heap id variable defined here */
/** Default island heap = POSAL_HEAP_DEFAULT on Linux */
POSAL_HEAP_ID spf_mem_island_heap_id = POSAL_HEAP_DEFAULT;

/* -------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */

bool_t posal_island_get_island_status(void)
{
   return FALSE;
}

ar_result_t posal_island_trigger_island_exit(void)
{
   return AR_EUNSUPPORTED;
}

ar_result_t core_drv_reset(){
   return AR_EOK;
}

POSAL_HEAP_ID posal_private_get_island_heap_id_v2(uint32_t island_heap_type)
{
   return spf_mem_island_heap_id;
}

mid_stack_pair_info_t *get_platform_prop_info(platform_prop_t prop, uint32_t *elem_num)
{
   *elem_num = 0;
   return NULL;
}

ar_result_t posal_power_mgr_send_command(uint32_t msg_opcode, void *payload_ptr , uint32_t payload_size){
   return AR_EOK;
}
