#ifndef THIN_TOPO_CNTR_UTILS_I_H
#define THIN_TOPO_CNTR_UTILS_I_H
/**
 * \file thin_topo_cntr_utils_i.h
 * \brief
 *     This file contains functions container internal utilties for thin topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "gen_cntr.h"
#include "thin_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/** Thin topo related functions */
ar_result_t gen_cntr_switch_from_thin_topo_to_gen_topo_during_process(gen_cntr_t *me_ptr);

#define THIN_TOPO_PRINT_PORT_BUF_INFO(m_iid, port_id, cmn_port, result, str1, str2)                                    \
   do                                                                                                                  \
   {                                                                                                                   \
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                             \
                      DBG_LOW_PRIO,                                                                                    \
                      " Module 0x%lX: " str1 " port id 0x%lx, assinged buffer " str2                                   \
                      ": length_per_buf %lu of %lu. buff addr: 0x%p, origin:%lu Flags 0x%lx",                          \
                      m_iid,                                                                                           \
                      port_id,                                                                                         \
                      cmn_port.bufs_ptr[0].actual_data_len,                                                            \
                      cmn_port.bufs_ptr[0].max_data_len,                                                               \
                      cmn_port.bufs_ptr[0].data_ptr,                                                                   \
                      cmn_port.flags.buf_origin,                                                                       \
                      cmn_port.sdata.flags.word);                                                                      \
   } while (0)

gu_module_list_t *thin_topo_get_gen_topo_module_list_ptr(gen_topo_t *topo_ptr, gu_module_t *gu_module_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef THIN_TOPO_CNTR_UTILS_I_H
