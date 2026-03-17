/**
 * \file apm_offload_memmap_handler.c
 * \brief
 *     This file implements utility functions to manage shared memory between Master DSP and Slave DSP in the
 *  Multi-DSP-Framework
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal.h"
#include "apm_offload_memmap_handler.h"
#include "apm_offload_memmap_utils.h"
#include "apm_memmap_api.h"
#include "apm_cmd_utils.h"
#include "apm_ext_cmn.h"
#include "apm.h"

static ar_result_t apm_offload_mem_shared_memory_map_regions_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t                               *pkt_ptr            = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t                               *sh_mem_map_gpr_ptr = NULL;
   apm_cmd_shared_mem_map_regions_t           *sh_mem_map_cmd_ptr = NULL;
   void                                       *payload_ptr;
   apm_cmd_shared_satellite_mem_map_regions_t *mdf_mem_map_regions_payload_ptr;
   apm_shared_map_region_payload_t            *region_ptr;
   void                                       *sh_mem_map_cmd_regions_ptr = NULL;
   bool_t                                      is_offset_map;
   apm_ext_utils_t                            *ext_utils_ptr;

   // get MDF_MEM_MAP payload.
   payload_ptr                     = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
   mdf_mem_map_regions_payload_ptr = (apm_cmd_shared_satellite_mem_map_regions_t *)payload_ptr;

   AR_MSG(DBG_HIGH_PRIO, "MDF: Received MDF Memmap CMD");

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   if (ext_utils_ptr->shmem_vtbl_ptr && ext_utils_ptr->shmem_vtbl_ptr->apm_shmem_is_supported_mem_pool_fptr)
   {
      // Mem-Pool ID has to be one of the supported ones
      if (FALSE == ext_utils_ptr->shmem_vtbl_ptr->apm_shmem_is_supported_mem_pool_fptr(
                      mdf_mem_map_regions_payload_ptr->mem_pool_id))
      {
         AR_MSG(DBG_ERROR_PRIO, "MDF: Memmap Unsupported Mempool ID %lu", mdf_mem_map_regions_payload_ptr->mem_pool_id);
         return __gpr_cmd_end_command(pkt_ptr, result);
      }
   }

   is_offset_map = (mdf_mem_map_regions_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE) >>
                   APM_MEMORY_MAP_SHIFT_IS_OFFSET_MODE;

   if ((1 != mdf_mem_map_regions_payload_ptr->num_regions) || (!is_offset_map))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MDF: apm_offload_shmem_util Mem Map: Received Invalid num regions %lu. Has to be ONE; is_offset_map = %d",
             mdf_mem_map_regions_payload_ptr->num_regions,
             is_offset_map);

      return __gpr_cmd_end_command(pkt_ptr, result);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "MDF Memmap CMD :Mem Pool ID = %lu, num regions = %lu, Sat Dom ID = %lu ",
          mdf_mem_map_regions_payload_ptr->mem_pool_id,
          mdf_mem_map_regions_payload_ptr->num_regions,
          mdf_mem_map_regions_payload_ptr->satellite_proc_domain_id);

   /** Allocate command handler resources  */
   if (AR_EOK != (result = apm_allocate_cmd_hdlr_resources(apm_info_ptr, msg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "MDF: Failed to allocate cmd rsc");

      __gpr_cmd_end_command(pkt_ptr, result);

      return result;
   }

   region_ptr =
      (apm_shared_map_region_payload_t *)((uint8_t *)payload_ptr + sizeof(apm_cmd_shared_satellite_mem_map_regions_t));

   // allocate the GPR packet to Send to Slave DSP
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id; // Master ID
   args.src_port      = pkt_ptr->dst_port;
   args.dst_domain_id = mdf_mem_map_regions_payload_ptr->satellite_proc_domain_id;
   args.dst_port      = pkt_ptr->dst_port;
   args.client_data   = 0;
   args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;
   // Will use this token during response tracking and for bookkeeping
   args.opcode       = APM_CMD_SHARED_MEM_MAP_REGIONS;
   args.payload_size = sizeof(apm_cmd_shared_mem_map_regions_t) +
                       (mdf_mem_map_regions_payload_ptr->num_regions * sizeof(apm_shared_map_region_payload_t));
   args.ret_packet = &sh_mem_map_gpr_ptr;

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == sh_mem_map_gpr_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO, "MDF: Allocating mapping command failed with %lu", result);
      goto __bailout_mdf_cmd_hdlr;
   }

   /* prepare the cmd payload */
   sh_mem_map_cmd_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_shared_mem_map_regions_t, sh_mem_map_gpr_ptr);

   sh_mem_map_cmd_regions_ptr = (void *)((uint8_t *)sh_mem_map_cmd_ptr + sizeof(apm_cmd_shared_mem_map_regions_t));

   sh_mem_map_cmd_ptr->mem_pool_id = mdf_mem_map_regions_payload_ptr->mem_pool_id;
   sh_mem_map_cmd_ptr->num_regions = mdf_mem_map_regions_payload_ptr->num_regions;
   // Property Flag: We always set it to 0 while sending it to the satellite so that it doesn't do the heap creation
   // Loaned is only relevant in the master DSP
   sh_mem_map_cmd_ptr->property_flag =
      (mdf_mem_map_regions_payload_ptr->property_flag & (~APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED));

   memscpy(sh_mem_map_cmd_regions_ptr,
           mdf_mem_map_regions_payload_ptr->num_regions * sizeof(apm_shared_map_region_payload_t),
           region_ptr,
           mdf_mem_map_regions_payload_ptr->num_regions * sizeof(apm_shared_map_region_payload_t));

   if (AR_EOK != (result = __gpr_cmd_async_send(sh_mem_map_gpr_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "MDF:sending mapping command failed with %lu", result);
      goto __bailout_mdf_cmd_hdlr_2;
   }

   /** Increment the number of commands issues */
   apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

   /** If at least 1 command issued to container, set the
    *  command response pending status */
   apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;

   return result;

__bailout_mdf_cmd_hdlr_2:
   __gpr_cmd_free(sh_mem_map_gpr_ptr);

__bailout_mdf_cmd_hdlr:
   __gpr_cmd_end_command(pkt_ptr, result);

   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_info_ptr->curr_cmd_ctrl_ptr);

   return result;
}

