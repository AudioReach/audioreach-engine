/*
 * gpr_init_zephyr_wrapper.c
 *
 * This file has implementation platform wrapper for the GPR datalink layer
 * for Zephyr.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ar_msg.h"
#include "gpr_api_i.h"
#include "gpr_zephyr.h"

#define GPR_NUM_PACKETS_TYPE 3

#define GPR_NUM_PACKETS_1 ( 100 )
#define GPR_DRV_BYTES_PER_PACKET_1 ( 512 )
#define GPR_NUM_PACKETS_2 ( 4 )
#define GPR_DRV_BYTES_PER_PACKET_2 ( 4096 )
#define GPR_NUM_PACKETS_3 ( 0 )
#define GPR_DRV_BYTES_PER_PACKET_3 ( 65536 )

/*****************************************************************************
 * Global variables                                                          *
 ****************************************************************************/
/* GPR IPC table containing init,deinit functions for datalink layers depending on
domains a given src domain wishes to establish a link with and the availability
of shared memory */
struct ipc_dl_v2_t gpr_zephyr_ipc_dl_table[GPR_PL_NUM_TOTAL_DOMAINS_V];

struct gpr_packet_pool_info_v2_t gpr_zephyr_packet_pool_table[GPR_NUM_PACKETS_TYPE]={
   { GPR_HEAP_INDEX_DEFAULT, 0, 0, GPR_NUM_PACKETS_1, GPR_DRV_BYTES_PER_PACKET_1},
   { GPR_HEAP_INDEX_DEFAULT, 0, 0, GPR_NUM_PACKETS_2, GPR_DRV_BYTES_PER_PACKET_2},
   { GPR_HEAP_INDEX_DEFAULT, 1, 0, GPR_NUM_PACKETS_3, GPR_DRV_BYTES_PER_PACKET_3},
};

uint32_t num_domains = 0;

/*****************************************************************************
 * Local function definitions                                                *
 ****************************************************************************/

GPR_INTERNAL uint32_t gpr_drv_init(void)
{
   AR_MSG(DBG_HIGH_PRIO,"GPR INIT START");
   uint32_t rc, domain_id;
   uint32_t num_packet_pools =
             sizeof(gpr_zephyr_packet_pool_table)/sizeof(gpr_packet_pool_info_v2_t);

   /* Reset to 0 to avoid wrong value when gpr_drv_init called multiple times due to failure */
   num_domains = 0;
   memset(&gpr_zephyr_ipc_dl_table[0], 0, (sizeof(struct ipc_dl_v2_t) * GPR_PL_NUM_TOTAL_DOMAINS_V));

   gpr_zephyr_ipc_dl_table[num_domains].domain_id = GPR_IDS_DOMAIN_ID_APPS_V;
   gpr_zephyr_ipc_dl_table[num_domains].init_fn = ipc_dl_zephyr_init;
   gpr_zephyr_ipc_dl_table[num_domains].deinit_fn = ipc_dl_zephyr_deinit;
   gpr_zephyr_ipc_dl_table[num_domains].supports_shared_mem = TRUE;

   num_domains++;

   gpr_zephyr_ipc_dl_table[num_domains].domain_id = GPR_IDS_DOMAIN_ID_ADSP_V;
   gpr_zephyr_ipc_dl_table[num_domains].init_fn = ipc_dl_local_init;
   gpr_zephyr_ipc_dl_table[num_domains].deinit_fn = ipc_dl_local_deinit;
   gpr_zephyr_ipc_dl_table[num_domains].supports_shared_mem = TRUE;

   num_domains++;
   domain_id = GPR_IDS_DOMAIN_ID_ADSP_V;
   rc = gpr_drv_internal_init_v2(domain_id,
                                 num_domains,
                                 gpr_zephyr_ipc_dl_table,
                                 num_packet_pools,
                                 gpr_zephyr_packet_pool_table);
   AR_MSG(DBG_HIGH_PRIO, "GPR INIT EXIT");
   return rc;
}

GPR_INTERNAL uint32_t gpr_drv_init_domain(uint32_t domain_id)
{
   AR_MSG(DBG_HIGH_PRIO, "GPR INIT START, for domain id %d", domain_id);
   uint32_t rc;
   uint32_t num_packet_pools =
             sizeof(gpr_zephyr_packet_pool_table)/sizeof(gpr_packet_pool_info_v2_t);

   memset(&gpr_zephyr_ipc_dl_table[0], 0, (sizeof(struct ipc_dl_v2_t) * GPR_PL_NUM_TOTAL_DOMAINS_V));

   gpr_zephyr_ipc_dl_table[num_domains].domain_id = domain_id;
   gpr_zephyr_ipc_dl_table[num_domains].init_fn = ipc_dl_local_init;
   gpr_zephyr_ipc_dl_table[num_domains].deinit_fn = ipc_dl_local_deinit;
   gpr_zephyr_ipc_dl_table[num_domains].supports_shared_mem = TRUE;

   num_domains++;

   rc = gpr_drv_internal_init_v2(domain_id,
                                 num_domains,
                                 gpr_zephyr_ipc_dl_table,
                                 num_packet_pools,
                                 gpr_zephyr_packet_pool_table);
   AR_MSG(DBG_HIGH_PRIO, "GPR INIT EXIT");
   return rc;
}