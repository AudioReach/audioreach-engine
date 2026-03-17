/**
 * \file capi_detection.h
 *
 * \brief
 *
 *     This file contains CAPI APIs for History Buffer Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "capi_history_buffer_i.h"
#include "capi_fwk_extns_island.h"
#include "capi_cmn.h"

/*----------------------------------------------------------------------------
 * Static function declarations
 * -------------------------------------------------------------------------*/
static capi_err_t capi_history_buffer_process(capi_t *_pif,
                                              capi_stream_data_t *input[],
                                              capi_stream_data_t *output[]);
static capi_err_t capi_history_buffer_end(capi_t *_pif);
static capi_err_t capi_history_buffer_set_param(capi_t *_pif,
                                                uint32_t param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *params_ptr);
static capi_err_t capi_history_buffer_get_param(capi_t *_pif,
                                                uint32_t param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *params_ptr);
static capi_err_t capi_history_buffer_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);
static capi_err_t capi_history_buffer_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_vtbl_t capi_history_buffer_vtbl = {
    capi_history_buffer_process, capi_history_buffer_end,
    capi_history_buffer_set_param, capi_history_buffer_get_param,
    capi_history_buffer_set_properties, capi_history_buffer_get_properties};

/* =========================================================================
 * FUNCTION : capi_history_buffer_get_static_properties
 *
 * DESCRIPTION:
 *    Returns the following static properties of the module like,
 *
 *       1. CAPI_INIT_MEMORY_REQUIREMENT - This is the size of the capi_history_buffer_t handle.
 *          Currently the default is set to HISTORY_BUFFER_STACK_SIZE_REQUIREMENT i.e 4K.
 *
 *       2. CAPI_IS_INPLACE - History Buffer is not inplace so returned false for this property.
 *          since it doesn't have any output port.
 *
 *       3. CAPI_REQUIRES_DATA_BUFFERING - Its set to False.
 *
 *       4. CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS - Currently no framework extns are
 *          supported.
 *
 *       5. CAPI_NEEDED_FRAMEWORK_EXTENSIONS  - Returns fwk extension Ids.
 *
 *       6. CAPI_INTERFACE_EXTENSIONS -
 *         Interface extensions supported by example History buffer module -
 *
 *          1. INTF_EXTN_IMCL - Inter module communication extension.
 *               Control port operation extension is needed to know the state of the
 *             control port. Its informs module about the port OPEN, CLOSE, Peer connected
 *             and Peer disconnected states.
 *
 *           NOTE:
 *              If the History buffer module is customized to handle mutliple ports,
 *            it might have to use INTF_EXTN_DATA_PORT_OPERATION and INTF_EXTN_METADATA
 *            as well.
 * ========================================================================= */
capi_err_t capi_history_buffer_get_static_properties(capi_proplist_t *init_set_properties,
                                                     capi_proplist_t *static_properties)
{
   capi_err_t result = CAPI_EFAILED;

   if (NULL != static_properties)
   {
      result = capi_history_buffer_handle_get_properties(NULL, static_properties);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: get static prop bad ptrs");
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_init
 *
 * DESCRIPTION:
 *    This is the first function called by the framework after creating the capi
 *  handle [*_pif]. Module must intialize the handle with init set properties
 *  payload passed by the frame work. Init set properties sets,
 *
 *   1) CAPI_HEAP_ID - which must be used by the module for any dyanmic memory
 *   			  allocations.
 *
 *   2) CAPI_PORT_NUM_INFO - maximum number of input/output ports that can be opened
 *      on the module.
 *
 *   3) CAPI_EVENT_CALLBACK_INFO - sets callback handle for raising module events.
 *
 *   4) CAPI_MODULE_INSTANCE_ID - Module instance ID as assigned in the ACDB graphs.
 *      This is helpul when printing the debug messages.
 * ========================================================================= */
