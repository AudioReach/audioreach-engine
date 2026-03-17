/**
 * \file gen_topo_public_functions.c
 *
 * \brief
 *
 *     Implementation of topology interface functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo_i.h"
#include "gen_topo_capi.h"
#include "gen_topo_buf_mgr.h"

/**
 * See comment above gen_topo_capi_set_port_operation.
 */
static ar_result_t gen_topo_check_set_connected_port_operation(gen_topo_t                *topo_ptr,
                                                               gen_topo_module_t         *this_module_ptr,
                                                               gu_cmn_port_t             *conn_gu_cmn_port_ptr,
                                                               gen_topo_common_port_t    *conn_cmn_port_ptr,
                                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                                               bool_t                     is_input,
                                                               uint32_t                   sg_ops)
{
   ar_result_t        result               = AR_EOK;
   gen_topo_module_t *connected_module_ptr = (gen_topo_module_t *)conn_gu_cmn_port_ptr->module_ptr;
   uint32_t           index                = conn_gu_cmn_port_ptr->index;
   uint32_t           id                   = conn_gu_cmn_port_ptr->id;

   // this whole func is only called for CLOSE when other port's module is not getting closed..

   // other port operation such as STOP are done in state propagation

   if (connected_module_ptr->capi_ptr)
   {

      result = gen_topo_capi_set_data_port_op_from_sg_ops(connected_module_ptr,
                                                          sg_ops,
                                                          &conn_cmn_port_ptr->last_issued_opcode,
                                                          is_input,
                                                          index,
                                                          id);
   }

   // see comments at gen_cntr_operate_on_ext_in_port
   // this cannot be tested as APM prevents opening of links unless at least one side is stopped.
   //  which in turn means closing links is not possible unless at least one side is stopped & at data flow gap.
   if (is_input)
   {
      if (topo_ptr->topo_to_cntr_vtable_ptr->check_insert_missing_eos_on_next_module)
      {
         topo_ptr->topo_to_cntr_vtable_ptr
            ->check_insert_missing_eos_on_next_module(topo_ptr, (gen_topo_input_port_t *)conn_gu_cmn_port_ptr);
      }
   }

   return result;
}

static ar_result_t gen_topo_check_set_self_port_operation(gen_topo_t             *topo_ptr,
                                                          gu_cmn_port_t          *gu_cmn_port_ptr,
                                                          gen_topo_common_port_t *cmn_port_ptr,
                                                          bool_t                  is_input,
                                                          uint32_t                sg_ops)
{
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gu_cmn_port_ptr->module_ptr;

   if (module_ptr->capi_ptr)
   {
      result |= gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                           sg_ops,
                                                           &cmn_port_ptr->last_issued_opcode,
                                                           is_input,
                                                           gu_cmn_port_ptr->index,
                                                           gu_cmn_port_ptr->id);
   }
   else
   {
      // framework module
   }
   return result;
}

