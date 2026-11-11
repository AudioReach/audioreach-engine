/**
 * \file gen_topo_propagation.c
 * \brief
 *     This file contains topo common port property propagation functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "ipc_tx_rx_api.h"

#define PROPAGATION_RECURSE_MAX_DEPTH 50

/* =======================================================================
Local declerations
========================================================================== */
#define FORWARD_PROP 1
#define BACKWARD_PROP 0

typedef bool_t topo_propagation_direction;

#ifdef DEBUG_TOPO_PORT_PROP_TYPE

#define LOG_FRWD_PROP(log_id, src, dst, prop_type, prop_payload_ptr)                                                   \
   TOPO_MSG(log_id,                                                                                                    \
            DBG_LOW_PRIO,                                                                                              \
            SPF_LOG_PREFIX "LOG_FRWD_PROP: prop_type=0x%lx from (mod-inst-id, port-id) (0x%lX,0x%lx) to (0x%lX,0x%lx) "\
            "prop_value=%lu",                                                                                          \
            prop_type,                                                                                                 \
            !src ? 0 : (src)->gu.cmn.module_ptr->module_instance_id,                                                   \
            !src ? 0 : (src)->gu.cmn.id,                                                                               \
            !dst ? 0 : (dst)->gu.cmn.module_ptr->module_instance_id,                                                   \
            !dst ? 0 : (dst)->gu.cmn.id,                                                                               \
            !prop_payload_ptr ? -1 : *((uint32_t *)prop_payload_ptr))

#define LOG_BKWRD_PROP(log_id, src, dst, prop_type, prop_payload_ptr)                                                  \
   TOPO_MSG(log_id,                                                                                                    \
            DBG_LOW_PRIO,                                                                                              \
            SPF_LOG_PREFIX "LOG_BKWRD_PROP: prop_type=0x%lx from (mod-inst-id, port-id) (0x%lX,0x%lx) to (0x%lX,0x%lx) "\
            "prop_value=%lu",                                                                                          \
            prop_type,                                                                                                 \
            !src ? 0 : (src)->gu.cmn.module_ptr->module_instance_id,                                                   \
            !src ? 0 : (src)->gu.cmn.id,                                                                               \
            !dst ? 0 : (dst)->gu.cmn.module_ptr->module_instance_id,                                                   \
            !dst ? 0 : (dst)->gu.cmn.id,                                                                               \
            !prop_payload_ptr ? -1 : *((uint32_t *)prop_payload_ptr))

#endif // DEBUG_TOPO_PORT_PROP_TYPE

/* =======================================================================
Static Functions
========================================================================== */

static ar_result_t gen_topo_propagate_is_downstream_realtime(gen_topo_t * topo_ptr);
static ar_result_t gen_topo_propagate_is_upstream_realtime(gen_topo_t *topo_ptr);

/* =======================================================================
Public Functions
========================================================================== */

static bool_t is_downstream_state_propagation_allowed(gen_topo_output_port_t *out_port_ptr)
{
   /* propagation must not be done for FTRT or if module blocks the propagation.
    * Stopping FTRT graph can cause resets -> data drops. SPR also resets session time.
    */

   return ((out_port_ptr->common.flags.is_upstream_realtime) && (!out_port_ptr->common.flags.is_state_prop_blocked));
}

static ar_result_t gen_topo_set_propagated_state_on_output_port(gen_topo_output_port_t *out_port_ptr,
                                                                topo_port_state_t       propagated_port_state)
{
   ar_result_t result = AR_EOK;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   // If the current port state is prepared, not need to set the propagated state. (already checked)

   // Get current output port state, which is set based on sg state.
   topo_port_state_t out_port_state = out_port_ptr->common.state;

   // Get downgraded state between port sg state and propagated state.
   topo_port_state_t downgraded_state = tu_get_downgraded_state(out_port_state, propagated_port_state);

   // update the downgraded state even if it is same as out_port_state.
   if (TOPO_PORT_STATE_INVALID != downgraded_state)
   {
      out_port_ptr->common.state = downgraded_state;

      // set port state on the module implementing extension
      if (module_ptr->flags.supports_prop_port_ds_state)
      {
         intf_extn_prop_data_port_state_t capi_prop_state = INTF_EXTN_PROP_DATA_PORT_STATE_STOPPED;
         switch (propagated_port_state)
         {
            case TOPO_PORT_STATE_STOPPED:
            {
               capi_prop_state = INTF_EXTN_PROP_DATA_PORT_STATE_STOPPED;
               break;
            }
            case TOPO_PORT_STATE_SUSPENDED:
            {
               capi_prop_state = INTF_EXTN_PROP_DATA_PORT_STATE_SUSPENDED;
               break;
            }
            case TOPO_PORT_STATE_STARTED:
            {
               capi_prop_state = INTF_EXTN_PROP_DATA_PORT_STATE_STARTED;
               break;
            }
            case TOPO_PORT_STATE_PREPARED:
            {
               capi_prop_state = INTF_EXTN_PROP_DATA_PORT_STATE_PREPARED;
               break;
            }
            default:
            {
               result = AR_EFAILED;
               break;
            }
         }

         if (AR_SUCCEEDED(result))
         {
            intf_extn_param_id_port_ds_state_t p = { .output_port_index = out_port_ptr->gu.cmn.index,
                                                    .port_state        = capi_prop_state };

            result = gen_topo_capi_set_param(module_ptr->topo_ptr->gu.log_id,
                                             module_ptr->capi_ptr,
                                             INTF_EXTN_PARAM_ID_PORT_DS_STATE,
                                             (int8_t *)&p,
                                             sizeof(p));
            // with this set param, module would raise an event and in the event we set on input port.
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "topo_port_state_prop:set propagated port state on module 0x%x, out port 0x%x "
                        "failed",
                        out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        out_port_ptr->gu.cmn.id);
            }
         }
      }

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
      TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               SPF_LOG_PREFIX "topo_port_state_prop: set propagated port state on module 0x%x, port 0x%x,state 0x%x",
               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->gu.cmn.id,
               out_port_ptr->common.state);
#endif
   }

   return AR_EOK;
}

static ar_result_t gen_topo_set_propagated_state_on_input_port(gen_topo_input_port_t *in_port_ptr,
                                                               topo_port_state_t      propagated_port_state)
{
   // If the current port state is prepared, not need to set the propagated state.(already checked)

   // Get current input ports state from sg state.
   topo_port_state_t in_port_state = in_port_ptr->common.state;

   // Get down graded state between port sg state and propagated state.
   topo_port_state_t downgraded_state = tu_get_downgraded_state(in_port_state, propagated_port_state);

   // update the state if needed.
   if ((TOPO_PORT_STATE_INVALID != downgraded_state) && (downgraded_state != in_port_state))
   {
      // no state propagation from US to DS and hence no need to set port state to the module from input port.

      in_port_ptr->common.state = downgraded_state;

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
      TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               SPF_LOG_PREFIX "topo_port_state_prop: set propagated port state on module 0x%x, port 0x%x,state 0x%x",
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id,
               in_port_ptr->common.state);
#endif
   }

   return AR_EOK;
}

/**
 * for input: is_rt = is_upstream_rt
 * for output: is_rt = is_downstream_rt
 */
static ar_result_t gen_topo_set_property_on_capi_port(gen_topo_module_t *module_ptr,
                                                      bool_t             is_input,
                                                      uint32_t           port_index,
                                                      bool_t             is_rt)
{
   ar_result_t result = AR_EOK;
   if (module_ptr->flags.supports_prop_is_rt_port_prop)
   {
      intf_extn_param_id_is_rt_port_property_t p = {.is_input = is_input, .port_index = port_index, .is_rt = is_rt };

      result = gen_topo_capi_set_param(module_ptr->topo_ptr->gu.log_id,
                                       module_ptr->capi_ptr,
                                       INTF_EXTN_PARAM_ID_IS_RT_PORT_PROPERTY,
                                       (int8_t *)&p,
                                       sizeof(p));

      if (AR_DID_FAIL(result))
      {
         TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "topo_port_rt_prop:set real time flag on module 0x%x, is_input %u, port-index 0x%x failed",
                  module_ptr->gu.module_instance_id,
                  is_input,
                  port_index);
      }
   }
   return result;
}