capi_err_t capi_history_buffer_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == _pif || (NULL == init_set_properties))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: Error! null pointer.");
      return CAPI_EBADPARAM;
   }

   capi_history_buffer_t *me_ptr = (capi_history_buffer_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_history_buffer_t));

   // apply init time set properties
   result = capi_history_buffer_handle_set_properties(me_ptr, init_set_properties);
   if (result == CAPI_EUNSUPPORTED)
   {
      // unsupported error must be ignored for init set properties
      result = CAPI_EOK;
   }
   else if (CAPI_FAILED(result)) // for any other failures return error.
   {
      // Free the allocated capi memory
      capi_history_buffer_end(_pif);
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Init Failed!");
   }

   // intialize the capi handle.
   me_ptr->vtbl_ptr = &capi_history_buffer_vtbl;
   me_ptr->is_enabled = TRUE;
   // me_ptr->is_event_inprogess = FALSE;
   // me_ptr->is_trigger_detected = FALSE;
   // me_ptr->data_out_mode = 0;

   DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "init done with result %d", result);

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_set_properties
 *
 * DESCRIPTION:
 *   This function is called by framework to set runtime properties. Currently
 *   only runtime property handled by this module is
 *
 *     1. CAPI_INPUT_MEDIA_FORMAT_V2 -
 *          Media format on input ports. Media format is set only on the input port.
 *        This is a per port operation and valid port info is passed by the framework.
 *
 * ========================================================================= */
static capi_err_t capi_history_buffer_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   if (NULL == _pif || (NULL == props_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: null pointer error");
      return CAPI_EBADPARAM;
   }

   return capi_history_buffer_handle_set_properties((capi_history_buffer_t *)_pif, props_ptr);
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_get_properties
 *
 * DESCRIPTION:
 *   Returns module properties during runtime. Currently the module doesn't
 * expect any runtime get properties.
 *
 * This example doesn't have output port. If output port support is added then
 * CAPI_OUTPUT_MEDIA_FORMAT_V2 property must be implemented.
 *
 * ========================================================================= */
static capi_err_t capi_history_buffer_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: Error!! NULL pointer error");
      return CAPI_EBADPARAM;
   }

   result = capi_history_buffer_handle_get_properties((capi_history_buffer_t *)_pif, props_ptr);

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_process

 * DESCRIPTION:

  Currently we are processing the input data and it is used to trigger the process call
  from framework. If the trigger event is detected send an event to DSP client or send
  an inter-module-message if the peer module expects detection information.

  Process call is done in following steps:

   STEP 1 - Check if the necessary conditions are met to process. For example,
      i. Module is enabled.
     ii. Mandatory calibration has been received to continue with the process

   STEP 2 - Check the trigger event.
      i. Exit from the island and handle the event.

   STEP 3 - Post trigger event detection, handling the event.
      i. If detected is triggered, module can raise event to the DSP client.
     ii. Module can also send detection trigger event to a peer module through IMC
    iii. Update the trigger event flag and raise the event in progress flag.

   STEP 4 - Reset the trigger flag and raise the event in progress flag.

 * ========================================================================= */
static capi_err_t capi_history_buffer_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;
   capi_history_buffer_t *me_ptr = (capi_history_buffer_t *)_pif;
   const uint32_t INPUT_PORT_INDEX_0 = 0;

#ifdef HISTORY_BUFFER_DEBUG
   DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Entering history buffer process");