/* =======================================================================
Public Function Definitions
========================================================================== */
ar_result_t gen_topo_operate_on_int_in_port(void                      *topo_ptr,
                                            gu_input_port_t           *in_port_ptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                            uint32_t                   sg_ops,
                                            bool_t                     set_port_op)
{
   ar_result_t            result           = AR_EOK;
   gen_topo_t            *me_ptr           = (gen_topo_t *)topo_ptr;
   gen_topo_input_port_t *topo_in_port_ptr = (gen_topo_input_port_t *)in_port_ptr;

   // Only flush/reset/Stop are handled here.
   // Start is handled based on port's downgraded state later.
   if ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops)
   {
      gen_topo_reset_input_port(topo_ptr, topo_in_port_ptr);
   }

   if (topo_in_port_ptr->gu.conn_out_port_ptr)
   {
      // For intra-container SG disconnections, it's ok to handle only close, w/o handling DISCONNECT.
      // Note that APM sends disconnect and close separately & disconnect is pretty much ignored.
      // For ext ports, we mark spf_handle as null for connections to avoid sending ctrl msg to peers
      //    (peer may have already received close from APM and might have destroyed ptr)
      // In intra-cntr, if we remove conn at disconnect, then at close we cannnot find out connected peer.
      if ((TOPO_SG_OP_CLOSE)&sg_ops)
      {
         gen_topo_output_port_t *conn_out_port_ptr = (gen_topo_output_port_t *)topo_in_port_ptr->gu.conn_out_port_ptr;
         gen_topo_module_t      *connected_module_ptr = (gen_topo_module_t *)conn_out_port_ptr->gu.cmn.module_ptr;
         gen_topo_module_t      *this_module_ptr      = (gen_topo_module_t *)topo_in_port_ptr->gu.cmn.module_ptr;

         // Inform the connected module in the connected intra-container SG only if it's not in the spf_sg_list_ptr
         // coming from the cmd. The module itself need not be informed as it's getting closed anyway

         if ((connected_module_ptr->gu.sg_ptr->id != this_module_ptr->gu.sg_ptr->id) &&
             !gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, connected_module_ptr->gu.sg_ptr->id))
         {
#ifdef VERBOSE_DEBUGGING
            TOPO_MSG(me_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Closing connection from Module, port: (0x%lX, 0x%lx) -> (0x%lX, 0x%lx)",
                     connected_module_ptr->gu.module_instance_id,
                     conn_out_port_ptr->gu.cmn.id,
                     this_module_ptr->gu.module_instance_id,
                     topo_in_port_ptr->gu.cmn.id);
#endif
            // imagine A->B->C, where B is elementary. B->C connection is removed, then while handling C's input port,
            // connected output port is checked. Conn out port is A's output.

            // Why similar code is not in gen_topo_operate_on_int_out_port: in above example A & B are in same SG (rule
            // for attaching).
            // if A & B both go then gen_topo_destroy*port is called during module destroy for A.
            if (conn_out_port_ptr->gu.attached_module_ptr)
            {
               // don't destroy module A's info as it's not actually being disconnected. need to keep bufs, raw-fmt-info
               // and path-delay stuff
               // GU takes care of re-instating connection b/w A->B
               gu_detach_module(&me_ptr->gu, conn_out_port_ptr->gu.attached_module_ptr, me_ptr->heap_id);

               // now connected out port ptr will be the output port of module B.
               conn_out_port_ptr = (gen_topo_output_port_t *)topo_in_port_ptr->gu.conn_out_port_ptr;
            }

            result |=
               gen_topo_check_set_connected_port_operation(me_ptr,
                                                           (gen_topo_module_t *)topo_in_port_ptr->gu.cmn.module_ptr,
                                                           &conn_out_port_ptr->gu.cmn,
                                                           &conn_out_port_ptr->common,
                                                           spf_sg_list_ptr,
                                                           FALSE /*is_input*/,
                                                           sg_ops);
            // for intra-container SG close, connected peer is destroyed by GU (in gu_graph_destroy func).
            // any additional memory such as path-delay or bufs_ptr must be freed here.
            // For data-link-close this is not useful as both links get closed anyway.
            gen_topo_destroy_output_port(me_ptr, conn_out_port_ptr);

            // set ref to self as NULL for both cases a) other port is getting destroyed because other module's SG is
            // getting closed b) other port is getting destroyed, but not the module.
            topo_in_port_ptr->gu.conn_out_port_ptr->conn_in_port_ptr = NULL;
            topo_in_port_ptr->gu.conn_out_port_ptr                   = NULL;
         }

         gen_topo_destroy_input_port(me_ptr, topo_in_port_ptr);
      }
   }

   if (set_port_op)
   {
      bool_t IS_INPUT_TRUE = TRUE;
      result |= gen_topo_check_set_self_port_operation(me_ptr,
                                                       &topo_in_port_ptr->gu.cmn,
                                                       &topo_in_port_ptr->common,
                                                       IS_INPUT_TRUE,
                                                       sg_ops);
   }

   return result;
}

