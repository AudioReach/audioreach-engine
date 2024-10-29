/**
 *   \file capi_congestion_buf_utils.c
 *   \brief
 *        This file contains CAPI implementation of RT Proxy module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_congestion_buf_i.h"

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* Metadata is parsed for num frames each buffer to update if any specific value is sent. The default num_frames is
 * one. Non-zero actual data len can not have zero frames. */
capi_err_t capi_congestion_buf_parse_md_num_frames(capi_congestion_buf_t *me_ptr, capi_stream_data_t *input)
{
   capi_err_t result = CAPI_EOK;

   // capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)capi_ptr;

   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input;

   if (in_stream_ptr->metadata_list_ptr)
   {
      module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
      module_cmn_md_list_t *next_ptr = NULL;

      while (node_ptr)
      {
         next_ptr                = node_ptr->next_ptr;
         module_cmn_md_t *md_ptr = (module_cmn_md_t *)node_ptr->obj_ptr;

         if (MD_ID_BT_SIDEBAND_INFO == md_ptr->metadata_id)
         {
            md_bt_sideband_info_t *side_ptr = (md_bt_sideband_info_t *)&md_ptr->metadata_buf[0];
            if (side_ptr->sideband_id == COP_SIDEBAND_ID_MEDIA_HEADER_WITH_CP_NUM_FRAMES)
            {
               uint8_t *num_frames_ptr = ((uint8_t *)side_ptr->sideband_data) + 7;

               me_ptr->driver_hdl.writer_handle->num_frames_in_cur_buf = *num_frames_ptr;
               /* TODO: If zero then this is incorrect from cong buf standpoint */

               AR_MSG(DBG_HIGH_PRIO, "Num frames received %d", *num_frames_ptr);
            }
         }
         else if (MD_ID_BT_SIDEBAND_INFO_V2 == md_ptr->metadata_id)
         {
            md_bt_sideband_info_v2_t *side_ptr = (md_bt_sideband_info_v2_t *)&md_ptr->metadata_buf[0];

            uint8_t *sideband_data = (uint8_t *)(side_ptr + 1);

            if (side_ptr->sideband_id == COP_SIDEBAND_ID_MEDIA_HEADER_WITH_CP_NUM_FRAMES)
            {
               uint8_t *num_frames_ptr = (sideband_data) + 7;

               me_ptr->driver_hdl.writer_handle->num_frames_in_cur_buf = *num_frames_ptr;
               /* TODO: If zero then this is incorrect from cong buf standpoint */

               AR_MSG(DBG_HIGH_PRIO, "Num frames received %d", *num_frames_ptr);
            }
         }
         node_ptr = next_ptr;
      }
   }

   return result;
}
