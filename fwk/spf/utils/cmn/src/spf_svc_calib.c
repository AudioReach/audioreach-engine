/**
 * \file spf_svc_calib.c
 * \brief
 *     This file contains utilities to be used by typical services.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_svc_calib.h"
/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */

/* =======================================================================
**                      Static Function Definitions
** ======================================================================= */
#define IS_ALIGN_8_BYTE(a) (!((a) & (uint32_t)0x7))

#define IS_ALIGN_4_BYTE(a) (!((a) & (uint32_t)0x3))

#define ALIGN_8_BYTES(a) ((a + 7) & (0xFFFFFFF8))

ar_result_t spf_svc_alloc_rsp_payload(uint32_t       log_id,
                                      gpr_packet_t * gpr_pkt_ptr,
                                      gpr_packet_t **gpr_rsp_pkt_pptr,
                                      uint32_t       cmd_payload_size,
                                      uint8_t **     cmd_rsp_payload_pptr,
                                      spf_msg_t *    gpr_cmd_rsp_ptr)
{
   ar_result_t              result = AR_EOK;
   uint8_t *                curr_payload_ptr;
   uint8_t *                curr_rsp_payload_ptr;
   uint8_t *                payload_end_ptr;
   uint32_t                 rsp_payload_size;
   gpr_packet_t *           gpr_rsp_pkt_ptr;
   apm_module_param_data_t *mod_data_ptr;
   uint32_t                 mod_param_size;
   uint32_t                 cmd_opcode = gpr_pkt_ptr->opcode;

   /** Get the overall response payload size */
   rsp_payload_size = cmd_payload_size;
   gpr_cmd_alloc_ext_t args;
   /** Allocate GPR response packet  */

   switch (cmd_opcode)
   {
      case APM_CMD_GET_CFG:
      {
         rsp_payload_size += sizeof(apm_cmd_rsp_get_cfg_t);
         args.opcode = APM_CMD_RSP_GET_CFG;
         break;
      }
      case AMDB_CMD_LOAD_MODULES:
      {
         args.opcode = AMDB_CMD_RSP_LOAD_MODULES;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "CALI:%08lX: Unsupported cmd opcode 0x%X", log_id, cmd_opcode);
         return AR_EFAILED;
      }
   }

   args.src_domain_id = gpr_pkt_ptr->dst_domain_id;
   args.dst_domain_id = gpr_pkt_ptr->src_domain_id;
   args.src_port      = gpr_pkt_ptr->dst_port;
   args.dst_port      = gpr_pkt_ptr->src_port;
   args.token         = gpr_pkt_ptr->token;

   args.payload_size = rsp_payload_size;
   args.client_data  = 0;
   args.ret_packet   = gpr_rsp_pkt_pptr;

   if (AR_EOK != (result = __gpr_cmd_alloc_ext(&args)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CALI:%08lX: Failed to allocate rsp payload for in-band GET_CFG, result: %lu",
             log_id,
             result);
      return result;
   }

   gpr_rsp_pkt_ptr = *gpr_rsp_pkt_pptr;
   /** Save the allocated response payload only if it not NULL, users can send NULL if they don't care about saving*/
   if (NULL != gpr_cmd_rsp_ptr)
   {
      gpr_cmd_rsp_ptr->msg_opcode  = SPF_MSG_CMD_GPR;
      gpr_cmd_rsp_ptr->payload_ptr = (void *)gpr_rsp_pkt_ptr;
   }

   /** Get the pointer to start of the command payload */
   curr_payload_ptr = GPR_PKT_GET_PAYLOAD(uint8_t, gpr_pkt_ptr);

   /** Increment the cmd payload pointer to point to module param data header start */
   curr_payload_ptr += sizeof(apm_cmd_header_t);

   // overflow check
   if (curr_payload_ptr > curr_payload_ptr + cmd_payload_size) {
      result = AR_EBADPARAM;
      return result;
   }

   /** Get the payload end address */
   payload_end_ptr = curr_payload_ptr + cmd_payload_size;

   /** Get the pointer to start of the response payload */
   curr_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(uint8_t, gpr_rsp_pkt_ptr);

   switch (cmd_opcode)
   {
      case APM_CMD_GET_CFG:
      {
         /** Increment the response payload pointer to get the start of the PID data start */
         curr_rsp_payload_ptr += sizeof(apm_cmd_rsp_get_cfg_t);
         /** Copy the param data headers from the existing GPR packet to response payload */

         /** Update the payload pointer to be used for aggregating module configuration data */
         *cmd_rsp_payload_pptr = curr_rsp_payload_ptr;
         do
         {
            /** Get the module header pointer */
            mod_data_ptr = (apm_module_param_data_t *)curr_payload_ptr;

            mod_param_size = sizeof(apm_module_param_data_t) + mod_data_ptr->param_size;

            /** Param data should be at least 4 byte aligned. If not, then abort command processing */
            if (!IS_ALIGN_4_BYTE(mod_data_ptr->param_size) || (payload_end_ptr < curr_payload_ptr) || (mod_param_size > (size_t)(payload_end_ptr - curr_payload_ptr)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CALI:%08lX: Module [IID: 0x%lX] param ID [0x%lX], param size[%lu] is not correct",
                      log_id,
                      mod_data_ptr->module_instance_id,
                      mod_data_ptr->param_id,
                      mod_data_ptr->param_size);

               result = AR_EBADPARAM;
               /** End GPR command */
               goto __bail_out_get_cfg_alloc;
            }

            /** Copy the param data header info in the response payload */
            memscpy((void *)curr_rsp_payload_ptr,
                    (sizeof(apm_module_param_data_t) + mod_data_ptr->param_size),
                    (void *)curr_payload_ptr,
                    (sizeof(apm_module_param_data_t) + mod_data_ptr->param_size));

            mod_param_size = ALIGN_8_BYTES(mod_param_size);

            /** Advance the current payload pointer to point to next module instance param data */
            curr_payload_ptr += mod_param_size;

            /** Advance the current response payload pointer to point to next module instance param data location */
            curr_rsp_payload_ptr += mod_param_size;

         } while (curr_payload_ptr < payload_end_ptr);
         break;
      }
      case AMDB_CMD_LOAD_MODULES:
      {
         /** Update the payload pointer to be used for aggregating module configuration data */
         *cmd_rsp_payload_pptr = curr_rsp_payload_ptr;
         /** Copy the param data header info in the response payload */
         memscpy((void *)curr_rsp_payload_ptr, rsp_payload_size, (void *)curr_payload_ptr, cmd_payload_size);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "CALI:%08lX: Unsupported cmd opcode 0x%X", log_id, cmd_opcode);
         return AR_EFAILED;
      }
   }

   return result;

