/*==============================================================================
@file capi_ipc_rx.c
@brief This file contains the implementation of the IPC_RX CAPI module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
/* clang-format off */
/* No Edit History for automated refactoring. */
/* clang-format on */

/*------------------------------------------------------------------------
 * Include Files
 * -----------------------------------------------------------------------*/
#include "capi_ipc_rx_utils.h"

/*------------------------------------------------------------------------
 * Static Function Declarations
 * -----------------------------------------------------------------------*/

/**
 * \brief De-initializes and cleans up the IPC_RX CAPI module instance.
 *
 * This function is called when the module is no longer needed, freeing
 * allocated resources and resetting the module's state.
 *
 * \param[in] _pif Pointer to the CAPI instance to be de-initialized.
 *
 * \return CAPI_EOK on success, or an error code if de-initialization fails.
 */
static capi_err_t capi_ipc_rx_end(capi_t *_pif);

/**
 * \brief Sets a specific parameter for the IPC_RX CAPI module.
 *
 * This function handles incoming parameter configurations, validating
 * them and applying them to the module's internal state.
 *
 * \param[in] _pif          Pointer to the CAPI instance.
 * \param[in] param_id      ID of the parameter to set.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL).
 * \param[in] params_ptr    Pointer to the CAPI buffer containing the parameter data.
 *
 * \return CAPI_EOK on success, or an error code if the parameter set operation fails.
 */
static capi_err_t capi_ipc_rx_set_param(capi_t *_pif, uint32_t param_id, const capi_port_info_t *port_info_ptr, capi_buf_t *params_ptr);

/**
 * \brief Retrieves a specific parameter from the IPC_RX CAPI module.
 *
 * This function provides the current value or structure of a requested
 * parameter from the module's internal state.
 *
 * \param[in] _pif          Pointer to the CAPI instance.
 * \param[in] param_id      ID of the parameter to get.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL).
 * \param[out] params_ptr   Pointer to the CAPI buffer to fill with the parameter data.
 *
 * \return CAPI_EOK on success, or an error code if the parameter get operation fails.
 */
static capi_err_t capi_ipc_rx_get_param(capi_t *_pif, uint32_t param_id, const capi_port_info_t *port_info_ptr, capi_buf_t *params_ptr);

/**
 * \brief Sets multiple properties for the IPC_RX CAPI module.
 *
 * This function processes a list of properties, applying each to the module's
 * configuration.
 *
 * \param[in] _pif      Pointer to the CAPI instance.
 * \param[in] props_ptr Pointer to the property list to set.
 *
 * \return CAPI_EOK on success, or an error code if any property set operation fails.
 */
static capi_err_t capi_ipc_rx_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

/**
 * \brief Retrieves multiple properties from the IPC_RX CAPI module.
 *
 * This function populates a property list with the current values of
 * requested properties from the module's configuration.
 *
 * \param[in] _pif      Pointer to the CAPI instance.
 * \param[out] props_ptr Pointer to the property list to fill.
 *
 * \return CAPI_EOK on success, or an error code if any property get operation fails.
 */
static capi_err_t capi_ipc_rx_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

/*------------------------------------------------------------------------
 * Global Variables
 * -----------------------------------------------------------------------*/

/**<
 * \brief Virtual function table instance for the IPC_RX CAPI module.
 *
 * This table links the generic CAPI function calls to their specific
 * implementations within the IPC_RX module.
 */
static capi_vtbl_t vtbl = {
   capi_ipc_rx_process,        /**< Process function */
   capi_ipc_rx_end,            /**< End function */
   capi_ipc_rx_set_param,      /**< Set parameter function */
   capi_ipc_rx_get_param,      /**< Get parameter function */
   capi_ipc_rx_set_properties, /**< Set properties function */
   capi_ipc_rx_get_properties  /**< Get properties function */
};

/*------------------------------------------------------------------------
 * Function Definitions
 * -----------------------------------------------------------------------*/

/**
 * \brief Gets static properties of the IPC_RX CAPI module.
 *
 * This function provides information about the module's capabilities and
 * requirements (e.g., memory, stack size) without needing to create an
 * instance of the module. It acts as a wrapper around `capi_ipc_rx_process_get_properties`.
 *
 * \param[in] init_set_properties Optional pointer to a property list for initialization.
 *                                  Can be NULL if no specific initialization properties are relevant.
 * \param[out] static_properties  Pointer to the property list to be filled with the module's static properties.
 *                                  Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `static_properties` is NULL.
 *         Other CAPI error codes if `capi_ipc_rx_process_get_properties` fails.
 */