//propagate the values from output port to the attached module.
static void gen_topo_set_propagated_property_on_attached_module(gen_topo_output_port_t *  host_port_ptr,
                                                                 topo_port_property_type_t prop_type)
{
   // return if there is no attached module.
   if (!host_port_ptr->gu.attached_module_ptr)
   {
      return;
   }

   gen_topo_module_t *    module_ptr = (gen_topo_module_t *)host_port_ptr->gu.attached_module_ptr;
   gen_topo_input_port_t *in_port_ptr =
      (gen_topo_input_port_t *)((module_ptr->gu.input_port_list_ptr) ? module_ptr->gu.input_port_list_ptr->ip_port_ptr
                                                                     : NULL);
   gen_topo_output_port_t *out_port_ptr =
      (gen_topo_output_port_t *)((module_ptr->gu.output_port_list_ptr)
                                    ? module_ptr->gu.output_port_list_ptr->op_port_ptr
                                    : NULL);
   // parse the payload pointer based on the property ID and apply it on the port.
   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         if (in_port_ptr)
         {
            in_port_ptr->common.flags.is_upstream_realtime = host_port_ptr->common.flags.is_upstream_realtime;
         }

         if (out_port_ptr)
         {
            out_port_ptr->common.flags.is_upstream_realtime = host_port_ptr->common.flags.is_upstream_realtime;
         }
         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         if (in_port_ptr)
         {
            in_port_ptr->common.flags.is_downstream_realtime = host_port_ptr->common.flags.is_downstream_realtime;
         }

         if (out_port_ptr)
         {
            out_port_ptr->common.flags.is_downstream_realtime = host_port_ptr->common.flags.is_downstream_realtime;
         }
         break;
      }
      case PORT_PROPERTY_TOPO_STATE:
      {

         if (in_port_ptr)
         {
            in_port_ptr->common.state = host_port_ptr->common.state;
         }

         if (out_port_ptr)
         {
            out_port_ptr->common.state = host_port_ptr->common.state;
         }
         break;
      }
      case PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING:
      {

         if (in_port_ptr)
         {
            in_port_ptr->common.flags.downstream_req_data_buffering =
               host_port_ptr->common.flags.downstream_req_data_buffering;
         }

         if (out_port_ptr)
         {
            out_port_ptr->common.flags.downstream_req_data_buffering =
               host_port_ptr->common.flags.downstream_req_data_buffering;
         }
         break;
      }
      default:
      {
         break;
      }
   }

   return;
}
/**
 * Set propagated state on input port, updates propagated value if necessary and checks if propagation can
 * be continued. Handles following properties,
 *
 * PORT_PROPERTY_IS_UPSTREAM_RT
 * 1. This property is propagated only from upstream to downstream i.e only in forward propagation.
 * 2. Irrespective of the module supports/doesn't support rt propagation, propagated value is set on
 *    the input port if there is any change and continue flag is set to TRUE. If there is no change
 *    in propagated vs current port value then there is no need to set the property and propagation
 *    terminates.
 *
 * PORT_PROPERTY_IS_DOWNSTREAM_RT
 * 1. This property is propagated from downstream to upstream i.e only backward propagation.
 * 2. If module supports rt propagation extension, framework will not propagate to inputs during
 *    backward propagation. Module must raise event to propagate to inputs.
 * 3. If module supports rt propagation extension, propagation continues only if the module raised
 *    any rt change event, else propagation terminates. If the module raised event, then the latest
 *    input port property is updated to the propagated value and propagation continues.
 * 4. If the module doesn't support propagation, propagated value is set on input only if there
 *    is a change. If there is no change, propagation terminates.
 *
 * PORT_PROPERTY_TOPO_STATE
 * 1. This property is propagated from downstream to upstream i.e only backward propagation. Also port
 *    state is propagated only if the upstream is realtime.
 * 2. If module supports state propagation extension, framework will not propagate to inputs during
 *    backward propagation. Module must raise event to propagate to inputs.
 * 3. If module supports state propagation extension, propagation continues only if the module raised
 *    any state change event on the input, else propagation terminates. If the module raised event, then
 *    the latest input port property is updated to the propagated value and propagation continues.
 * 4. If the module doesn't support propagation, propagated value is set on input only if there
 *    is a change. If there is no change, propagation terminates.
 */
ar_result_t gen_topo_set_get_propagated_property_on_the_input_port(gen_topo_t               *topo_ptr,
                                                                   gen_topo_input_port_t    *in_port_ptr,
                                                                   topo_port_property_type_t prop_type,
                                                                   void                     *propagated_payload_ptr,
                                                                   bool_t                   *continue_propagation_ptr)
{
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
   // parse the payload pointer based on the property ID and apply it on the port.
   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         uint32_t *is_rt_ptr = (uint32_t *)propagated_payload_ptr;
         bool_t    is_rt     = (*is_rt_ptr > 0) ? TRUE : FALSE;
         // set only if there is a change.
         if (in_port_ptr->common.flags.is_upstream_realtime != is_rt)
         {
            in_port_ptr->common.flags.is_upstream_realtime = is_rt;
            *continue_propagation_ptr                      = TRUE;

            gen_topo_set_property_on_capi_port(module_ptr, TRUE /*is_input*/, in_port_ptr->gu.cmn.index, is_rt);
         }
         else // terminate propagation if nothing changed.
         {
            *continue_propagation_ptr = FALSE;
         }

         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         // if supports_prop_is_rt_port_prop, then input is already set with is_rt thru capi event
         uint32_t *is_ds_rt_ptr = (uint32_t *)propagated_payload_ptr;
         if (!module_ptr->flags.supports_prop_is_rt_port_prop)
         {
            bool_t is_rt = (*is_ds_rt_ptr > 0) ? TRUE : FALSE;
            if (in_port_ptr->common.flags.is_downstream_realtime != is_rt)
            {
               in_port_ptr->common.flags.is_downstream_realtime = is_rt;
               *continue_propagation_ptr                        = TRUE;
            }
            else // terminate propagation if nothing changed.
            {
               *continue_propagation_ptr = FALSE;
            }
         }
         else
         {
            *continue_propagation_ptr = FALSE;

            // update propagated value from the input port property value
            *is_ds_rt_ptr = in_port_ptr->common.flags.is_downstream_realtime;
            if (in_port_ptr->common.flags.port_prop_is_rt_change)
            {
               // Continue propagation if module had raised an event. else terminate.
               *continue_propagation_ptr = TRUE;

               // clear input port rt changed flag
               in_port_ptr->common.flags.port_prop_is_rt_change = FALSE;
            }
         }

         // Override the propagated value in the following cases even if module implements RT/NRT propagation.
         //  1. If signal triggered cntr && ext input, then overwrite to based on is data/signal triggered port.
         //  2. If the input is at internal sg boundary, and self SG is stopped then propagate NRT backwards.
         //     eg: if RT graph is stopped upstream needs to be informed that the graph is NRT,
         if (in_port_ptr->gu.ext_in_port_ptr)
         {
            if (topo_ptr->flags.is_signal_triggered)
            {
               if ((*is_ds_rt_ptr == FALSE) && !gen_topo_is_module_need_data_trigger_in_st_cntr(module_ptr))
               {
                  *is_ds_rt_ptr                                    = TRUE;
                  in_port_ptr->common.flags.is_downstream_realtime = *is_ds_rt_ptr;
                  *continue_propagation_ptr                        = TRUE;

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "LOG_BKWRD_PROP: Overriding is_downstream_realtime=%lu at ext input of module-id,port_id "
                           "(0x%lX,0x%lx).",
                           *is_ds_rt_ptr,
                           in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           in_port_ptr->gu.cmn.id);
#endif
               }
            }
         }
         else if (gen_topo_is_input_port_at_sg_boundary(in_port_ptr))
         {
            // if sg boundary and self sg is stopped/suspended then propagate non-real time to the peer.
            *is_ds_rt_ptr = gen_topo_is_module_sg_stopped_or_suspended(module_ptr) ? FALSE : *is_ds_rt_ptr;

            // note that we should not update the is_downstream_realtime for the port itself
            // once the SG is started, actually value needs to be propagated

            // if upstream sg is not started then can terminate the propagation here.
            *continue_propagation_ptr = gen_topo_is_module_sg_stopped_or_suspended(
                                           (gen_topo_module_t *)in_port_ptr->gu.conn_out_port_ptr->cmn.module_ptr)
                                           ? FALSE
                                           : TRUE;

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "LOG_BKwRD_PROP: Overriding propagated is_downstream_realtime=%lu at SG boundary of "
                     "module-id,port_id (0x%lX,0x%lx).",
                     *is_ds_rt_ptr,
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id);
#endif
         }

         break;
      }
      case PORT_PROPERTY_TOPO_STATE:
      {
         topo_port_state_t *propagated_state = (topo_port_state_t *)propagated_payload_ptr;

         // Propagates only in RT path.
         if (!in_port_ptr->common.flags.is_upstream_realtime)
         {
            *continue_propagation_ptr = FALSE;
         }
         else if (module_ptr->flags.supports_prop_port_ds_state) /* upstream is RT && supports extn*/
         {
            // state is already set in the event context by calling gen_topo_set_propagated_state_on_input_port.
            // propagated_state doesn't change based on what module raises. propagated_state is decided by the
            // boundary port.

            // don't propagate unless module raised an event
            *continue_propagation_ptr = FALSE;
            if (in_port_ptr->common.flags.port_prop_state_change)
            {
               *continue_propagation_ptr = TRUE;

               // update the propagted state based on modules input port only if module raised an event.
               *propagated_state = in_port_ptr->common.state;
            }
         }
         else /* upstream is RT && doesn't supports extn */
         {
            // Set input port state and return.
            gen_topo_set_propagated_state_on_input_port(in_port_ptr, *propagated_state);
            *continue_propagation_ptr = TRUE;
         }

         // modules raise state propagation event only on the input port as port-state propagates backwards only
         // (DS to US). US->DS is data flow state.
         in_port_ptr->common.flags.port_prop_state_change = FALSE;
         break;
      }
      case PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING:
      {
         uint32_t *propagated_value                              = (uint32_t *)propagated_payload_ptr;
         in_port_ptr->common.flags.downstream_req_data_buffering = (*propagated_value > 0);

         *continue_propagation_ptr = TRUE;
         break;
      }
      default:
      {
         *continue_propagation_ptr = FALSE;
         return AR_EFAILED;
      }
   }

   return result;
}

