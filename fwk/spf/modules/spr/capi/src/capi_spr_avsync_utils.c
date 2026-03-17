/**
 *   \file capi_spr_avsync_utils.c
 *   \brief
 *        This file contains implementation of the AVSync Utilities used by the SPR module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_spr_i.h"

/*==============================================================================
   Static Functions
==============================================================================*/

static uint32_t spr_get_outport_id_from_arr_idx(capi_spr_t *me_ptr, uint32_t arr_idx);
static capi_err_t capi_spr_avsync_check_setup_defaults(avsync_t *avsync_ptr);
static capi_err_t capi_spr_update_avsync_config(capi_spr_t *me_ptr, param_id_spr_avsync_config_t *params_ptr);

/*==============================================================================
   Functions
==============================================================================*/

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_create
  Creates the avsync instance for the SPR module
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_create(avsync_t **avsync_ptr_ptr, POSAL_HEAP_ID heap_id, uint32_t miid)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == avsync_ptr_ptr)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG(miid, DBG_ERROR_PRIO, "avsync: Received bad params for create");
#endif
      return CAPI_EBADPARAM;
   }

   // allocate memory for the avsync lib
   *avsync_ptr_ptr = (avsync_t *)posal_memory_aligned_malloc(sizeof(avsync_t), MALLOC_8_BYTE_ALIGN, heap_id);

   if (NULL == *avsync_ptr_ptr)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG(miid, DBG_ERROR_PRIO, "avsync: failed to allocate memory during create");
#endif
      return CAPI_ENOMEMORY;
   }

   avsync_t *alloc_ptr = *avsync_ptr_ptr;

   // Initialize the allocated avsync library
   memset(alloc_ptr, 0, sizeof(avsync_t));

   capi_spr_avsync_set_miid(alloc_ptr, miid);
   capi_spr_avsync_check_setup_defaults(alloc_ptr);

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_destroy
  Destroys the avsync instance for the SPR module
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_destroy(avsync_t **avsync_ptr_ptr, uint32_t miid)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == avsync_ptr_ptr)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG(miid, DBG_ERROR_PRIO, "avsync: invalid pointer");
#endif
      return result;
   }

   avsync_t *lib_ptr = *avsync_ptr_ptr;

   if (!lib_ptr)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG(miid, DBG_HIGH_PRIO, "avsync: lib already destroyed");
