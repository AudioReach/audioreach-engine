/**
 * \file gen_topo_buf_mgr.h
 *
 * \brief
 *
 *     Generic topology buffer manager
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef GENERIC_TOPO_BUF_MGR_H_
#define GENERIC_TOPO_BUF_MGR_H_

#include "spf_list_utils.h"
#include "gen_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/////////////////////////////////////////////// gen_topo_internal_buffer_utils ///////////////////////////////////

/* Dont call this function directly, use gen_topo_check_get_out_buf_from_buf_mgr() instead */
ar_result_t gen_topo_check_get_out_buf_from_buf_mgr_util_(gen_topo_t *            topo_ptr,
                                                          gen_topo_module_t *     module_ptr,
                                                          gen_topo_output_port_t *curr_out_port_ptr);

/* Dont call this function directly, use gen_topo_check_get_in_buf_from_buf_mgr() instead */
ar_result_t gen_topo_check_get_in_buf_from_buf_mgr_util_(gen_topo_t *            topo_ptr,
                                                         gen_topo_input_port_t * curr_in_port_ptr,
                                                         gen_topo_output_port_t *prev_out_port_ptr);

ar_result_t gen_topo_initialize_bufs_sdata(gen_topo_t *            topo_ptr,
                                           gen_topo_common_port_t *cmn_port_ptr,
                                           uint32_t                miid,
                                           uint32_t                port_id);

/**
 * Buffer management in topo-cmn
 *
 * First module calls gen_topo_check_get_in_buf_from_buf_mgr from container
 * Other modules call gen_topo_check_get_in_buf_from_buf_mgr before copying data from prev module output
 *    - previous buf is reused unless there's already a buf at the module input.
 * All modules call gen_topo_check_get_out_buf_from_buf_mgr before calling process.
 *    - for inplace modules input buf is reused.
 * After every module process its input buffer and previous out buf are returned if
 *    empty by calling gen_topo_return_one_buf_mgr_buf.
 * Last module releases the buf from container by calling gen_topo_return_one_buf_mgr_buf
 */
/**
 * get out buf from buf mgr
 * in case of inplace out buf can be same as in buf, which should be assigned by this time.
 */
static inline ar_result_t gen_topo_check_get_out_buf_from_buf_mgr(gen_topo_t *            topo_ptr,
                                                                  gen_topo_module_t *     module_ptr,
                                                                  gen_topo_output_port_t *curr_out_port_ptr)
{
   ar_result_t result = AR_EOK;

#if SAFE_MODE
   if (0 != curr_out_port_ptr->common.port_event_new_threshold)
   {
      // thresh event is pending, propagate thresh & allocate once proper thresh is known.
      return result;
   }
#endif

   if (NULL != curr_out_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return result;
   }

   return gen_topo_check_get_out_buf_from_buf_mgr_util_(topo_ptr, module_ptr, curr_out_port_ptr);
}

/*
 * connection = prev_out_port_ptr -> curr_in_port_ptr
 */
static inline ar_result_t gen_topo_check_get_in_buf_from_buf_mgr(gen_topo_t *            topo_ptr,
                                                                 gen_topo_input_port_t * curr_in_port_ptr,
                                                                 gen_topo_output_port_t *prev_out_port_ptr)
{
   ar_result_t result = AR_EOK;

#ifdef SAFE_MODE
   // this shouldn't happen
   if (0 != curr_in_port_ptr->common.port_event_new_threshold)
   {
      // thresh event is pending, propagate thresh & allocate once proper thresh is known.
      return result;
   }
#endif

   if (NULL != curr_in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return result;
   }

   return gen_topo_check_get_in_buf_from_buf_mgr_util_(topo_ptr, curr_in_port_ptr, prev_out_port_ptr);
}

static inline void gen_topo_buf_mgr_wrapper_inc_ref_count(gen_topo_common_port_t *cmn_port_ptr)
{
   if (GEN_TOPO_BUF_ORIGIN_BUF_MGR == cmn_port_ptr->flags.buf_origin)
   {
      topo_buf_manager_element_t *wrapper_ptr =
         (topo_buf_manager_element_t *)(cmn_port_ptr->bufs_ptr[0].data_ptr - TBF_BUF_PTR_OFFSET);
      wrapper_ptr->ref_count++;
   }
}