/**
 * Set propagated state on output port, updates propagated value if necessary and checks if propagation can
 * be continued. Handles following properties,
 *
 * PORT_PROPERTY_IS_UPSTREAM_RT
 * 1. This property is propagated from upstream to downstream i.e only in forward propagation.
 * 2. If module supports rt propagation extension, framework will not set property on the outputs.
 *    Module must raise an event to propagate to the outputs.
 * 3. If module supports rt propagation extension, propagation continues only if the module raised
 *    any rt change event, else propagation terminates. If the module raised event, then the latest
 *    output port property is updated to the propagated value and propagation continues.
 * 4. If the module doesn't support propagation, propagated value is set on output's only if there
 *    is a change. If there is no change, propagation terminates.
 *
 * PORT_PROPERTY_IS_DOWNSTREAM_RT *
 * 1. This property is propagated only from downstream to upstream i.e only in backward propagation.
 * 2. Irrespective of the module supports/doesn't support propagation extn, propagated value is set on
 *    the output port if there is any change and continue flag is set to TRUE. If there is no change
 *    in propagated vs current port value then the propagation terminates.
 *
 * PORT_PROPERTY_TOPO_STATE
 * 1. This property is propagated from downstream to upstream i.e only backward propagation. Also port
 *    state is propagated only if the upstream is realtime.
 * 2. Irrespective of the module supports/doesn't support propagation extn, propagated value is set on
 *    the output port if there is any change and continue flag is set to TRUE. If there is no change
 *    in propagated vs current port value then the propagation terminates.
 */
ar_result_t gen_topo_set_get_propagated_property_on_the_output_port(gen_topo_t               *topo_ptr,
                                                                    gen_topo_output_port_t   *out_port_ptr,
                                                                    topo_port_property_type_t prop_type,
                                                                    void                     *payload_ptr,
                                                                    bool_t                   *continue_propagation_ptr)
{
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   // parse the payload pointer based on the property ID and apply it on the port.
   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         // if supports_prop_is_rt_port_prop, then out port is already set with is_rt thru capi event
         uint32_t *is_us_rt_ptr = (uint32_t *)payload_ptr;
         if (!module_ptr->flags.supports_prop_is_rt_port_prop)
         {
            // Set the propagated value on the port
            if (out_port_ptr->common.flags.is_upstream_realtime != *is_us_rt_ptr)
            {
               out_port_ptr->common.flags.is_upstream_realtime = *is_us_rt_ptr;
               *continue_propagation_ptr                       = TRUE;
            }
            else
            {
               *continue_propagation_ptr = FALSE;
            }
         }
         else
         {
            *continue_propagation_ptr = FALSE;

            // update propagated value from the output port property value
            *is_us_rt_ptr = out_port_ptr->common.flags.is_upstream_realtime;

            if (out_port_ptr->common.flags.port_prop_is_rt_change)
            {
               // Continue propagation if module had raised an event. else terminate.
               *continue_propagation_ptr = TRUE;

               // clear the event flag.
               out_port_ptr->common.flags.port_prop_is_rt_change = FALSE;
            }
         }

         // Override the propagated value in the following cases even if module implements RT/NRT propagation.
         //  1. If signal triggered cntr && ext output && not data triggered port, then overwrite to RT
         //  2. If the output is at internal sg boundary, and self SG is stopped then propagate NRT.
         if (out_port_ptr->gu.ext_out_port_ptr)
         {
            if (topo_ptr->flags.is_signal_triggered)
            {
               // can override at the contianer boundary only if the module doesn't need data trigger in ST.
               if ((*is_us_rt_ptr == FALSE) && !gen_topo_is_module_need_data_trigger_in_st_cntr(module_ptr))
               {
                  *is_us_rt_ptr                                   = TRUE;
                  out_port_ptr->common.flags.is_upstream_realtime = *is_us_rt_ptr;
                  *continue_propagation_ptr                       = TRUE;

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "LOG_FWRD_PROP: Overriding is_upstream_realtime=%lu at external output module-id,port_id "
                           "(0x%lX,0x%lx).",
                           *is_us_rt_ptr,
                           out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                           out_port_ptr->gu.cmn.id);
#endif
               }
            }
         }
         else if (gen_topo_is_output_port_at_sg_boundary(out_port_ptr))
         {
            // if we hit a sg boundary and self sg is stopped/suspended then propagate non-real time to the peer.
            *is_us_rt_ptr = gen_topo_is_module_sg_stopped_or_suspended(module_ptr) ? FALSE : *is_us_rt_ptr;

            // note that we should not update the is_upstream_realtime for the port itself
            // once the SG is started, actual value need to be propagated

            // if downstream sg is not started then can terminate the propagation here.
            *continue_propagation_ptr = gen_topo_is_module_sg_stopped_or_suspended(
                                           (gen_topo_module_t *)out_port_ptr->gu.conn_in_port_ptr->cmn.module_ptr)
                                           ? FALSE
                                           : TRUE;

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "LOG_FWRD_PROP: Overriding propagated is_upstream_realtime=%lu at SG boundary of "
                     "module-id,port_id (0x%lX,0x%lx).",
                     *is_us_rt_ptr,
                     out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     out_port_ptr->gu.cmn.id);
#endif
         }

         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;

         bool_t is_rt = (*is_rt_ptr > 0) ? TRUE : FALSE;
         if (out_port_ptr->common.flags.is_downstream_realtime != is_rt)
         {
            out_port_ptr->common.flags.is_downstream_realtime = is_rt;
            *continue_propagation_ptr                         = TRUE;

            gen_topo_set_property_on_capi_port(module_ptr, FALSE /*is_input*/, out_port_ptr->gu.cmn.index, is_rt);
         }
         else // terminate propagation
         {
            *continue_propagation_ptr = FALSE;
         }

         break;
      }
      case PORT_PROPERTY_TOPO_STATE:
      {
         if (gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
         {
            // break the propagation within stopped/suspended SGs
            *continue_propagation_ptr = FALSE;
            break;
         }

         topo_port_state_t *propagated_state = (topo_port_state_t *)payload_ptr;

         if (!is_downstream_state_propagation_allowed(out_port_ptr))
         {
            *continue_propagation_ptr = FALSE;
            return result;
         }

         // Set output port state and return.
         gen_topo_set_propagated_state_on_output_port(out_port_ptr, *propagated_state);

         *continue_propagation_ptr = TRUE;
         break;
      }
      case PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING:
      {
         uint32_t *propagated_value = (uint32_t *)payload_ptr;
         out_port_ptr->common.flags.downstream_req_data_buffering |= (*propagated_value > 0);

         *continue_propagation_ptr = TRUE;
         break;
      }
      default:
      {
         *continue_propagation_ptr = FALSE;
         return AR_EFAILED;
      }
   }

   //propagate property to the attached module
   if (*continue_propagation_ptr)
   {
      gen_topo_set_propagated_property_on_attached_module(out_port_ptr, prop_type);
   }

   return result;
}