ar_result_t gen_topo_operate_on_int_out_port(void                      *topo_ptr,
                                             gu_output_port_t          *out_port_ptr,
                                             spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                             uint32_t                   sg_ops,
                                             bool_t                     set_port_op)
{
   ar_result_t             result            = AR_EOK;
   gen_topo_t             *me_ptr            = (gen_topo_t *)topo_ptr;
   gen_topo_output_port_t *topo_out_port_ptr = (gen_topo_output_port_t *)out_port_ptr;

   // Only flush/reset/Stop are handler here.
   // Start is handled based on port's downgraded state later.
   if ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops)
   {
      gen_topo_reset_output_port(topo_ptr, topo_out_port_ptr);
   }

   if (topo_out_port_ptr->gu.conn_in_port_ptr)
   {
      // see comments in gen_topo_operate_on_int_in_port
      if ((TOPO_SG_OP_CLOSE)&sg_ops)
      {
         gen_topo_module_t     *this_module_ptr      = (gen_topo_module_t *)topo_out_port_ptr->gu.cmn.module_ptr;
         gen_topo_input_port_t *conn_in_port_ptr     = (gen_topo_input_port_t *)topo_out_port_ptr->gu.conn_in_port_ptr;
         gen_topo_module_t     *connected_module_ptr = (gen_topo_module_t *)conn_in_port_ptr->gu.cmn.module_ptr;

         if ((connected_module_ptr->gu.sg_ptr->id != this_module_ptr->gu.sg_ptr->id) &&
             !gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, connected_module_ptr->gu.sg_ptr->id))
         {
#ifdef VERBOSE_DEBUGGING
            TOPO_MSG(me_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Closing connection from Module, port: (0x%lX, 0x%lx) -> (0x%lX, 0x%lx)",
                     this_module_ptr->gu.module_instance_id,
                     topo_out_port_ptr->gu.cmn.id,
                     connected_module_ptr->gu.module_instance_id,
                     conn_in_port_ptr->gu.cmn.id);
#endif
            result |=
               gen_topo_check_set_connected_port_operation(me_ptr,
                                                           (gen_topo_module_t *)topo_out_port_ptr->gu.cmn.module_ptr,
                                                           &conn_in_port_ptr->gu.cmn,
                                                           &conn_in_port_ptr->common,
                                                           spf_sg_list_ptr,
                                                           TRUE /* is_input*/,
                                                           sg_ops);

            gen_topo_destroy_input_port(me_ptr, conn_in_port_ptr);

            topo_out_port_ptr->gu.conn_in_port_ptr->conn_out_port_ptr = NULL;
            topo_out_port_ptr->gu.conn_in_port_ptr                    = NULL;
         }

         gen_topo_destroy_output_port(me_ptr, topo_out_port_ptr);
      }
   }

   if (set_port_op)
   {
      bool_t IS_INPUT_FALSE = FALSE;
      result |= gen_topo_check_set_self_port_operation(me_ptr,
                                                       &topo_out_port_ptr->gu.cmn,
                                                       &topo_out_port_ptr->common,
                                                       IS_INPUT_FALSE,
                                                       sg_ops);
   }

   return result;
}

ar_result_t gen_topo_operate_on_int_ctrl_port(void                      *topo_ptr,
                                              gu_ctrl_port_t            *ctrl_port_ptr,
                                              spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                              uint32_t                   sg_ops,
                                              bool_t                     set_port_op)
{
   ar_result_t           result             = AR_EOK;
   gen_topo_t           *me_ptr             = (gen_topo_t *)topo_ptr;
   gen_topo_ctrl_port_t *topo_ctrl_port_ptr = (gen_topo_ctrl_port_t *)ctrl_port_ptr;

   if (topo_ctrl_port_ptr->gu.peer_ctrl_port_ptr)
   {
      gen_topo_ctrl_port_t *peer_ctrl_port_ptr = (gen_topo_ctrl_port_t *)topo_ctrl_port_ptr->gu.peer_ctrl_port_ptr;

      // see comments in gen_topo_operate_on_int_in_port
      if ((TOPO_SG_OP_CLOSE)&sg_ops)
      {
         if ((peer_ctrl_port_ptr) &&
             ((peer_ctrl_port_ptr->gu.module_ptr->sg_ptr->id != topo_ctrl_port_ptr->gu.module_ptr->sg_ptr->id) &&
              (!gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, peer_ctrl_port_ptr->gu.module_ptr->sg_ptr->id))))
         {
#ifdef VERBOSE_DEBUGGING
            TOPO_MSG(me_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Closing control connection Module, port: (0x%lX, 0x%lx) <-> (0x%lX, 0x%lx)",
                     topo_ctrl_port_ptr->gu.module_ptr->module_instance_id,
                     topo_ctrl_port_ptr->gu.id,
                     peer_ctrl_port_ptr->gu.module_ptr->module_instance_id,
                     peer_ctrl_port_ptr->gu.id);
#endif
            // Set connected port operation handles CLOSE on connected ports.
            // INTF_EXTN_IMCL_PORT_CLOSE might never be called for the module at close
            result |=
               gen_topo_check_set_connected_ctrl_port_operation(me_ptr->gu.log_id,
                                                                (gen_topo_module_t *)topo_ctrl_port_ptr->gu.module_ptr,
                                                                peer_ctrl_port_ptr,
                                                                spf_sg_list_ptr,
                                                                sg_ops);
         }
         // don't invalidate the link, we need to ensure that the buf-q is destroyed in cu_deinit_internal_ctrl_ports
         // later link will be invalidated in gu_destroy_graph
         // topo_ctrl_port_ptr->gu.peer_ctrl_port_ptr->peer_ctrl_port_ptr = NULL;
         // topo_ctrl_port_ptr->gu.peer_ctrl_port_ptr                     = NULL;
      }
   }

   if (set_port_op)
   {
      result |= gen_topo_check_set_self_ctrl_port_operation(me_ptr->gu.log_id, topo_ctrl_port_ptr, sg_ops);
   }
   return result;
}

