/**
 * \file sgm_path_delay_utils.c
 * \brief
 *     This file contains the utility functions for the path delay implementation
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

ar_result_t sgm_get_satellite_cont_id(spgm_info_t *spgm_ptr, uint32_t sat_miid, uint32_t *sat_cont_id_ptr)
{
   spf_list_node_t *  curr_node_ptr   = NULL;
   sgm_module_info_t *module_node_ptr = NULL;

   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = spgm_ptr->gu_graph_info.satellite_module_list_ptr;

   // spgm_ptr->gu_graph_info.num_satellite_modules

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         return AR_EBADPARAM;
      }

      if (sat_miid == module_node_ptr->instance_id)
      {
         *sat_cont_id_ptr = module_node_ptr->container_id;
         return AR_EOK;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return AR_EFAILED;
}

ar_result_t sgm_add_cont_id_delay_event_reg_list(spgm_info_t *spgm_ptr, uint32_t sat_cont_id, uint32_t master_path_id)
{
   ar_result_t                     result                = AR_EOK;
   sgm_sat_cont_delay_event_reg_t *pd_event_reg_node_ptr = NULL;

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_MED_PRIO,
               "add contd id 0x%lx & path id %lu to event reg list",
               sat_cont_id,
               master_path_id);

   bool_t found_node = FALSE;
   if (spgm_ptr->path_delay_list.num_sat_cont_registered)
   {
      spf_list_node_t *node_ptr = spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr;
      while (NULL != node_ptr)
      {
         pd_event_reg_node_ptr = (sgm_sat_cont_delay_event_reg_t *)node_ptr->obj_ptr;
         if (sat_cont_id == pd_event_reg_node_ptr->sat_cont_id)
         {
            found_node = TRUE;
            break;
         }
         node_ptr = node_ptr->next_ptr;
      }
   }

   if (FALSE == found_node)
   {
      pd_event_reg_node_ptr =
         (sgm_sat_cont_delay_event_reg_t *)posal_memory_malloc(sizeof(sgm_sat_cont_delay_event_reg_t),
                                                               spgm_ptr->cu_ptr->heap_id);
      if (NULL == pd_event_reg_node_ptr)
      {
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_ERROR_PRIO,
                     "Failed to allocate memory for delay event registration node per satellite container, size %lu",
                     sizeof(sgm_sat_cont_delay_event_reg_t));
         return AR_ENOMEMORY;
      }

      sgm_util_add_node_to_list(spgm_ptr,
                                &spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr,
                                pd_event_reg_node_ptr,
                                &spgm_ptr->path_delay_list.num_sat_cont_registered);

      pd_event_reg_node_ptr->is_event_registered = FALSE; // will be updated after registration
      pd_event_reg_node_ptr->sat_cont_id         = sat_cont_id;
   }

   if (pd_event_reg_node_ptr)
   {
      found_node            = FALSE;
      uint32_t *path_id_ptr = NULL;
      if (pd_event_reg_node_ptr->num_path_ref)
      {
         spf_list_node_t *node_ptr = pd_event_reg_node_ptr->master_path_id_list_ptr;
         while (NULL != node_ptr)
         {
            path_id_ptr = (uint32_t *)node_ptr->obj_ptr;
            if (master_path_id == *path_id_ptr)
            {
               found_node = TRUE;
               break;
            }
            node_ptr = node_ptr->next_ptr;
         }
      }

      if (FALSE == found_node)
      {
         path_id_ptr = (uint32_t *)posal_memory_malloc(sizeof(uint32_t), spgm_ptr->cu_ptr->heap_id);
         if (NULL == path_id_ptr)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "Failed to allocate memory for path_id_node_ptr in delay event registration node per satellite "
                        "container, size %lu",
                        sizeof(uint32_t));
            return AR_ENOMEMORY;
         }
         *path_id_ptr = master_path_id;
         sgm_util_add_node_to_list(spgm_ptr,
                                   &pd_event_reg_node_ptr->master_path_id_list_ptr,
                                   path_id_ptr,
                                   &pd_event_reg_node_ptr->num_path_ref);
      }
   }
   else
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to update the delay event registration node for satellite container id 0x%lx during "
                  "processing path id %lu",
                  sat_cont_id,
                  master_path_id);
   }

   return result;
}

ar_result_t sgm_register_path_delay_event(spgm_info_t *spgm_ptr, bool_t is_register)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   typedef struct get_delay_event_cfg_t
   {
      apm_cmd_header_t             header;
      apm_module_register_events_t reg_evt;
   } get_delay_event_cfg_t;

   get_delay_event_cfg_t *         get_delay_event_ptr          = NULL;
   spf_list_node_t *               sat_cont_delay_event_reg_ptr = NULL;
   sgm_sat_cont_delay_event_reg_t *pd_event_reg_node_ptr        = NULL;

   // Allocate the memory for the registration payload
   get_delay_event_ptr =
      (get_delay_event_cfg_t *)posal_memory_malloc(sizeof(get_delay_event_cfg_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == get_delay_event_ptr)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate memory to register for delay events from satellite containers");
      THROW(result, AR_ENOMEMORY);
      // if the registration fails. We may not be able to process this graph further
   }

   memset((void *)get_delay_event_ptr, 0, sizeof(get_delay_event_cfg_t));

   // fill the header with the size information
   get_delay_event_ptr->header.payload_size = sizeof(get_delay_event_cfg_t) - sizeof(apm_cmd_header_t);

   // fill the event payload
   get_delay_event_ptr->reg_evt.event_config_payload_size = 0;
   get_delay_event_ptr->reg_evt.event_id                  = OFFLOAD_EVENT_ID_GET_CONTAINER_DELAY;
   get_delay_event_ptr->reg_evt.is_register               = is_register;
   get_delay_event_ptr->reg_evt.module_instance_id        = 0; // will be updated below

   // fill the details to be filled in the GPR packet
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(get_delay_event_cfg_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)get_delay_event_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = spgm_ptr->sgm_id.cont_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = 0; // will be updated below
   spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_REGISTER_MODULE_EVENTS;
   spgm_ptr->process_info.active_data_hndl.token        = 0;

   sat_cont_delay_event_reg_ptr = spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr;

   while (sat_cont_delay_event_reg_ptr)
   {
      pd_event_reg_node_ptr = (sgm_sat_cont_delay_event_reg_t *)sat_cont_delay_event_reg_ptr->obj_ptr;
      spgm_ptr->process_info.active_data_hndl.dst_port = pd_event_reg_node_ptr->sat_cont_id;
      get_delay_event_ptr->reg_evt.module_instance_id  = pd_event_reg_node_ptr->sat_cont_id;
      if (is_register)
      {
         if (FALSE == pd_event_reg_node_ptr->is_event_registered)
         {
            // Send the Registration command to the satellite graph
            TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));
            pd_event_reg_node_ptr->is_event_registered = TRUE;
         }
      }
      else
      {
         if ((TRUE == pd_event_reg_node_ptr->is_event_registered) && (0 == pd_event_reg_node_ptr->num_path_ref))
         {
            // Send the de-registration command to the satellite graph
            TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));
            pd_event_reg_node_ptr->is_event_registered = FALSE;
         }
      }
      sat_cont_delay_event_reg_ptr = sat_cont_delay_event_reg_ptr->next_ptr;
   }

   // reset the active handl
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the event payload memory
   if (NULL != get_delay_event_ptr)
   {
      posal_memory_free(get_delay_event_ptr);
   }

   return result;
}

ar_result_t sgm_update_path_delay_list(spgm_info_t *spgm_ptr,
                                       uint32_t     master_path_id,
                                       uint32_t     sat_path_id,
                                       bool_t       add_to_list)
{
   ar_result_t                 result     = AR_EOK;
   sgm_path_id_mapping_info_t *pd_map_ptr = NULL;
   if (add_to_list)
   {
      pd_map_ptr = (sgm_path_id_mapping_info_t *)posal_memory_malloc(sizeof(sgm_path_id_mapping_info_t),
                                                                     spgm_ptr->cu_ptr->heap_id);
      if (pd_map_ptr)
      {
         sgm_util_add_node_to_list(spgm_ptr,
                                   &spgm_ptr->path_delay_list.path_id_mapping_info_ptr,
                                   pd_map_ptr,
                                   &spgm_ptr->path_delay_list.num_delay_paths);

         pd_map_ptr->master_path_id    = master_path_id;
         pd_map_ptr->satellite_path_id = sat_path_id;
      }
      else
      {
         // error
      }
   }
   else
   {
      bool_t found_node = FALSE;
      if (spgm_ptr->path_delay_list.num_delay_paths)
      {
         spf_list_node_t *node_ptr = spgm_ptr->path_delay_list.path_id_mapping_info_ptr;
         while (NULL != node_ptr)
         {
            pd_map_ptr = (sgm_path_id_mapping_info_t *)node_ptr->obj_ptr;
            if (master_path_id == pd_map_ptr->master_path_id)
            {
               found_node = TRUE;
               break;
            }
            node_ptr = node_ptr->next_ptr;
         }

         if (found_node)
         {
            sgm_util_remove_node_from_list(spgm_ptr,
                                           &spgm_ptr->path_delay_list.path_id_mapping_info_ptr,
                                           pd_map_ptr,
                                           &spgm_ptr->path_delay_list.num_delay_paths);
            posal_memory_free(pd_map_ptr);
            pd_map_ptr = NULL;
            found_node = FALSE;
         }
      }
   }

   return result;
}

ar_result_t sgm_get_master_path_id_given_sat_path_id(spgm_info_t *spgm_ptr,
                                                     uint32_t     sat_path_id,
                                                     uint32_t *   master_path_id)
{
   ar_result_t                 result     = AR_EOK;
   sgm_path_id_mapping_info_t *pd_map_ptr = NULL;
   spf_list_node_t *           node_ptr   = NULL;
   bool_t                      found_node = FALSE;

   *master_path_id = 0;

   if (spgm_ptr->path_delay_list.num_delay_paths)
   {
      node_ptr = spgm_ptr->path_delay_list.path_id_mapping_info_ptr;
      while (NULL != node_ptr)
      {
         pd_map_ptr = (sgm_path_id_mapping_info_t *)node_ptr->obj_ptr;
         if (sat_path_id == pd_map_ptr->satellite_path_id)
         {
            found_node = TRUE;
            break;
         }
         node_ptr = node_ptr->next_ptr;
      }

      if (TRUE == found_node)
      {
         *master_path_id = pd_map_ptr->master_path_id;
      }
      else
      {
         result = AR_EFAILED;
      }
   }

   return result;
}

ar_result_t spgm_handle_event_get_container_delay(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id         = 0;
   uint32_t payload_size   = 0;
   uint32_t master_path_id = 0;

   get_container_delay_event_t *delay_event_rsp_ptr = NULL;
   apm_module_event_t *         payload_ptr         = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "processing get container delay event response");

   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
   VERIFY(result, (payload_size >= (sizeof(get_container_delay_event_t) + sizeof(apm_module_event_t))));

   ////payload will have apm_module_event_t followed by the payload
   payload_ptr = GPR_PKT_GET_PAYLOAD(apm_module_event_t, packet_ptr);
   VERIFY(result, (NULL != payload_ptr));
   delay_event_rsp_ptr = (get_container_delay_event_t *)(payload_ptr + 1);

   result = sgm_get_master_path_id_given_sat_path_id(spgm_ptr, delay_event_rsp_ptr->path_id, &master_path_id);
   if (AR_EOK != result)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "failed to get master path id from satellite path id %lu. "
                  "check if path is closed on the master domain ",
                  delay_event_rsp_ptr->path_id);

      // there is a possibility of the OLC destroying the path and the satellite has raised path delay change event.
      // So we are not returning a failure in this case. only log messages would help us to  identify the scenario
      return AR_EOK;
   }

   olc_update_path_delay(spgm_ptr->cu_ptr, master_path_id, (void *)delay_event_rsp_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t sgm_destroy_path(spgm_info_t *spgm_ptr, uint32_t master_path_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t dummy_sat_path_id = 0;
   bool_t   found_node        = TRUE;

   uint32_t *                      path_id_node_ptr      = NULL;
   spf_list_node_t *               cont_node_ptr         = NULL;
   spf_list_node_t *               path_list_node_ptr    = NULL;
   sgm_sat_cont_delay_event_reg_t *pd_event_reg_node_ptr = NULL;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "processing destroy path, path id %lu", master_path_id);

   TRY(result,
       sgm_update_path_delay_list(spgm_ptr,
                                  master_path_id,
                                  dummy_sat_path_id /*ignore in this case */,
                                  FALSE /*implies to remove*/));

   if (spgm_ptr->path_delay_list.num_sat_cont_registered)
   {
      cont_node_ptr = spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr;
      while (NULL != cont_node_ptr)
      {
         pd_event_reg_node_ptr = (sgm_sat_cont_delay_event_reg_t *)cont_node_ptr->obj_ptr;
         if (pd_event_reg_node_ptr->num_path_ref)
         {
            path_list_node_ptr = pd_event_reg_node_ptr->master_path_id_list_ptr;
            while (NULL != path_list_node_ptr)
            {
               path_id_node_ptr = path_list_node_ptr->obj_ptr;
               if (master_path_id == *path_id_node_ptr)
               {
                  found_node = TRUE;
                  OLC_SGM_MSG(OLC_SGM_ID,
                              DBG_MED_PRIO,
                              "processing destroy path, satellite cont id 0x%lx in part for the path id %lu",
                              pd_event_reg_node_ptr->sat_cont_id,
                              master_path_id);
                  break;
               }
               path_list_node_ptr = path_list_node_ptr->next_ptr;
            }
            if (found_node)
            {
               sgm_util_remove_node_from_list(spgm_ptr,
                                              &pd_event_reg_node_ptr->master_path_id_list_ptr,
                                              path_id_node_ptr,
                                              &pd_event_reg_node_ptr->num_path_ref);
               posal_memory_free(path_id_node_ptr);
               path_id_node_ptr = NULL;
               found_node = FALSE;
            }
         }
         cont_node_ptr = cont_node_ptr->next_ptr;
         if (0 == pd_event_reg_node_ptr->num_path_ref)
         {
            cont_node_ptr = cont_node_ptr->next_ptr;
            // event deregister
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_MED_PRIO,
                        "processing destroy path, deregister and remove satellite cont id 0x%lx in path delay list",
                        pd_event_reg_node_ptr->sat_cont_id);
            sgm_register_path_delay_event(spgm_ptr, FALSE); // deregister
            sgm_util_remove_node_from_list(spgm_ptr,
                                           &spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr,
                                           pd_event_reg_node_ptr,
                                           &spgm_ptr->path_delay_list.num_sat_cont_registered);
            posal_memory_free(pd_event_reg_node_ptr);
            pd_event_reg_node_ptr = NULL;
            // remove event node
         }
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t sgm_path_delay_list_destroy(spgm_info_t *spgm_ptr, bool_t deregister_sat_cont)
{
   ar_result_t result = AR_EOK;

   /** delete the path delay, master-satellite path id mapping info list  */
   spf_list_delete_list_and_free_objs(&spgm_ptr->path_delay_list.path_id_mapping_info_ptr, TRUE);
   spgm_ptr->path_delay_list.num_delay_paths = 0;

   if (spgm_ptr->path_delay_list.num_sat_cont_registered)
   {
      sgm_sat_cont_delay_event_reg_t *pd_event_reg_node_ptr = NULL;
      spf_list_node_t *               node_ptr              = spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr;
      while (NULL != node_ptr)
      {
         pd_event_reg_node_ptr = (sgm_sat_cont_delay_event_reg_t *)node_ptr->obj_ptr;
         if (pd_event_reg_node_ptr->num_path_ref)
         {
            spf_list_delete_list_and_free_objs(&pd_event_reg_node_ptr->master_path_id_list_ptr, TRUE);
            pd_event_reg_node_ptr->num_path_ref = 0;
         }
         node_ptr = node_ptr->next_ptr;
      }
   }

   if ((TRUE == deregister_sat_cont))
   {
      sgm_register_path_delay_event(spgm_ptr, FALSE); // register
   }

   /** delete the satellite container delay event registration list  */
   spf_list_delete_list_and_free_objs(&spgm_ptr->path_delay_list.sat_cont_delay_event_reg_ptr, TRUE);
   spgm_ptr->path_delay_list.num_sat_cont_registered = 0;

   return result;
}