capi_err_t capi_ipc_rx_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   /* Validate the output parameter */
   if (NULL == static_properties)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Get static properties received NULL pointer for static_properties.");
      return CAPI_EBADPARAM;
   }

   IPC_RX_MSG(MIID_UNKNOWN, DBG_HIGH_PRIO, "INFO: Entered capi_ipc_rx_get_static_properties.");

   /* Delegate to the shared get_properties function, passing NULL for the instance pointer */
   capi_err_t capi_result = capi_ipc_rx_process_get_properties((capi_ipc_rx_t *)NULL, static_properties);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Processing of static properties failed with error: %lu.", capi_result);
   }

   return capi_result;
}

/**
 * \brief Initializes a new instance of the IPC_RX CAPI module.
 *
 * This function allocates and initializes the module's state, sets up its
 * virtual function table, and processes any initial configuration properties.
 * It also initializes the data flow state and input trigger information.
 *
 * \param[in,out] _pif               Pointer to the CAPI instance to be initialized.
 *                                   Must point to a pre-allocated `capi_ipc_rx_t` structure.
 * \param[in] init_set_properties  Optional pointer to a property list for initial setup.
 *                                   Can be NULL if no initial properties are to be set.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `_pif` is NULL.
 *         Other CAPI error codes if property processing or event raising fails.
 */
capi_err_t capi_ipc_rx_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   /* Validate the input CAPI instance pointer */
   if (NULL == _pif)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Init received NULL pointer for _pif.");
      return CAPI_EBADPARAM;
   }

   /* Cast to the concrete module structure and initialize all members to zero */
   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_ipc_rx_t));

   /* Set the virtual function table */
   me_ptr->vtbl.vtbl_ptr = &vtbl;

   /* Initialize module-specific state variables */
   me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;
   me_ptr->dfs                = IPC_RX_DFS_AT_GAP; /* Data flow state starts at a gap (no data flowing) */

   /* Initialize the input media format structure */
   capi_cmn_init_media_fmt_v2(&me_ptr->input_media_fmt);

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Entered capi_ipc_rx_init.");

   capi_err_t capi_result = CAPI_EOK;

   /* Process any initial properties provided */
   if (NULL != init_set_properties)
   {
      capi_result = capi_ipc_rx_process_set_properties(me_ptr, init_set_properties);
      /* Ignore unsupported errors during initialization, but return for critical failures */
      if (CAPI_FAILED(capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Initialization set property failed with error: %lu.", capi_result);
         return capi_result;
      }
   }

   /* Raise initial events after setting properties */
   capi_result = capi_ipc_rx_raise_event(me_ptr);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Raise event failed with error: %lu.", capi_result);
      /* Even if event raising fails, module is considered initialized if other steps passed
       * This might need refinement based on critical events for module operation. */
   }

   IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: IPC_RX module initialization completed.");
   return capi_result;
}

/**
 * \brief Ends the IPC_RX CAPI module instance and frees allocated resources.
 *
 * This function is responsible for cleaning up the module's state,
 * including clearing its virtual function table pointer and releasing
 * any dynamically allocated memory to prevent memory leaks.
 *
 * \param[in] _pif Pointer to the CAPI instance to be de-initialized.
 *                  Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `_pif` is NULL.
 */
static capi_err_t capi_ipc_rx_end(capi_t *_pif)
{
   /* Validate the input CAPI instance pointer */
   if (NULL == _pif)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: End received NULL pointer for _pif.");
      return CAPI_EBADPARAM;
   }

   /* Cast to the concrete module structure */
   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;

   /* Cache module instance ID for logging before clearing module content */
   uint32_t miid = me_ptr->miid;

   /* Clear the virtual function table pointer to prevent accidental use after end */
   me_ptr->vtbl.vtbl_ptr = NULL;

   /* Free the internal buffer if it was allocated */
   if (me_ptr->int_buf_ptr)
   {
      posal_memory_free(me_ptr->int_buf_ptr);
      me_ptr->int_buf_ptr = NULL; /* Set to NULL after freeing to prevent double-free */
   }

   /* Free any pending media format packet if it exists */
   if (me_ptr->mf_packet_ptr)
   {
      __gpr_cmd_free(me_ptr->mf_packet_ptr);
      me_ptr->mf_packet_ptr = NULL;
   }

   IPC_RX_MSG(miid, DBG_HIGH_PRIO, "INFO: IPC_RX module de-initialization completed.");
   return CAPI_EOK;
}