static inline uint32_t gen_topo_buf_mgr_wrapper_get_ref_count(gen_topo_common_port_t *cmn_port_ptr)
{
   if (GEN_TOPO_BUF_ORIGIN_BUF_MGR == cmn_port_ptr->flags.buf_origin)
   {
      topo_buf_manager_element_t *wrapper_ptr =
         (topo_buf_manager_element_t *)(cmn_port_ptr->bufs_ptr[0].data_ptr - TBF_BUF_PTR_OFFSET);
      return wrapper_ptr->ref_count;
   }
   return 0;
}

static inline ar_result_t gen_topo_buf_mgr_wrapper_get_buf(gen_topo_t *topo_ptr,
                                                           gen_topo_common_port_t *cmn_port_ptr)
{
   int8_t *ptr = NULL;

   // this can happen b4 thresh/MF prop
   if (0 == cmn_port_ptr->max_buf_len)
   {
      return AR_EOK;
   }

   // internally mem needed for topo_buf_mgr_element_t is counted.
   // Also ref count is initialized to 1.
   ar_result_t result = topo_buf_manager_get_buf(topo_ptr,
                                                 &ptr,
                                                 cmn_port_ptr->max_buf_len);

   if (ptr)
   {
      cmn_port_ptr->bufs_ptr[0].data_ptr       = ptr;
      cmn_port_ptr->flags.buf_origin           = GEN_TOPO_BUF_ORIGIN_BUF_MGR;

      if (cmn_port_ptr->flags.is_pcm_unpacked)
      {
         for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
         {
            cmn_port_ptr->bufs_ptr[b].data_ptr =
               cmn_port_ptr->bufs_ptr[0].data_ptr + b * cmn_port_ptr->max_buf_len_per_buf;
            // cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
         }
         cmn_port_ptr->bufs_ptr[0].max_data_len = cmn_port_ptr->max_buf_len_per_buf;
      }
      else
      {

         for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
         {
            cmn_port_ptr->bufs_ptr[b].data_ptr =
               cmn_port_ptr->bufs_ptr[0].data_ptr + b * cmn_port_ptr->max_buf_len_per_buf;
            // cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
            cmn_port_ptr->bufs_ptr[b].max_data_len = cmn_port_ptr->max_buf_len_per_buf;
         }
      }
   }

   return result;
}

static inline void gen_topo_assign_bufs_ptr(uint32_t                log_id,
                                            gen_topo_common_port_t *dst_cmn_port_ptr,
                                            gen_topo_common_port_t *src_cmn_port_ptr,
                                            gen_topo_module_t *     dst_module_ptr,
                                            uint32_t                dst_port_id)
{
#if defined(SAFE_MODE) || defined(VERBOSE_DEBUGGING)
   /**
    * Without valid MF, bufs_num is not properly assigned. E.g. prev out and next in might have
            diff bufs_num, whereas we assume them to be same. In this case, even though we see below error we can safely
    ignore it as allocated buf is not used for modules. trigger condition won't be satisfied due to invalid MF.
    */
   if (dst_cmn_port_ptr->sdata.bufs_num != src_cmn_port_ptr->sdata.bufs_num)
   {
      TOPO_MSG(log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: Port 0x%lx, bufs num are different %lu, %lu",
               dst_module_ptr->gu.module_instance_id,
               dst_port_id,
               dst_cmn_port_ptr->sdata.bufs_num,
               src_cmn_port_ptr->sdata.bufs_num);
   }
#endif

   if (dst_cmn_port_ptr->flags.is_pcm_unpacked)
   {
      for (uint32_t b = 0; b < dst_cmn_port_ptr->sdata.bufs_num; b++)
      {
         dst_cmn_port_ptr->bufs_ptr[b].data_ptr = src_cmn_port_ptr->bufs_ptr[b].data_ptr;
      }
      // dst_cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
      dst_cmn_port_ptr->bufs_ptr[0].max_data_len = src_cmn_port_ptr->bufs_ptr[0].max_data_len;
   }
   else
   {
      for (uint32_t b = 0; b < dst_cmn_port_ptr->sdata.bufs_num; b++)
      {
         dst_cmn_port_ptr->bufs_ptr[b].data_ptr = src_cmn_port_ptr->bufs_ptr[b].data_ptr;
         // dst_cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
         dst_cmn_port_ptr->bufs_ptr[b].max_data_len = src_cmn_port_ptr->bufs_ptr[b].max_data_len;
      }
   }
}

