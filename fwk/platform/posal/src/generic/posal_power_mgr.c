/**
 * \file posal_power_mgr.c
 * \brief
 *  	This file contains profiling utilities.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal_power_mgr.h"
#include "platform_internal_api.h"
#include "pm_server.h"
#define POSAL_POWER_MGR_INVALID_CLIENT_ID 0

/** PM_WRAPPER max out information */
#define POSAL_POWER_MGR_MAX_OUT_BW (250 * 1024 * 1024)
#define POSAL_POWER_MGR_MAX_OUT_MPPS (500)
#define POSAL_POWER_MGR_MAX_OUT_FLOOR_CLK (500)

/* =======================================================================
 **                          Function Definitions
 ** ======================================================================= */

/** Sets up channel and response queue for local messaging */
static ar_result_t posal_power_mgr_setup_signal(posal_signal_t *signal_pptr, posal_channel_t *channel_pptr)
{
   ar_result_t result = AR_EOK;

   /** Create signal */
   if (AR_EOK != (result = posal_signal_create(signal_pptr, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "POSAL_POWER_MGR: Failed to create signal: %lu", result);
      return result;
   }

   /** Set up channel */
   if (AR_EOK != (result = posal_channel_create(channel_pptr, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "POSAL_POWER_MGR: Failed to create channel, result: %lu", result);
      return result;
   }

   /** Add signal to the channel */
   if (AR_EOK != (result = posal_channel_add_signal(*channel_pptr, *signal_pptr, POSAL_CHANNEL_MASK_DONT_CARE)))
   {
      AR_MSG(DBG_ERROR_PRIO, "POSAL_POWER_MGR: Failed to add signal to channel, result: %lu", result);
      return result;
   }

   return result;
}

/** Destroys channel and response queue for local messaging */
static void posal_power_mgr_destroy_signal(posal_signal_t *pp_sig, posal_channel_t *pp_chan)
{
   /** Destroy the signal */
   posal_signal_destroy(pp_sig);

   /** Destroy the channel */
   posal_channel_destroy(pp_chan);
}

static ar_result_t posal_power_mgr_send_command_internal(uint32_t msg_opcode, pm_server_payload_t *payload_ptr)
{
   ar_result_t cmd_result;

   // Send message to server
   spf_msg_t             msg;
   uint32_t             msg_payload_size     = GET_SPF_MSG_REQ_SIZE(sizeof(pm_server_payload_t));
   pm_server_payload_t *msg_payload_ptr      = NULL;
   spf_handle_t *        pm_server_handle_ptr = pm_server_get_handle();
   spf_msg_token_t       token;

   if(NULL == pm_server_handle_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "PM sever handle is NULL");
      return AR_EFAILED;
   }
   token.token_ptr = msg_payload_ptr;

   if (AR_EOK != (cmd_result = spf_msg_create_msg(&msg,                 /** MSG Ptr */
                                                  &msg_payload_size,    /** MSG payload size */
                                                  msg_opcode,           /** MSG opcode */
                                                  NULL,                 /** Response handle */
                                                  &token,               /** MSG Token */
                                                  pm_server_handle_ptr, /** Destination handle */
                                                  POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "PMSR:%08x: POSAL_POWER_MGR: FAILED to create pm_server msg payload, opcode: 0x%lx, result: 0x%lx",
             payload_ptr->client_log_id,
             msg_opcode,
             cmd_result);
   }

   /* Get the pointer to GK message header */
   spf_msg_header_t *msg_header_ptr = (spf_msg_header_t *)msg.payload_ptr;

   /* Get the pointer to start of the message */
   msg_payload_ptr = (pm_server_payload_t *)(&msg_header_ptr->payload_start);

   /* Populate message payload */
   memscpy(msg_payload_ptr, sizeof(pm_server_payload_t), payload_ptr, sizeof(pm_server_payload_t));

   //increase the request count from thie client.
   if (PM_SERVER_CMD_RELEASE == msg_opcode)
   {
      pm_server_handle_t *server_hdl_ptr = (pm_server_handle_t *)payload_ptr->release_info.pm_handle_ptr;
      posal_atomic_increment(&server_hdl_ptr->pending_request_count);
   }
   else if (PM_SERVER_CMD_REQUEST == msg_opcode)
   {
      pm_server_handle_t *server_hdl_ptr = (pm_server_handle_t *)payload_ptr->request_info.pm_handle_ptr;
      posal_atomic_increment(&server_hdl_ptr->pending_request_count);
   }

   /* Clear signal before sending the message */
   if (payload_ptr->signal_ptr != NULL)
   {
      posal_signal_clear(payload_ptr->signal_ptr);

      // if it is a blocking command then send to sync command queue.
      cmd_result = spf_msg_send_cmd(&msg, pm_server_handle_ptr);
   }
   else
   {
       // if it is a non-blocking command then send to async command queue.
      cmd_result = posal_queue_push_back(pm_server_handle_ptr->q_ptr, (posal_queue_element_t *)(&msg));
   }

   /* Send registration message to server */
   if (AR_EOK != cmd_result)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "PMSR:%08x: Failed to send msg to pm_server cmdQ, result: 0x%lx",
             payload_ptr->client_log_id,
             cmd_result);
   }

   /* If signal is not NULL, do a blocking wait for the message to return */
   if (payload_ptr->signal_ptr != NULL)
   {
      posal_channel_t *channel_ptr = posal_signal_get_channel(payload_ptr->signal_ptr);
      posal_channel_wait(channel_ptr, posal_signal_get_channel_bit(payload_ptr->signal_ptr));
      posal_signal_clear(payload_ptr->signal_ptr);

      // Return message
      spf_msg_return_msg(&msg);
   }

   return cmd_result;
}

