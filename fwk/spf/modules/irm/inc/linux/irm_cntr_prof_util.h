/**
@file irm_cntr_prof_util.h

@brief IRM container profile util.

================================================================================
Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

#ifndef _IRM_CNTR_PROF_UTIL_H_
#define _IRM_CNTR_PROF_UTIL_H_

#include "ar_error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
#define IRM_MAX_NUM_HW_THREADS 6

#define PROF_BEFORE_PROCESS(prof_info_ptr)
#define PROF_AFTER_PROCESS(prof_info_ptr, prof_mutex)

#define IRM_PROFILE_MOD_PROCESS_SECTION(prof_info_ptr, prof_mutex, XX_CODE_SECTION_XX)        \
   do                                                                                         \
   {                                                                                          \
      XX_CODE_SECTION_XX                                                                      \
   } while (0)

#define IRM_PROFILE_MODULE_PROCESS_BEGIN(prof_info_ptr)
#define IRM_PROFILE_MODULE_PROCESS_END(prof_info_ptr, prof_mutex)

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* _IRM_CNTR_PROF_UTIL_H_ */