/**
 * \brief Sets a parameter value or a parameter structure for the IPC_RX CAPI module.
 *
 * This function acts as a thin wrapper, delegating the call to the
 * common utility function `capi_ipc_rx_process_set_param` for actual processing.
 *
 * \param[in] _pif          Pointer to the CAPI instance.
 * \param[in] param_id      ID of the parameter to set.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL).
 * \param[in] params_ptr    Pointer to the CAPI buffer containing the parameter data.
 *
 * \return CAPI_EOK on success, or an error code from `capi_ipc_rx_process_set_param`.
 */
static capi_err_t capi_ipc_rx_set_param(capi_t *_pif, uint32_t param_id, const capi_port_info_t *port_info_ptr, capi_buf_t *params_ptr)
{
   capi_err_t capi_result = capi_ipc_rx_process_set_param(_pif, param_id, port_info_ptr, params_ptr);
   if (CAPI_FAILED(capi_result))
   {
      // Log the error using the module's miid if available, otherwise use MIID_UNKNOWN
      uint32_t miid = (_pif && ((capi_ipc_rx_t *)_pif)->miid) ? ((capi_ipc_rx_t *)_pif)->miid : MIID_UNKNOWN;
      /* Log the error using the module's miid if available, otherwise use MIID_UNKNOWN */
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Set param ID 0x%lx failed with result %lu.", param_id, capi_result);
   }
   return capi_result;
}

/**
 * \brief Gets a parameter value or a parameter structure from the IPC_RX CAPI module.
 *
 * This function acts as a thin wrapper, delegating the call to the
 * common utility function `capi_ipc_rx_process_get_param` for actual processing.
 *
 * \param[in] _pif          Pointer to the CAPI instance.
 * \param[in] param_id      ID of the parameter to get.
 * \param[in] port_info_ptr Pointer to port-specific information (can be NULL).
 * \param[out] params_ptr   Pointer to the CAPI buffer to fill with the parameter data.
 *
 * \return CAPI_EOK on success, or an error code from `capi_ipc_rx_process_get_param`.
 */
static capi_err_t capi_ipc_rx_get_param(capi_t *_pif, uint32_t param_id, const capi_port_info_t *port_info_ptr, capi_buf_t *params_ptr)
{
   capi_err_t capi_result = capi_ipc_rx_process_get_param(_pif, param_id, port_info_ptr, params_ptr);
   if (CAPI_FAILED(capi_result))
   {
      // Log the error using the module's miid if available, otherwise use MIID_UNKNOWN
      uint32_t miid = (_pif && ((capi_ipc_rx_t *)_pif)->miid) ? ((capi_ipc_rx_t *)_pif)->miid : MIID_UNKNOWN;
      /* Log the error using the module's miid if available, otherwise use MIID_UNKNOWN */
      IPC_RX_MSG(miid, DBG_ERROR_PRIO, "Error: Get param ID 0x%lx failed with result %lu.", param_id, capi_result);
   }
   return capi_result;
}

/**
 * \brief Sets properties for the IPC_RX CAPI module.
 *
 * This function casts the generic CAPI instance pointer to the module's
 * concrete type (`capi_ipc_rx_t`) and delegates the call to the
 * common utility function `capi_ipc_rx_process_set_properties`.
 *
 * \param[in] _pif      Pointer to the CAPI instance.
 * \param[in] props_ptr Pointer to the property list to set.
 *
 * \return CAPI_EOK on success, or an error code from `capi_ipc_rx_process_set_properties`.
 */
static capi_err_t capi_ipc_rx_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   /* Validate the input CAPI instance pointer */
   if (NULL == _pif)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Set properties received NULL pointer for _pif.");
      return CAPI_EBADPARAM;
   }

   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;
   capi_err_t capi_result = capi_ipc_rx_process_set_properties(me_ptr, props_ptr);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Set properties failed with result %lu.", capi_result);
   }
   return capi_result;
}

/**
 * \brief Gets properties from the IPC_RX CAPI module.
 *
 * This function casts the generic CAPI instance pointer to the module's
 * concrete type (`capi_ipc_rx_t`) and delegates the call to the
 * common utility function `capi_ipc_rx_process_get_properties`.
 *
 * \param[in] _pif      Pointer to the CAPI instance.
 * \param[out] props_ptr Pointer to the property list to fill.
 *
 * \return CAPI_EOK on success, or an error code from `capi_ipc_rx_process_get_properties`.
 */
static capi_err_t capi_ipc_rx_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   /* Validate the input CAPI instance pointer */
   if (NULL == _pif)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Get properties received NULL pointer for _pif.");
      return CAPI_EBADPARAM;
   }

   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)_pif;
   capi_err_t capi_result = capi_ipc_rx_process_get_properties(me_ptr, props_ptr);
   if (CAPI_FAILED(capi_result))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Get properties failed with result %lu.", capi_result);
   }
   return capi_result;
}