/**
 * Finds if the a given property value can be propagated across module.
 *
 * 1. Note that, the function can update the propagated value if the module MIMO and it doesn't
 * support propagation extension. If the module doesn't support extension the propagated value across
 * the module is updated based on default policy. Current defaul policy for each property is,
 *
 * PORT_PROPERTY_IS_UPSTREAM_RT -
 *   This property is propagated only in forward direction. So if atleast one input is realtime
 * then all the output port's  upstream is marked as realtime, in this case propagated value can be
 * changed by this function.
 *
 * PORT_PROPERTY_IS_DOWNSTREAM_RT -
 *    This property is propagated only in backward direction. So if atleast one output is realtime
 * then all the input port's  are treated as downstream realtime, in this case propagated value can be
 * changed by this function.
 *
 * PORT_PROPERTY_TOPO_STATE -
 *    This property is propagated only in backward direction. So if atleast one output is started
 * then all the input port's are propagated as output port is started, in this case propagated value can be
 * changed by this function.
 *
 * 2. Propagated value can be changed only if the propagation is possible across the module.
 *
*/
static bool_t gen_topo_check_and_propagate_across_module(gen_topo_module_t *        module_ptr,
                                                         topo_port_property_type_t  prop_type,
                                                         topo_propagation_direction direction,
                                                         void *                     prop_payload_ptr)
{
   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         // upstream Rt is propagated only in forward direction.
         if (direction != FORWARD_PROP)
         {
            return FALSE;
         }

         // Can propagate across module which supports rt port propagation extension.
         if (module_ptr->flags.supports_prop_is_rt_port_prop)
         {
            return TRUE;
         }

         // cannot propagate across modules with buffering extn or STM modules.
         if (module_ptr->flags.need_stm_extn || module_ptr->flags.need_mp_buf_extn)
         {
            return FALSE;
         }

         if ((module_ptr->gu.num_output_ports == 0) && (module_ptr->gu.num_ipc_output_ports == 0))
         {
            // Cannot propagate further across sink modules.
            return FALSE;
         }
         else // if multi input module propagate based on default policy
         {
            /** If the module is MIMO and doesn't support RT propagation extension,
             *  then framework can propagate property based on default policy i.e,
             *    1. For forward propagation of upstream RT, if one of the inputs is Realtime then
             *       we can mark upstream realtime for all the output ports of the module.
             */

            uint32_t *is_upstream_rt_ptr = (uint32_t *)prop_payload_ptr;

            // Iterate through all inputs and check if atleast one input has upstream realtime.
            bool_t atleast_one_inputs_upstream_is_rt = FALSE;

            gu_input_port_list_t *in_port_lists[] = { module_ptr->gu.input_port_list_ptr,
                                                      module_ptr->gu.ipc_input_port_list_ptr };
            for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
            {
               for (gu_input_port_list_t *in_port_list_ptr = in_port_lists[i]; (NULL != in_port_list_ptr);
                    LIST_ADVANCE(in_port_list_ptr))
               {
                  gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

                  if (TRUE == in_port_ptr->common.flags.is_upstream_realtime)
                  {
                     atleast_one_inputs_upstream_is_rt = TRUE;
                     break;
                  }
               }
            }

            // Set propagate value of is_upstream_rt across MIMO module to TRUE.
            if (atleast_one_inputs_upstream_is_rt)
            {
               // overwrite payload to TRUE.
               *is_upstream_rt_ptr = TRUE;
            }
            else
            {
               // overwrite payload to FALSE.
               *is_upstream_rt_ptr = FALSE;
            }
            return TRUE;
         }

         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         // upstream Rt is propagated only in backward direction.
         if (direction != BACKWARD_PROP)
         {
            return FALSE;
         }

         if (module_ptr->flags.supports_prop_is_rt_port_prop)
         {
            return TRUE;
         }

         // cannot propagate across modules with buffering extn or STM modules.
         if (module_ptr->flags.need_mp_buf_extn || module_ptr->flags.need_stm_extn)
         {
            return FALSE;
         }

         if ((module_ptr->gu.num_input_ports == 0) && (module_ptr->gu.num_ipc_input_ports == 0 ))
         {
            /* Cannot propagate backwards across a source module */
            return FALSE;
         }
         else // inputs >= 1 && output > 1
         {
            /** If the module is MIMO and doesn't support RT propagation extension,
             *  then framework can propagate property based on default policy,
             *    1. For backward propagation of downstream RT, if one of the outputs downstream
             *       is Realtime then downstream of all the inputs of module can be marked as realtime.
             */

            uint32_t *is_downstream_rt_ptr = (uint32_t *)prop_payload_ptr;

            // Iterate through all outputs and check if atleast one outputs downstream is realtime.
            bool_t                 atleast_one_outputs_downstream_is_rt = FALSE;
            gu_output_port_list_t *out_port_lists[]                     = { module_ptr->gu.output_port_list_ptr,
                                                                            module_ptr->gu.ipc_output_port_list_ptr };
            for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
            {
               for (gu_output_port_list_t *list_ptr = out_port_lists[i]; list_ptr != NULL; LIST_ADVANCE(list_ptr))
               {
                  gen_topo_output_port_t *op_port_ptr = (gen_topo_output_port_t *)list_ptr->op_port_ptr;

                  if (TRUE == op_port_ptr->common.flags.is_downstream_realtime)
                  {
                     atleast_one_outputs_downstream_is_rt = TRUE;
                     break;
                  }
               }
            }

            // Set propagate value of is_downstream_rt across MIMO module to TRUE.
            if (atleast_one_outputs_downstream_is_rt)
            {
               // overwrite payload to TRUE.
               *is_downstream_rt_ptr = TRUE;
            }
            else
            {
               // overwrite payload to FALSE.
               *is_downstream_rt_ptr = FALSE;
            }
            return TRUE;
         }

         break;
      }
      case PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING:
      {
         // requires data buffering is propapgated only in the backward direction
         if (direction != BACKWARD_PROP)
         {
            return FALSE;
         }

         // terminate propagation if the module is not inplace
         if (!module_ptr->flags.inplace)
         {
            return FALSE;
         }

         return TRUE;

         break;
      }
      default:
      {
         return FALSE;
      }
   }

   return FALSE;
}

/**
 * Recursive state propagation (backwards in graph shape).
 *
 * 1. Set the propagated property on the input port of the module.
 * 2. Check if the propagation is possible across the next module.
 * 3. If propagation is possible, set the property on the next modules output ports.
 * 4. And propagate further from next modules output port to next_next module's input.
 */
ar_result_t gen_topo_propagate_port_property_backwards(void *                    vtopo_ptr,
                                                       void *                    vout_port_ptr,
                                                       topo_port_property_type_t prop_type,
                                                       uint32_t                  propagated_value,
                                                       uint32_t *                recurse_depth_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_t *            topo_ptr     = (gen_topo_t *)vtopo_ptr;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)vout_port_ptr;

   if (!out_port_ptr || !vtopo_ptr)
   {
      return AR_EOK;
   }
   RECURSION_ERROR_CHECK_ON_FN_ENTRY(topo_ptr->gu.log_id, recurse_depth_ptr, PROPAGATION_RECURSE_MAX_DEPTH);

   // Set output ports state.
   bool_t can_propagate_further = TRUE;
   gen_topo_set_get_propagated_property_on_the_output_port(topo_ptr,
                                                           out_port_ptr,
                                                           prop_type,
                                                           &propagated_value,
                                                           &can_propagate_further);

   if (!can_propagate_further)
   {
#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "LOG_BKWRD_PROP: Stop backward propagation of prop_type=0x%x at module-id,port_id (0x%lX,0x%lx).",
               prop_type,
               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->gu.cmn.id);
#endif
      return AR_EOK;
   }

   gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

   // Propagate across next module
   if (!gen_topo_check_and_propagate_across_module(prev_module_ptr, prop_type, BACKWARD_PROP, &propagated_value))
   {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "LOG_BKWRD_PROP: cannot propagate prop_type=0x%lx further from (module-id, out-port-id)= "
               "(0x%lX, %lu)",
               prop_type,
               prev_module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id);
#endif
      RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
      return result;
   }

   // Iterate through all input port of the previous modules and propagate.

   gu_input_port_list_t *in_port_lists[] = { prev_module_ptr->gu.input_port_list_ptr,
                                             prev_module_ptr->gu.ipc_input_port_list_ptr };
   for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
   {
      for (gu_input_port_list_t *in_port_list_ptr = in_port_lists[i]; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         uint32_t bkwrd_prop_payload = propagated_value;

         // Set property on input port.
         bool_t can_propagate_further = TRUE;
         gen_topo_set_get_propagated_property_on_the_input_port(topo_ptr,
                                                                in_port_ptr,
                                                                prop_type,
                                                                &bkwrd_prop_payload,
                                                                &can_propagate_further);

         if (!can_propagate_further)
         {
#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "LOG_BKWRD_PROP: Stop backward propagation of prop_type=0x%x at module-id,port_id (0x%lX,0x%lx) ",
                     prop_type,
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id);
#endif
            continue;
         }

         // If ext input is hit, propagation terminates and handle port state change on ext input port
         // through call back.
         if (in_port_ptr->gu.ext_in_port_ptr)
         {
            if (topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_input)
            {
               // Send propagated property state on external port.
               topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_input(topo_ptr,
                                                                                   in_port_ptr->gu.ext_in_port_ptr,
                                                                                   prop_type,
                                                                                   &bkwrd_prop_payload);
            }
         }
         else // If a connected output port exists, propagate further backwards.
         {
            gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

#ifdef DEBUG_TOPO_PORT_PROP_TYPE
         LOG_BKWRD_PROP(topo_ptr->gu.log_id,
                        in_port_ptr,
                        (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr,
                        prop_type,
                        &bkwrd_prop_payload);
#endif

         gen_topo_propagate_port_property_backwards(vtopo_ptr,
                                                    (void *)prev_out_port_ptr,
                                                    prop_type,
                                                    bkwrd_prop_payload,
                                                    recurse_depth_ptr);
      }
      }
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
   return result;
}

/**
 * Recursive state propagation (forwards in graph shape).
 *
 * 1. Set the propagated property on the input port of the module.
 * 2. Check if the propagation is possible across the next module.
 * 3. If propagation is possible, set the property on the next modules output ports.
 * 4. And propagate further from next modules output port to next_next module's input.
 */
