#ifndef PT_CNTR_I_H
#define PT_CNTR_I_H

/**
 * \file pt_cntr.h
 *
 * \brief
 *     Container internal utitily functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_cmn_utils.h"
#include "spf_utils.h"
#include "container_utils.h"

#include "gen_cntr_i.h"
#include "pt_cntr.h"

// enable static to get accurate savings.
#define PT_CNTR_STATIC static

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
Functions
========================================================================== */

/* =======================================================================
Static Functions
========================================================================== */

/**
 * events raised at process-call by the modules
 * framework addresses the events and calls the modules again.
 */
static inline bool_t pt_cntr_check_if_module_raised_events(gen_topo_t *topo_ptr)
{
   // kpps, bw, rt change can be handled at the end of topo processing. but media fmt, threshold events need
   // to be handled in b/w module processing also.
   // process state - we must break at this module and call the module again to avoid buffering in nblc.
   return (((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word);
}

/**
 * Gets the sorted module list ptr for a given module instance id.
 */
static inline gu_module_list_t *pt_cntr_get_gu_sorted_list_ptr(gen_topo_t *topo_ptr, pt_cntr_module_t *module_ptr)
{

   for (gu_module_list_t *sorted_module_list_ptr = topo_ptr->gu.sorted_module_list_ptr; NULL != sorted_module_list_ptr;
        LIST_ADVANCE(sorted_module_list_ptr))
   {
      pt_cntr_module_t *temp_module_ptr = (pt_cntr_module_t *)sorted_module_list_ptr->module_ptr;

      if (module_ptr && (module_ptr == temp_module_ptr))
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Found gu list ptr 0x%lx for the module 0x%lx",
                      sorted_module_list_ptr,
                      module_ptr->gc.topo.gu.module_instance_id);
#endif
         return sorted_module_list_ptr;
      }
   }

   return NULL;
}

/**
 * events raised at process-call by the modules
 * framework addresses the events and calls the modules again.
 */