#endif

   /* =========================================================================
    * STEP 1. Check if the necessary conditions are met to process.
    * ========================================================================= */

   // Return if the module is disabled
   if (!me_ptr->is_enabled)
   {
#ifdef HISTORY_BUFFER_DEBUG
      DE_DBG(me_ptr->miid, DBG_MED_PRIO, "Module is disabled, nothing to process ");
#endif
      return CAPI_EOK;
   }

   if (me_ptr->is_event_inprogess)
   {
#ifdef HISTORY_BUFFER_DEBUG
      DE_DBG(me_ptr->miid, DBG_MED_PRIO, "Module event in progess, nothing to process ");
#endif
      return CAPI_EOK;
   }

   /* Module can also check if necessary,
         1. Library has been initialized
         2. Media format is received on the input ports.
         3. Mandatory calibration has been received to continue with the process
         4. Check if sufficient data is present in input buffers to process. Since
            this module raises threshold, it should always expect threshold amount of
            data except if EOF flag is set.
    */
   if ((NULL == input[INPUT_PORT_INDEX_0]) && (NULL == input[INPUT_PORT_INDEX_0]->buf_ptr))
   {
      // sanity check
#ifdef HISTORY_BUFFER_DEBUG
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Input buffers are not present, cannot process");
#endif
      return CAPI_EFAILED;
   }

   /* ====================== End of STEP 1 ====================== */

   /* =========================================================================
    * STEP 2. Process data and detect keyword.
    * ========================================================================= */

   /* If trigger is detected  exit the island and cache all the necessary information needed to raise the detection event*/
   uint32_t keyword_length_in_us = 0;
   if (TRUE == me_ptr->is_trigger_detected)
   {

      result = capi_cmn_raise_island_vote_event(&me_ptr->cb_info, CAPI_CMN_ISLAND_VOTE_EXIT);

      if (CAPI_FAILED(result))
      {
         AR_MSG_ISLAND(DBG_ERROR_PRIO, "CAPI History Buffer: Failed to raise island exit event");
         return result;
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO, "CAPI History Buffer: raised island exit event");
      }

      if (TRUE == me_ptr->latest_trigger_info.is_kw_detected)
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Error! Found a pending detection event, dropping it.");
         me_ptr->is_trigger_detected = FALSE;
      }

      me_ptr->latest_trigger_info.is_kw_detected = TRUE;

      // TODO: keyword_length_in_us is needed for sending IMC message to DAM. DAM module uses this length
      // to start reading data from the begining of the keyword. This is optional, if history data is not
      // required after detection, module can just send zero as the length.
      keyword_length_in_us = me_ptr->max_dam_buffer_size_us; // max_possible_size in usec

      /* FTRT info is set when history buffer gets IMC message [PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH]
         from DAM module */
      if (me_ptr->event_mode & EVENT_INFO_FTRT_LENGTH)
      {
         // This flag is cleared on receiving IMC message.
         me_ptr->latest_trigger_info.waiting_for_ftrt_len = TRUE;
      }
   }

   /* ====================== End of STEP 2 ====================== */

   /* =========================================================================
    * STEP 3. Handle detection event.
    * ========================================================================= */

   /* TODO: Send an IMC message to peer module to inform about the detection event
      if necessary.

      History Buffer module have a control link from history to DAM to control the data
      flow of DAM output. Whenever a detection event is triggered, history buffer sends an
      IMC message to DAM module to open the DAMs gate. DAM module in turn opens the output
      port gate and drains buffered data to Read client.

      As a response to GATE_OPEN, DAM module returns the length of buffered data (in bytes)
      to detection engine. DAMs response is received asynchronously, so detection engine has
      two options here -

        1. If Generic detection event is configured to  recieve DETECTION_EVENT_FTRT_INFO bit,
         detection event must wait until it gets FTRT info from DAM module to raise the
         event to DSP client.

        2. If DETECTION_EVENT_FTRT_INFO is not configured in detection event, module can raise
        event to DSP client without waiting for FTRT info from DAM module.
    */
   if (TRUE == me_ptr->is_trigger_detected)
   {
      /* Populate flow control param payload. */
      param_id_audio_dam_data_flow_ctrl_t flow_ctrl_cfg;
      flow_ctrl_cfg.is_gate_open = TRUE;
      flow_ctrl_cfg.read_offset_in_us = keyword_length_in_us;

      flow_ctrl_cfg.num_best_channels = 0; // TODO: update best channels info if needed. This is optional.

      /* Send flow control param to DAM module through control port.*/
      if (CAPI_EOK !=
          capi_history_buffer_send_flow_ctrl_msg_to_dam(me_ptr, &flow_ctrl_cfg))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI History Buffer: Sending GATE OPEN to DAM Failed.");
         me_ptr->is_trigger_detected = FALSE;
         return CAPI_EFAILED;
      }
      else
      {
         AR_MSG(DBG_MED_PRIO, "CAPI History Buffer: Sent gate open to audio DAM module");
      }

      /* If keyword is detected, module needs to send the detection event to the
         DSP clients. Detection module populates the current detection info into the
         event payload and raises the detection event.

         If all the inforamtion regarding the detection is present at the moment then
         the detection event can be raised. But in some cases module has to wait for
         IMC message from DAM module before it sends the detection event.

         IF generic detection event is configured with EVENT_INFO_FTRT_LENGTH then
         module needs to wait for PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH from DAM.
      */

      result = capi_populate_trigger_info_and_raise_event(me_ptr);
      me_ptr->is_trigger_detected = FALSE;
      me_ptr->is_event_inprogess = TRUE;
   }
#ifdef HISTORY_BUFFER_DEBUG
   else
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "History Buffer: no trigger detected");
   }
#endif

   /* ====================== End of STEP 3 ====================== */

   /* =========================================================================
    * STEP 4 - Update input actual data length.
    * ========================================================================= */

   /* TODO: update inputs actual data lengths indicating the amount of data consumed
            on Input input[0]->buf_ptr[0].actual_data_len.

            It can be updated only if the module consumes partial input.
            If module consumes all the input bytes, actual_data_len need not be updated.*/

   /* ====================== End of STEP 4 ====================== */

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_end
 *
 * DESCRIPTION:
 *    Free all the dynamic resources allocated by the module. Do NOT free the
 *  capi handle it will be freed by the framework.
 * ========================================================================= */