ar_result_t gen_topo_propagate_port_property_forwards(void                     *vtopo_ptr,
                                                      void                     *vin_port_ptr,
                                                      topo_port_property_type_t prop_type,
                                                      uint32_t                  propagated_value,
                                                      uint32_t                 *recurse_depth_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_t *           topo_ptr    = (gen_topo_t *)vtopo_ptr;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)vin_port_ptr;

   if (!in_port_ptr || !vtopo_ptr)
   {
      return AR_EOK;
   }
   RECURSION_ERROR_CHECK_ON_FN_ENTRY(topo_ptr->gu.log_id, recurse_depth_ptr, PROPAGATION_RECURSE_MAX_DEPTH);

   // Set next input ports state.
   bool_t can_propagate_further = TRUE;
   gen_topo_set_get_propagated_property_on_the_input_port(topo_ptr,
                                                          in_port_ptr,
                                                          prop_type,
                                                          &propagated_value,
                                                          &can_propagate_further);

   if (!can_propagate_further)
   {
#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "LOG_FRWD_PROP: Stop forward propagation of prop_type=0x%x at module-id,port_id (0x%lX,0x%lx) ",
               prop_type,
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id);
#endif
      return AR_EOK;
   }

   // Propagate across next module
   gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
   if (!gen_topo_check_and_propagate_across_module(next_module_ptr, prop_type, FORWARD_PROP, &propagated_value))
   {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "LOG_FRWD_PROP: cannot propagate prop_type=0x%lx further from (module-id, in-port-id)= "
               "(0x%lX, 0x%lx)",
               prop_type,
               next_module_ptr->gu.module_instance_id,
               in_port_ptr->gu.cmn.id);
#endif
      RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
      return result;
   }

   gu_output_port_list_t *out_port_lists[] = { next_module_ptr->gu.output_port_list_ptr,
                                               next_module_ptr->gu.ipc_output_port_list_ptr };

   for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
   {
      for (gu_output_port_list_t *out_port_list_ptr = out_port_lists[i]; (NULL != out_port_list_ptr);
         LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         uint32_t frwd_prop_payload = propagated_value;

         // Set propagated property on output port.
         bool_t can_propagate_further = TRUE;
         gen_topo_set_get_propagated_property_on_the_output_port(topo_ptr,
                                                               out_port_ptr,
                                                               prop_type,
                                                               &frwd_prop_payload,
                                                               &can_propagate_further);
         if (!can_propagate_further)
         {
   #ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "LOG_FRWD_PROP: Stop forward propagation of prop_type=0x%x at module-id,port_id (0x%lX,0x%lx) ",
                     prop_type,
                     out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     out_port_ptr->gu.cmn.id);
   #endif
            continue;
         }

         // If ext output is hit, propagation terminates and handle port state change on ext output port
         // through call back.
         if (out_port_ptr->gu.ext_out_port_ptr)
         {
            if (topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_output)
            {
               // Send propagated property state on external port.
               topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_output(topo_ptr,
                                                                                    out_port_ptr->gu.ext_out_port_ptr,
                                                                                    prop_type,
                                                                                    &frwd_prop_payload);
            }
         }
         else
         {
            gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;

   #ifdef DEBUG_TOPO_PORT_PROP_TYPE
            LOG_FRWD_PROP(topo_ptr->gu.log_id,
                        out_port_ptr,
                        (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr,
                        prop_type,
                        &frwd_prop_payload);
   #endif

            gen_topo_propagate_port_property_forwards(vtopo_ptr,
                                                      (void *)next_in_port_ptr,
                                                      prop_type,
                                                      frwd_prop_payload,
                                                      recurse_depth_ptr);
         }
      }
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
   return result;
}

/**
 * Propagate port data type starting from STM module and propagate downstream.
 */
static ar_result_t gen_topo_propagate_is_upstream_realtime(gen_topo_t *topo_ptr)
{
   // this is an event handler function, assuming the critical section lock has been taken by the caller
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT

   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   // Setting do_reconcile as TRUE as this can RT event can be propagated both in command and data path context
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, topo_ptr, TRUE /*do reconcile*/)

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         uint32_t           is_upstream_realtime = FALSE;
         bool_t             need_to_propagate    = FALSE;
         gen_topo_module_t *module_ptr           = (gen_topo_module_t *)module_list_ptr->module_ptr;

         /* Propagate from,
          *    1. RT flag from STM modules.
               1.a Prop RT from Source Module in ST container, only if the src module is not data triggered in ST.
                   If the src module is data triggered it needs to implement RT prop extension and raise
                   RT/NRT depending upon the module tgp. Limitation: It might be tricky for module
                   because to propagate since doesnt have the context if its in Signal triggered container or not.
                   In that case module, can probably reflect whatever us/ds propagate state.
                   Ex: Auto usecase (EAVB src placed along with TDM module, TDM is to drive EAVB src at realtime )
          *    2. WR shm modules.
          *    3. Container output boundary or SG output boundary modules. */
         if (gen_topo_is_stm_module(module_ptr) ||
             (topo_ptr->flags.is_signal_triggered && module_ptr->gu.flags.is_source &&
              !gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr)))
         {
            // STM module is a real time source.
            is_upstream_realtime = TRUE;
            need_to_propagate    = TRUE;
         }
         else if (MODULE_ID_WR_SHARED_MEM_EP == module_ptr->gu.module_id)
         {
            /* Propagate FTRT source.
             *
             * This is needed if upstream SG changes from RT to FTRT source. So the downstream ports need to be informed
             * about the switch. So its recommended to propagate from all EP sources if RT or not.
             *
             * Eg: Source switch from HW EP to WR shared memory, we need to reset all the downstream port to FTRT. */

            gu_input_port_list_t * list_ptr    = module_ptr->gu.input_port_list_ptr;
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)list_ptr->ip_port_ptr;
            is_upstream_realtime               = in_port_ptr->common.flags.is_upstream_realtime;
            need_to_propagate                  = TRUE;
         }
         else if (MODULE_ID_RD_SHARED_MEM_CLIENT == module_ptr->gu.module_id)
         {
            gu_output_port_list_t *list_ptr = module_ptr->gu.output_port_list_ptr;
            if (NULL != list_ptr)
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)list_ptr->op_port_ptr;
               if (NULL != out_port_ptr)
               {
                  is_upstream_realtime = out_port_ptr->common.flags.is_upstream_realtime;
                  need_to_propagate    = TRUE;
               }
            }
         }
         else if(MODULE_ID_IPC_RX == module_ptr->gu.module_id)
         {
            /* by default propagates FTRT, if upstream IPC Tx has propagated the same would be propagated here as well. */

            gu_input_port_list_t * list_ptr    = module_ptr->gu.ipc_input_port_list_ptr;
            if(list_ptr)
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)list_ptr->ip_port_ptr;
               is_upstream_realtime               = in_port_ptr->common.flags.is_upstream_realtime;
            }
            need_to_propagate                  = TRUE;
         }
         else
         {
            if (module_ptr->gu.flags.is_ds_at_sg_or_cntr_boundary || module_ptr->flags.supports_prop_is_rt_port_prop)
            {

               /* Check and propagate if module is at sg output or container output boundary
                *
                * This is needed to handle when upstream SG is RT and a SG switch happens in downstream.
                * After the switch, the new downstream SG modules will have is_RT=FALSE, but it needs to be RT.
                * Hence whenever a container gets PREPARE its recommended to propagate from the ext output modules
                * or SG boundary modules so that the downstream modules are coherent with upstream modules.
                *  */

               // need to propagate when there's a module which supports port property prop.
               need_to_propagate = TRUE;
            }

            if (need_to_propagate)
            {
               /* Get is_RT from the inp port of the cur module if frwd propagation is possible across
                * it. Because the out port at SG boundary can be a new connection. Propagated value is
                * set based on the following conditions,
                *   1. If module supports propagation extn, then module's output port property is propagated
                *      to downstream.
                *   2. If module doesn't support extension then propagation is done based on default policy.
                *      That is if atleast one input is realtime propagate upstream realtime to from all outputs.
                *
                * Eg:
                * 1. dynamic connection to splitter output port, output port should inherit is_rt from input port.
                * the new output port's is_rt= FALSE, but splitters input may be RT.
                * So its recommended to get is_rt from input if propagation is possible.
                *
                * 2. If propagation is not possible like Dam module case, if a dynamic output is connected.
                * we cannot propagate from input port, we need to propagate output ports property [is_RT= FALSE] to
                * downstream.
                */
               if (!gen_topo_check_and_propagate_across_module(module_ptr,
                                                               PORT_PROPERTY_IS_UPSTREAM_RT,
                                                               FORWARD_PROP,
                                                               &is_upstream_realtime))
               {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "LOG_FRWD_PROP: cannot propagate prop_type=0x%lx further from module-id %xlx ",
                           (PORT_PROPERTY_IS_UPSTREAM_RT),
                           module_ptr->gu.module_instance_id);
#endif
                  need_to_propagate = FALSE;
               }
            }
         }

         if (FALSE == need_to_propagate)
         {
#if 0
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "gen_topo_propagate_is_upstream_realtime: Not a STM/WR shm/SG out boundary module-id 0x%lX, "
                        "skipping it ",
                        module_ptr->gu.module_instance_id);