#endif
      return result;
   }

   posal_memory_aligned_free(lib_ptr);
   *avsync_ptr_ptr = NULL;

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_init
  Initializes the avsync instance for the SPR module
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_init(avsync_t *avsync_ptr)
{
   if (!avsync_ptr)
   {
      AR_MSG(DBG_LOW_PRIO, "capi_spr: avsync: feature not enabled");
      return CAPI_EOK;
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_deinit
  De-initializes the avsync instance for the SPR module
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_deinit(avsync_t *avsync_ptr)
{
   if (!avsync_ptr)
   {
      AR_MSG(DBG_LOW_PRIO, "capi_spr: avsync: feature not enabled");
      return CAPI_EOK;
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_reset_session_clock_params
  Resets the session clock parameters for the avsync instance. Currently invoked
  when input port is stopped or reset
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_reset_session_clock_params(avsync_t *avsync_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!is_spr_avsync_enabled(avsync_ptr))
   {
      return CAPI_EOK;
   }

   avsync_ptr->absolute_time_us            = 0;
   avsync_ptr->base_timestamp_us           = 0;
   avsync_ptr->expected_session_clock_us   = 0;
   avsync_ptr->flags.is_ts_valid           = 0;
   avsync_ptr->flags.is_first_buf_rcvd     = 0;
   avsync_ptr->flags.is_first_buf_rendered = 0;
   avsync_ptr->proc_timestamp_us           = 0;
   avsync_ptr->session_clock_us            = 0;

   avsync_ptr->elapsed_session_clock_samples          = 0;
   avsync_ptr->elapsed_expected_session_clock_samples = 0;

   // Retain speed factor till SPR is closed/TSM sends a new MD
   // This assumes that SPR and TSM are in the same subgraph (stream)
   avsync_ptr->tsm_info.tsm_session_clk_samples = 0;
   avsync_ptr->tsm_info.remainder_tsm_samples = 0;

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_reset_session_clock
  Resets the session clock value
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_reset_session_clock(avsync_t *avsync_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == avsync_ptr)
   {
#ifdef AVSYNC_DEBUG
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: avsync: Received invalid params for reset session clock ");
#endif
      return CAPI_EBADPARAM;
   }

   avsync_ptr->session_clock_us = 0;

#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: reset session clock %ld", avsync_ptr->session_clock_us);
#endif

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_check_update_primary_output_port
   Identifies the primary output port for the module. This is invoked any time
   there is a change in the data port state.
* ------------------------------------------------------------------------------*/
void capi_spr_avsync_check_update_primary_output_port(capi_spr_t *me_ptr)
{

   uint32_t curr_pri_arr_idx    = me_ptr->primary_output_arr_idx;
   uint32_t updated_pri_arr_idx = UMAX_32;

#ifdef AVSYNC_DEBUG
   SPR_MSG(me_ptr->miid,
           DBG_MED_PRIO,
           "avsync: Curr Primary Output Port ID 0x%x ",
           spr_get_outport_id_from_arr_idx(me_ptr, curr_pri_arr_idx));
#endif

   if (curr_pri_arr_idx < me_ptr->max_output_ports &&
       DATA_PORT_STATE_STARTED == me_ptr->out_port_info_arr[curr_pri_arr_idx].port_state)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG(me_ptr->miid, DBG_MED_PRIO, "avsync: No need to update primary output port ");
#endif
      return;
   }

   for (uint32_t idx = 0; idx < me_ptr->max_output_ports; idx++)
   {
      if (DATA_PORT_STATE_STARTED == me_ptr->out_port_info_arr[idx].port_state)
      {
         updated_pri_arr_idx = idx;
         break;
      }
   }

   me_ptr->primary_output_arr_idx = updated_pri_arr_idx;
   SPR_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Updated Primary Output Port ID 0x%X Port Idx 0x%X",
           spr_get_outport_id_from_arr_idx(me_ptr, updated_pri_arr_idx),
           updated_pri_arr_idx);

   capi_spr_send_output_drift_info(me_ptr);
}

/*------------------------------------------------------------------------------
  Function name: spr_get_outport_id_from_arr_idx
    Given array index, return the output id
* ------------------------------------------------------------------------------*/
static uint32_t spr_get_outport_id_from_arr_idx(capi_spr_t *me_ptr, uint32_t arr_idx)
{
   if (arr_idx >= me_ptr->max_output_ports)
   {
      return UMAX_32;
   }

   if (me_ptr->out_port_info_arr[arr_idx].port_state)
   {
      return me_ptr->out_port_info_arr[arr_idx].port_id;
   }

   return UMAX_32;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_check_setup_defaults
    Setup the default values for the avsync instance
* ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_avsync_check_setup_defaults(avsync_t *avsync_ptr)
{
   if (NULL == avsync_ptr)
   {
      // TODO : MSG error
      return CAPI_EFAILED;
   }

#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: tsm: Setting speed factor to UNITY and disabling timescaling");
#endif

   avsync_ptr->tsm_info.tsm_speed_factor = TSM_UNITY_SPEED_FACTOR;
   avsync_ptr->flags.is_timescaled_data  = FALSE; //Enabled only when MD from TSM is received
#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: Attempt to set default values for the avsync params");
#endif

   spr_avsync_config_t *avsync_cfg_ptr = &avsync_ptr->client_config;

   // Set Render Mode Defaults
   avsync_cfg_ptr->render_mode   = SPR_RENDER_MODE_IMMEDIATE;
   avsync_cfg_ptr->start_time_us = 0; // to honor Render Mode Immediate
#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid,
           DBG_LOW_PRIO,
           "avsync: Default Render Mode SPR_RENDER_MODE_IMMEDIATE and Render Start Time "
           "to 0us configured");
#endif

   // Set Render Reference Defaults
   avsync_cfg_ptr->render_reference = SPR_RENDER_REFERENCE_DEFAULT;
#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: Default Render Reference configured");
#endif

   // Set Render Window defaults to +/- infinity
   time_us_t default_render_window_start  = { DEFAULT_RENDER_WINDOW_START_LSW, DEFAULT_RENDER_WINDOW_START_MSW };
   time_us_t default_render_window_end    = { DEFAULT_RENDER_WINDOW_END_LSW, DEFAULT_RENDER_WINDOW_END_MSW };
   avsync_cfg_ptr->render_window_start_us = process_time_us_type(default_render_window_start);
   avsync_cfg_ptr->render_window_end_us   = process_time_us_type(default_render_window_end);
#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: Default Render Windows +/- Infinity configured");
#endif

   avsync_cfg_ptr->hold_buf_duration_us     = 0;
   avsync_ptr->hold_buffer.cur_fill_size_us = 0;
   avsync_ptr->hold_buffer.buf_list_ptr     = NULL;
   avsync_ptr->hold_buffer.is_used_for_hold = TRUE;
#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid, DBG_LOW_PRIO, "avsync: Default hold buffer duration 0 configured");
#endif

#ifdef AVSYNC_DEBUG
   SPR_MSG(avsync_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Config mask : %lu, Render mode : %lu, Render reference : %lu, Start time : "
           "((0x%lx%lx))us",
           avsync_cfg_ptr->cfg_mask,
           avsync_cfg_ptr->render_mode,
           avsync_cfg_ptr->render_reference,
           (uint32_t)(avsync_cfg_ptr->start_time_us >> 32),
           (uint32_t)avsync_cfg_ptr->start_time_us);
   SPR_MSG(avsync_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Render window start : (0x%lx%lx) us, Render window end : (0x%lx%lx) us, Hold buf "
           "duration : (0x%lx%lx) us",
           (uint32_t)((avsync_cfg_ptr->render_window_start_us) >> 32),
           (uint32_t)(avsync_cfg_ptr->render_window_start_us),
           (uint32_t)((avsync_cfg_ptr->render_window_end_us) >> 32),
           (uint32_t)(avsync_cfg_ptr->render_window_end_us),
           (uint32_t)(avsync_cfg_ptr->hold_buf_duration_us >> 32),
           (uint32_t)avsync_cfg_ptr->hold_buf_duration_us);
#endif

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_process_avsync_config_param
    Process the client configuration for the avsync config param.
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_process_avsync_config_param(capi_spr_t *me_ptr, param_id_spr_avsync_config_t *params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == me_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "avsync: received bad pointer for SPR avsync config parameter");
      return CAPI_EFAILED;
   }

   // Check if the client is allowed to update the avsync configuration.
   // If input port is started & client has not enabled avsync feature so far, reject this set param
   // Run time enable/disable of avsync feature is not allowed
   // If the avsync feature was enabled before start, then only run time render window is honored.
   if (DATA_PORT_STATE_STARTED & me_ptr->in_port_info_arr[0].port_state)
   {
      if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
      {
         SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Run Time AVSync Config is not allowed");
         return CAPI_EFAILED;
      }

      if (me_ptr->avsync_ptr->client_config.is_enable != params_ptr->enable)
      {
         SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Run Time AVSync Disable/Enable not allowed");
         return CAPI_EFAILED;
      }

      // If the client has enabled AVSync before start, then only render window change is supported
      // Any other change in the parameters is rejected as well.

      uint16_t supported_render_mask = SPR_RENDER_BIT_MASK_RENDER_WINDOW;
      if (params_ptr->render_mask != supported_render_mask)
      {
         SPR_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "avsync: Run Time configuration is supported only for Render Window. Mask received = 0x%x",
                 params_ptr->render_mask);
         return CAPI_EFAILED;
      }
   }

   // If the parameter is received before start, allow the client to modify the configuration freely
   bool_t is_enable_avsync_from_client = params_ptr->enable;

   if (is_enable_avsync_from_client)
   {
      if (!me_ptr->avsync_ptr)
      {
         if (CAPI_FAILED(capi_spr_avsync_create(&me_ptr->avsync_ptr, me_ptr->heap_id, me_ptr->miid)))
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Failed to create avsync structure");
            return CAPI_ENOMEMORY;
         }
      }

      me_ptr->avsync_ptr->client_config.is_enable = params_ptr->enable;
      result |= capi_spr_update_avsync_config(me_ptr, params_ptr);
   }
   else
   {
      capi_spr_avsync_destroy(&me_ptr->avsync_ptr, me_ptr->miid);
      me_ptr->avsync_ptr = NULL;
   }

   SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Set param done for avsync config ");
   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_update_avsync_config
    Apply the client configuration for the avsync config param.
* ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_update_avsync_config(capi_spr_t *me_ptr, param_id_spr_avsync_config_t *params_ptr)
{
   if (NULL == me_ptr || NULL == params_ptr)
   {
      return CAPI_EFAILED;
   }

   if (NULL == me_ptr->avsync_ptr)
   {
      return CAPI_EBADPARAM;
   }

   capi_err_t           result         = CAPI_EOK;
   spr_avsync_config_t *avsync_cfg_ptr = &me_ptr->avsync_ptr->client_config;

   avsync_cfg_ptr->is_enable = params_ptr->enable;

   // If render mask is not set by the client, then configure the default parameters for
   // the avsync functionality
   if (0 == params_ptr->render_mask)
   {
      return CAPI_EOK;
   }

   SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Process PARAM_ID_AVSYNC_CONFIG from client");

   // Cache the render mode params (start mode & start time)
   if (params_ptr->render_mask & SPR_RENDER_BIT_MASK_RENDER_MODE)
   {
      avsync_cfg_ptr->cfg_mask |= SPR_RENDER_BIT_MASK_RENDER_MODE;
      avsync_cfg_ptr->render_mode   = params_ptr->render_mode_config.render_mode;
      avsync_cfg_ptr->start_time_us = process_time_us_type(params_ptr->render_mode_config.render_start_time);
   }

   // Cache the render reference type
   if (params_ptr->render_mask & SPR_RENDER_BIT_MASK_RENDER_REFERENCE)
   {
      avsync_cfg_ptr->cfg_mask |= SPR_RENDER_BIT_MASK_RENDER_REFERENCE;
      avsync_cfg_ptr->render_reference = params_ptr->render_reference_config.render_reference;
   }

   if (params_ptr->render_mask & SPR_RENDER_BIT_MASK_RENDER_WINDOW)
   {
      avsync_cfg_ptr->cfg_mask |= SPR_RENDER_BIT_MASK_RENDER_WINDOW;
      avsync_cfg_ptr->render_window_start_us = process_time_us_type(params_ptr->render_window.render_window_start);
      avsync_cfg_ptr->render_window_end_us   = process_time_us_type(params_ptr->render_window.render_window_end);

      // TODO: Validation for render window start & end
   }

   if (params_ptr->render_mask & SPR_RENDER_BIT_MASK_HOLD_DURATION)
   {
      avsync_cfg_ptr->cfg_mask |= SPR_RENDER_BIT_MASK_HOLD_DURATION;
      avsync_cfg_ptr->hold_buf_duration_us =
         process_time_us_type(params_ptr->render_hold_buffer_size.max_hold_buffer_duration);
      if (avsync_cfg_ptr->hold_buf_duration_us > DEFAULT_HOLD_BUF_SIZE_US)
      {
         SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Hold buffer config exceeded max size, setting to default");
         avsync_cfg_ptr->hold_buf_duration_us = DEFAULT_HOLD_BUF_SIZE_US;
      }
   }
   else
   {
      SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Warning. Hold buffer size not configured. set to 0");
      // This is for any avsync configuration being set but hold buffer size not set. Hold buffer size is 0 indicating
      // no hold possible.
      avsync_cfg_ptr->hold_buf_duration_us = 0;
   }
   if (params_ptr->render_mask & SPR_RENDER_BIT_MASK_ALLOW_NON_TIMESTAMP_HONOR_MODE)
   {
      avsync_cfg_ptr->allow_non_timestamp_honor_mode = TRUE;
      // No need to check timer disable mode update here as this variable is not configurable after port start
   }

   SPR_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Config mask : %lu, Render mode : %lu, Render reference : %lu, Start time : "
           "((0x%lx%lx))us",
           avsync_cfg_ptr->cfg_mask,
           avsync_cfg_ptr->render_mode,
           avsync_cfg_ptr->render_reference,
           (uint32_t)(avsync_cfg_ptr->start_time_us >> 32),
           (uint32_t)avsync_cfg_ptr->start_time_us);
   SPR_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Render window start : (0x%lx%lx) us, Render window end : (0x%lx%lx) us, Hold buf "
           "duration : (0x%lx%lx) us",
           (uint32_t)((avsync_cfg_ptr->render_window_start_us) >> 32),
           (uint32_t)(avsync_cfg_ptr->render_window_start_us),
           (uint32_t)((avsync_cfg_ptr->render_window_end_us) >> 32),
           (uint32_t)(avsync_cfg_ptr->render_window_end_us),
           (uint32_t)(avsync_cfg_ptr->hold_buf_duration_us >> 32),
           (uint32_t)avsync_cfg_ptr->hold_buf_duration_us);
   SPR_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: allow_non_timestamp_honor_mode : %lu",
           avsync_cfg_ptr->allow_non_timestamp_honor_mode);

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_process_session_time_reset_param
    Process the client configuration for the session time reset param.
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_process_session_time_reset_param(capi_spr_t *me_ptr, param_id_spr_session_time_reset_info_t *params_ptr)
{
   if (NULL == me_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "avsync: received bad pointer for SPR session time parameter");
      return CAPI_EFAILED;
   }

   // Check if the client is allowed to update the session time reset configuration.
   // If input port is started, reject any param configuration.
   if (DATA_PORT_STATE_STARTED & me_ptr->in_port_info_arr[0].port_state)
   {
         SPR_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "avsync: Session Time Reset change not allowed after START");
         return CAPI_EFAILED;
   }

   me_ptr->reset_sess_time_info.mode = params_ptr->mode;

   if(is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      me_ptr->avsync_ptr->av_reset_info.mode = params_ptr->mode;
      SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "avsync: reset session time info %d", params_ptr->mode);
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_destroy_hold_buf_list
    Destroy the hold buffer list
* ------------------------------------------------------------------------------*/
void capi_spr_destroy_hold_buf_list(capi_spr_t *me_ptr)
{

   if (!me_ptr)
   {
      return;
   }

   // Validate if a hold buffer list exists
   if (!spr_avsync_does_hold_buf_exist(me_ptr->avsync_ptr))
   {
      return;
   }

#ifdef SPR_INT_BUF_DEBUG
   SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Destroying SPR hold buffer list");
#endif

   spr_int_buffer_t *spr_hold_buf_ptr = &me_ptr->avsync_ptr->hold_buffer;

   capi_spr_destroy_int_buf_list(me_ptr, spr_hold_buf_ptr, &me_ptr->operating_mf);

   spr_hold_buf_ptr->buf_list_ptr     = NULL;
   spr_hold_buf_ptr->cur_fill_size_us = 0;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_set_miid
   Assigns the MIID to the avsync instance structure
* ------------------------------------------------------------------------------*/
void capi_spr_avsync_set_miid(avsync_t *avsync_ptr, uint32_t miid)
{
   if (avsync_ptr)
   {
      avsync_ptr->miid = miid;
   }
}