/** @ingroup posal_pm_wrapper
  Sends request to PM Server

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t posal_power_mgr_request(posal_pm_request_info_t *request_info_ptr)
{
   ar_result_t cmd_result = AR_EOK;

   if (request_info_ptr->pm_handle_ptr)
   {
      pm_server_payload_t payload;
      // TODO: make payload available to clients instead of copying over data
      payload.client_log_id = request_info_ptr->client_log_id;
      payload.signal_ptr    = request_info_ptr->wait_signal_ptr;
      payload.request_info  = *request_info_ptr;

      cmd_result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_REQUEST, &payload);

      if (request_info_ptr->wait_signal_ptr == NULL)
      {
         pm_server_handle_t* server_hdl_ptr = (pm_server_handle_t*)request_info_ptr->pm_handle_ptr;
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR request submitted to server for client_id %lu with result: %lx",
                request_info_ptr->client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
      }
      else
      {
#ifdef DEBUG_PRINTS
         pm_server_handle_t* server_hdl_ptr = (pm_server_handle_t*)request_info_ptr->pm_handle_ptr;
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR request complete for client_id %lu with result: %lx",
                request_info_ptr->client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
#endif
      }
   }

   return cmd_result;
}

/** @ingroup posal_pm_wrapper
  Sends release to PM Server

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t posal_power_mgr_release(posal_pm_release_info_t *release_info_ptr)
{
   ar_result_t cmd_result = AR_EOK;

   if (release_info_ptr->pm_handle_ptr)
   {
      pm_server_payload_t payload;
      payload.client_log_id = release_info_ptr->client_log_id;
      payload.signal_ptr    = NULL; // no need to wait for releases
      payload.release_info  = *release_info_ptr;

      cmd_result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_RELEASE, &payload);

      if (release_info_ptr->wait_signal_ptr == NULL)
      {
         pm_server_handle_t *server_hdl_ptr = (pm_server_handle_t *)release_info_ptr->pm_handle_ptr;
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR release submitted to server for client_id %lu with result: %lx",
                release_info_ptr->client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
      }
      else
      {
#ifdef DEBUG_PRINTS
        pm_server_handle_t* server_hdl_ptr = (pm_server_handle_t*)release_info_ptr->pm_handle_ptr;
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR release complete for client_id %lu with result: %lx",
                release_info_ptr->client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
#endif
      }
   }

   return cmd_result;
}

/** @ingroup posal_pm_wrapper
  Registers for kpps and bw

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t posal_power_mgr_register(posal_pm_register_t register_info,
                                      posal_pm_handle_t*   pm_handle_pptr,
                                      posal_signal_t       wait_signal_ptr,
                                      uint32_t             log_id)
{

   posal_signal_t   signal_ptr;
   posal_channel_t  channel_ptr;
   bool_t           has_signal = TRUE;
   ar_result_t      result     = AR_EOK;

   if (posal_power_mgr_is_registered(*pm_handle_pptr))
   {
      return AR_EOK;
   }

   pm_server_handle_t *pm_handle_ptr =
      (pm_server_handle_t *)posal_memory_malloc(sizeof(pm_server_handle_t), POSAL_HEAP_DEFAULT);
   if (NULL == pm_handle_ptr)
   {
      result = AR_ENOMEMORY;

      AR_MSG(DBG_HIGH_PRIO, "PMSR:%08x: POSAL_POWER_MGR register failed, result %lx", log_id, result);

      return result;
   }

   memset(pm_handle_ptr, 0, sizeof(pm_server_handle_t));

   /* Store register information */
   memscpy(&pm_handle_ptr->register_info, sizeof(posal_pm_register_t), &register_info, sizeof(posal_pm_register_t));

   /* If signal wasn't provided, use a local one */
   if (wait_signal_ptr == NULL)
   {
      if (AR_DID_FAIL(result = posal_power_mgr_setup_signal(&signal_ptr, &channel_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "PMSR:%08x: POSAL_POWER_MGR signal setup failed, result %lx", log_id, result);
         return result;
      }
      wait_signal_ptr = signal_ptr;
      has_signal      = FALSE;
   }

   pm_server_payload_t payload;

   /* Assign client name.*/
   snprintf(payload.client_info.client_name,
            PM_SERVER_CLIENT_NAME_MAX_LENGTH,
            "%s%2lx",
            PM_SERVER_CLIENT_TOKEN_PREFIX,
            log_id);

   payload.client_info.client_log_id = log_id;
   payload.client_info.pm_handle_ptr = (posal_pm_handle_t)pm_handle_ptr;
   payload.client_info.client_class  = AUDIO_CLIENT_CLASS;
   payload.client_log_id             = log_id;
   payload.signal_ptr                = wait_signal_ptr;

   result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_REGISTER, &payload);

   AR_MSG(DBG_HIGH_PRIO,
          "PMSR:%08x: POSAL_POWER_MGR registered client %lu with result %lx",
          log_id,
          pm_handle_ptr->mmpm_client_id,
          result);

   /* If registration fails, need to free pm handle memory */
   if (POSAL_POWER_MGR_INVALID_CLIENT_ID == pm_handle_ptr->mmpm_client_id)
   {
      posal_memory_free(pm_handle_ptr);
      pm_handle_ptr = NULL;

      result = AR_EFAILED;
   }

   /* Assign this memory to registering client */
   *pm_handle_pptr = (posal_pm_handle_t)pm_handle_ptr;

   /* If local signal was used, destroy it */
   if (!has_signal)
   {
      // Destroy queue and channel
      posal_power_mgr_destroy_signal(&signal_ptr, &channel_ptr);
   }

   return result;
}

