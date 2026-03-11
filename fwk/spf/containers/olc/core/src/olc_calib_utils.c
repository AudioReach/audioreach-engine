/**
 * \file olc_calib_utils.c
 * \brief
 *     This file contains utility functions for calibration
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_driver.h"
#include "olc_i.h"
#include "apm.h"
#include "proxy_cntr_if.h"
#include "irm_cntr_if.h"
#include "apm_cntr_debug_if.h"

/* =======================================================================
Static Function Definitions
========================================================================== */
/**
 * called for all use cases. both for internal and external clients.
 */




/**
 * path-id = 0 => all paths
 *
 * used for destroy or update
 */
ar_result_t olc_cu_update_path_delay(cu_base_t *base_ptr, uint32_t path_id)
{
   ar_result_t result           = AR_EOK;
   uint32_t    total_path_delay = 0;

   spf_list_node_t *    node_ptr       = base_ptr->delay_path_list_ptr;
   spf_list_node_t *    next_ptr       = NULL;
   olc_cu_delay_info_t *delay_info_ptr = NULL;

   while (node_ptr)
   {
      delay_info_ptr = (olc_cu_delay_info_t *)node_ptr->obj_ptr;
      next_ptr       = node_ptr->next_ptr;

      if ((CU_PATH_ID_ALL_PATHS == path_id) || (path_id == delay_info_ptr->path_id))
      {
         uint32_t           algo_delay          = 0;
         uint32_t ext_out_delay = 0, ext_in_delay = 0;

         if (base_ptr->topo_vtbl_ptr && base_ptr->topo_vtbl_ptr->update_path_delays)
         {
            base_ptr->topo_vtbl_ptr->update_path_delays(base_ptr->topo_ptr,
                                                        delay_info_ptr->path_id,
                                                        &algo_delay,
                                                        &ext_in_delay,
                                                        &ext_out_delay);
         }

         delay_info_ptr->module_delay  = algo_delay;
         delay_info_ptr->ext_buf_delay = ext_out_delay;

         // PRINT_PATH_DELAY(base_ptr, delay_info_ptr->path_id, algo_delay, ext_out_delay);
         total_path_delay = algo_delay + ext_in_delay + ext_out_delay + delay_info_ptr->read_ipc_delay +
                            delay_info_ptr->write_ipc_delay + delay_info_ptr->satellite_path_delay;

         *(delay_info_ptr->delay_us_ptr) = total_path_delay;
      }

      node_ptr = next_ptr;
   }

   return result;
}

ar_result_t olc_update_path_delay(cu_base_t *cu_ptr, uint32_t master_path_id, void *sat_delay_event_rsp_ptr)
{
   ar_result_t                  result              = AR_EOK;
   spf_list_node_t *            node_ptr            = cu_ptr->delay_path_list_ptr;
   spf_list_node_t *            next_ptr            = NULL;
   get_container_delay_event_t *delay_event_rsp_ptr = (get_container_delay_event_t *)sat_delay_event_rsp_ptr;

   while (node_ptr)
   {
      olc_cu_delay_info_t *delay_info_ptr = (olc_cu_delay_info_t *)node_ptr->obj_ptr;
      next_ptr                            = node_ptr->next_ptr;

      if (master_path_id == delay_info_ptr->path_id)
      {
         *(delay_info_ptr->delay_us_ptr) +=
            (delay_event_rsp_ptr->new_delay_in_us - delay_event_rsp_ptr->prev_delay_in_us);
         delay_info_ptr->satellite_path_delay = delay_event_rsp_ptr->new_delay_in_us;
         break;
      }
      node_ptr = next_ptr;
   }

   return result;
}

/**
 * APM get param for CNTR_PARAM_ID_PATH_DELAY_CFG
 */