#endif
            continue;
         }

         gu_output_port_list_t *out_port_lists[] = { module_ptr->gu.output_port_list_ptr,
                                                     module_ptr->gu.ipc_output_port_list_ptr };

         for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
         {
            for (gu_output_port_list_t *out_port_list_ptr = out_port_lists[i]; (NULL != out_port_list_ptr);
                 LIST_ADVANCE(out_port_list_ptr))
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

               uint32_t forward_prop_value = is_upstream_realtime;

               // Set propagated property on output port.
               bool_t can_proapagate_further = TRUE;
               gen_topo_set_get_propagated_property_on_the_output_port(topo_ptr,
                                                                       out_port_ptr,
                                                                       PORT_PROPERTY_IS_UPSTREAM_RT,
                                                                       &forward_prop_value,
                                                                       &can_proapagate_further);
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        SPF_LOG_PREFIX
                        "gen_topo_propagate_is_upstream_realtime: Propagating forwards from module-id 0x%lX, "
                        "out-port=0x%lx, is_upstream_rt=0x%lx",
                        module_ptr->gu.module_instance_id,
                        out_port_ptr->gu.cmn.id,
                        forward_prop_value);
#endif

               // ext-out cases, propagation terminates.
               if (out_port_ptr->gu.ext_out_port_ptr)
               {
                  if ((can_proapagate_further) &&
                      (topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_output))
                  {
                     // Send propagated property state on external port.
                     topo_ptr->topo_to_cntr_vtable_ptr
                        ->set_propagated_prop_on_ext_output(topo_ptr,
                                                            out_port_ptr->gu.ext_out_port_ptr,
                                                            PORT_PROPERTY_IS_UPSTREAM_RT,
                                                            &forward_prop_value);
                  }
               }
               else
               {
                  gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;

                  if (next_in_port_ptr && !gen_topo_is_module_sg_stopped_or_suspended(
                                             (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr))
                  {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
                     LOG_FRWD_PROP(topo_ptr->gu.log_id,
                                   out_port_ptr,
                                   next_in_port_ptr,
                                   PORT_PROPERTY_IS_UPSTREAM_RT,
                                   &forward_prop_value);
#endif

                     // Propagate to the next module.
                     uint32_t recurse_depth = 0;
                     result |= gen_topo_propagate_port_property_forwards(topo_ptr,
                                                                         (void *)next_in_port_ptr,
                                                                         PORT_PROPERTY_IS_UPSTREAM_RT,
                                                                         forward_prop_value,
                                                                         &recurse_depth);
                  }
               }
            }
         }
      }
   }

   // since event is handled therefore can clear the event flag from the main capi_event_flag
   capi_event_flag_ptr->port_prop_is_up_strm_rt_change = FALSE;

   return result;
}

/**
 * Propagate is_downstream_rt backwards from downstream to upstream. Propagation starts at,
 *   1. STM module
 *   2. RD shm module
 *   3. container/SG output boundary modules.
 */
static ar_result_t gen_topo_propagate_is_downstream_realtime(gen_topo_t *topo_ptr)
{
   // this is an event handler function, assuming the critical section lock has been taken by the caller
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT

   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   // Setting do_reconcile as TRUE as this can RT event can be propagated both in command and data path context
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, topo_ptr, TRUE /*do reconcile*/)

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         uint32_t           is_downstream_realtime = FALSE;
         gen_topo_module_t *module_ptr             = (gen_topo_module_t *)module_list_ptr->module_ptr;

         /* Propagate from,
          *    1. Prop RT from STM modules.
               1.a Prop RT from Sink Module in ST container, if the sink module is not data triggered in ST.
                   If the sink module is data triggered in ST, it needs to implement RT prop extension and raise
                   RT/NRT depending upon the module tgp requirements. Limitation: It might be tricky for module
                   because to propagate since doesnt have the context if its in Signal triggered container or not.
                   In that case module, can probably reflect whatever us/ds propagate state.
                  Ex: Auto usecase (TDM placed in EAVB sink container, to drive EAVB sink at realtime)
          *    2. RD shm modules.
          *    3. container/SG output boundary modules.*/
         bool_t need_to_propagate = FALSE;
         if (gen_topo_is_stm_module(module_ptr) ||
             (topo_ptr->flags.is_signal_triggered && module_ptr->gu.flags.is_sink &&
              !gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr)))
         {
            // STM module is real time sink.
            is_downstream_realtime = TRUE;
            need_to_propagate      = TRUE;
         }
         else if (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->gu.module_instance_id)
         {
            // FTRT case
            gu_output_port_list_t * list_ptr     = module_ptr->gu.output_port_list_ptr;
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)list_ptr->op_port_ptr;
            is_downstream_realtime               = out_port_ptr->common.flags.is_downstream_realtime;
            need_to_propagate                    = TRUE;
         }
         else if (MODULE_ID_WR_SHARED_MEM_CLIENT == module_ptr->gu.module_id)
         {
            gu_input_port_list_t *list_ptr = module_ptr->gu.input_port_list_ptr;
            if (NULL != list_ptr)
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)list_ptr->ip_port_ptr;
               if (NULL != in_port_ptr)
               {
                  is_downstream_realtime = in_port_ptr->common.flags.is_downstream_realtime;
                  need_to_propagate      = TRUE;
               }
            }
         }
         else if (MODULE_ID_IPC_TX == module_ptr->gu.module_id)
         {
            // FTRT case
            gu_output_port_list_t * list_ptr     = module_ptr->gu.ipc_output_port_list_ptr;
            if(list_ptr)
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)list_ptr->op_port_ptr;
               is_downstream_realtime               = out_port_ptr->common.flags.is_downstream_realtime;
               need_to_propagate                    = TRUE;
            }
         }
         else
         {
            if (module_ptr->gu.flags.is_us_at_sg_or_cntr_boundary || module_ptr->flags.supports_prop_is_rt_port_prop)
            {
               /* Check and propagate if module is at sg output or container output boundary
                *
                * This is needed to handle when downstream SG is RT and a SG switch happens and changes it to FTRT.
                * After the switch, the downstream needs to propagate is_rt= FALSE to upstream. Else upstream continue
                * to assume downstream to be RT.
                *  */
               need_to_propagate = TRUE;
            }

            if (need_to_propagate)
            {
               /* Get is_RT from the output port of the cur module if backward propagation is possible across
                * it. Because the out port at SG boundary can be a new connection. Propagation is done based
                * on following conditions,
                *  1. If module supports propagation extn, then propagate the value as set by the module on it's
                *     input.
                *  2. If module doesn't support extn, then propagate based on default policy. If alteast one
                *     output is realtime then propagate downstream as RT to all the input ports.
                *
                * refer upstream RT propagation for examples.*/
               if (!gen_topo_check_and_propagate_across_module(module_ptr,
                                                               PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                               BACKWARD_PROP,
                                                               &is_downstream_realtime))
               {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "LOG_BKWRD_PROP: Stop backward propagation of prop_type=0x%lx further from module-id 0x%lx",
                           (PORT_PROPERTY_IS_DOWNSTREAM_RT),
                           module_ptr->gu.module_instance_id);
#endif
                  need_to_propagate = FALSE;
               }
            }
         }

         if (FALSE == need_to_propagate)
         {
#if 0
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "gen_topo_propagate_is_downstream_realtime: Not a STM/WR shm/SG out boundary module-id 0x%lX, "
                        "skipping it ",
                        module_ptr->gu.module_instance_id);
#endif
            continue;
         }

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            uint32_t bkwrd_prop_value = is_downstream_realtime;

            // Set propagated property on input port.
            bool_t can_proapagate_further = TRUE;
            gen_topo_set_get_propagated_property_on_the_input_port(topo_ptr,
                                                                   in_port_ptr,
                                                                   PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                                   &bkwrd_prop_value,
                                                                   &can_proapagate_further);

#ifdef DEBUG_TOPO_PORT_PROP_TYPE
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     SPF_LOG_PREFIX "gen_topo_propagate_is_downstream_realtime: Propagating backwards from module-id 0x%lX, "
                     "in-port=0x%lx, is_downstream_rt=0x%lx",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     bkwrd_prop_value);
#endif

            // ext-in cases, propagation terminates.
            if (in_port_ptr->gu.ext_in_port_ptr)
            {
               if ((can_proapagate_further) && (topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_input))
               {
                  // Send propagated property state on external port.
                  topo_ptr->topo_to_cntr_vtable_ptr->set_propagated_prop_on_ext_input(topo_ptr,
                                                                                      in_port_ptr->gu.ext_in_port_ptr,
                                                                                      PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                                                      &bkwrd_prop_value);
               }
            }
            else
            {
               gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

               if (prev_out_port_ptr && !gen_topo_is_module_sg_stopped_or_suspended(
                                           (gen_topo_module_t *)prev_out_port_ptr->gu.cmn.module_ptr))
               {
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
                  LOG_BKWRD_PROP(topo_ptr->gu.log_id,
                                 in_port_ptr,
                                 prev_out_port_ptr,
                                 PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                 &bkwrd_prop_value);
#endif

                  // Propagate to the previous modules in the chain.
                  uint32_t recurse_depth = 0;
                  result |= gen_topo_propagate_port_property_backwards((void *)topo_ptr,
                                                                       (void *)prev_out_port_ptr,
                                                                       PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                                       (uint32_t)bkwrd_prop_value,
                                                                       &recurse_depth);
               }
            }
         }
      }
   }

   // since event is handled therefore can clear the event flag from the main capi_event_flag
   capi_event_flag_ptr->port_prop_is_down_strm_rt_change = FALSE;

   return result;
}