// Operate on modules only handles self SG CLOSE and disconnect.
// START, STOP, Prepare and Suspend are not handled here, they are handled based on downgraded state
// after state propagation is complete.
ar_result_t gen_topo_operate_on_modules(void                      *topo_ptr,
                                        uint32_t                   sg_ops,
                                        gu_module_list_t          *module_list_ptr,
                                        spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t result = AR_EOK;
   gen_topo_t *me_ptr = (gen_topo_t *)topo_ptr;
   SPF_MANAGE_CRITICAL_SECTION

   // if self-sg is stop/suspended then their downgraded state will also be same, so can apply to the ports directly.
   bool_t is_set_port_op = ((TOPO_SG_OP_SUSPEND | TOPO_SG_OP_STOP) & sg_ops) ? TRUE : FALSE;

   // Operate on external ports of modules in the subgraph.
   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         SPF_CRITICAL_SECTION_START(&me_ptr->gu);
         result |= gen_topo_operate_on_int_out_port(me_ptr,
                                                    out_port_list_ptr->op_port_ptr,
                                                    spf_sg_list_ptr,
                                                    sg_ops,
                                                    is_set_port_op);
         SPF_CRITICAL_SECTION_END(&me_ptr->gu);
      }

      // if internal metadata list has nodes, then destroy them before destroying any nodes in ports.
      //  this is to maintain order of metadata: output, internal list, input
      if ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops)
      {
         gen_topo_reset_module(me_ptr, module_ptr);
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         SPF_CRITICAL_SECTION_START(&me_ptr->gu);
         result |= gen_topo_operate_on_int_in_port(me_ptr,
                                                   in_port_list_ptr->ip_port_ptr,
                                                   spf_sg_list_ptr,
                                                   sg_ops,
                                                   is_set_port_op);
         SPF_CRITICAL_SECTION_END(&me_ptr->gu);
      }

      for (gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->gu.ctrl_port_list_ptr; (NULL != ctrl_port_list_ptr);
           LIST_ADVANCE(ctrl_port_list_ptr))
      {
         SPF_CRITICAL_SECTION_START(&me_ptr->gu);
         result |= gen_topo_operate_on_int_ctrl_port(me_ptr,
                                                     ctrl_port_list_ptr->ctrl_port_ptr,
                                                     spf_sg_list_ptr,
                                                     sg_ops,
                                                     FALSE);
         SPF_CRITICAL_SECTION_END(&me_ptr->gu);
      }
   }
   return result;
}

ar_result_t gen_topo_get_port_property(void                     *vtopo_ptr,
                                       topo_port_type_t          port_type,
                                       topo_port_property_type_t prop_type,
                                       void                     *port_ptr,
                                       uint32_t                 *val_ptr)
{
   gen_topo_t *topo_ptr = (gen_topo_t *)vtopo_ptr;
   ar_result_t result   = AR_EOK;

   /* Port is considered non-real time if it is stopped. */
   switch (prop_type)
   {
      /* upstream and downstream RT property depends on subgraph state.
       * If subgraph is stopped then NRT is propagated irrespective of actual port property.
       * We can not rely on port state because port state will be a down grade of self subgraph state and downstream
       * state.*/
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)port_ptr;
               gen_topo_module_t     *module_ptr  = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

               // Return RT=TRUE only if module's self SG is !(stopped || suspended)
               *val_ptr = ((in_port_ptr->common.flags.is_upstream_realtime) &&
                           !gen_topo_is_module_sg_stopped_or_suspended(module_ptr));
               break;
            }
            case TOPO_DATA_OUTPUT_PORT_TYPE:
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)port_ptr;
               gen_topo_module_t      *module_ptr   = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

               // Return RT=TRUE only if module's self SG is !(stopped || suspended)
               *val_ptr = ((out_port_ptr->common.flags.is_upstream_realtime) &&
                           !gen_topo_is_module_sg_stopped_or_suspended(module_ptr));
               break;
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Invalid port type %d for prop type %d",
                        port_type,
                        prop_type);
            }
         }
         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)port_ptr;
               gen_topo_module_t     *module_ptr  = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

               // Return RT=TRUE only if module's self SG is !(stopped || suspended)
               *val_ptr = ((in_port_ptr->common.flags.is_downstream_realtime) &&
                           !gen_topo_is_module_sg_stopped_or_suspended(module_ptr));
               break;
            }
            case TOPO_DATA_OUTPUT_PORT_TYPE:
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)port_ptr;
               gen_topo_module_t      *module_ptr   = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

               // Return RT=TRUE only if module's self SG is !(stopped || suspended)
               *val_ptr = ((out_port_ptr->common.flags.is_downstream_realtime) &&
                           !gen_topo_is_module_sg_stopped_or_suspended(module_ptr));
               break;
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Invalid port type %d for prop type %d",
                        port_type,
                        prop_type);
               break;
            }
         }
         break;
      }
      case PORT_PROPERTY_TOPO_STATE:
      {
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)port_ptr;
               *val_ptr                           = (uint32_t)in_port_ptr->common.state;
               break;
            }
            case TOPO_DATA_OUTPUT_PORT_TYPE:
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)port_ptr;
               *val_ptr                             = (uint32_t)out_port_ptr->common.state;
               break;
            }
            case TOPO_CONTROL_PORT_TYPE:
            {
               gen_topo_ctrl_port_t *ctrl_port_ptr = (gen_topo_ctrl_port_t *)port_ptr;
               *val_ptr                            = (uint32_t)ctrl_port_ptr->state;
               break;
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Invalid port type %d for prop_type %d",
                        port_type,
                        prop_type);
               break;
            }
         }
         break;
      }
      case PORT_PROPERTY_DATA_FLOW_STATE:
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)port_ptr;
               *val_ptr                           = (uint32_t)in_port_ptr->common.data_flow_state;
               break;
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Invalid port type %d for prop_type %d",
                        port_type,
                        prop_type);
               break;
            }
         }
         break;
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Invalid property type %d", prop_type);
         break;
      }
   }

   return result;
}

