/**
 * \file gen_topo_data_port_ops_intf_ext.c
 *
 * \brief
 *
 *     Implementation of path delay aspects of topology interface functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"

ar_result_t gen_topo_intf_extn_data_ports_hdl_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   INIT_EXCEPTION_HANDLING
   capi_err_t                       result          = CAPI_EOK;
   intf_extn_data_port_operation_t *in_port_op_ptr  = NULL;
   intf_extn_data_port_operation_t *out_port_op_ptr = NULL;

   if (!topo_ptr || !module_ptr)
   {
      return AR_EFAILED;
   }

   VERIFY(result, module_ptr->capi_ptr);

   // Open all the input ports of the module in one shot.
   uint32_t num_input_ports = module_ptr->gu.num_input_ports + module_ptr->gu.num_ipc_input_ports;
   if (num_input_ports)
   {
      uint32_t size =
         sizeof(intf_extn_data_port_operation_t) + sizeof(intf_extn_data_port_id_idx_map_t) * num_input_ports;

      MALLOC_MEMSET(in_port_op_ptr, intf_extn_data_port_operation_t, size, topo_ptr->heap_id, result);

      in_port_op_ptr->is_input_port                      = TRUE;
      in_port_op_ptr->opcode                             = INTF_EXTN_DATA_PORT_OPEN;
      in_port_op_ptr->num_ports                          = num_input_ports;
      in_port_op_ptr->opcode_payload_buf.data_ptr        = NULL;
      in_port_op_ptr->opcode_payload_buf.actual_data_len = 0;
      in_port_op_ptr->opcode_payload_buf.max_data_len    = 0;
      intf_extn_data_port_id_idx_map_t *id_idx_map_ptr   = in_port_op_ptr->id_idx;
      uint32_t                          j                = 0;
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gu_input_port_t *ip_port_ptr = in_port_list_ptr->ip_port_ptr;
         id_idx_map_ptr[j].port_id    = ip_port_ptr->cmn.id;
         id_idx_map_ptr[j].port_index = ip_port_ptr->cmn.index;
         j++;
      }

      for (gu_input_port_list_t *ipc_in_port_list_ptr = module_ptr->gu.ipc_input_port_list_ptr;
           (NULL != ipc_in_port_list_ptr);
           LIST_ADVANCE(ipc_in_port_list_ptr))
      {
         gu_input_port_t *vir_ip_port_ptr = ipc_in_port_list_ptr->ip_port_ptr;
         id_idx_map_ptr[j].port_id        = vir_ip_port_ptr->cmn.id;
         id_idx_map_ptr[j].port_index     = vir_ip_port_ptr->cmn.index;
         j++;
      }

      capi_buf_t buf;
      buf.actual_data_len = size;
      buf.max_data_len    = size;
      buf.data_ptr        = (int8_t *)(in_port_op_ptr);

      capi_port_info_t port_info;
      port_info.is_valid = FALSE;

      result |= module_ptr->capi_ptr->vtbl_ptr->set_param(module_ptr->capi_ptr,
                                                          INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION,
                                                          &port_info,
                                                          &buf);
   }

   // Open all the output ports of the module in one shot.
   uint32_t num_output_ports = module_ptr->gu.num_output_ports + module_ptr->gu.num_ipc_output_ports;
   if (num_output_ports)
   {
      uint32_t size =
         sizeof(intf_extn_data_port_operation_t) + sizeof(intf_extn_data_port_id_idx_map_t) * num_output_ports;

      MALLOC_MEMSET(out_port_op_ptr, intf_extn_data_port_operation_t, size, topo_ptr->heap_id, result);

      out_port_op_ptr->is_input_port                      = FALSE;
      out_port_op_ptr->opcode                             = INTF_EXTN_DATA_PORT_OPEN;
      out_port_op_ptr->num_ports                          = num_output_ports;
      out_port_op_ptr->opcode_payload_buf.data_ptr        = NULL;
      out_port_op_ptr->opcode_payload_buf.actual_data_len = 0;
      out_port_op_ptr->opcode_payload_buf.max_data_len    = 0;
      intf_extn_data_port_id_idx_map_t *id_idx_map_ptr    = out_port_op_ptr->id_idx;
      uint32_t                          j                 = 0;
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gu_output_port_t *out_port_ptr = out_port_list_ptr->op_port_ptr;
         id_idx_map_ptr[j].port_id      = out_port_ptr->cmn.id;
         id_idx_map_ptr[j].port_index   = out_port_ptr->cmn.index;
         j++;
      }

      for (gu_output_port_list_t *ipc_out_port_list_ptr = module_ptr->gu.ipc_output_port_list_ptr;
           (NULL != ipc_out_port_list_ptr);
           LIST_ADVANCE(ipc_out_port_list_ptr))
      {
         gu_output_port_t *ipc_out_port_ptr = ipc_out_port_list_ptr->op_port_ptr;
         id_idx_map_ptr[j].port_id           = ipc_out_port_ptr->cmn.id;
         id_idx_map_ptr[j].port_index        = ipc_out_port_ptr->cmn.index;
         j++;
      }

      capi_buf_t buf;
      buf.actual_data_len = size;
      buf.max_data_len    = size;
      buf.data_ptr        = (int8_t *)(out_port_op_ptr);

      capi_port_info_t port_info;
      port_info.is_valid = FALSE;

      result |= module_ptr->capi_ptr->vtbl_ptr->set_param(module_ptr->capi_ptr,
                                                          INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION,
                                                          &port_info,
                                                          &buf);
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   // free allocated memory
   MFREE_NULLIFY(in_port_op_ptr);
   MFREE_NULLIFY(out_port_op_ptr);

   return result;
}

// correspnding func for control ports: topo_port_state_to_ctrl_port_opcode
static intf_extn_data_port_opcode_t topo_port_state_to_data_port_opcode(topo_port_state_t topo_state)
{
   switch (topo_state)
   {
      case TOPO_PORT_STATE_STARTED:
         return INTF_EXTN_DATA_PORT_START;

      case TOPO_PORT_STATE_STOPPED:
         return INTF_EXTN_DATA_PORT_STOP;

      case TOPO_PORT_STATE_SUSPENDED:
         return INTF_EXTN_DATA_PORT_SUSPEND;

      default:
         return INTF_EXTN_DATA_PORT_OP_INVALID;
   }
}

/**
 * port operation on one port. returns back for single port module.
 *
 * 1. in open:
 *    a) set port-open to the module one by one if only port is created (ext port or intra-container SG port)
 *    b) set port open for all ports if module itself id new.
   2. in close:
      a) if ext port close, close only port.
      b) if intra container SG  close is done, then close ports.
         if whole module is getting closed, no need to close ports.
 */