static inline bool_t pt_cntr_any_events_raised_by_module(gen_topo_t *topo_ptr)
{
   // kpps, bw, rt change can be handled at the end of topo processing. but media fmt, threshold events need
   // to be handled in b/w module processing also.
   // process state - we must break at this module and call the module again to avoid buffering in nblc.
   return ((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word ? TRUE : FALSE;
}

/**
 * events raised at process-call by the modules
 * framework addresses the events and calls the modules again.
 *
 */
static inline bool_t pt_cntr_any_process_call_events(gen_topo_t *topo_ptr)
{
   // kpps, bw, rt change can be handled at the end of topo processing. but media fmt, threshold events need
   // to be handled in b/w module processing also.
   // process state - we must break at this module and call the module again to avoid buffering in nblc.
   return (((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word &
           (GT_CAPI_EVENT_PORT_THRESH_BIT_MASK | GT_CAPI_EVENT_MEDIA_FMT_BIT_MASK));
}

/**
 * when inplace nblc end is assigned as pointer to ext-in port, it may not be 4 byte aligned.
 */
#define FEF_PRINT_BUF_INFO_ALL_CHS_FLAG (FALSE)

#define FEF_PRINT_PORT_INFO_AT_PROCESS(m_iid, port_id, sdata_ptr, result, str1, str2, origin)                          \
   do                                                                                                                  \
   {                                                                                                                   \
      uint32_t num_chs_to_print = FEF_PRINT_BUF_INFO_ALL_CHS_FLAG ? sdata_ptr->bufs_num : 1;                           \
      for (uint32_t i = 0; i < num_chs_to_print; i++)                                                                  \
      {                                                                                                                \
         if (sdata_ptr->buf_ptr[i].data_ptr)                                                                           \
         {                                                                                                             \
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                       \
                            DBG_HIGH_PRIO,                                                                             \
                            " Module 0x%lX: " str1 " port id 0x%lx, process ch[%lu]" str2                              \
                            ": length_per_buf %lu of %lu. buff addr: 0x%p, origin: %lu result 0x%lx",                  \
                            m_iid,                                                                                     \
                            port_id,                                                                                   \
                            i,                                                                                         \
                            sdata_ptr->buf_ptr[i].actual_data_len,                                                     \
                            sdata_ptr->buf_ptr[i].max_data_len,                                                        \
                            sdata_ptr->buf_ptr[i].data_ptr,                                                            \
                            origin,                                                                                    \
                            result);                                                                                   \
            uint64_t temp_num = (uint64_t)sdata_ptr->buf_ptr[0].data_ptr;                                              \
            if (!(temp_num & 0xF))                                                                                     \
            {                                                                                                          \
               uint32_t *data_ptr = (uint32_t *)sdata_ptr->buf_ptr[0].data_ptr;                                        \
                                                                                                                       \
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                    \
                               DBG_HIGH_PRIO,                                                                          \
                               " Module 0x%lX: " str1 " bytes (u32): %0lX %0lX %0lX %0lX  %0lX %0lX %0lX",             \
                               m_iid,                                                                                  \
                               *(data_ptr),                                                                            \
                               *(data_ptr + 1),                                                                        \
                               *(data_ptr + 2),                                                                        \
                               *(data_ptr + 3),                                                                        \
                               *(data_ptr + 4),                                                                        \
                               *(data_ptr + 5),                                                                        \
                               *(data_ptr + 6));                                                                       \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
               uint8_t *data_ptr = (uint8_t *)sdata_ptr->buf_ptr[0].data_ptr;                                          \
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                    \
                               DBG_HIGH_PRIO,                                                                          \
                               " Module 0x%lX: " str1 " bytes (u8): %0X %0X %0X %0X  %0X %0X %0X",                     \
                               m_iid,                                                                                  \
                               *(data_ptr),                                                                            \
                               *(data_ptr + 1),                                                                        \
                               *(data_ptr + 2),                                                                        \
                               *(data_ptr + 3),                                                                        \
                               *(data_ptr + 4),                                                                        \
                               *(data_ptr + 5),                                                                        \
                               *(data_ptr + 6));                                                                       \
            }                                                                                                          \
         }                                                                                                             \
      }                                                                                                                \
                                                                                                                       \
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                             \
                      DBG_HIGH_PRIO,                                                                                   \
                      " Module 0x%lX: " str1 " timestamp: %ld (0x%lx%lx), Flags0x%lX",                                 \
                      m_iid,                                                                                           \
                      (uint32_t)sdata_ptr->timestamp,                                                                  \
                      (uint32_t)(sdata_ptr->timestamp >> 32),                                                          \
                      (uint32_t)sdata_ptr->timestamp,                                                                  \
                      sdata_ptr->flags.word);                                                                          \
   } while (0)

static inline void pt_cntr_set_bufs_actual_len_to_zero(capi_stream_data_v2_t *sdata_ptr)
{
   for (uint32_t i = 0; i < sdata_ptr->bufs_num; i++)
   {
      sdata_ptr->buf_ptr[i].actual_data_len = 0;
   }
}

static inline void pt_cntr_ext_input_return_buffer(uint32_t log_id, pt_cntr_ext_in_port_t *ext_in_port_ptr)
{
#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(log_id,
                DBG_LOW_PRIO,
                "(miid,port-id) (0x%lX, 0x%lx) : Returning ext input data msg buffer 0x%lx, opcode 0x%x",
                ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gc.gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->gc.cu.input_data_q_msg.msg_opcode);
#endif
   spf_msg_ack_msg(&ext_in_port_ptr->gc.cu.input_data_q_msg, AR_EOK);
   ext_in_port_ptr->gc.cu.input_data_q_msg.payload_ptr = NULL;
}

static inline bool_t pt_cntr_is_inplace_or_disabled_siso(pt_cntr_module_t *module_ptr)
{
   return ((module_ptr->gc.topo.flags.inplace ||
            (module_ptr->gc.topo.bypass_ptr && (module_ptr->gc.topo.num_proc_loops == 1))) &&
           (module_ptr->gc.topo.gu.num_input_ports == 1) && (module_ptr->gc.topo.gu.num_output_ports == 1));
}

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // PT_CNTR_I_H