/** @ingroup posal_pm_wrapper
  Deregisters with PM Server

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t posal_power_mgr_deregister(posal_pm_handle_t *pm_handle_pptr, uint32_t log_id)
{
   ar_result_t      result = AR_EOK;

   if (!posal_power_mgr_is_registered(*pm_handle_pptr))
   {
      return AR_EOK;
   }

   uint32_t mmpm_client_id = ((pm_server_handle_t*)(*pm_handle_pptr))->mmpm_client_id;

   /* De-register has to be async request.
    * Since PM_SERVER gives priority to sync requests so if De-register is pushed to sync queue then it is possible that
    * the deregister will be handled before any other previous async requests from this client.
    * */
   pm_server_payload_t payload;
   payload.client_info.client_log_id = log_id;
   payload.client_info.pm_handle_ptr = *pm_handle_pptr;
   payload.client_log_id             = log_id;
   payload.signal_ptr                = NULL;

   result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_DEREGISTER, &payload);
   if (*pm_handle_pptr)
   {
     /* FREE is done in PM_SERVER thread.
      * It is possible that there are many async requests/releases are pending from this client.
      * So can not free pm_handle here.
      */
//    posal_memory_free(*pm_handle_pptr);

      //For clients, handle is set to NULL.
      *pm_handle_pptr = NULL;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "PMSR:%08x: POSAL_POWER_MGR deregistered client %lu with result %lx",
          log_id,
          mmpm_client_id,
          result);

   return result;
}

/**
 * bumps up the bus and Q6 clocks.
 */
ar_result_t posal_power_mgr_request_max_out(posal_pm_handle_t pm_handle_ptr, posal_signal_t wait_signal, uint32_t log_id)
{
   ar_result_t cmd_result = AR_EOK;

   if (NULL != pm_handle_ptr)
   {
      pm_server_payload_t      payload;
      posal_pm_request_info_t request;
      memset(&request, 0, sizeof(posal_pm_request_info_t));

      request.pm_handle_ptr                    = pm_handle_ptr;
      request.wait_signal_ptr                  = wait_signal;
      request.resources.mpps.is_valid          = TRUE;
      request.resources.mpps.value             = POSAL_POWER_MGR_MAX_OUT_MPPS;
      request.resources.mpps.floor_clk         = 0;
      request.resources.bw.is_valid            = TRUE;
      request.resources.bw.value               = POSAL_POWER_MGR_MAX_OUT_BW;
      request.resources.sleep_latency.is_valid = FALSE;
      request.resources.sleep_latency.value    = 0;
      request.client_log_id                    = log_id;

      payload.client_log_id = request.client_log_id;
      payload.signal_ptr    = request.wait_signal_ptr;
      memscpy(&payload.request_info, sizeof(posal_pm_request_info_t), &request, sizeof(posal_pm_request_info_t));

      cmd_result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_REQUEST, &payload);

      pm_server_handle_t* server_hdl_ptr = (pm_server_handle_t*)payload.request_info.pm_handle_ptr;

      if (NULL == wait_signal)
      {
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR max out request submitted to server for client_id %lu with result: %lx",
                payload.request_info.client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
      }
      else
      {
#ifdef DEBUG_PRINTS
         AR_MSG(DBG_HIGH_PRIO,
                "PMSR:%08x: POSAL_POWER_MGR max out request completed to server for client_id %lu with result: %lx",
                payload.request_info.client_log_id,
                server_hdl_ptr->mmpm_client_id,
                cmd_result);
#endif
      }
   }

   return cmd_result;
}

