/**
 * \file capi_splitter_island.c
 * \brief
 *     Source file to implement the CAPI Interface for Simple Splitter (SPLITTER) Module.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_splitter.h"
#include "capi_splitter_utils.h"
#include "spf_list_utils.h"
#include "posal_timer.h"

static capi_vtbl_t vtbl = { capi_splitter_process,        capi_splitter_end,
                            capi_splitter_set_param,      capi_splitter_get_param,
                            capi_splitter_set_properties, capi_splitter_get_properties };

capi_vtbl_t *capi_splitter_get_vtbl()
{
   return &vtbl;
}

// Note: this function implementation implicitly assumes that MD configuration was received
bool_t capi_splitter_is_md_blcoked_wl(uint32_t *wl_arr_ptr, uint32_t num_metadata, uint32_t md_id)
{
   if ((NULL == wl_arr_ptr) || (0 == num_metadata))
   {
      return TRUE; // MD is disabled if WL is NULL => eos is also disabled
   }

   for (uint32_t i = 0; i < num_metadata; i++)
   {
      if (md_id == wl_arr_ptr[i])
      {
         return FALSE; // MD is there in WL => not disabled
      }
   }
   return TRUE; //  if WL doesn't have MD, it is disabled
}

bool_t capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info(capi_splitter_t *me_ptr,
                                                              uint32_t         port_id,
                                                              uint32_t **      wl_ptr,
                                                              uint32_t *       num_md_ptr)
{
   *wl_ptr     = NULL;
   *num_md_ptr = 0;

   uint32_t data_len = sizeof(param_id_splitter_metadata_prop_cfg_t);
   int8_t * data_ptr = (int8_t *)me_ptr->out_port_md_prop_cfg_ptr;

   for (uint32_t i = 0; i < me_ptr->out_port_md_prop_cfg_ptr->num_ports; i++)
   {
      per_port_md_cfg_t *per_port_md_cfg_ptr = (per_port_md_cfg_t *)(data_ptr + data_len);

      if (port_id == per_port_md_cfg_ptr->port_id)
      {
         *wl_ptr     = (per_port_md_cfg_ptr->num_white_listed_md) ? (uint32_t *)(per_port_md_cfg_ptr + 1) : NULL;
         *num_md_ptr = per_port_md_cfg_ptr->num_white_listed_md;

#ifdef SPLITTER_DBG_LOW
         AR_MSG(DBG_HIGH_PRIO,
                "capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info: Returning True for Port ID %lu, idx = %lu",
                port_id,
                i);
#endif // SPLITTER_DBG_LOW
         return TRUE;
      }

      data_len += sizeof(per_port_md_cfg_t) + (per_port_md_cfg_ptr->num_white_listed_md * sizeof(uint32_t));
   }

#ifdef SPLITTER_DBG_LOW
   AR_MSG(DBG_HIGH_PRIO,
          "capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info: Returning FALSE for Port ID %lu",
          port_id);
#endif // SPLITTER_DBG_LOW
   return FALSE;
}

static bool_t is_blocking_md(capi_splitter_t *me_ptr, uint32_t out_port_index, uint32_t md_id)
{
   uint32_t *wl_arr = NULL, num_metadata = 0;

   if (!me_ptr->out_port_md_prop_cfg_ptr)
   {
      return FALSE;
   }

   if (me_ptr->out_port_state_arr[out_port_index].flags.is_all_md_blocked)
   {
      return TRUE;
   }

   if (TRUE ==
       capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info(me_ptr,
                                                              me_ptr->out_port_state_arr[out_port_index].port_id,
                                                              &wl_arr,
                                                              &num_metadata))
   {
      return capi_splitter_is_md_blcoked_wl((uint32_t *)wl_arr, num_metadata, md_id);
   }

   return FALSE;
}

static capi_err_t handle_metadata(capi_splitter_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;
   /*
    * Since splitter has no delay and doesn't prioritize the output ports in any order,
    * we need to set the bit corresponding to uniform order eos
    *
    * No algo delay in splitter, hence no need to update sample_offset
    *
    * a) send eos only on ports with is_eos_disable = FALSE.
    * b) clone if it's not the first one
    * c) if only one out port exists and it has is_eos_disable = TRUE, then destroy EOS
    */

   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[0];

   module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
   module_cmn_md_list_t *next_ptr = NULL;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      // need to detach node, or else, if spf_list_merge_lists is called, it will cause merging multiple times
      //
      // Err e.g. first call intending to merge only C, but merges D as well :
      //                       spf_list_merge_lists (A -> B, "C" -> D) results in A -> B -> C -> D
      //          second call: spf_list_merge_lists (A -> B -> C -> D, D) results in A -> B -> C -> D -> D
      node_ptr->next_ptr = NULL;
      node_ptr->prev_ptr = NULL;

      module_cmn_md_t *    md_ptr            = (module_cmn_md_t *)node_ptr->obj_ptr;
      module_cmn_md_eos_t *eos_md_ptr        = NULL;
      uint32_t             active_port_count = 0;
      bool_t               eos_present       = FALSE;
      bool_t               flush_eos_present = FALSE;
      bool_t               is_internal_eos   = FALSE;
      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         bool_t out_of_band = md_ptr->metadata_flag.is_out_of_band;
         if (out_of_band)
         {
            eos_md_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            eos_md_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }

         eos_present       = TRUE;
         flush_eos_present = eos_md_ptr->flags.is_flushing_eos;

         // was the EOS introduced due to upstream gap (stop/flush)?
         is_internal_eos = eos_md_ptr->flags.is_internal_eos;

         AR_MSG_ISLAND(DBG_LOW_PRIO,
                        "capi_splitter: EOS metadata found 0x%p, flush_eos_present %u, is_internal_eos %u",
                        eos_md_ptr,
                        flush_eos_present,
                        is_internal_eos);
      }
      else
      {

#ifdef SPLITTER_DBG_LOW
         AR_MSG_ISLAND(DBG_LOW_PRIO, "capi_splitter: metadata 0x%lx ", md_ptr->metadata_id);
#endif
      }

      for (uint32_t j = 0; j < me_ptr->num_out_ports; j++)
      {
         if ((DATA_PORT_STATE_STARTED != me_ptr->out_port_state_arr[j].state) || (NULL == output[j]) ||
             (NULL == output[j]->buf_ptr) || (NULL == output[j]->buf_ptr[0].data_ptr))
         {
            // port is closed or inactive
            continue;
         }

#ifdef SPLITTER_DBG_LOW
         AR_MSG_ISLAND(DBG_LOW_PRIO, "capi_splitter: active_port_count %lu", active_port_count);
#endif
         capi_stream_data_v2_t *stream_ptr = (capi_stream_data_v2_t *)output[j];
         bool_t propagate_md = TRUE;

         if (flush_eos_present)
         {
            stream_ptr->flags.marker_eos = TRUE;
         }

         if (eos_present)
         {
            if ((FALSE == is_internal_eos) && (FALSE == me_ptr->out_port_state_arr[j].flags.is_eos_disable))
            {
               eos_md_ptr->flags.is_internal_eos = FALSE;
               // propagate this as false if:
               // 1> External EOS without WL configured
               // 2> external eos with WL configured and allowing EOS in WL
               // both the above cases are handled by the is_eos_disable flag
               AR_MSG_ISLAND(DBG_LOW_PRIO, "capi_splitter: propagating as external eos on port %lu", j);
            }
            else
            {
               eos_md_ptr->flags.is_internal_eos = TRUE;
               // propagate this as true in all other cases
               // 1> internal eos
               // 2> external eos with WL (configured) and blocking
               // Container takes care of destroying internal non-flushing EOSes when it copies metadata b/w ports.
               AR_MSG_ISLAND(DBG_LOW_PRIO, "capi_splitter: propagating as internal eos on port %lu", j);
            }
         }
         else
         {
            propagate_md = !is_blocking_md(me_ptr, j, md_ptr->metadata_id);
         }

         if (propagate_md)
         {
            active_port_count++;

            if (1 == active_port_count)
            {
               // note: second arg to spf_list_merge_lists is not input stream->metadata_list_ptr because, we don't
               // want to clear it.
               module_cmn_md_list_t *temp_node_ptr = node_ptr;
               spf_list_merge_lists((spf_list_node_t **)&stream_ptr->metadata_list_ptr,
                                    (spf_list_node_t **)&temp_node_ptr);
            }
            else
            {
               result = me_ptr->metadata_handler.metadata_clone(me_ptr->metadata_handler.context_ptr,
                                                                node_ptr->obj_ptr,
                                                                &stream_ptr->metadata_list_ptr,
                                                                me_ptr->heap_mem);
               if (CAPI_FAILED(result))
               {
                  AR_MSG_ISLAND(DBG_LOW_PRIO,
                                 "capi_splitter:cloning metadata ID 0x%x failed %lx",
                                 md_ptr->metadata_id,
                                 result);
                  break;
               }
            }
         }
      }

      if (0 == active_port_count)
      {
         // if no active output ports, EOS/MD must be dropped.
         // calling destroy here otherwise MD(EOS) gets stuck.
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   TRUE /* dropped */,
                                                   &in_stream_ptr->metadata_list_ptr);
      }
      node_ptr = next_ptr;
   }

   in_stream_ptr->flags.marker_eos  = FALSE; // always clear flush-eos flag since we propagated to output.
   in_stream_ptr->metadata_list_ptr = NULL;

   return result;
}
/*------------------------------------------------------------------------
  Function name: capi_splitter_process
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t       result = CAPI_EOK;
   capi_splitter_t *me_ptr = (capi_splitter_t *)_pif;
   uint32_t         data_len;

   POSAL_ASSERT(_pif);
   POSAL_ASSERT(input[0]);
   POSAL_ASSERT(output[0]);

   if (!me_ptr->flags.is_in_media_fmt_set)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Input Media format not set yet");
      return CAPI_EFAILED;
   }

   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[0];
   if (in_stream_ptr->metadata_list_ptr)
   {
      result = handle_metadata(me_ptr, input, output);
   }

   data_len = input[0]->buf_ptr[0].actual_data_len;
   // each ch buffer on the in port
   for (uint32_t i = 0; i < input[0]->bufs_num; i++)
   {
      for (uint32_t j = 0; j < me_ptr->num_out_ports; j++)
      {
         // Due to fwk optimization, proceed only when first channel data pointer is non NULL.
         if (DATA_PORT_STATE_STARTED != me_ptr->out_port_state_arr[j].state || (NULL == output[j]) ||
             (NULL == output[j]->buf_ptr) || (NULL == output[j]->buf_ptr[i].data_ptr) ||
             (NULL == output[j]->buf_ptr[0].data_ptr) )
         {
            continue;
         }
         // copy all words but EOS (EOS was already copied conditionally in handle_metadata)
         bool_t eos_flag = output[j]->flags.marker_eos;
         output[j]->flags.word |= input[0]->flags.word;
         output[j]->flags.marker_eos = eos_flag;

         if (SPLITTER_OUT_PORT_DEFAULT_TS_PROPAGATION == me_ptr->out_port_state_arr[j].flags.ts_cfg)
         {
            output[j]->timestamp = input[0]->timestamp;
         }
         else if (SPLITTER_OUT_PORT_STM_TS_PROPAGATION == me_ptr->out_port_state_arr[j].flags.ts_cfg)
         {
             if (me_ptr->ts_payload.ts_ptr)
        	 {
                 output[j]->flags.is_timestamp_valid = me_ptr->ts_payload.ts_ptr->is_valid;
                 output[j]->timestamp = me_ptr->ts_payload.ts_ptr->timestamp;
        	 }

        	 else
        	 {
        		 output[j]->flags.is_timestamp_valid = FALSE;
        	 }
         }
         else
         {
            // TODO: In signal triggered container, fwk is asignign timestamp for the buffers where it is not valid.
            // this should be removed so that timestamp can remain invalid.
            output[j]->flags.is_timestamp_valid = FALSE;
         }
#ifdef SPLITTER_DBG_LOW
         AR_MSG_ISLAND(DBG_HIGH_PRIO,
                       "input_ts_lsb %lu, output_port_index %lu, output_ts_lsb %lu, is_valid %lu",
                       (uint32_t)input[0]->timestamp,
                       j,
                       (uint32_t)output[j]->timestamp,
                       output[j]->flags.is_timestamp_valid);
#endif

         // if input MF is unpacked v2 read/update lengths only for the first ch's buffer
         uint32_t actual_data_len;
         uint32_t max_data_len_per_buf = output[j]->buf_ptr[0].max_data_len;
         if (input[0]->buf_ptr[i].data_ptr == output[j]->buf_ptr[i].data_ptr)
         {
            actual_data_len = MIN(max_data_len_per_buf, data_len);
         }
         else
         {
            actual_data_len =
               memscpy(output[j]->buf_ptr[i].data_ptr, max_data_len_per_buf, input[0]->buf_ptr[i].data_ptr, data_len);
         }

         // for unpacked v2 read/update buf lengths only for the first ch
         if (0 == i)
         {
            output[j]->buf_ptr[0].actual_data_len = actual_data_len;
         }
      }
   }

   // clear the flags once we propagate to the outputs.
   input[0]->flags.end_of_frame = FALSE;

   return result;
}
