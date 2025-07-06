#ifndef _APM_GPR_CMD_PARSER_H_
#define _APM_GPR_CMD_PARSER_H_

/**
 * \file apm_gpr_cmd_parser.h
 *  
 * \brief
 *     This file contains function declaration for GPR command parsing routines.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_internal.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/**
  APM Service get command Q.
  param[out] p_cmd_q: Pointer to cmdQ pointer

  return: None
 */

// ar_result_t apm_parse_cmd_payload(apm_t *apm_info_ptr);

ar_result_t apm_get_prop_data_size(uint8_t * prop_payload_ptr,
                                   uint32_t  num_prop,
                                   uint32_t *prop_list_payload_size_ptr,
                                   uint32_t  cmd_payload_size);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_GPR_CMD_PARSER_H_ */