__bail_out_get_cfg_alloc:

   /** Free up the allocated GPR response packet */
   __gpr_cmd_free(gpr_rsp_pkt_ptr);

   return result;
}

/* NOTE:
 * gpr_cmd_rsp_ptr can be NULL if the user of this function do not care about filling it's contents
 */

ar_result_t spf_svc_get_cmd_payload_addr(uint32_t       log_id,
                                         gpr_packet_t * gpr_pkt_ptr,
                                         gpr_packet_t **gpr_rsp_pkt_pptr,
                                         uint8_t **     cmd_payload_pptr,
                                         uint32_t *     byte_aligned_size_ptr,
                                         spf_msg_t *    gpr_cmd_rsp_ptr,
                                         uint32_t       memory_map_client)
{
   ar_result_t       result = AR_EOK;
   uint64_t          payload_address;
   apm_cmd_header_t *cmd_header_ptr = (apm_cmd_header_t *)GPR_PKT_GET_PAYLOAD(void, gpr_pkt_ptr);
   uint32_t          cmd_opcode     = gpr_pkt_ptr->opcode;
   /** Clear the return payload address */
   *cmd_payload_pptr = NULL;

   if (0 == cmd_header_ptr->mem_map_handle) /** in-band */
   {
      /** Get the GPR payload pointer */
      *cmd_payload_pptr = (uint8_t *)(cmd_header_ptr + 1);

      AR_MSG(DBG_MED_PRIO,
             "CALI:%08lX: Received in-band parameters of size: %lu bytes, cmd_opcode[0x%lX]",
             log_id,
             cmd_header_ptr->payload_size,
             cmd_opcode);

      /** If the current opcode is In-band get config, existing GPR packet needs to be freed up and response payload for
       * get config needs to be allocated */
      if (APM_CMD_GET_CFG == cmd_opcode)
      {
         if (AR_EOK != (result = spf_svc_alloc_rsp_payload(log_id,
                                                           gpr_pkt_ptr,
                                                           gpr_rsp_pkt_pptr,
                                                           cmd_header_ptr->payload_size,
                                                           cmd_payload_pptr,
                                                           gpr_cmd_rsp_ptr)))
         {
            return result;
         }
      }

      /** Update the payload alignment size */
      *byte_aligned_size_ptr = 8;
   }
   else /** Out-of-band */
   {
      if (0 == memory_map_client)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CALI:%08lX: Memory map cient Null, cannot get virtual address, cmd_opcode[0x%lX]",
                log_id,
                cmd_opcode);
         return AR_EBADPARAM;
      }

      /** Get virtual address corresponding to physical adddess */
      if (AR_EOK != (result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(memory_map_client,
                                                                                  cmd_header_ptr->mem_map_handle,
                                                                                  cmd_header_ptr->payload_address_lsw,
                                                                                  cmd_header_ptr->payload_address_msw,
                                                                                  cmd_header_ptr->payload_size,
                                                                                  FALSE,
                                                                                  &payload_address)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CALI:%08lX: Failed to get virtual addr for phy addr, MSW[0x%lX], LSW[0x%lX], cmd_opcode[0x%lX]",
                log_id,
                cmd_header_ptr->payload_address_msw,
                cmd_header_ptr->payload_address_lsw,
                cmd_opcode);

         return AR_EFAILED;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "CALI:%08lX: Memory Map PhyMSW(0x%lx) PhyLSW(0x%lx) virt addr(0x%lx), %lu bytes, cmd_opcode[0x%lX]",
             log_id,
             cmd_header_ptr->payload_address_msw,
             cmd_header_ptr->payload_address_lsw,
             payload_address,
             cmd_header_ptr->payload_size,
             cmd_opcode);

      if (AR_EOK != (result = posal_cache_invalidate_v2((&payload_address), cmd_header_ptr->payload_size)))
      {
         AR_MSG(DBG_ERROR_PRIO, "CALI:%08lX: Failed to invalidate cache", log_id);
         return AR_EFAILED;
      }

      /** Check if the out-of-band payload start address is 8 byte aligned */
      if (!IS_ALIGN_8_BYTE(payload_address))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CALI:%08lX: Out-band param data start address: (0x%8lX) is not 8 byte aligned, "
                "cmd_opcode[0x%lX]",
                log_id,
                payload_address,
                cmd_opcode);

         return AR_EBADPARAM;
      }

      /** Update the payload alignment size */
      *byte_aligned_size_ptr = 8;

      /** Populate the command address if there are no failures  */
      *cmd_payload_pptr = (uint8_t *)payload_address;
   } /** End of else ouf-of-band*/

   return result;
}