static inline void gen_topo_buf_mgr_wrapper_dec_ref_count_return(gen_topo_t *            topo_ptr,
                                                                 uint32_t                module_inst_id,
                                                                 uint32_t                port_id,
                                                                 gen_topo_common_port_t *cmn_port_ptr)
{

#ifdef SAFE_MODE
   if ((NULL == cmn_port_ptr->bufs_ptr[0].data_ptr) || (GEN_TOPO_BUF_ORIGIN_BUF_MGR != cmn_port_ptr->flags.buf_origin))
   {
      return;
   }
#endif

   topo_buf_manager_element_t *wrapper_ptr =
      (topo_buf_manager_element_t *)(cmn_port_ptr->bufs_ptr[0].data_ptr - TBF_BUF_PTR_OFFSET);
   if (wrapper_ptr->ref_count >= 1)
   {
      wrapper_ptr->ref_count--;
   }

   if (0 == wrapper_ptr->ref_count)
   {
#ifdef BUF_MGMT_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: port id 0x%lx, return buf to buf mgr 0x%p",
               module_inst_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr);
#endif
      topo_buf_manager_return_buf(topo_ptr, cmn_port_ptr->bufs_ptr[0].data_ptr);
   }
}

/**
 * Prefer using gen_topo_check_return_one_buf_mgr_buf() for safe checks, if the callers is not checking
 * data_ptr and actual data lengths()
 *
 * here length check is not made as it is called during destroy
 *
 * For low latency cases, the buffers can be held in certain cases. If buffer needs to be
 * returned force return buf flag needs to be set to TRUE. [ cmn_port_ptr->flags.force_return_buf = TRUE ]
 */
static inline ar_result_t gen_topo_return_one_buf_mgr_buf(gen_topo_t *            topo_ptr,
                                                          gen_topo_common_port_t *cmn_port_ptr,
                                                          uint32_t                module_inst_id,
                                                          uint32_t                port_id)
{
#ifdef BUF_MGMT_DEBUG
   if (cmn_port_ptr->bufs_ptr[0].actual_data_len)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               " Module 0x%lX: port id 0x%lx, return buf with valid data %lu",
               module_inst_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].actual_data_len);
   }

   if (cmn_port_ptr->bufs_ptr[0].data_ptr)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: port id 0x%lx, return buf 0x%p , force_return_buf:%u",
               module_inst_id,
               port_id,
               cmn_port_ptr->bufs_ptr[0].data_ptr,
               cmn_port_ptr->flags.force_return_buf);
   }
#endif

   if (cmn_port_ptr->bufs_ptr[0].data_ptr && (GEN_TOPO_BUF_ORIGIN_BUF_MGR == cmn_port_ptr->flags.buf_origin))
   {
      if (TOPO_BUF_LOW_LATENCY == topo_ptr->buf_mgr.mode)
      {
         // check if buffer can be held for low latency usecases
         if (!cmn_port_ptr->flags.force_return_buf)
         {
            // Return EOK i.e hold the buffer if,
            //  1. downstream_req_data_buffering = false, if nblc end has buffer at input due to some partial data this
            //  buffer
            //     could have been borrowed from there(refer get_output_buffer() ). Buffer borrowed from nblc end cannot
            //     be held accross process cycle since partial data len can change. Ex: DTMG gen -> log -> MFC. Dtmf can
            //     borrow partial buffer from MFC and must always return at the end of process. Because max buf len is
            //     adjusted when borrowed and must be updated everytime dtmf borrows.
            //  2. IF data flow state is not at GAP, in other words if data is flowing.
            //  3. We can hold buffers only in Real time paths. In FTRT paths, the pile up can vary
            if ((cmn_port_ptr->data_flow_state != TOPO_DATA_FLOW_STATE_AT_GAP) &&
                (FALSE == cmn_port_ptr->flags.downstream_req_data_buffering) &&
                 gen_topo_is_port_in_realtime_path(cmn_port_ptr))
            {
               return AR_EOK;
            }
         }

         // reaches here if buffer cannot be held, need to return buffer to buf mgr.
         cmn_port_ptr->flags.force_return_buf = FALSE;
      }

      gen_topo_buf_mgr_wrapper_dec_ref_count_return(topo_ptr, module_inst_id, port_id, cmn_port_ptr);
   }

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF != cmn_port_ptr->flags.buf_origin)
   {
// for ext-out buffers assigned to non-ext-ports (borrowed), mark as returned (they will be assigned back with
// gen_topo_check_get_out_buf_from_buf_mgr)
//   not clearing can cause stale pointers if buf gets freed due to threshold change.
// for internal ports uisng buf-mgr buf, also, mark as null

// cmn_port_ptr->bufs_ptr[0].data_ptr = NULL;
#if 0
            cmn_port_ptr->buf.actual_data_len = 0;  //buf is empty that's the reason we are freeing
            cmn_port_ptr->buf.max_data_len    = 0;  //init'ed at get_buf
            cmn_port_ptr->flags.buf_origin    = GEN_TOPO_BUF_ORIGIN_INVALID; //init'ed at get_buf
#endif

#if 1
      cmn_port_ptr->bufs_ptr[0].data_ptr = NULL;
#else
      for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
      {
         cmn_port_ptr->bufs_ptr[b].data_ptr        = NULL;
         cmn_port_ptr->bufs_ptr[b].actual_data_len = 0; // buf is empty that's the reason we are freeing
         cmn_port_ptr->bufs_ptr[b].max_data_len    = 0;
      }
#endif
   }
   return AR_EOK;
}

