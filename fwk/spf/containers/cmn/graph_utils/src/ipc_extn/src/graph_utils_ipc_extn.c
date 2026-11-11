/**
 * \file graph_utils_ipc_extn.c
 *
 * \brief
 *
 *     Graph utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "graph_utils.h"
#include "posal.h"
#include "amdb_static.h"
#include "spf_list_utils.h"
#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_client_api.h"
#include "rd_sh_mem_client_api.h"
#include "spf_macros.h"
#include "gpr_api_inline.h"
#include "offload_apm_api.h"
#include "spf_svc_utils.h"
#include "ipc_tx_rx_api.h"

/** max ports just for bounds check. also internal variables for num ports are 8 bits*/
#define MAX_PORTS 100

#define GU_MSG_PREFIX "GU  :%08lX: "

#define GU_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, GU_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

ar_result_t gu_insert_ipc_output_port(gu_t             *gu_ptr,
                                       gu_module_t      *module_ptr,
                                       gu_output_port_t *output_port_ptr,
                                       POSAL_HEAP_ID     heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // module can have only IPC or regular ports, not both, check module structure for more details.
   VERIFY(result, (NULL == module_ptr->output_port_list_ptr));

   TRY(result,
       gu_insert_data_port(gu_ptr,
                           module_ptr,
                           ((spf_list_node_t **)&module_ptr->ipc_output_port_list_ptr),
                           (gu_cmn_port_t *)output_port_ptr,
                           heap_id));

   output_port_ptr->cmn.flags.is_ipc_port = TRUE;
   module_ptr->num_ipc_output_ports++;

   GU_MSG(gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Created IPC internal output port %lu for module 0x%X",
          output_port_ptr->cmn.id,
          module_ptr->module_instance_id);

   // Mark module and sg as updated
   gu_set_status(&module_ptr->gu_status, GU_STATUS_UPDATED);
   gu_set_status(&module_ptr->sg_ptr->gu_status, GU_STATUS_UPDATED);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

ar_result_t gu_insert_ipc_input_port(gu_t            *gu_ptr,
                                      gu_module_t     *module_ptr,
                                      gu_input_port_t *input_port_ptr,
                                      POSAL_HEAP_ID    heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // module can have only IPC or regular ports, not both, check module structure for more details.
   VERIFY(result, (NULL == module_ptr->input_port_list_ptr));

   TRY(result,
       gu_insert_data_port(gu_ptr,
                           module_ptr,
                           ((spf_list_node_t **)&module_ptr->ipc_input_port_list_ptr),
                           (gu_cmn_port_t *)input_port_ptr,
                           heap_id));

   input_port_ptr->cmn.flags.is_ipc_port = TRUE;
   module_ptr->num_ipc_input_ports++;

   GU_MSG(gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Created IPC internal input port %lu for module 0x%X",
          input_port_ptr->cmn.id,
          module_ptr->module_instance_id);

   // Mark module and sg as updated
   gu_set_status(&module_ptr->gu_status, GU_STATUS_UPDATED);
   gu_set_status(&module_ptr->sg_ptr->gu_status, GU_STATUS_UPDATED);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

bool_t gu_does_module_needs_ipc_port(gu_module_t *module_ptr, bool_t is_input_port)
{
   if ((MODULE_ID_IPC_RX == module_ptr->module_id) && is_input_port)
   {
      return TRUE;
   }
   if ((MODULE_ID_IPC_TX == module_ptr->module_id) && !is_input_port)
   {
      return TRUE;
   }
   return FALSE;
}