ar_result_t gen_topo_set_port_property(void                     *vtopo_ptr,
                                       topo_port_type_t          port_type,
                                       topo_port_property_type_t prop_type,
                                       void                     *port_ptr,
                                       uint32_t                  val)
{
   gen_topo_t *topo_ptr = (gen_topo_t *)vtopo_ptr;
   ar_result_t result   = AR_EOK;
   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            case TOPO_DATA_OUTPUT_PORT_TYPE:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Setting %lu, port type %d for prop type %d is not allowed.",
                        val,
                        port_type,
                        prop_type);
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Invalid port type %d for prop type %d",
                        port_type,
                        prop_type);
            }
         }
         break;
      }
      case PORT_PROPERTY_TOPO_STATE:
      {
         switch (port_type)
         {
            case TOPO_DATA_INPUT_PORT_TYPE:
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)port_ptr;
               in_port_ptr->common.state          = (topo_port_state_t)val;
               break;
            }
            case TOPO_DATA_OUTPUT_PORT_TYPE:
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)port_ptr;
               out_port_ptr->common.state           = (topo_port_state_t)val;
               break;
            }
            case TOPO_CONTROL_PORT_TYPE:
            {
               gen_topo_set_ctrl_port_state(port_ptr, (topo_port_state_t)val);
               break;
            }
            default:
            {
               TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Invalid port type %d", port_type);
            }
         }
         break;
      }
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Invalid property type %d", prop_type);
         break;
      }
   }

   return result;
}

ar_result_t gen_topo_set_param(void *vtopo_ptr, apm_module_param_data_t *param_ptr)
{
   gen_topo_t        *topo_ptr    = (gen_topo_t *)vtopo_ptr;
   ar_result_t        result      = AR_EOK;
   int8_t            *payload_ptr = (int8_t *)param_ptr + sizeof(apm_module_param_data_t);
   gen_topo_module_t *module_ptr  = (gen_topo_module_t *)gu_find_module(&topo_ptr->gu, param_ptr->module_instance_id);

   if (!module_ptr)
   {
      result = AR_EUNSUPPORTED;
   }
   else
   {
      result = gen_topo_capi_set_param(topo_ptr->gu.log_id,
                                       module_ptr->capi_ptr,
                                       param_ptr->param_id,
                                       payload_ptr,
                                       param_ptr->param_size);
   }
   return result;
}

