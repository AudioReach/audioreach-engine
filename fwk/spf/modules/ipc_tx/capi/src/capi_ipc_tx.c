/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx.cpp
 *
 * C source file to implement the IPC_TX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

#include "capi_ipc_tx_utils.h"

static capi_err_t capi_ipc_tx_end(capi_t *_pif);

static capi_err_t capi_ipc_tx_set_param(capi_t                 *_pif,
                                        uint32_t                param_id,
                                        const capi_port_info_t *port_info_ptr,
                                        capi_buf_t             *params_ptr);

static capi_err_t capi_ipc_tx_get_param(capi_t                 *_pif,
                                        uint32_t                param_id,
                                        const capi_port_info_t *port_info_ptr,
                                        capi_buf_t             *params_ptr);

static capi_err_t capi_ipc_tx_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_ipc_tx_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_vtbl_t vtbl = { capi_ipc_tx_process,        capi_ipc_tx_end,
                            capi_ipc_tx_set_param,      capi_ipc_tx_get_param,
                            capi_ipc_tx_set_properties, capi_ipc_tx_get_properties };

/* -------------------------------------------------------------------------
 * Function name: capi_ipc_tx_get_static_properties
 * Capi_v2 IPC_TX function to get the static properties
 * -------------------------------------------------------------------------*/
capi_err_t capi_ipc_tx_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   capi_err_t capi_result = CAPI_EOK;
   IPC_TX_MSG(MIID_UNKNOWN, DBG_HIGH_PRIO, "Enter get static properties");
   if (NULL != static_properties)
   {
      capi_result = capi_ipc_tx_process_get_properties((capi_ipc_tx_t *)NULL, static_properties);
      if (CAPI_FAILED(capi_result))
      {
         IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "get static properties failed!");
         return capi_result;
      }
   }
   else
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get static properties received bad pointer, 0x%p", static_properties);
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_ipc_tx_init
  Initialize the CAPIv2 IPC_TX Module. This function can allocate memory.
 * -----------------------------------------------------------------------*/

capi_err_t capi_ipc_tx_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;
   IPC_TX_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, "Enter ipc_tx init");
   if (NULL == _pif || NULL == init_set_properties)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Init received bad pointer, 0x%p, 0x%p", _pif, init_set_properties);

      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)_pif;

   memset(me_ptr, 0, sizeof(capi_ipc_tx_t));

   me_ptr->vtbl.vtbl_ptr                           = &vtbl;
   me_ptr->sh_buf_info.ipc_sh_mem_info.shm_mem_ptr = NULL;
   me_ptr->sh_buf_info.metadata_buff_size          = IPC_TX_MD_BUFFER_SIZE;
   me_ptr->sh_buf_info.num_ipc_bufs_needed         = 2; // default ipc buffer count(ping-pong)
   me_ptr->base_token                              = 0;

   // to prevent the container from waiting at the output port;
   // it should begin waiting at the input port
   me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;

   capi_cmn_init_media_fmt_v2(&me_ptr->inp_media_fmt);

   if (NULL != init_set_properties)
   {
      capi_result = capi_ipc_tx_process_set_properties(me_ptr, init_set_properties);
      // ignore unsupported error
      if (CAPI_FAILED(capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Initialization Set Property Failed");
         return capi_result;
      }
   }

   capi_result |= capi_ipc_tx_raise_event(me_ptr);

   capi_result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Init done !!");
   return capi_result;
}

/*------------------------------------------------------------------------
 * Function name: capi_ipc_tx_end
 * IPC_TX End function, returns the library to the uninitialized
 * state and frees all the memory that was allocated. This function also
 * frees the virtual function table.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif)
   {
      IPC_TX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "End received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)_pif;
   uint32_t       miid   = me_ptr->miid;

   for (uint32_t buffer_index = 0; buffer_index < me_ptr->sh_buf_info.num_ipc_bufs_created; buffer_index++)
   {
      capi_result = capi_ipc_tx_free_shmem(me_ptr, buffer_index);
   if (CAPI_FAILED(capi_result))
   {
         IPC_TX_MSG(miid, DBG_ERROR_PRIO, "Free shared memory failed at end for index %d", buffer_index);
         return capi_result;
      }
   }
   if (me_ptr->sh_buf_info.shared_mem_buf_handle)
   {
      posal_memory_free(me_ptr->sh_buf_info.shared_mem_buf_handle);
   }

   if (me_ptr->sh_buf_info.overrun_buffer_ptr)
   {
      posal_memory_free(me_ptr->sh_buf_info.overrun_buffer_ptr);
      me_ptr->sh_buf_info.overrun_buffer_ptr = NULL;
   }

   if(me_ptr->sh_buf_info.pending_prebuf_index_arr)
   {
      posal_memory_free(me_ptr->sh_buf_info.pending_prebuf_index_arr);
      me_ptr->sh_buf_info.pending_prebuf_index_arr = NULL;
   }

   me_ptr->vtbl.vtbl_ptr = NULL;

   IPC_TX_MSG(miid, DBG_HIGH_PRIO, "End done");
   return capi_result;
}

/* -------------------------------------------------------------------------
 * Function name: capi_ipc_tx_set_param
 * Sets either a parameter value or a parameter structure containing
 * multiple parameters. In the event of a failure, the appropriate error
 * code is returned.
 * -------------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_set_param(capi_t                 *_pif,
                                        uint32_t                param_id,
                                        const capi_port_info_t *port_info_ptr,
                                        capi_buf_t             *params_ptr)
{
   return capi_ipc_tx_process_set_param(_pif, param_id, port_info_ptr, params_ptr);
}

/* -------------------------------------------------------------------------
 * Function name: capi_ipc_tx_get_param
 * Gets either a parameter value or a parameter structure containing
 * multiple parameters. In the event of a failure, the appropriate error
 * code is returned.
 * -------------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_get_param(capi_t                 *_pif,
                                        uint32_t                param_id,
                                        const capi_port_info_t *port_info_ptr,
                                        capi_buf_t             *params_ptr)
{
   return capi_ipc_tx_process_get_param(_pif, param_id, port_info_ptr, params_ptr);
}

/* -------------------------------------------------------------------------
 * Function name: capi_ipc_tx_set_properties
 * Function to set the properties of IPC_TX module
 * -------------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)_pif;
   return capi_ipc_tx_process_set_properties(me_ptr, props_ptr);
}

/* -------------------------------------------------------------------------
 * Function name: capi_ipc_tx_get_properties
 * Function to get the properties of IPC_TX module
 * -------------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)_pif;
   return capi_ipc_tx_process_get_properties(me_ptr, props_ptr);
}