/* Propagates backwards for each module that requires data buffering. Propagation breaks if a module which is non-inplace is hit */
ar_result_t gen_topo_propagate_requires_data_buffering_upstream(gen_topo_t *topo_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         /* Propagate from inputs of the modules with requires data buffering to upstream modules.*/
         if(!module_ptr->flags.requires_data_buf)
         {
            continue;
         }

         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Propagating requires data buffering from module iid 0x%lX ",
                  module_ptr->gu.module_instance_id);

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            // propagate TRUE
            uint32_t bkwrd_prop_value = module_ptr->flags.requires_data_buf;

            // Set propagated property on input port.
            bool_t can_proapagate_further = TRUE;
            gen_topo_set_get_propagated_property_on_the_input_port(topo_ptr,
                                                                   in_port_ptr,
                                                                   PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING,
                                                                   &bkwrd_prop_value,
                                                                   &can_proapagate_further);
#ifdef DEBUG_TOPO_PORT_PROP_TYPE
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Propagating requires data buffering backwards from module-id 0x%lX, "
                     "in-port=0x%lx, required_data_buffering=0x%lx",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     in_port_ptr->common.flags.downstream_req_data_buffering);
#endif

            // ext-in cases, propagation terminates.
            if (in_port_ptr->gu.conn_out_port_ptr)
            {
               gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

#ifdef DEBUG_TOPO_PORT_PROP_TYPE
               LOG_BKWRD_PROP(topo_ptr->gu.log_id,
                              in_port_ptr,
                              prev_out_port_ptr,
                              PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING,
                              &bkwrd_prop_value);
#endif

               // Propagate to the previous modules in the chain.
               uint32_t recurse_depth = 0;
               result |= gen_topo_propagate_port_property_backwards((void *)topo_ptr,
                                                                    (void *)prev_out_port_ptr,
                                                                    PORT_PROPERTY_DOWNSTREAM_REQUIRES_DATA_BUFFERING,
                                                                    (uint32_t)bkwrd_prop_value,
                                                                    &recurse_depth);
            }
         }
      }
   }

   return result;
}

/**
 * Backward state propagation through all modules.
 */
ar_result_t gen_topo_propagate_boundary_modules_port_state(void *base_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = (gen_topo_t *)base_ptr;

   topo_ptr->flags.state_prop_going_on = TRUE;

   // get the last module from the sorted list and start backward. It is a non-recurrsive function.
   gu_module_list_t *module_list_ptr = NULL;
   spf_list_get_tail_node((spf_list_node_t *)topo_ptr->gu.sorted_module_list_ptr, (spf_list_node_t **)&module_list_ptr);

   for (; (NULL != module_list_ptr); LIST_RETREAT(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      topo_port_state_t state_to_propagate_backwrd      = TOPO_PORT_STATE_STOPPED;
      bool_t            atleast_one_output_is_started   = FALSE;
      bool_t            atleast_one_output_is_suspended = FALSE;
      bool_t            continue_propagation            = FALSE;

      // if the self subgraph is in stopped or suspended state then no need to propagate as US_RT can be considered as
      // FALSE.
      if (gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
      {
         continue;
      }

      /** Cannot start propagation from,
       *  1. STM/Buffering modules that doesn't support propagation.
       *  2. Pseudo sink modules must not propagate downstream state
       */
      if (!module_ptr->flags.supports_prop_port_ds_state &&
          (module_ptr->flags.need_stm_extn || module_ptr->flags.need_mp_buf_extn ||
           (0 == module_ptr->gu.min_output_ports)))
      {
         continue;
      }

      // When supports_port_property_prop module is the boundary module (dangling or ext-out/subgraph-out)
      //      then then we still assume out port is stopped & corresponding input is assumed to be also stopped.
      //      currently module has no control over raising event as there's no out port. Once out port is created
      //      it gets set-param and then it can raise event.

      /*
       * if number of connected output ports is ZERO and its not a sink module. Then its is dangling boundary module.
       */
      if ((0 == module_ptr->gu.num_output_ports && 0 == module_ptr->gu.num_ipc_output_ports) &&
          !module_ptr->gu.flags.is_sink)
      {
         continue_propagation = TRUE;

         // for dangling module at output boundary, output port doesn't exist. in that case output port state is
         // considered as stopped.
         // we can not propagate stopped from sink modules because they can run without any output
         // RD Shared Mem EP in not the sink module or boundary module, it has one output port (internally created).
         state_to_propagate_backwrd      = TOPO_PORT_STATE_STOPPED;
         atleast_one_output_is_started   = FALSE;
         atleast_one_output_is_suspended = FALSE;
      }
      else
      {
         /** If the module doesn't support state propagation extension, then framework
          *  is expected to propagate the state based on default policy.
          *  The default policy is,
          *  1. if at least one output port is started then topo state is propagated as START to all the inputs.
          *  2. if none of the output ports are started and at least one output port is in SUSPEND state then propagate
          * SUSPEND.
          *  3. if none of the output ports are in START/SUSPEND state then propagate STOP.
          */

         gu_output_port_list_t *out_port_lists[] = { module_ptr->gu.output_port_list_ptr,
                                                     module_ptr->gu.ipc_output_port_list_ptr };

         for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
         {
            for (gu_output_port_list_t *out_port_list_ptr = out_port_lists[i]; (NULL != out_port_list_ptr);
                 LIST_ADVANCE(out_port_list_ptr))
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

               // if state propagation is blocked from this output ports then
               // we need to revert the downgraded state and assign self state to this ports.
               // ports which blocks the propagated state means that they can run even if downstream is stopped.
               if (out_port_ptr->common.flags.is_state_prop_blocked)
               {
                  topo_port_state_t self_port_state =
                     topo_sg_state_to_port_state(gen_topo_get_sg_state(out_port_ptr->gu.cmn.module_ptr->sg_ptr));
                  out_port_ptr->common.state = self_port_state;
               }

               if (TOPO_PORT_STATE_STARTED == out_port_ptr->common.state)
               {
                  atleast_one_output_is_started = TRUE;
               }
               else if (TOPO_PORT_STATE_SUSPENDED == out_port_ptr->common.state)
               {
                  atleast_one_output_is_suspended = TRUE;
               }

               continue_propagation |= is_downstream_state_propagation_allowed(out_port_ptr);

               // if it is an external output port and supports the ds-state extension then inform capi about the state
               // now. for internal output ports, it is handled later in the function.
               if (module_ptr->flags.supports_prop_port_ds_state)
               {
                  topo_port_state_t port_state                = out_port_ptr->common.state;
                  bool_t            temp_continue_propagation = FALSE;
                  gen_topo_set_get_propagated_property_on_the_output_port(topo_ptr,
                                                                          out_port_ptr,
                                                                          PORT_PROPERTY_TOPO_STATE,
                                                                          &port_state,
                                                                          &temp_continue_propagation);
               }
            }
         }
      }

      if (!continue_propagation)
      {
         continue;
      }

      // if at least one output started then propagate start
      // note that module shouldn't support propagation extension. If module supports extension,
      // then input port's state is propagated backwards in the later loop.
      if (atleast_one_output_is_started)
      {
         state_to_propagate_backwrd = TOPO_PORT_STATE_STARTED;
      }
      else if (atleast_one_output_is_suspended)
      {
         state_to_propagate_backwrd = TOPO_PORT_STATE_SUSPENDED;
      }
      else
      {
         state_to_propagate_backwrd = TOPO_PORT_STATE_STOPPED;
      }

#ifdef DEBUG_TOPO_BOUNDARY_STATE_PROP
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "gen_topo_port_state_prop: Propagating state %u backwards from "
               "module-id 0x%lX",
               state_to_propagate_backwrd,
               module_ptr->gu.module_instance_id);
#endif
      /**
       * propagate to input ports
       */

      gu_input_port_list_t *in_port_lists[] = { module_ptr->gu.input_port_list_ptr,
                                                module_ptr->gu.ipc_input_port_list_ptr };

      for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
      {
         for (gu_input_port_list_t *in_port_list_ptr = in_port_lists[i]; (NULL != in_port_list_ptr);
            LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            uint32_t bkwrd_prop_payload = state_to_propagate_backwrd;

            // Set input ports state.
            bool_t temp_can_propagate_backwards = TRUE;
            gen_topo_set_get_propagated_property_on_the_input_port(topo_ptr,
                                                                  in_port_ptr,
                                                                  PORT_PROPERTY_TOPO_STATE,
                                                                  &bkwrd_prop_payload,
                                                                  &temp_can_propagate_backwards);

            if (in_port_ptr->gu.conn_out_port_ptr)
            {
   #ifdef DEBUG_TOPO_PORT_PROP_TYPE
               LOG_BKWRD_PROP(topo_ptr->gu.log_id,
                              in_port_ptr,
                              (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr,
                              PORT_PROPERTY_TOPO_STATE,
                              &bkwrd_prop_payload);
   #endif
               // propagate to prev module's output port.
               // if module supports ds-state extension then Capi will be informed in this call.
               gen_topo_set_get_propagated_property_on_the_output_port(topo_ptr,
                                                                     (gen_topo_output_port_t *)
                                                                        in_port_ptr->gu.conn_out_port_ptr,
                                                                     PORT_PROPERTY_TOPO_STATE,
                                                                     &bkwrd_prop_payload,
                                                                     &temp_can_propagate_backwards);
            }
         }
      }
   }

   topo_ptr->flags.state_prop_going_on = FALSE;

   return result;
}

/**
 * Propagate port data type starting from STM module and propagate downstream.
 */
ar_result_t gen_topo_propagate_port_props(void *base_ptr, topo_port_property_type_t prop_type)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = (gen_topo_t *)base_ptr;

   switch (prop_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         result = gen_topo_propagate_is_upstream_realtime(topo_ptr);
         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         result = gen_topo_propagate_is_downstream_realtime(topo_ptr);
         break;
      }
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Invalid prop_type=0x%lx", prop_type);
         result = AR_EFAILED;
      }
   }

   return result;
}