ar_result_t gen_topo_set_input_port_media_format(gen_topo_t            *topo_ptr,
                                                 gen_topo_input_port_t *input_port_ptr,
                                                 topo_media_fmt_t      *media_fmt_ptr)
{
   ar_result_t      result = AR_EOK;
   topo_media_fmt_t tmp_mf = { 0 };
   // This copies ptr for raw & sets ext port media fmt buf ptr as NULL.
   tu_copy_media_fmt(&tmp_mf, media_fmt_ptr);

   result = tu_set_media_fmt(&topo_ptr->mf_utils, &input_port_ptr->common.media_fmt_ptr, &tmp_mf, topo_ptr->heap_id);
   if (AR_FAILED(result))
   {
      return result;
   }
   input_port_ptr->flags.media_fmt_received     = TRUE;
   input_port_ptr->common.flags.media_fmt_event = TRUE;

   input_port_ptr->common.flags.is_mf_valid = topo_is_valid_media_fmt(input_port_ptr->common.media_fmt_ptr);

   gen_topo_reset_pcm_unpacked_mask(&input_port_ptr->common);

   // if input media format is not PCM then disable the simple threshold propagation.
   if (input_port_ptr->common.flags.is_mf_valid &&
       !SPF_IS_PCM_DATA_FORMAT(input_port_ptr->common.media_fmt_ptr->data_format))
   {
      topo_ptr->flags.simple_threshold_propagation_enabled = FALSE;

      if (SPF_DEINTERLEAVED_RAW_COMPRESSED == input_port_ptr->common.media_fmt_ptr->data_format)
      {
         THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_unsupported_mf, TRUE);
      }
   }

   return AR_EOK;
}

static ar_result_t gen_topo_input_port_algo_reset_(gen_topo_module_t     *module_ptr,
                                                   gen_topo_input_port_t *ip_port_ptr,
                                                   uint32_t               log_id)
{
   if (module_ptr->capi_ptr && ip_port_ptr->common.flags.port_is_not_reset)
   {
      ip_port_ptr->common.flags.port_is_not_reset = FALSE;

      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "Module 0x%lX: reset input port 0x%lx",
               module_ptr->gu.module_instance_id,
               ip_port_ptr->gu.cmn.id);

      return gen_topo_capi_algorithmic_reset(log_id, module_ptr->capi_ptr, TRUE, TRUE, ip_port_ptr->gu.cmn.index);
   }
   return AR_EOK;
}

static ar_result_t gen_topo_output_port_algo_reset_(gen_topo_module_t      *module_ptr,
                                                    gen_topo_output_port_t *out_port_ptr,
                                                    uint32_t                log_id)
{
   if (module_ptr->capi_ptr && out_port_ptr->common.flags.port_is_not_reset)
   {
      out_port_ptr->common.flags.port_is_not_reset = FALSE;

      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "Module 0x%lX: reset output port 0x%lx",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id);

      return gen_topo_capi_algorithmic_reset(log_id, module_ptr->capi_ptr, TRUE, FALSE, out_port_ptr->gu.cmn.index);
   }
   return AR_EOK;
}

ar_result_t topo_shared_reset_input_port(void *topo_ptr, void *topo_in_port_ptr, bool_t use_bufmgr)
{
   gen_topo_t            *me_ptr      = (gen_topo_t *)topo_ptr;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)topo_in_port_ptr;
   gen_topo_module_t     *module_ptr  = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

   gen_topo_destroy_all_metadata(me_ptr->gu.log_id,
                                 (void *)module_ptr,
                                 &in_port_ptr->common.sdata.metadata_list_ptr,
                                 TRUE /*is_dropped*/);

   if (TOPO_DATA_FLOW_STATE_AT_GAP != in_port_ptr->common.data_flow_state)
   {
      topo_basic_reset_input_port(me_ptr, in_port_ptr, use_bufmgr);
   }

   gen_topo_input_port_algo_reset_(module_ptr, in_port_ptr, me_ptr->gu.log_id);

   return AR_EOK;
}

ar_result_t topo_shared_reset_output_port(void *topo_ptr, void *topo_out_port_ptr, bool_t use_bufmgr)
{
   gen_topo_t             *me_ptr       = (gen_topo_t *)topo_ptr;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)topo_out_port_ptr;
   gen_topo_module_t      *module_ptr   = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

   gen_topo_destroy_all_metadata(me_ptr->gu.log_id,
                                 (void *)module_ptr,
                                 &out_port_ptr->common.sdata.metadata_list_ptr,
                                 TRUE /*is_dropped*/);

   topo_basic_reset_output_port(me_ptr, out_port_ptr, use_bufmgr);

   gen_topo_output_port_algo_reset_(module_ptr, out_port_ptr, me_ptr->gu.log_id);

   return AR_EOK;
}

ar_result_t gen_topo_reset_all_in_ports(gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_reset_input_port(module_ptr->topo_ptr, (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr);
   }
   return result;
}

ar_result_t gen_topo_reset_all_out_ports(gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_reset_output_port(module_ptr->topo_ptr, (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr);
   }
   return result;
}

/**
 * useful when CAPI itself is replaced with another (such as in placeholder)
 */