static ar_result_t apm_offload_mem_shared_memory_map_regions_rsp_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t *                              pkt_ptr             = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t *                              mdf_gpr_resp_ptr    = NULL;
   gpr_packet_t *                              cmd_gpr_payload_ptr = NULL;
   void *                                      cmd_payload_ptr, *rsp_payload_ptr, *dummy_ptr = NULL;
   apm_cmd_shared_satellite_mem_map_regions_t *mem_map_mdf_cmd_payload_ptr = NULL;
   apm_cmd_rsp_shared_mem_map_regions_t *      curr_rsp_payload_ptr        = NULL;
   apm_cmd_ctrl_t *                            ctrl_obj_ptr                = NULL;
   bool_t                                      is_mem_loaned;
   //apm_shared_map_region_payload_t *           region_ptr;

   AR_MSG(DBG_HIGH_PRIO, "Received MDF MEMMAP RSP CMD");

   // First figure out which command this is a response to.
   uint32_t cmd_ctrl_list_idx = pkt_ptr->token;

   if (cmd_ctrl_list_idx >= APM_NUM_MAX_PARALLEL_CMD)
   {
      AR_MSG(DBG_ERROR_PRIO,
             " Unexpected Error: MDF Memmap Rsp Handler: NOT sending MEMMAP RSP to the client. Can't recover the "
             "command ctx, Token %lu is out of bounds. Bailing out.",
             cmd_ctrl_list_idx);

      result = __gpr_cmd_free(pkt_ptr);
      return (result | AR_EUNEXPECTED);
   }

   ctrl_obj_ptr = apm_get_nth_cmd_ctrl_obj(apm_info_ptr, cmd_ctrl_list_idx);

   if (ctrl_obj_ptr->cmd_opcode != APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MDF Memmap Rsp Handler: ctrl_obj_ptr->cmd_opcode 0x%lx is Incorrect. Bailing out.",
             ctrl_obj_ptr->cmd_opcode);

      goto __bailout_rsp_hdlr_1;
   }

   // original command payload
   cmd_gpr_payload_ptr         = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
   cmd_payload_ptr             = GPR_PKT_GET_PAYLOAD(void, cmd_gpr_payload_ptr);
   mem_map_mdf_cmd_payload_ptr = (apm_cmd_shared_satellite_mem_map_regions_t *)cmd_payload_ptr;

   // get MEM_MAP payload.
   rsp_payload_ptr      = GPR_PKT_GET_PAYLOAD(void, pkt_ptr); // this has sat memhandle and its domain
   curr_rsp_payload_ptr = (apm_cmd_rsp_shared_mem_map_regions_t *)rsp_payload_ptr;

   /* Send the MDF MEM MAP response packet */
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id;             // current packet's dst
   args.dst_domain_id = cmd_gpr_payload_ptr->src_domain_id; // src domain of the original cmd packet.
   args.src_port      = pkt_ptr->dst_port;
   args.dst_port      = cmd_gpr_payload_ptr->src_port;
   args.token         = cmd_gpr_payload_ptr->token;
   args.opcode        = APM_CMD_RSP_SHARED_SATELLITE_MEM_MAP_REGIONS;
   args.payload_size  = sizeof(apm_cmd_rsp_shared_satellite_mem_map_regions_t);
   args.ret_packet    = &mdf_gpr_resp_ptr;
   args.client_data   = 0;
   result             = __gpr_cmd_alloc_ext(&args);

   if (AR_DID_FAIL(result) || NULL == mdf_gpr_resp_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO,
             "MDF Memmap Rsp Handler: memory map response packet allocation failed with error code  = %d!",
             result);
      goto __bailout_rsp_hdlr_1;
   }

   /* prepare the Response payload pointer */
   apm_cmd_rsp_shared_satellite_mem_map_regions_t *mdf_resp_payload_ptr;
   dummy_ptr                            = GPR_PKT_GET_PAYLOAD(void, mdf_gpr_resp_ptr);
   mdf_resp_payload_ptr                 = (apm_cmd_rsp_shared_satellite_mem_map_regions_t *)dummy_ptr;
   mdf_resp_payload_ptr->mem_map_handle = curr_rsp_payload_ptr->mem_map_handle; // from current rsp pkt

   is_mem_loaned = (mem_map_mdf_cmd_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED) >>
                   APM_MEMORY_MAP_SHIFT_IS_MEM_LOANED;

   /*region_ptr = (apm_shared_map_region_payload_t *)((uint8_t *)mem_map_mdf_cmd_payload_ptr +
                                                    sizeof(apm_cmd_shared_satellite_mem_map_regions_t));*/

   /*If memory is loaned to the master, we need to register the satellite handle*/
   if (is_mem_loaned)
   {
      result = apm_offload_satellite_memorymap_register(mem_map_mdf_cmd_payload_ptr->master_mem_handle,
                                                        curr_rsp_payload_ptr->mem_map_handle, /*satellite handle*/
                                                        mem_map_mdf_cmd_payload_ptr->satellite_proc_domain_id);
   }
   else
   {
      result = apm_offload_unloaned_mem_register(mem_map_mdf_cmd_payload_ptr->satellite_proc_domain_id,
                                                 mem_map_mdf_cmd_payload_ptr->master_mem_handle,
                                                 curr_rsp_payload_ptr->mem_map_handle);
   }

   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload Satellite Memmap Register with posal failed. is_mem_loaned = %u", is_mem_loaned);
      goto __bailout_rsp_hdlr_2;
   }

   /* send response */
   if (AR_EOK != (result = __gpr_cmd_async_send(mdf_gpr_resp_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO,
             "MDF: Unexpected: GPR Send failed. But SAT map is registered Memmap Rsp Handler failed to send the memory "
             "map response command to client, error code is  = 0x%x",
             result);
      goto __bailout_rsp_hdlr_2;
   }

   // Free the mdf resp packet as we don't respond to it
   __gpr_cmd_free(pkt_ptr);
   // Free the original command packet
   __gpr_cmd_free(cmd_gpr_payload_ptr);

   // Clear APM cmd message control
   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, ctrl_obj_ptr);

   return result;