/**
 * releases the bus and Q6 clocks.
 */
ar_result_t posal_power_mgr_release_max_out(posal_pm_handle_t pm_handle_ptr, uint32_t log_id, uint32_t delay_ms)
{
   ar_result_t cmd_result  = AR_EOK;

   if (NULL != pm_handle_ptr)
   {
      pm_server_payload_t      payload;
      posal_pm_release_info_t release;
      memset(&release, 0, sizeof(posal_pm_release_info_t));

      release.pm_handle_ptr                    = pm_handle_ptr;
      release.wait_signal_ptr                  = NULL;
      release.resources.mpps.is_valid          = TRUE;
      release.resources.mpps.value             = 0;
      release.resources.mpps.floor_clk         = 0;
      release.resources.bw.is_valid            = TRUE;
      release.resources.bw.value               = 0;
      release.resources.sleep_latency.is_valid = FALSE;
      release.resources.sleep_latency.value    = 0;
      release.client_log_id                    = log_id;
      release.delay_ms                         = delay_ms;

      payload.client_log_id = release.client_log_id;
      payload.signal_ptr    = release.wait_signal_ptr;
      memscpy(&payload.release_info, sizeof(posal_pm_release_info_t), &release, sizeof(posal_pm_release_info_t));

      cmd_result = posal_power_mgr_send_command_internal(PM_SERVER_CMD_RELEASE, &payload);

      pm_server_handle_t *server_hdl_ptr = (pm_server_handle_t *)payload.release_info.pm_handle_ptr;
      AR_MSG(DBG_HIGH_PRIO,
             "PMSR:%08x: POSAL_POWER_MGR max out release submitted to server for client_id %lu with result: %lx",
             payload.release_info.client_log_id,
             server_hdl_ptr->mmpm_client_id,
             cmd_result);
   }

   return cmd_result;
}

/**
 * To send Message Commands to PM Server
 */
ar_result_t posal_power_mgr_send_command(uint32_t msg_opcode, void *payload_ptr, uint32_t payload_size)
{
   ar_result_t cmd_result;

   // Send message to server
   spf_msg_t         msg;
   uint32_t          msg_payload_size     = GET_SPF_MSG_REQ_SIZE(payload_size);
   void             *msg_payload_ptr      = NULL;
   spf_handle_t     *pm_server_handle_ptr = pm_server_get_handle();
   spf_msg_header_t *msg_header_ptr       = NULL;
   spf_msg_token_t   token;

   token.token_ptr = msg_payload_ptr;

   if (AR_EOK != (cmd_result = spf_msg_create_msg(&msg,                 /** MSG Ptr */
                                                  &msg_payload_size,    /** MSG payload size */
                                                  msg_opcode,           /** MSG opcode */
                                                  NULL,                 /** Response handle */
                                                  &token,               /** MSG Token */
                                                  pm_server_handle_ptr, /** Destination handle */
                                                  POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "POSAL_POWER_MGR: FAILED to create msg payload, opcode: 0x%lx, result: 0x%lx",
             msg_opcode,
             cmd_result);
      return cmd_result;
   }

   /* Get the pointer to GK message header */
   msg_header_ptr = (spf_msg_header_t *)msg.payload_ptr;

   /* Get the pointer to start of the message */
   msg_payload_ptr = (void *)(&msg_header_ptr->payload_start);

   /* Populate message payload */
   memscpy(msg_payload_ptr, payload_size, payload_ptr, payload_size);

   /* Send registration message to server */
   if (AR_EOK != (cmd_result = spf_msg_send_cmd(&msg, pm_server_handle_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to send msg to pm_server cmdQ, result: 0x%lx", cmd_result);
      return cmd_result;
   }

   return cmd_result;
}

/**
 * Checks PM registration.
 */
bool_t posal_power_mgr_is_registered(posal_pm_handle_t pm_handle_ptr)
{
   /* PM handle will be non-null only for successful registration */
   if (NULL == pm_handle_ptr)
   {
      return FALSE;
   }

   return TRUE;
}

/**
* No platform specific initialization currently
*/
void posal_power_mgr_init()
{
   return;
}

/**
* No platform specific de-initialization currently
*/
void posal_power_mgr_deinit()
{
   return;
}