static capi_err_t capi_history_buffer_end(capi_t *_pif)
{
   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: End received bad pointer");
      return CAPI_EBADPARAM;
   }
   capi_history_buffer_t *me_ptr = (capi_history_buffer_t *)(_pif);
   me_ptr->vtbl_ptr = NULL;

   // Free generic event payload ptr
   if (me_ptr->event_payload_ptr)
   {
      posal_memory_free(me_ptr->event_payload_ptr);
      me_ptr->event_payload_ptr = NULL;
   }

   /* TODO: if any event client are not deregisterd, clean up*/

   /* TODO: Free any input or control port related resources here. like scratch buffers
      etc.*/

   DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "End done");

   return CAPI_EOK;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_get_param
 *
 * DESCRIPTION:
 *  Module must return the param configuration based on current state of the module.
 *
 *  Currently, get parameters is not supported for history buffer module.
 *
 * ========================================================================= */
static capi_err_t capi_history_buffer_get_param(capi_t *_pif,
                                                uint32_t param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *params_ptr)
{
   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: get param received bad pointers");
      return CAPI_EBADPARAM;
   }

   capi_err_t result = CAPI_EOK;
   capi_history_buffer_t *me_ptr = (capi_history_buffer_t *)(_pif);

   switch (param_id)
   {
   default:
   {
      DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "unsupported get param 0x%lx", param_id);
      result = CAPI_EUNSUPPORTED;
      break;
   }
   }

   DE_DBG(me_ptr->miid,
          DBG_HIGH_PRIO,
          "Get param id (%#lx) param size %lu completed with result(%#lx)",
          param_id,
          params_ptr->actual_data_len,
          result);

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_set_param
 *
 * DESCRIPTION:
 *  Calibrates the module with the given set param payload. All the param ID
 * calibrations supported by the module must be handled here. Currently these
 * are the calibrations supported by the module -
 *
 *   1. PARAM_ID_MODULE_ENABLE - This is a realtime calibration param, useful
 *      to enable/disable module runtime.
 *
 *   2. PARAM_ID_DETECTION_ENGINE_GENERIC_EVENT_CFG -
 *        This sets the event mode bit mask. Each bit selects the type of information
 *      expected as part of generic detection event sent by the detection engine. This is a
 *      mandatory param for event id EVENT_ID_DETECTION_ENGINE_GENERIC_INFO.
 *
 *      The event id EVENT_ID_DETECTION_ENGINE_GENERIC_INFO supports a variable event payload.
 *      The event payload supports only one keyword information for history buffer module.
 *         1. FTRT data length inforamtion - EVENT_INFO_FTRT_LENGTH
 *
 *      Set param will error out if unsupported keyword information is sent by the client
 *      through the event_mode bit mask.
 *
 *   3. PARAM_ID_DETECTION_ENGINE_RESET-
 *        Detection engine reset is set by the client when the detection event is completed
 *      and necessary history buffering data has been read from DAM module. As a part of reset,
 *      history buffer must send Gate close param to DAM module to stop draning its data and
 *      get back to buffering mode.
 *
 *      This also indicates that the module can reset to its initial state and prepare for
 *      a new trigger detection event by client.
 *
 *   4. PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER - Sets the buffering requirements for the
 *      History buffer module. Based on this requirement, History buffer module resizes the
 *      Audio Dam buffers via IMCL.
 *
 *   5. PARAM_ID_DETECTION_ENGINE_BUFFERING_CONFIG - Sets the buffering required by History Buffer
 *      module. Resizes the Audio Dam buffers via IMCL. pre_roll_duration_in_ms is don't care,
 *      only hist_buffer_duration_msec is used for history buffer module.
 *
 *   6. PARAM_ID_HISTORY_BUFFER_MODE - Sets the output data flow mode to client. Hstory buffer sends
 *      this information to DAM to select the buffer stream mode during the triggered event.
 *
 *  Apart from calibration set param handles the interface extensions as well.
 *  History Buffer module currently supports only INTF_EXTN_IMCL.
 *
 *  NOTE:
 *  If this is a multi in/out module it needs to support data port operation extension
 *  as well. If module needs to propagate metadata, it needs to support metadata
 *  propagation extension.
 * ========================================================================= */