__bailout_rsp_hdlr_2:
   __gpr_cmd_free(mdf_gpr_resp_ptr);
__bailout_rsp_hdlr_1:
   __gpr_cmd_free(pkt_ptr);
   __gpr_cmd_end_command(cmd_gpr_payload_ptr, result);

   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, ctrl_obj_ptr);

   return result;
}

static ar_result_t apm_offload_mem_shared_memory_unmap_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t *                                pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;
   void *                                        payload_ptr;
   apm_cmd_shared_satellite_mem_unmap_regions_t *mdf_mem_unmap_payload_ptr;

   // get MDF_MEM_UNMAP payload.
   payload_ptr               = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
   mdf_mem_unmap_payload_ptr = (apm_cmd_shared_satellite_mem_unmap_regions_t *)payload_ptr;

   // Make the payload for the unmap cmd to be sent to satellite
   apm_cmd_shared_mem_unmap_regions_t sat_unmap_cmd;

   sat_unmap_cmd.mem_map_handle =
      apm_offload_get_sat_handle_from_master_handle(mdf_mem_unmap_payload_ptr->master_mem_handle,
                                                    mdf_mem_unmap_payload_ptr->satellite_proc_domain_id);

   if (APM_OFFLOAD_INVALID_VAL == sat_unmap_cmd.mem_map_handle)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Couldn't find the sat handle associated with the master handle 0x%lx and sat domain id %lu",
             mdf_mem_unmap_payload_ptr->master_mem_handle,
             mdf_mem_unmap_payload_ptr->satellite_proc_domain_id);

      return __gpr_cmd_end_command(pkt_ptr, AR_EFAILED);
   }

   /** Allocate command handler resources  */
   if (AR_EOK != (result = apm_allocate_cmd_hdlr_resources(apm_info_ptr, msg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "MDF: Failed to allocate cmd rsc");

      __gpr_cmd_end_command(pkt_ptr, result);

      return result;
   }

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id; // Master ID
   args.src_port      = pkt_ptr->dst_port;
   args.dst_domain_id = mdf_mem_unmap_payload_ptr->satellite_proc_domain_id;
   args.dst_port      = pkt_ptr->dst_port;
   args.client_data   = 0;
   args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;
   // Will use this token during response tracking and for bookkeeping
   args.opcode       = APM_CMD_SHARED_MEM_UNMAP_REGIONS;
   args.payload_size = sizeof(sat_unmap_cmd);
   args.payload      = &sat_unmap_cmd;

   AR_MSG(DBG_HIGH_PRIO,
          "Received MDF Unmap from src_domain %lu, src port 0x%lx, Sat Mem handle 0x%lx",
          pkt_ptr->src_domain_id,
          pkt_ptr->src_port,
          sat_unmap_cmd.mem_map_handle);

   result = __gpr_cmd_alloc_send(&args);

   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "MDF: Sending memory unmapping command failed with %lu", result);

      apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_info_ptr->curr_cmd_ctrl_ptr);

      return __gpr_cmd_end_command(pkt_ptr, result);
   }
   /** Increment the number of commands issues */
   apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

   /** If at least 1 command issued to container, set the
    *  command response pending status */
   apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;

   return result;
}