ar_result_t gen_topo_capi_set_data_port_op(gen_topo_module_t *           module_ptr,
                                           intf_extn_data_port_opcode_t  opcode,
                                           intf_extn_data_port_opcode_t *last_issued_opcode_ptr,
                                           bool_t                        is_input,
                                           uint32_t                      port_index,
                                           uint32_t                      port_id)
{
   capi_err_t  err_code = CAPI_EOK;
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;
   uint32_t    log_id   = topo_ptr->gu.log_id;

   // function is only for CAPI modules
   if (!module_ptr->capi_ptr)
   {
      return result;
   }
   // if data port intf extension is not supported return
   if (FALSE == module_ptr->flags.supports_data_port_ops)
   {
      return result;
   }

   // For last port (first port), num_port must be decremented before (incremented after) calling this function

   // Modules may not handle if the same command is issued twice. So issue command only if opcode is different.
   // And Do not apply any operations on Closed ports. Closed ports will be eventually destroyed, they cannot move to
   // other states.
   if (*last_issued_opcode_ptr == opcode || (*last_issued_opcode_ptr == INTF_EXTN_DATA_PORT_CLOSE))
   {
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "No need to apply port operation on module iid= 0x%lx , port id= 0x%lx, last_issued_opcode=0x%lx, cur "
               "opcode=0x%lx",
               module_ptr->gu.module_instance_id,
               port_id,
               *last_issued_opcode_ptr,
               opcode);
#endif
      return AR_EOK;
   }

   struct
   {
      intf_extn_data_port_operation_t  op;
      intf_extn_data_port_id_idx_map_t id_index;
   } ops;

   ops.op.is_input_port                      = is_input;
   ops.op.opcode                             = opcode;
   ops.op.opcode_payload_buf.data_ptr        = NULL;
   ops.op.opcode_payload_buf.actual_data_len = 0;
   ops.op.opcode_payload_buf.max_data_len    = 0;
   ops.op.num_ports                          = 1;

   ops.op.id_idx->port_id                    = port_id;
   ops.op.id_idx->port_index                 = port_index;

   /* In Windows [64-bit] following method causing issues in accessing the port information inside
      module code [first observed in capi_rat]. This is due to the structure paddind because of the
      last Flexible Array Member [FAM] element inside 'intf_extn_data_port_operation_t' struct.
      So, it is recommended to use the port index variables using FAM.
      'id_index' element inside 'ops' struct is not used but it is still needed to satisfy the length
      check condition inside the module code.
   */
   //ops.id_index.port_id                      = port_id;
   //ops.id_index.port_index                   = port_index;

   capi_buf_t buf;
   buf.data_ptr        = (int8_t *)(&ops);
   buf.actual_data_len = sizeof(ops);
   buf.max_data_len    = sizeof(ops);

   capi_port_info_t port_info;
   port_info.is_valid      = TRUE;
   port_info.is_input_port = is_input;
   port_info.port_index    = port_index;

   err_code = module_ptr->capi_ptr->vtbl_ptr->set_param(module_ptr->capi_ptr,
                                                        INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION,
                                                        &port_info,
                                                        &buf);

   if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "setting port operation failed");
      return capi_err_to_ar_result(err_code);
   }
   else
   {
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "Data port op done with opcode=0x%lx on module iid= 0x%lx , port id= 0x%lx, result = 0x%lx",
               opcode,
               module_ptr->gu.module_instance_id,
               port_id,
               err_code);