ar_result_t gen_topo_handle_port_propagated_capi_event(gen_topo_t *       topo_ptr,
                                                       gen_topo_module_t *module_ptr,
                                                       capi_event_info_t *event_info_ptr)
{
   ar_result_t                       result        = AR_EOK;
   capi_buf_t *                      payload       = &event_info_ptr->payload;
   capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

   switch (dsp_event_ptr->param_id)
   {
      case INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP:
      {
         intf_extn_event_id_block_port_ds_state_prop_t *data_ptr =
            (intf_extn_event_id_block_port_ds_state_prop_t *)(dsp_event_ptr->payload.data_ptr);

         if (dsp_event_ptr->payload.actual_data_len < sizeof(intf_extn_event_id_block_port_ds_state_prop_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(intf_extn_event_id_block_port_ds_state_prop_t),
                     (uint32_t)(dsp_event_ptr->param_id));
            return AR_ENEEDMORE;
         }

         gen_topo_output_port_t *out_port_ptr =
            (gen_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->gu, data_ptr->output_port_index);

         if (!out_port_ptr)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. port index %lu not found",
                     module_ptr->gu.module_instance_id,
                     data_ptr->output_port_index);
            return AR_EFAILED;
         }

         // This event can not be handled if sg is already started/prepared/suspended and state propagation already
         // happened. module should send this event during port open.
         gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)module_ptr->gu.sg_ptr;
         if ((TOPO_SG_STATE_STOPPED != sg_ptr->state))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: State propagation can not be blocked if it already happened.",
                     module_ptr->gu.module_instance_id);
            result = AR_EFAILED;
         }
         else
         {
            TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "gen_topo_propagate_state: Module 0x%lX, port 0x%lx state propagation is blocked.",
                     module_ptr->gu.module_instance_id,
                     out_port_ptr->gu.cmn.id);

            out_port_ptr->common.flags.is_state_prop_blocked = TRUE;
         }
         break;
      }
      case INTF_EXTN_EVENT_ID_PORT_DS_STATE:
      {
         intf_extn_event_id_port_ds_state_t *data_ptr =
            (intf_extn_event_id_port_ds_state_t *)(dsp_event_ptr->payload.data_ptr);

         if (dsp_event_ptr->payload.actual_data_len < sizeof(intf_extn_event_id_port_ds_state_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(intf_extn_event_id_port_ds_state_t),
                     (uint32_t)(dsp_event_ptr->param_id));
            return AR_ENEEDMORE;
         }

         if (!topo_ptr->flags.state_prop_going_on)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: State can be propagated only when state propagation is going on",
                     module_ptr->gu.module_instance_id);
            result = AR_EFAILED;
         }
         else
         {
            topo_port_state_t prop_state = TOPO_PORT_STATE_INVALID;
            if (INTF_EXTN_PROP_DATA_PORT_STATE_STARTED == data_ptr->port_state)
            {
               prop_state = TOPO_PORT_STATE_STARTED;
            }
            else if (INTF_EXTN_PROP_DATA_PORT_STATE_PREPARED == data_ptr->port_state)
            {
               prop_state = TOPO_PORT_STATE_PREPARED;
            }
            else if (INTF_EXTN_PROP_DATA_PORT_STATE_STOPPED == data_ptr->port_state)
            {
               prop_state = TOPO_PORT_STATE_STOPPED;
            }
            else if (INTF_EXTN_PROP_DATA_PORT_STATE_SUSPENDED == data_ptr->port_state)
            {
               prop_state = TOPO_PORT_STATE_SUSPENDED;
            }
            else
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error in callback function. invalid propagated property val %u",
                        module_ptr->gu.module_instance_id,
                        data_ptr->port_state);
            }

            gen_topo_input_port_t *in_port_ptr =
               (gen_topo_input_port_t *)gu_find_input_port_by_index(&module_ptr->gu, data_ptr->input_port_index);

            if (!in_port_ptr)
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error in callback function. port index %lu not found",
                        module_ptr->gu.module_instance_id,
                        data_ptr->input_port_index);
               result = AR_EFAILED;
            }
            else
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        SPF_LOG_PREFIX "gen_topo_propagate_state: Module 0x%lX, port 0x%lx prop state event %u",
                        module_ptr->gu.module_instance_id,
                        in_port_ptr->gu.cmn.id,
                        prop_state);

               // for port state, don't check diff with current as current state may be the temporary one being used
               // for propagation.
               // store reported state and also a bit to indicate the event.
               gen_topo_set_propagated_state_on_input_port(in_port_ptr, prop_state);
               in_port_ptr->common.flags.port_prop_state_change = TRUE;
            }
         }
         break;
      }
      case INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY:
      {
         intf_extn_event_id_is_rt_port_property_t *data_ptr =
            (intf_extn_event_id_is_rt_port_property_t *)(dsp_event_ptr->payload.data_ptr);

         if (dsp_event_ptr->payload.actual_data_len < sizeof(intf_extn_event_id_is_rt_port_property_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(intf_extn_event_id_is_rt_port_property_t),
                     (uint32_t)(dsp_event_ptr->param_id));
            return AR_ENEEDMORE;
         }

         bool_t is_real_time = data_ptr->is_rt;

         // module raises event on input port for a set param on output port. hence propagation is of downstream RT
         // flag.
         if (data_ptr->is_input)
         {
            gen_topo_input_port_t *in_port_ptr =
               (gen_topo_input_port_t *)gu_find_input_port_by_index(&module_ptr->gu, data_ptr->port_index);
            if (NULL == in_port_ptr)
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX, port %lu port index not found",
                        module_ptr->gu.module_instance_id,
                        data_ptr->port_index);
            }
            else
            {
               if (is_real_time != in_port_ptr->common.flags.is_downstream_realtime)
               {
                  in_port_ptr->common.flags.is_downstream_realtime                       = is_real_time;
                  in_port_ptr->common.flags.port_prop_is_rt_change                       = TRUE;
                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_prop_is_down_strm_rt_change);
                  TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           SPF_LOG_PREFIX  "gen_topo_propagate_is_downstream_realtime: Module 0x%lX, port 0x%lx raised event"
                           " is_downstream_realtime = 0x%lx",
                           module_ptr->gu.module_instance_id,
                           in_port_ptr->gu.cmn.id,
                           is_real_time);
               }
            }
         }
         else
         {
            gen_topo_output_port_t *out_port_ptr =
               (gen_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->gu, data_ptr->port_index);
            if (NULL == out_port_ptr)
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX, port %lu port index not found",
                        module_ptr->gu.module_instance_id,
                        data_ptr->port_index);
            }
            else
            {
               if (is_real_time != out_port_ptr->common.flags.is_upstream_realtime)
               {
                  out_port_ptr->common.flags.is_upstream_realtime                      = is_real_time;
                  out_port_ptr->common.flags.port_prop_is_rt_change                    = TRUE;
                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_prop_is_up_strm_rt_change);
                  TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           SPF_LOG_PREFIX  "gen_topo_propagate_is_upstream_realtime: Module 0x%lX, port 0x%lx raised event"
                           " is_upstream_realtime = 0x%lx",
                           module_ptr->gu.module_instance_id,
                           out_port_ptr->gu.cmn.id,
                           is_real_time);
               }
            }
         }
         break;
      }
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in callback function. invalid event id 0x%lx",
                  module_ptr->gu.module_instance_id,
                  dsp_event_ptr->param_id);
         break;
      }
   }

   return result;
}
