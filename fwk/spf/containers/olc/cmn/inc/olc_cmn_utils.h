/**
 * \file olc_cmn_utils.h
 * \brief
 *     This file contains declarations of OLC common utilities.
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "olc.h"
#include "capi.h"
#include "posal.h"
#include "spf_macros.h"
#include "shared_lib_api.h"
#include "gpr_packet.h"
#include "gpr_api_inline.h"
#include "ar_guids.h"
#include "gen_topo.h"
#include "apm_offload_mem.h"
#include "spf_sys_util.h"

#ifndef OLC_CMN_UTIL_H
#define OLC_CMN_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

//#define OLC_VERBOSE_DEBUGGING

#define OLC_SGM_ID spgm_ptr->sgm_id.log_id, spgm_ptr->sgm_id.sat_pd, spgm_ptr->sgm_id.cont_id
#define OLC_SDM_ID spgm_ptr->sgm_id.log_id, spgm_ptr->sgm_id.sat_pd, spgm_ptr->sgm_id.cont_id, port_index

#define OLC_MSG_PREFIX1 "OLC:%08lX: sat_pd[0x%lX] : cid[0x%lX]: "
#define OLC_SGM_MSG_PREFIX1 "OLC_SGM:%08lX: sat_pd[0x%lX]: cid[0x%lX]: "
#define OLC_SDM_MSG_PREFIX1 "OLC_SDM:%08lX: sat_pd[0x%lX]: cid[0x%lX]: port_index[%lu]:  "
#define OLC_MSG1(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, OLC_MSG_PREFIX1 xx_fmt, ID, ##__VA_ARGS__)
#define OLC_SGM_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, OLC_SGM_MSG_PREFIX1 xx_fmt, ID, ##__VA_ARGS__)
#define OLC_SDM_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, OLC_SDM_MSG_PREFIX1 xx_fmt, ID, ##__VA_ARGS__)

#define OLC_MSG_PREFIX "OLC:%08lX: "
#define OLC_SPGM_MSG_PREFIX "OLC_SGM:%08lX: "
#define OLC_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, OLC_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)
#define OLC_SPGM_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, OLC_SPGM_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#ifdef SIM
#define OLC_SIM_DEBUG(dbg_log, msg, ...)                                                                               \
   do                                                                                                                  \
   {                                                                                                                   \
      AR_MSG(DBG_LOW_PRIO, OLC_MSG_PREFIX msg, dbg_log, ##__VA_ARGS__);                                                \
   } while (0)

#else
#define OLC_SIM_DEBUG(dbg_log, msg, ...)                                                                               \
   do                                                                                                                  \
   {                                                                                                                   \
   } while (0)
#endif

#ifdef SIM
#define OLC_SPGM_SIM_DEBUG(dbg_log, msg, ...)                                                                          \
   do                                                                                                                  \
   {                                                                                                                   \
      AR_MSG(DBG_LOW_PRIO, OLC_SPGM_MSG_PREFIX msg, dbg_log, ##__VA_ARGS__);                                           \
   } while (0)

#else
#define OLC_SIM_DEBUG(dbg_log, msg, ...)                                                                               \
   do                                                                                                                  \
   {                                                                                                                   \
   } while (0)
#endif

#define OLC_ENABLE_CMD_LEVEL_MSG 1
#define OLC_ENABLE_CMD_RSP_LEVEL_MSG 1
#define OLC_ENABLE_DATA_FLOW_LEVEL_MSG 1
#define OLC_ENABLE_METADATA_FLOW_LEVEL_MSG 1

#define SGM_ENABLE_INIT_LEVEL_MSG 1
#define SGM_ENABLE_CMD_LEVEL_MSG 1
#define SGM_ENABLE_CMD_RSP_LEVEL_MSG 1
#define SGM_ENABLE_EVENT_HNDLR_LEVEL_MSG 1
#define SGM_ENABLE_DATA_PORT_INIT_LEVEL_MSG 1
//#define SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG   1
//#define SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG    1
//#define SGM_ENABLE_METADATA_FLOW_LEVEL_MSG     1
#define SGM_ENABLE_EOS_HANLDLING_MSG 1
#define SGM_ENABLE_STATE_PROPAGATION_MSG 1
//#define SGM_ENABLE_DATA_RSP_LEVEL_MSG          1

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_CMN_UTIL_H