// This way of writing avoids function call overhead.
static inline ar_result_t gen_topo_check_return_one_buf_mgr_buf(gen_topo_t *            topo_ptr,
                                                                gen_topo_common_port_t *cmn_port_ptr,
                                                                uint32_t                module_inst_id,
                                                                uint32_t                port_id)
{
   if ((NULL == cmn_port_ptr->bufs_ptr[0].data_ptr) || (0 != cmn_port_ptr->bufs_ptr[0].actual_data_len))
   {
      return AR_EOK;
   }
   return gen_topo_return_one_buf_mgr_buf(topo_ptr, cmn_port_ptr, module_inst_id, port_id);
}

/**
 *
 *                      -----------------                             ---------------
 *                     | previous module |                           |current module |
 * prev_in_port_ptr -> |                 | -> prev_out -> curr_in -> | (module)      | ->  curr_out
 *                      -----------------                             ---------------
 *
 * prev_in, prev_out, curr_in, curr_out all may use same or different buffers.
 *
 * the reason prev_out_port_ptr's buf cannot be released earlier is because we do a re-try final copy after module
 * process.
 *
 * Since these fns are called during process context force return is not set.
 *
 * prev_out_port_ptr is NULL for first module
 * curr_in_port_ptr  is NULL for last module
 *
 * release only if buf is empty.
 */
static inline ar_result_t gen_topo_input_port_return_buf_mgr_buf(gen_topo_t *           topo_ptr,
                                                                 gen_topo_input_port_t *curr_in_port_ptr)
{
   // return to buf mgr only if not shared with output
   return gen_topo_check_return_one_buf_mgr_buf(topo_ptr,
                                                &curr_in_port_ptr->common,
                                                curr_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                curr_in_port_ptr->gu.cmn.id);
}
static inline ar_result_t gen_topo_output_port_return_buf_mgr_buf(gen_topo_t *            topo_ptr,
                                                                  gen_topo_output_port_t *prev_out_port_ptr)
{
   return gen_topo_check_return_one_buf_mgr_buf(topo_ptr,
                                                &prev_out_port_ptr->common,
                                                prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                prev_out_port_ptr->gu.cmn.id);
}

static inline ar_result_t gen_topo_return_buf_mgr_buf_for_both(gen_topo_t *            topo_ptr,
                                                               gen_topo_input_port_t * curr_in_port_ptr,
                                                               gen_topo_output_port_t *prev_out_port_ptr)
{
   gen_topo_output_port_return_buf_mgr_buf(topo_ptr, prev_out_port_ptr);
   gen_topo_input_port_return_buf_mgr_buf(topo_ptr, curr_in_port_ptr);
   return AR_EOK;
}

static inline ar_result_t gen_topo_return_buf_mgr_buf(gen_topo_t             *topo_ptr,
                                                      gen_topo_input_port_t  *curr_in_port_ptr,
                                                      gen_topo_output_port_t *prev_out_port_ptr)
{
   if (prev_out_port_ptr)
   {
      gen_topo_output_port_return_buf_mgr_buf(topo_ptr, prev_out_port_ptr);
   }
   if (curr_in_port_ptr)
   {
      gen_topo_input_port_return_buf_mgr_buf(topo_ptr, curr_in_port_ptr);
   }
   return AR_EOK;
}

capi_err_t gen_topo_mark_buf_mgr_buffers_to_force_return(gen_topo_t *topo_ptr);
void       topo_buf_check_for_corruption_(uint32_t                log_id,
                                          uint32_t                miid,
                                          uint32_t                port_id,
                                          gen_topo_common_port_t *cmn_port_ptr);

bool_t gen_topo_is_inplace_nblc_from_ext_input(gen_topo_t            *topo_ptr,
                                               gen_topo_module_t     *module_ptr,
                                               gen_topo_input_port_t *start_in_port_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* GENERIC_TOPO_BUF_MGR_H_ */