static capi_err_t capi_history_buffer_set_param(capi_t *_pif,
                                                uint32_t param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *params_ptr)
{
   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: set param received bad pointers");
      return CAPI_EBADPARAM;
   }

   capi_err_t result = CAPI_EOK;
   capi_history_buffer_t *me_ptr = (capi_history_buffer_t *)(_pif);

   switch (param_id)
   {
      case PARAM_ID_MODULE_ENABLE:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_module_enable_t))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "set param (%#lx) failed. expected param_size(%ld), actual_param_size:(%ld)",
                   param_id,
                   sizeof(param_id_module_enable_t),
                   params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }

         // Extract param config from set param payload
         param_id_module_enable_t *cfg_ptr = (param_id_module_enable_t *)params_ptr->data_ptr;

         // If the module is disable the process can be simply skipped.
         me_ptr->is_enabled = cfg_ptr->enable;

         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Enabled = %lu ", cfg_ptr->enable);

         if (me_ptr->is_enabled)
         {
            me_ptr->is_trigger_detected = FALSE;
         }

         break;
      }

      case PARAM_ID_DETECTION_ENGINE_GENERIC_EVENT_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_detection_engine_generic_event_cfg_t))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "set param (%#lx) failed. expected param_size(%ld), actual_param_size:(%ld)",
                   param_id,
                   sizeof(param_id_module_enable_t),
                   params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }

         // Extract param config from set param payload
         param_id_detection_engine_generic_event_cfg_t *cfg_ptr =
             (param_id_detection_engine_generic_event_cfg_t *)params_ptr->data_ptr;

         /* Cache the event mode, event mode configures the payload thats expected
            from each detection event of detection engine module. Currently only 1 param
            is exposed as part of generic detection info for history buffer,
             1. FTRT info - Length of the data buffered in DAM. This is useful for
                the detection event client to create read buffers.

            As part of this configuration, event payload is allocated a head to avoid
            dynamic memory allocation. It only allocated and memeset at this point.
            When the trigger keyword invoked, the event payload is populated with appropriate
            information and event is raised, during data process.
         */
         me_ptr->event_mode = cfg_ptr->event_mode;

         /*deallocate memory if already allocated*/
         if (NULL != me_ptr->event_payload_ptr)
         {
            posal_memory_free(me_ptr->event_payload_ptr);
            me_ptr->event_payload_ptr = NULL;
         }

         /* Event payload size, minimum size is event header */
         uint32_t detection_event_size = sizeof(event_id_detection_engine_generic_info_t);

         /*Checks if unsupported event mode is recieved.*/
         if ((me_ptr->event_mode) && (me_ptr->event_mode != EVENT_INFO_FTRT_LENGTH))
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_history_buffer: unsupported event mode 0x%lx recieved", me_ptr->event_mode);
            result = CAPI_EBADPARAM;
            break;
         }

         /*check whether FTRT info needs to be included in variable payload*/
         if (me_ptr->event_mode & EVENT_INFO_FTRT_LENGTH)
         {
            detection_event_size += sizeof(ftrt_data_info_t);
            detection_event_size += sizeof(detection_event_info_header_t);
         }

         DE_DBG(me_ptr->miid,
                DBG_HIGH_PRIO,
                "Event mode = 0x%lx detection_event_size = %lu",
                me_ptr->event_mode,
                detection_event_size);

         me_ptr->event_payload_ptr =
             (event_id_detection_engine_generic_info_t *)posal_memory_malloc(detection_event_size, me_ptr->heap_id);
         if (NULL == me_ptr->event_payload_ptr)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to malloc for detection event with size %d", detection_event_size);
            return CAPI_ENOMEMORY;
         }

         me_ptr->event_payload_ptr->payload_size =
             me_ptr->event_payload_size - sizeof(event_id_detection_engine_generic_info_t);

         me_ptr->event_payload_size = detection_event_size;
         break;
      }
      case PARAM_ID_DETECTION_ENGINE_RESET:
      {
         /* Engine reset indicates the earlier keyword detection is complete and module
            can reset to initial state. It helps in handling the trigger events when previous
            event is not completed.*/

         /* This param doesn't have any payload. This is received runtime to reset the state of
            the module. Usually client issues this param to reset after a detection event.

            Currently this module set flow control message to DAM if there is a exist a control
            link to DAM module. Detection module must send gate close command to DAM to stop
            draining data and go back to buffering state. Reset is usually issued once client
            finishes handling the most recent detection event and finished reading the FTRT data
            from DAM module. */

         DE_DBG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "Received history buffer reset, sending gate close to DAM");

         /* Populate dam gate control IMC param payload. */
         param_id_audio_dam_data_flow_ctrl_t ctrl_cfg;
         memset(&ctrl_cfg, 0, sizeof(param_id_audio_dam_data_flow_ctrl_t));

         // populate payload for gate close
         ctrl_cfg.is_gate_open = FALSE;

         result = capi_history_buffer_send_flow_ctrl_msg_to_dam(me_ptr, &ctrl_cfg);

         // update flag to show, detection event is finished.
         me_ptr->is_event_inprogess = FALSE;
         break;
      }

      case PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER:
      {
         /*Check if previous trigger event in progress. Skip this flag is the event is in the progress
         else set the trigger detection flag. Also, Vote for island exit.*/

         if (me_ptr->is_event_inprogess)
         {
            AR_MSG(DBG_ERROR_PRIO, "Previous event is in progress, Trigger Detection is not set");
         }
         else
         {
            DE_DBG(me_ptr->miid, DBG_MED_PRIO, "Trigger detection param is SET.");
            me_ptr->is_trigger_detected = TRUE;
         }

         break;
      }

      case PARAM_ID_DETECTION_ENGINE_BUFFERING_CONFIG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_detection_engine_buffering_config_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Set param fail: Invalid payload size: %lu",
                   params_ptr->actual_data_len);
            return CAPI_EBADPARAM;
         }

         param_id_detection_engine_buffering_config_t *hist_buf_dur_ptr =
             (param_id_detection_engine_buffering_config_t *)params_ptr->data_ptr;

         me_ptr->hist_buffer_duration_msec = hist_buf_dur_ptr->hist_buffer_duration_msec;

         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Configured hist buffer duration in msec: %u",
                hist_buf_dur_ptr->hist_buffer_duration_msec);

         /*Send the resize command to the DAM Module, to adjust the circular buffer*/
         if (CAPI_FAILED(capi_history_buffer_imcl_send_resize_to_dam(me_ptr)))
         {
            AR_MSG(DBG_HIGH_PRIO, "Couldn't send resize to DAM with result %lu", result);
         }

         me_ptr->max_dam_buffer_size_us = (1000 * (me_ptr->hist_buffer_duration_msec));
         break;
      }

      case PARAM_ID_HISTORY_BUFFER_MODE:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_history_buffer_mode_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Set param fail: Invalid payload size: %lu",
                   params_ptr->actual_data_len);
            return CAPI_EBADPARAM;
         }

         param_id_history_buffer_mode_t *cfg_ptr = (param_id_history_buffer_mode_t *)params_ptr->data_ptr;

         me_ptr->mode.data_flow_mode = cfg_ptr->data_flow_mode;

         if (me_ptr->mode.data_flow_mode != HISTORY_BUFFER_ON_DEMAND)
         {
            me_ptr->mode.data_flow_mode = HISTORY_BUFFER_ON_DEMAND;
            me_ptr->mode.batch_size_ms = 0;

            DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Unsupported mode %u, switched to ON_DEMAND", me_ptr->mode.data_flow_mode);
         }

         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Configured data flow mode is: %u and batch size in msec: %u",
                me_ptr->mode.data_flow_mode, me_ptr->mode.batch_size_ms);

         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_PORT_OPERATION:
      {
         /* Handle IMCL port operation like OPEN, CLOSE, PEER CONNECTED and PEER DISCONNECTED */
         result = capi_history_buffer_handle_intf_extn_ctrl_port_operation(me_ptr, params_ptr);

         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_INCOMING_DATA:
      {
         if (NULL == params_ptr->data_ptr)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         /* Sanity check for the incoming intent size */
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_incoming_data_t))
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid payload size for incoming data %d", params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /* TODO: handle the incoming intent message from peer module, the following helper extracts the param payload
            from the incoming intent. Control port id to which intent is sent is part of the payload header. */
         result = capi_history_buffer_handle_incoming_imc_message(me_ptr, params_ptr);

         break;
      }
      default:
      {
         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "unsupported set param 0x%lx", param_id);
         result = CAPI_EUNSUPPORTED;
         break;
      }
   }

   DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Set param for param id (0x%lx) completed with result(%#lx)", param_id, result);

   return result;
}