#endif
   }

   // Update last issued opcode.
   *last_issued_opcode_ptr = opcode;

   return AR_EOK;
}

ar_result_t gen_topo_capi_set_data_port_op_from_sg_ops(gen_topo_module_t *           module_ptr,
                                                       uint32_t                      sg_ops,
                                                       intf_extn_data_port_opcode_t *last_issued_opcode_ptr,
                                                       bool_t                        is_input,
                                                       uint32_t                      port_index,
                                                       uint32_t                      port_id)
{
   ar_result_t                  result = AR_EOK;
   intf_extn_data_port_opcode_t opcode = INTF_EXTN_DATA_PORT_OP_INVALID;

   if (TOPO_SG_OP_STOP & sg_ops)
   {
      opcode = INTF_EXTN_DATA_PORT_STOP;
   }
   else if (TOPO_SG_OP_SUSPEND & sg_ops)
   {
      opcode = INTF_EXTN_DATA_PORT_SUSPEND;
   }
   else if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_DISCONNECT) & sg_ops)
   {
      opcode = INTF_EXTN_DATA_PORT_CLOSE;
   }

   if (opcode == INTF_EXTN_DATA_PORT_OP_INVALID)
   {
      return result;
   }

   return gen_topo_capi_set_data_port_op(module_ptr, opcode, last_issued_opcode_ptr, is_input, port_index, port_id);
}

ar_result_t gen_topo_capi_set_data_port_op_from_state(gen_topo_module_t *           module_ptr,
                                                      topo_port_state_t             downgraded_state,
                                                      intf_extn_data_port_opcode_t *last_issued_opcode_ptr,
                                                      bool_t                        is_input,
                                                      uint32_t                      port_index,
                                                      uint32_t                      port_id)
{
   ar_result_t result = AR_EOK;
   // convert the port state to Capi data port operation.
   intf_extn_data_port_opcode_t opcode = topo_port_state_to_data_port_opcode(downgraded_state);

   // Only START/STOP states are updated here.
   if (opcode == INTF_EXTN_DATA_PORT_OP_INVALID)
   {
      return result;
   }

   return gen_topo_capi_set_data_port_op(module_ptr, opcode, last_issued_opcode_ptr, is_input, port_index, port_id);
}
