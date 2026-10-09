/**
 * \file gen_topo_mod_buf_access_ext.c
 * \brief
 *  This file contains utility functions for INTF_EXTN_MODULE_BUFFER_ACCESS

 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause

/* =======================================================================
Includes
========================================================================== */
#include "gen_topo.h"

/** Iterate backwards from the given input till the external input port(nblc_start_ptr)*/
bool_t gen_topo_cur_output_has_capi_op_buf_extn_port_us(uint32_t log_id, gen_topo_output_port_t *out_port_ptr)
{

   /* -------------------------------------------------------------
    * Propagate the (original or newly created) buffer upstream.
    * ------------------------------------------------------------- */
   /** return CAPI buffers only from the buffer source port context */

   gen_topo_output_port_t *curr_out_port_ptr             = out_port_ptr;
   gen_topo_output_port_t *inplace_start_output_port_ptr = NULL;
   while (curr_out_port_ptr)
   {
      /* Stop at an NBLc‑start module. */
      gen_topo_module_t *cur_mod_ptr = (gen_topo_module_t *)curr_out_port_ptr->gu.cmn.module_ptr;


      if (((curr_out_port_ptr == curr_out_port_ptr->nblc_start_ptr) &&
              (FALSE == gen_topo_is_inplace_or_disabled_siso(cur_mod_ptr))) ||
          (TOPO_PORT_STATE_STARTED != curr_out_port_ptr->common.state))
      {
#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Module 0x%lX (out port 0x%lx) is not_inplace?%lu or not_started?%lu, breaking "
                  "search",
                  curr_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  curr_out_port_ptr->gu.cmn.id,
                  (FALSE == gen_topo_is_inplace_or_disabled_siso(cur_mod_ptr)),
                  (TOPO_PORT_STATE_STARTED != curr_out_port_ptr->common.state));
#endif
         inplace_start_output_port_ptr = curr_out_port_ptr;
         break;
      }

      gen_topo_input_port_t *cu_mod_in_port_ptr =
         (gen_topo_input_port_t *)cur_mod_ptr->gu.input_port_list_ptr->ip_port_ptr;

      gen_topo_output_port_t *prev_mod_out_port_ptr =
         (gen_topo_output_port_t *)cu_mod_in_port_ptr->gu.conn_out_port_ptr;

      curr_out_port_ptr = prev_mod_out_port_ptr;
   }

   if (inplace_start_output_port_ptr &&
       (GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS == inplace_start_output_port_ptr->common.flags.supports_buffer_reuse_extn))
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Found GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS extn port Module 0x%lX (output port "
               "0x%lx) in inplace upstream of Module 0x%lX (out port 0x%lx) ",
               inplace_start_output_port_ptr->gu.cmn.module_ptr->module_instance_id,
               inplace_start_output_port_ptr->gu.cmn.id,
               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->gu.cmn.id);
#endif
      return TRUE;
   }

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Could not find GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS extn port in the inplace upstream "
            "of Module 0x%lX (out port 0x%lx) ",
            out_port_ptr->gu.cmn.module_ptr->module_instance_id,
            out_port_ptr->gu.cmn.id);
#endif

   return FALSE;
}

/** Iterate backwards from the given input till the external input port(nblc_start_ptr)*/
bool_t gen_topo_cur_input_has_capi_inp_buf_extn_port_ds(uint32_t log_id, gen_topo_input_port_t *in_port_ptr)
{


   gen_topo_input_port_t *curr_in_port_ptr        = in_port_ptr;
   gen_topo_input_port_t *inplace_end_in_port_ptr = NULL;
   while (curr_in_port_ptr)
   {
      /* Stop at an NBLc‑start module. */
      gen_topo_module_t *cur_mod_ptr = (gen_topo_module_t *)curr_in_port_ptr->gu.cmn.module_ptr;
      if ((curr_in_port_ptr == curr_in_port_ptr->nblc_end_ptr) ||
          (FALSE == gen_topo_is_inplace_or_disabled_siso(cur_mod_ptr)) ||
          (TOPO_PORT_STATE_STARTED != curr_in_port_ptr->common.state))
      {
#ifdef MOD_BUF_ACCESS_DEBUG
         TOPO_MSG(log_id,
                  DBG_LOW_PRIO,
                  "MOD_BUF_ACCESS_DEBUG: Module 0x%lX (in port 0x%lx) is not_inplace?%lu or not_started?%lu, breaking  "
                  "search",
                  curr_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  curr_in_port_ptr->gu.cmn.id,
                  (FALSE == gen_topo_is_inplace_or_disabled_siso(cur_mod_ptr)),
                  (TOPO_PORT_STATE_STARTED != curr_in_port_ptr->common.state));
#endif
         inplace_end_in_port_ptr = curr_in_port_ptr;
         break;
      }

      gen_topo_output_port_t *cur_mod_out_port_ptr =
         (gen_topo_output_port_t *)cur_mod_ptr->gu.output_port_list_ptr->op_port_ptr;

      gen_topo_input_port_t *next_mod_in_port_ptr = (gen_topo_input_port_t *)cur_mod_out_port_ptr->gu.conn_in_port_ptr;

      curr_in_port_ptr = next_mod_in_port_ptr;
   }

   if (inplace_end_in_port_ptr &&
       (GEN_TOPO_MODULE_INPUT_BUF_ACCESS == inplace_end_in_port_ptr->common.flags.supports_buffer_reuse_extn))
   {
#ifdef MOD_BUF_ACCESS_DEBUG
      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "MOD_BUF_ACCESS_DEBUG: Found GEN_TOPO_MODULE_INPUT_BUF_ACCESS extn port Module 0x%lX (in port 0x%lx) in "
               "the inplace downstream of Module 0x%lX (in port 0x%lx) ",
               inplace_end_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               inplace_end_in_port_ptr->gu.cmn.id,
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id);
#endif
      return TRUE;
   }

#ifdef MOD_BUF_ACCESS_DEBUG
   TOPO_MSG(log_id,
            DBG_LOW_PRIO,
            "MOD_BUF_ACCESS_DEBUG: Could not find GEN_TOPO_MODULE_INPUT_BUF_ACCESS in the inplace downstream of Module "
            "0x%lX (in port 0x%lx) ",
            in_port_ptr->gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->gu.cmn.id);
#endif

   return FALSE;
}