ar_result_t olc_path_delay_cfg(cu_base_t *                       base_ptr,
                               int8_t *                          param_payload_ptr,
                               uint32_t                          param_size,
                               spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *me_ptr = (olc_t *)base_ptr;

   uint32_t delay_us         = 0;
   uint32_t ext_buf_delay_us = 0;
   uint32_t num_ext_in_port  = 0;
   uint32_t num_ext_out_port = 0;
   uint32_t pre_sat_cont_id  = 0;
   uint32_t sec_opcode       = CNTR_PARAM_ID_PATH_DELAY_CFG;

   cu_ext_out_port_t *             ext_out_port_ptr    = NULL;
   gu_ext_out_port_t *             gu_ext_out_port_ptr = NULL;
   gu_module_t *                   prev_module_ptr     = NULL;
   gu_cmn_port_t *                 prev_port_ptr       = NULL;
   cntr_param_id_path_delay_cfg_t *cmd_ptr             = NULL;
   cntr_graph_vertex_t             src_path_mid        = { 0, 0 };
   cntr_graph_vertex_t             dst_path_mid        = { 0, 0 };

   bool_t is_src_updated                 = FALSE;
   bool_t is_prev_module_in_master_pd    = FALSE;
   //bool_t is_prev_module_in_satellite_pd = FALSE;

   olc_cu_delay_info_t *delay_info_ptr = NULL;

   VERIFY(result,
          (NULL != base_ptr->topo_vtbl_ptr) && (NULL != base_ptr->topo_vtbl_ptr->query_module_delay) &&
             (NULL != base_ptr->topo_vtbl_ptr->add_path_delay_info));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:PATH_DELAY: Executing path delay cfg set-param. current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   VERIFY(result, param_size >= sizeof(cntr_param_id_path_delay_cfg_t));
   cmd_ptr = (cntr_param_id_path_delay_cfg_t *)param_payload_ptr;

   // reserve 0 for internal use (0 == all paths)
   VERIFY(result, (NULL != cmd_ptr) && (0 != cmd_ptr->path_id));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "CMD:PATH_DELAY: path_id 0x%lx, one time query? %u",
          cmd_ptr->path_id,
          cmd_ptr->is_one_time_query);

   VERIFY(result,
          (NULL != cmd_ptr->path_def_ptr) && (NULL != cmd_ptr->path_def_ptr->obj_ptr) &&
             (NULL != cmd_ptr->delay_us_ptr));

   cntr_graph_vertex_t *vertex_ptr = NULL;

   for (cntr_list_node_t *node_ptr = cmd_ptr->path_def_ptr; (NULL != node_ptr); LIST_ADVANCE(node_ptr))
   {
      gu_module_t *  module_ptr = NULL;
      gu_cmn_port_t *port_ptr   = NULL;

      vertex_ptr = (cntr_graph_vertex_t *)node_ptr->obj_ptr;

      module_ptr = gu_find_module(base_ptr->gu_ptr, vertex_ptr->module_instance_id);
      if (NULL != module_ptr)
      {
         port_ptr                       = gu_find_cmn_port_by_id(module_ptr, vertex_ptr->port_id);
         is_prev_module_in_master_pd    = TRUE;
         //is_prev_module_in_satellite_pd = FALSE;

         if (port_ptr)
         {
            if (!cmd_ptr->is_one_time_query)
            {
               base_ptr->topo_vtbl_ptr->add_path_delay_info(base_ptr->topo_ptr,
                                                            module_ptr,
                                                            vertex_ptr->port_id,
                                                            port_ptr,
                                                            cmd_ptr->path_id);
            }

            gu_ext_in_port_t *gu_ext_in_port_ptr = gu_get_ext_in_port_from_cmn_port(port_ptr);
            if (NULL != gu_ext_in_port_ptr)
            {
               num_ext_in_port++;
            }

            gu_ext_out_port_t *temp_gu_ext_out_port_ptr = gu_get_ext_out_port_from_cmn_port(port_ptr);
            if (NULL != temp_gu_ext_out_port_ptr)
            {
               num_ext_out_port++;
               gu_ext_out_port_ptr = temp_gu_ext_out_port_ptr;
               ext_out_port_ptr =
                  (cu_ext_out_port_t *)((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);
            }

            /*
             * in the vertex list, if prev and current module's are same, then prev_port_ptr is the input and
             * port_ptr is the output. Using this we can query delay of MIMO module.
             *
             * in case modules are distributed in the list, this method won't work. we need another loop.
             *
             * For now, it's assumed that APM sorts the list properly to have same module ports in subsequent array
             * indices.
             *
             * see comment in APM_PARAM_ID_PATH_DELAY: source, sink module delays are always included.
             * for other modules it's src output to dst output
             */
            uint32_t temp_delay = 0;
            if (module_ptr->flags.is_sink || module_ptr->flags.is_source ||
                (prev_module_ptr && (prev_module_ptr->module_instance_id == vertex_ptr->module_instance_id)))
            {
               base_ptr->topo_vtbl_ptr->query_module_delay(base_ptr->topo_ptr,
                                                           module_ptr,
                                                           prev_port_ptr,
                                                           port_ptr,
                                                           &temp_delay);
            }
            delay_us += temp_delay;
         }
      }
      else
      {
         if (FALSE == is_src_updated)
         {
            src_path_mid = *vertex_ptr;
            if (TRUE == is_prev_module_in_master_pd)
            {
               src_path_mid.port_id = 0;
            }
            is_src_updated = TRUE;
         }
         dst_path_mid = *vertex_ptr;

         is_prev_module_in_master_pd    = FALSE;
         //is_prev_module_in_satellite_pd = TRUE;
      }

      if (is_prev_module_in_master_pd)
      {
         dst_path_mid.port_id = 0;
      }
      prev_module_ptr = module_ptr;
      prev_port_ptr   = port_ptr;

      if ((FALSE == cmd_ptr->is_one_time_query) && (NULL == module_ptr))
      {
         uint32_t cont_id = 0;
         TRY(result, sgm_get_satellite_cont_id(&me_ptr->spgm_info, vertex_ptr->module_instance_id, &cont_id));
         if (pre_sat_cont_id != cont_id)
         {
            // register the container for path delay event
            pre_sat_cont_id = cont_id;
            sgm_add_cont_id_delay_event_reg_list(&me_ptr->spgm_info, cont_id, cmd_ptr->path_id);
         }
      }
   }

   if ((num_ext_in_port > 1) || (num_ext_out_port > 1))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "CMD:PATH_DELAY: Only one external input or output expected. found %lu inputs and %lu outputs",
             num_ext_in_port,
             num_ext_out_port);
      THROW(result, AR_EFAILED);
   }

   if (!cmd_ptr->is_one_time_query)
   {
      MALLOC_MEMSET(delay_info_ptr, olc_cu_delay_info_t, sizeof(olc_cu_delay_info_t), base_ptr->heap_id, result);
      TRY(result,
          spf_list_insert_tail(&base_ptr->delay_path_list_ptr, delay_info_ptr, base_ptr->heap_id, TRUE /*use pool*/));

      delay_info_ptr->path_id      = cmd_ptr->path_id;
      delay_info_ptr->delay_us_ptr = cmd_ptr->delay_us_ptr; // delay pointer from APM
   }

   if (ext_out_port_ptr)
   {
      // fail one time query if the data format is not pcm/packetized (raw / unknown)
      if (SPF_IS_PACKETIZED_OR_PCM(ext_out_port_ptr->media_fmt.data_format))
      {
         ext_buf_delay_us = cu_aggregate_ext_out_port_delay(base_ptr, gu_ext_out_port_ptr);
      }
      else
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "CMD:PATH_DELAY: path delay query not including delay of ext port data format is %d. ",
                ext_out_port_ptr->media_fmt.data_format);
      }
   }

   apm_offload_graph_path_defn_for_delay_t get_path_delay;

   get_path_delay.src_module_instance_id = src_path_mid.module_instance_id;
   get_path_delay.src_port_id            = src_path_mid.port_id;
   get_path_delay.dst_module_instance_id = dst_path_mid.module_instance_id;
   get_path_delay.dst_port_id            = dst_path_mid.port_id;
   get_path_delay.is_client_query        = cmd_ptr->is_one_time_query;
   get_path_delay.delay_us               = 0;
   get_path_delay.get_sat_path_id        = 0;

   TRY(result,
       sgm_handle_set_get_path_delay_cfg(&me_ptr->spgm_info,
                                         (uint8_t *)&get_path_delay,
                                         (uint8_t *)param_payload_ptr,
                                         param_size,
                                         sec_opcode,
                                         cmd_extn_ptr));
   bool_t wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GET_CFG);
   if (TRUE == wait_for_response)
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GET_CFG, &me_ptr->cu.cmd_msg);
      cmd_extn_ptr->pending_resp_counter++;
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   // clang-format off
   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {

   }

   if (cmd_ptr)
   {
      if (cmd_ptr->delay_us_ptr)
      {
         *cmd_ptr->delay_us_ptr = (delay_us + ext_buf_delay_us);

         if (!cmd_ptr->is_one_time_query)
         {
        	 if(delay_info_ptr)
        	 {
				 delay_info_ptr->module_delay  = delay_us;
				 delay_info_ptr->ext_buf_delay = ext_buf_delay_us;
        	 }
         }
      }

      //PRINT_PATH_DELAY(base_ptr, cmd_ptr->path_id, delay_us, ext_buf_delay_us);
   }

   // clang-format on
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:PATH_DELAY: Done Executing path delay cfg cmd, current channel mask=0x%x. result=0x%lx",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t olc_cu_data_port_mf_rtm_enable( int8_t *  param_payload_ptr, apm_module_param_data_t * param_data_ptr, cu_base_t* base_ptr)
{
   ar_result_t result = AR_EOK;
   if ((param_data_ptr->param_size) < (sizeof(cntr_port_mf_param_data_cfg_t)))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "cu_cntr_get_data_port_media_fmt();Wrong payload size %lu ",
             param_data_ptr->param_size);
   }
   cntr_port_mf_param_data_cfg_t *cmd_ptr = NULL;

   cmd_ptr = (cntr_port_mf_param_data_cfg_t *)param_payload_ptr;

   if (!cmd_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "cmd_ptr is NULL");
   }
   else
   {
         base_ptr->topo_vtbl_ptr->rtm_dump_data_port_media_fmt(base_ptr->topo_ptr, base_ptr->gu_ptr->container_instance_id,cmd_ptr->enable);
   }
   return result;
}