ar_result_t apm_offload_mem_basic_rsp_handler(apm_t *         apm_info_ptr,
                                              apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                              gpr_packet_t *  gpr_pkt_ptr)
{
   ar_result_t                                   result                        = AR_EOK;
   void *                                        cmd_payload_ptr               = NULL;
   apm_cmd_shared_satellite_mem_unmap_regions_t *mem_unmap_mdf_cmd_payload_ptr = NULL;
   gpr_packet_t *                                cmd_gpr_payload_ptr           = NULL;
   gpr_ibasic_rsp_result_t *                     rsp_payload_ptr               = NULL;

   rsp_payload_ptr     = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, gpr_pkt_ptr);
   cmd_gpr_payload_ptr = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** We can get the basic response in two main cases:
       1. A response for the MDF Unmap command, or
       2. Perhaps a failure for the MDF MAP command */
   switch (rsp_payload_ptr->opcode)
   {
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      {
         result = rsp_payload_ptr->status;

         cmd_payload_ptr               = GPR_PKT_GET_PAYLOAD(void, cmd_gpr_payload_ptr);
         mem_unmap_mdf_cmd_payload_ptr = (apm_cmd_shared_satellite_mem_unmap_regions_t *)cmd_payload_ptr;

         // handle success and failure case
         if (AR_EOK == result)
         {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
            AR_MSG(DBG_HIGH_PRIO, "APM: apm_offload_mem_basic_rsp_handler: EOK result for Unmap from SAT");
#endif // APM_OFFLOAD_MEM_MAP_DBG
            result = apm_offload_satellite_memorymap_check_deregister(mem_unmap_mdf_cmd_payload_ptr->master_mem_handle,
                                                                      mem_unmap_mdf_cmd_payload_ptr
                                                                         ->satellite_proc_domain_id);
         }
         else
         {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
            AR_MSG(DBG_HIGH_PRIO, "APM: apm_offload_mem_basic_rsp_handler: NOT EOK result for Unmap from SAT");
#endif // APM_OFFLOAD_MEM_MAP_DBG
         }
         break;
      }
      case APM_CMD_SHARED_MEM_MAP_REGIONS:
      {
         // handle just the failure case.
         if (AR_EOK != rsp_payload_ptr->status)
         {
            result = rsp_payload_ptr->status;
#ifdef APM_OFFLOAD_MEM_MAP_DBG
            AR_MSG(DBG_HIGH_PRIO, "APM: apm_offload_mem_basic_rsp_handler: NOT EOK result for MAP from SAT");
#endif // APM_OFFLOAD_MEM_MAP_DBG
         }
         // else case should never really happen
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "APM: apm_offload_mem_basic_rsp_handler: Unexpected response for cmd opcode 0x%lx",
                rsp_payload_ptr->opcode);
         break;
      }
   }
   AR_MSG(DBG_HIGH_PRIO,
          "APM: apm_offload_basic_rsp_handler: Ending Command with result %lu, dst domain id = %lu, dst port = 0x%lx, "
          "src_domain_id = %lu, src_port = 0x%lx, token = %lu",
          result,
          cmd_gpr_payload_ptr->src_domain_id,
          cmd_gpr_payload_ptr->src_port,
          cmd_gpr_payload_ptr->dst_domain_id,
          cmd_gpr_payload_ptr->dst_port,
          cmd_gpr_payload_ptr->token);

   __gpr_cmd_end_command(cmd_gpr_payload_ptr, result); // send response to master's client
   __gpr_cmd_free(gpr_pkt_ptr);
   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_cmd_ctrl_ptr);

   return result;
}

ar_result_t apm_offload_shmem_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      cmd_opcode;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   switch (cmd_opcode)
   {
      case APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS:
      {
         result = apm_offload_mem_shared_memory_map_regions_cmd_handler(apm_info_ptr, msg_ptr);
         break;
      }
      case APM_CMD_RSP_SHARED_MEM_MAP_REGIONS:
      {
         result = apm_offload_mem_shared_memory_map_regions_rsp_handler(apm_info_ptr, msg_ptr);
         break;
      }
      case APM_CMD_SHARED_SATELLITE_MEM_UNMAP_REGIONS:
      {
         result = apm_offload_mem_shared_memory_unmap_cmd_handler(apm_info_ptr, msg_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_offload_shmem_cmd_handler(): Unsupported mdf shmem opcode: 0x%lX", cmd_opcode);

         result = AR_EUNSUPPORTED;

         /** End the GPR command */
         __gpr_cmd_end_command(gpr_pkt_ptr, result);

         break;
      }

   } /** End of switch (cmd_opcode) */

   return result;
}