void gen_topo_reset_input_port_capi_dependent_portion(gen_topo_t            *topo_ptr,
                                                      gen_topo_module_t     *module_ptr,
                                                      gen_topo_input_port_t *in_port_ptr)
{
   in_port_ptr->common.flags.port_has_threshold = FALSE;
   in_port_ptr->common.threshold_raised         = 0;
   in_port_ptr->flags.word                      = 0;
   in_port_ptr->bytes_from_prev_buf             = 0;
}

/**
 * useful when CAPI itself is replaced with another (such as in placeholder)
 */
void gen_topo_reset_output_port_capi_dependent_portion(gen_topo_t             *topo_ptr,
                                                       gen_topo_module_t      *module_ptr,
                                                       gen_topo_output_port_t *out_port_ptr)
{
   out_port_ptr->common.flags.port_has_threshold = FALSE;
   out_port_ptr->common.threshold_raised         = 0;
}

/**
 * useful when CAPI itself is replaced with another (such as in placeholder)
 */
void gen_topo_reset_module_capi_dependent_portion(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   module_ptr->flags.word           = 0;
   module_ptr->algo_delay           = 0;
   module_ptr->code_bw              = 0;
   module_ptr->data_bw              = 0;
   module_ptr->kpps                 = 0;
   module_ptr->kpps_scale_factor_q4 = UNITY_Q4;
   module_ptr->num_proc_loops       = 1;

   topo_ptr->module_count++;
   module_ptr->serial_num = topo_ptr->module_count; // let the replaced module after reset have new log-id

   gen_topo_reset_top_level_flags(topo_ptr);
}

void gen_topo_update_dm_enabled_flag(gen_topo_t *topo_ptr)
{
   topo_ptr->flags.is_dm_enabled = FALSE;

   // enable DM for container only if at atleast one DM module is enabled
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->flags.need_dm_extn && gen_topo_is_dm_enabled(module_ptr))
         {
            topo_ptr->flags.is_dm_enabled = TRUE;
            THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_dm_module, (topo_ptr->flags.is_dm_enabled ? TRUE : FALSE));
            return;
         }
      }
   }

   return;
}

void gen_topo_update_is_signal_triggered_active_flag(gen_topo_t *topo_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // Assuming only one STM module is present in the Container
         if (module_ptr->flags.need_stm_extn)
         {
            topo_ptr->flags.is_signal_triggered = TRUE;
            if (module_ptr->flags.is_signal_trigger_deactivated)
            {
               topo_ptr->flags.is_signal_triggered_active = FALSE;
            }
            else
            {
               // By default is_signal_trigger_deactivated = False,
               // so is_signal_triggered_active = TRUE for STM modules
               topo_ptr->flags.is_signal_triggered_active = TRUE;
            }
         }
      }
   }

   return;
}