ar_result_t olc_process_container_set_get_cfg(cu_base_t *                       base_ptr,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr,
                                              uint32_t                          msg_opcode)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   olc_t *                       me_ptr            = (olc_t *)base_ptr;
   apm_module_param_data_t *     param_data_ptr    = NULL;
   int8_t *                      param_payload_ptr = NULL;
   spf_msg_cmd_param_data_cfg_t *cntr_cfg_cmd_ptr  = NULL;

   VERIFY(result, (NULL != base_ptr) && (NULL != cmd_extn_ptr));
   me_ptr           = (olc_t *)base_ptr;
   log_id           = me_ptr->topo.gu.log_id;
   cntr_cfg_cmd_ptr = cmd_extn_ptr->cntr_cfg_cmd_ptr;
   VERIFY(result, (NULL != cntr_cfg_cmd_ptr));

   if ((SPF_MSG_CMD_SET_CFG == msg_opcode) || (SPF_MSG_CMD_GET_CFG == msg_opcode))
   {
      for (uint32_t i = 0; i < cntr_cfg_cmd_ptr->num_param_id_cfg; i++)
      {
         param_data_ptr    = (apm_module_param_data_t *)cntr_cfg_cmd_ptr->param_data_pptr[i];
         param_payload_ptr = (int8_t *)(param_data_ptr + 1);
         if (me_ptr->topo.gu.container_instance_id == param_data_ptr->module_instance_id)
         {
            switch (param_data_ptr->param_id)
            {
               case CNTR_PARAM_ID_PATH_DELAY_CFG:
               {
                  result = olc_path_delay_cfg(base_ptr, param_payload_ptr, param_data_ptr->param_size, cmd_extn_ptr);
                  param_data_ptr->error_code = result;
                  break;
               }
               case CNTR_PARAM_ID_PATH_DESTROY:
               {
                  cntr_param_id_path_destroy_t *cmd_ptr = (cntr_param_id_path_destroy_t *)param_payload_ptr;
                  if (0 == cmd_ptr->path_id) // destroy all paths
                  {
                     result = sgm_path_delay_list_destroy(&me_ptr->spgm_info, TRUE /* de-register containers */);
                  }
                  else
                  {
                     result = sgm_destroy_path(&me_ptr->spgm_info, cmd_ptr->path_id);
                  }
                  result |= cu_destroy_delay_path_cfg(base_ptr, param_payload_ptr, &param_data_ptr->param_size);
                  param_data_ptr->error_code = result;
                  break;
               }

               case CNTR_PARAM_ID_PROC_DURATION:
               {
                  if (param_data_ptr->param_size < sizeof(cntr_param_id_proc_duration_t))
                  {
                     CU_MSG(base_ptr->gu_ptr->log_id,
                            DBG_ERROR_PRIO,
                            "Wrong payload size %lu for PID 0x%lx; Min expected size == %lu",
                            param_data_ptr->param_size,
                            param_data_ptr->param_id,
                            sizeof(cntr_param_id_proc_duration_t));
                     result = AR_EFAILED;
                     break;
                  }
                  cntr_param_id_proc_duration_t *proc_dur_ptr = (cntr_param_id_proc_duration_t *)param_payload_ptr;

                  if (0 == proc_dur_ptr->proc_duration_us)
                  {
                     CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Proc Duration cannot be 0. Failing.");
                     result = AR_EFAILED;
                     break;
                  }

                  base_ptr->cntr_proc_duration                 = proc_dur_ptr->proc_duration_us;
                  base_ptr->flags.is_cntr_proc_dur_set_paramed = TRUE;
                  CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, proc_dur_change);

                  if (base_ptr->voice_info_ptr)
                  {
                     base_ptr->voice_info_ptr->safety_margin_us =
                        MAX(proc_dur_ptr->safety_margin_us, proc_dur_ptr->proc_duration_us);
                  }

                  CU_MSG(base_ptr->gu_ptr->log_id,
                         DBG_HIGH_PRIO,
                         "Container Proc Duration %lu us set on the container, safety margin %lu us (max is chosen). "
                         "voice info "
                         "0x%p",
                         base_ptr->cntr_proc_duration,
                         proc_dur_ptr->safety_margin_us,
                         base_ptr->voice_info_ptr);

                  // todo : evaluate
                  // result = base_ptr->cntr_vtbl_ptr->handle_proc_duration_change(base_ptr);

                  // result = base_ptr->cntr_vtbl_ptr->handle_thread_prio_change(base_ptr);

                  break;
               }
               case CNTR_PARAM_ID_CFG_SRC_MOD_DELAY_LIST:
               case CNTR_PARAM_ID_DESTROY_SRC_MOD_DELAY_LIST: //todo : review once again
               {
                  CU_MSG(base_ptr->gu_ptr->log_id,
                         DBG_ERROR_PRIO,
                         "Unsupported param-id 0x%lX",
                         param_data_ptr->param_id);
                  result = AR_EBADPARAM;
                  break;
               }
               case CNTR_PARAM_ID_VOICE_SESSION_INFO:
               {
                  CU_MSG(base_ptr->gu_ptr->log_id,
                         DBG_ERROR_PRIO,
                         "voice param-id 0x%lX",
                         param_data_ptr->param_id);
                  // todo : check handling
                  result = AR_EOK;
                  break;
               }
               case CNTR_PARAM_ID_GET_PROF_INFO:
               {
                  result |= gen_topo_get_prof_info(&me_ptr->topo, param_payload_ptr, &param_data_ptr->param_size);
                  param_data_ptr->error_code = result;
                  break;
               }
               case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
               {
            	   olc_cu_data_port_mf_rtm_enable(param_payload_ptr, param_data_ptr, base_ptr);
                  param_data_ptr->error_code = result;
                  break;
               }
               case CNTR_PARAM_ID_CONTAINER_PROC_PARAMS_INFO:
               {
                  result = cu_cntr_proc_params_query(base_ptr, param_payload_ptr, &param_data_ptr->param_size);
                  param_data_ptr->error_code = result;
                  break;
               }
               default:
               {
                  CU_MSG(base_ptr->gu_ptr->log_id,
                         DBG_ERROR_PRIO,
                         "Unexpected param-id 0x%lX",
                         param_data_ptr->param_id);
                  result = AR_EUNEXPECTED;
                  break;
               }
            }
         }
      }
   }
   else
   {
      // error
   }

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
   }

   return result;
}

static ar_result_t olc_determine_set_get_cfg_mode(cu_base_t *                   base_ptr,
                                                  spf_msg_cmd_param_data_cfg_t *cfg_cmd_ptr,
                                                  sgm_cfg_destn_type_t *        set_get_cfg_dest_mode_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id        = 0;
   uint32_t num_cntr_pids = 0;
   uint32_t num_sat_pids  = 0;

   olc_t *                  me_ptr         = NULL;
   apm_module_param_data_t *param_data_ptr = NULL;

   VERIFY(result, (NULL != base_ptr) && (NULL != cfg_cmd_ptr));
   me_ptr = (olc_t *)base_ptr;
   log_id = me_ptr->topo.gu.log_id;

   for (uint32_t i = 0; i < cfg_cmd_ptr->num_param_id_cfg; i++)
   {
      param_data_ptr = (apm_module_param_data_t *)cfg_cmd_ptr->param_data_pptr[i];
      if (me_ptr->topo.gu.container_instance_id == param_data_ptr->module_instance_id)
      {
         num_cntr_pids++;
      }
      else
      {
         num_sat_pids++;
      }
   }

   if ((0 < num_cntr_pids) && (0 < num_sat_pids))
   {
      *set_get_cfg_dest_mode_ptr = CFG_FOR_SATELLITE_AND_CONTAINER;
   }
   else if (0 < num_cntr_pids)
   {
      *set_get_cfg_dest_mode_ptr = CFG_FOR_CONTAINER_ONLY;
   }
   else if (0 < num_sat_pids)
   {
      *set_get_cfg_dest_mode_ptr = CFG_FOR_SATELLITE_ONLY;
   }
   else
   {
      THROW(result, AR_EUNEXPECTED);
   }

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t olc_split_set_get_cfg(cu_base_t *                       base_ptr,
                                  spf_msg_cmd_param_data_cfg_t *    cfg_cmd_ptr,
                                  spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   uint32_t log_id             = 0;
   uint32_t num_cntr_param_idx = 0;
   uint32_t num_sat_param_idx  = 0;
   uint32_t psize              = 0;
   olc_t *  me_ptr             = NULL;
   uint8_t *ptr                = NULL;

   VERIFY(result, (NULL != base_ptr) && (NULL != cfg_cmd_ptr) && (NULL != cmd_extn_ptr));
   me_ptr = (olc_t *)base_ptr;
   log_id = me_ptr->topo.gu.log_id;

   psize = (sizeof(spf_msg_cmd_param_data_cfg_t) + sizeof(uint32_t) * cfg_cmd_ptr->num_param_id_cfg);

   ptr = (uint8_t *)posal_memory_malloc(psize, base_ptr->heap_id);
   if (NULL == ptr)
   {
      result = AR_ENOMEMORY;
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "CMD:SET_GET_CFG: Failed to allocate memory for parsing satellite configuration");
      THROW(result, AR_ENOMEMORY);
   }
   memset(ptr, 0, psize);
   cmd_extn_ptr->sat_cfg_cmd_ptr                  = (spf_msg_cmd_param_data_cfg_t *)ptr;
   cmd_extn_ptr->sat_cfg_cmd_ptr->param_data_pptr = (void *)(ptr + sizeof(spf_msg_cmd_param_data_cfg_t));

   ptr = (uint8_t *)posal_memory_malloc(psize, base_ptr->heap_id);
   if (NULL == ptr)
   {
      result = AR_ENOMEMORY;
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "CMD:SET_GET_CFG: Failed to allocate memory for parsing container configuration");
      THROW(result, AR_ENOMEMORY);
   }
   memset(ptr, 0, psize);
   cmd_extn_ptr->cntr_cfg_cmd_ptr                  = (spf_msg_cmd_param_data_cfg_t *)ptr;
   cmd_extn_ptr->cntr_cfg_cmd_ptr->param_data_pptr = (void *)(ptr + sizeof(spf_msg_cmd_param_data_cfg_t));

   for (uint32_t i = 0; i < cfg_cmd_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)cfg_cmd_ptr->param_data_pptr[i];
      if (me_ptr->topo.gu.container_instance_id == param_data_ptr->module_instance_id)
      {
         cmd_extn_ptr->cntr_cfg_cmd_ptr->param_data_pptr[num_cntr_param_idx] = (void *)param_data_ptr;
         num_cntr_param_idx++;
      }
      else
      {
         cmd_extn_ptr->sat_cfg_cmd_ptr->param_data_pptr[num_sat_param_idx] = (void *)param_data_ptr;
         num_sat_param_idx++;
      }
   }

   cmd_extn_ptr->cntr_cfg_cmd_ptr->num_param_id_cfg = num_cntr_param_idx;
   cmd_extn_ptr->sat_cfg_cmd_ptr->num_param_id_cfg  = num_sat_param_idx;

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
      if (cmd_extn_ptr)
      {
         if (cmd_extn_ptr->sat_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->sat_cfg_cmd_ptr);
         }
         if (cmd_extn_ptr->cntr_cfg_cmd_ptr)
         {
            posal_memory_free(cmd_extn_ptr->cntr_cfg_cmd_ptr);
         }
      }
   }

   return result;
}