void gen_topo_reset_top_level_flags(gen_topo_t *topo_ptr)
{
   topo_ptr->flags.is_signal_triggered    = FALSE;
   topo_ptr->flags.is_sync_module_present = FALSE;
   topo_ptr->flags.is_src_module_present  = FALSE;
   topo_ptr->flags.is_dm_enabled          = FALSE;
   topo_ptr->num_data_tpm                 = 0;

   bool_t is_any_module_has_active_data_trigger_policy = FALSE;
   bool_t to_update_tpm_count                          = FALSE;

   TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Resetting top level flags.");

   bool_t has_voice_sg                   = FALSE;
   bool_t need_soft_timer_extn            = FALSE;
   bool_t has_duty_cycling_module        = FALSE;
   bool_t has_signal_tgp_module          = FALSE;
   bool_t requires_module_looping        = FALSE;
   bool_t mod_type_unsupported_thin_topo = FALSE;
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      if (APM_SUB_GRAPH_SID_VOICE_CALL == topo_ptr->gu.sg_list_ptr->sg_ptr->sid)
      {
         has_voice_sg = TRUE;
      }

      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // reset the flags for all modules that didn't raise an event for data trigger in ST,
         // The need_data_trigger_in_st will be updated properly by propagation.
         // There could be multiple modules that raised data-trigger-in-st = ALLOW_DATA_TRIGGER_IN_ST_EVENT
         if (!gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr))
         {
            module_ptr->flags.need_data_trigger_in_st = BLOCKED_DATA_TRIGGER_IN_ST;
         }

         // if module has raised data trigger policy
         if (module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_DATA_TRIGGER)])
         {
            is_any_module_has_active_data_trigger_policy = TRUE;
         }

         if (module_ptr->flags.need_sync_extn)
         {
            topo_ptr->flags.is_sync_module_present = TRUE;
         }

         if (module_ptr->gu.flags.is_source)
         {
            topo_ptr->flags.is_src_module_present = TRUE;
         }

         // Assuming only one STM module is present in the Container
         if (module_ptr->flags.need_stm_extn)
         {
            topo_ptr->flags.is_signal_triggered = TRUE;
            if (module_ptr->flags.is_signal_trigger_deactivated)
            {
               topo_ptr->flags.is_signal_triggered_active = FALSE;
            }
            else
            {
               // By default is_signal_trigger_deactivated = False,
               // so is_signal_triggered_active = TRUE for STM modules
               topo_ptr->flags.is_signal_triggered_active = TRUE;
            }
         }

         // update dm enabled info
         if (module_ptr->flags.need_dm_extn && gen_topo_is_dm_enabled(module_ptr))
         {
            topo_ptr->flags.is_dm_enabled = TRUE;
         }

         // checks for thin topo
         if (module_ptr->flags.need_soft_timer_extn)
         {
            need_soft_timer_extn = TRUE;
         }

         if (module_ptr->flags.supports_module_allow_duty_cycling)
         {
            has_duty_cycling_module = TRUE;
         }

         if (module_ptr->num_proc_loops > 1)
         {
            requires_module_looping = TRUE;
         }

         if ((AMDB_MODULE_TYPE_DECODER == module_ptr->gu.module_type) ||
             (AMDB_MODULE_TYPE_ENCODER == module_ptr->gu.module_type) ||
             (AMDB_MODULE_TYPE_PACKETIZER == module_ptr->gu.module_type) ||
             (AMDB_MODULE_TYPE_DEPACKETIZER == module_ptr->gu.module_type) ||
             (AMDB_MODULE_TYPE_CONVERTER == module_ptr->gu.module_type) ||
             (MODULE_ID_WR_SHARED_MEM_EP == module_ptr->gu.module_id) ||
             (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->gu.module_id))
         {
            mod_type_unsupported_thin_topo = TRUE;
         }
      }
   }

   if (topo_ptr->flags.is_signal_triggered)
   {
      for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

            // if module has asked for data trigger in ST container
            if (gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr))
            {
               if (module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_DATA_TRIGGER)])
               {
                  // in ST Cntr, any trigger policy is allowed only when module specifically asks for it.
                  to_update_tpm_count = TRUE;
               }
               gen_topo_propagate_data_trigger_in_st_cntr_event(module_ptr);
            }

            if (gen_topo_is_module_signal_trigger_policy_active(module_ptr))
            {
               has_signal_tgp_module = TRUE;
            }
         }
      }
   }
   else
   {
      if (is_any_module_has_active_data_trigger_policy)
      {
         // in non ST Cntr, any trigger policy is enabled if any module has active data trigger policy.
         to_update_tpm_count = TRUE;
      }
   }

   if (to_update_tpm_count)
   {
      // need to be done after gen_topo_propagate_data_trigger_in_st_cntr_event
      gen_topo_update_data_tpm_count(topo_ptr);
   }

   // reset all all the thin topo related static property flags here
   if (topo_ptr->flags.is_signal_triggered)
   {
      // intializes data structures requires for tracking if cntr can switch to thin topo.
      // note: cntr is not switching to thin topo at this point. Cntr will switch to thin topo, only at the end of gen
      // topo process context if conditions are met.
      thin_topo_init_handle(topo_ptr);
      thin_topo_update_exit_flags(topo_ptr,
                                  has_voice_sg,
                                  need_soft_timer_extn,
                                  has_duty_cycling_module,
                                  has_signal_tgp_module,
                                  requires_module_looping,
                                  mod_type_unsupported_thin_topo);
   }
   else
   {
      // free thin topo structure if container becomes non-signal triggered
      thin_topo_reset_handle(topo_ptr, TRUE);
   }
}

/**
 * resets only container portion of the module
 */
ar_result_t gen_topo_reset_module(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   if (module_ptr->int_md_list_ptr)
   {
      gen_topo_destroy_all_metadata(topo_ptr->gu.log_id,
                                    (void *)module_ptr,
                                    &module_ptr->int_md_list_ptr,
                                    TRUE /*is_dropped*/);
   }
   module_ptr->pending_zeros_at_eos = 0;
   return result;
}

topo_sg_state_t gen_topo_get_sg_state(gu_sg_t *sg_ptr)
{
   return ((gen_topo_sg_t *)sg_ptr)->state;
}

void gen_topo_set_sg_state(gu_sg_t *sg_ptr, topo_sg_state_t state)
{
   ((gen_topo_sg_t *)sg_ptr)->state = state;
}