ar_result_t olc_preprocess_set_get_cfg(cu_base_t *                       base_ptr,
                                       spf_msg_cmd_param_data_cfg_t *    cfg_cmd_ptr,
                                       spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)

{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;
   olc_t *  me_ptr = NULL;

   sgm_cfg_destn_type_t set_get_cfg_destn = CFG_FOR_SATELLITE_ONLY;

   VERIFY(result, (NULL != base_ptr));
   VERIFY(result, ((NULL != cmd_extn_ptr) && (NULL != cfg_cmd_ptr)));
   me_ptr = (olc_t *)base_ptr;
   log_id = me_ptr->topo.gu.log_id;

   TRY(result, olc_determine_set_get_cfg_mode(base_ptr, cfg_cmd_ptr, &set_get_cfg_destn));

   OLC_MSG(log_id, DBG_HIGH_PRIO, "CMD:SET_CFG: set_get_cfg_destn type %lu", set_get_cfg_destn);

   cmd_extn_ptr->cfg_destn_type       = set_get_cfg_destn;
   cmd_extn_ptr->cmd_ack_done         = FALSE;
   cmd_extn_ptr->pending_resp_counter = 0;

   switch (set_get_cfg_destn)
   {
      case CFG_FOR_SATELLITE_ONLY:
      {
         cmd_extn_ptr->sat_cfg_cmd_ptr  = cfg_cmd_ptr;
         cmd_extn_ptr->cntr_cfg_cmd_ptr = NULL;
         break;
      }
      case CFG_FOR_CONTAINER_ONLY:
      {
         cmd_extn_ptr->sat_cfg_cmd_ptr  = NULL;
         cmd_extn_ptr->cntr_cfg_cmd_ptr = cfg_cmd_ptr;
         break;
      }
      case CFG_FOR_SATELLITE_AND_CONTAINER:
      {
         TRY(result, olc_split_set_get_cfg(base_ptr, cfg_cmd_ptr, cmd_extn_ptr));
         break;
      }
      default:
      {
         THROW(result, AR_EUNEXPECTED);
         break;
      }
   }

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/**
 * Handling of the control path set cfg command and get cfg command.
 */
ar_result_t olc_process_satellite_set_get_cfg(cu_base_t *                       base_ptr,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr,
                                              uint32_t                          payload_size,
                                              bool_t                            is_set_cfg_msg)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t            log_id            = 0;
   bool_t              wait_for_response = FALSE;
   bool_t              is_deregister     = FALSE;
   spf_cfg_data_type_t data_type         = SPF_CFG_DATA_TYPE_DEFAULT;
   uint32_t            opcode            = 0;
   olc_t *             me_ptr            = NULL;

   spf_msg_cmd_param_data_cfg_t *cfg_cmd_ptr = NULL;

   VERIFY(result, (NULL != base_ptr));
   VERIFY(result, (NULL != cmd_extn_ptr));
   me_ptr      = (olc_t *)base_ptr;
   cfg_cmd_ptr = cmd_extn_ptr->sat_cfg_cmd_ptr;
   log_id      = me_ptr->topo.gu.log_id;

   switch (me_ptr->cu.cmd_msg.msg_opcode)
   {
      case SPF_MSG_CMD_REGISTER_CFG:
      {
         data_type = SPF_CFG_DATA_PERSISTENT;
         opcode    = APM_CMD_REGISTER_CFG;
         break;
      }
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         data_type     = SPF_CFG_DATA_PERSISTENT;
         opcode        = APM_CMD_DEREGISTER_CFG;
         is_deregister = TRUE;
         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      {
         data_type      = SPF_CFG_DATA_TYPE_DEFAULT;
         opcode         = APM_CMD_SET_CFG;
         is_set_cfg_msg = TRUE;
         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      {
         data_type = SPF_CFG_DATA_TYPE_DEFAULT;
         opcode    = APM_CMD_GET_CFG;
         break;
      }
      default:
      {
         THROW(result, AR_EUNSUPPORTED);
         break;
      }
   }

   switch (data_type)
   {
      case SPF_CFG_DATA_TYPE_DEFAULT:
      {
         if (cfg_cmd_ptr->num_param_id_cfg > 0)
         {
            TRY(result,
                sgm_handle_set_get_cfg(&me_ptr->spgm_info,
                                       cfg_cmd_ptr,
                                       payload_size,
                                       is_set_cfg_msg,
                                       FALSE, /*is_inband*/
                                       cmd_extn_ptr));
            wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, opcode);
         }
         break;
      }

      case SPF_CFG_DATA_PERSISTENT:
      {
         void **                  param_data_pptr           = cfg_cmd_ptr->param_data_pptr;
         int8_t *                 param_data_ptr            = param_data_pptr[0];
         apm_module_param_data_t *module_param_data_ptr     = NULL;
         uint32_t                 payload_size              = 0;
         bool_t                   is_payload_mem_contiguous = TRUE;

         for (uint32_t pid_cnt = 0; pid_cnt < cfg_cmd_ptr->num_param_id_cfg; pid_cnt++)
         {
            param_data_ptr        = param_data_pptr[pid_cnt];
            module_param_data_ptr = (apm_module_param_data_t *)param_data_ptr;
            payload_size          = sizeof(apm_module_param_data_t) + ALIGN_8_BYTES(module_param_data_ptr->param_size);
            if ((pid_cnt + 1) < cfg_cmd_ptr->num_param_id_cfg)
            {
               int8_t *next_param_data_ptr = param_data_pptr[pid_cnt + 1];
               if ((param_data_ptr + payload_size) != next_param_data_ptr)
               {
                  is_payload_mem_contiguous = FALSE;
               }
            }
         }

#ifdef OLC_VERBOSE_DEBUGGING
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "is_deregister = %u", is_deregister);
#endif
         if ((1 == cfg_cmd_ptr->num_param_id_cfg) || (is_payload_mem_contiguous))
         {
            TRY(result,
                sgm_handle_persistent_cfg(&me_ptr->spgm_info,
                                          (void *)param_data_ptr,
                                          payload_size,
                                          FALSE /*is_inband*/,
                                          is_deregister,
                                          cmd_extn_ptr));
            wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, opcode);
         }
         else
         {
            TRY(result,
                sgm_handle_persistent_cfg_v2(&me_ptr->spgm_info,
                                             param_data_pptr,
                                             cfg_cmd_ptr->num_param_id_cfg,
                                             payload_size,
                                             FALSE /*is_inband*/,
                                             is_deregister,
                                             cmd_extn_ptr));
            wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, opcode);
         }

         break;
      }
      default:
      {
         THROW(result, AR_EUNSUPPORTED);
      }
   }

   if (TRUE == wait_for_response)
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, opcode, &me_ptr->cu.cmd_msg);
      cmd_extn_ptr->pending_resp_counter++;
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:SET_PARAM: executing set-cfg command for the satellite configuration, "
           "pending response count %lu, result=%lu",
           cmd_extn_ptr->pending_resp_counter,
           result);

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
   }

   return result;
}
