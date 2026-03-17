#ifndef SHARED_LIB_API_H
#define SHARED_LIB_API_H
/**
 * \file shared_lib_api.h
 * \brief
 *     This file contains the C APIs that are exposed to the shared libraries used for dynamic loading.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define SHARED_LIB_API__API_MAJOR_VERSION 1

/**
 * Minor version of the shared library API.
 *
 * Incremented by one for backward compatible changes.
 *
 * Version 0: initial version
 * Version 1: removed build_id in the library build property and introduced ENGG and QCOM time stamps.
 */
#define SHARED_LIB_API__API_MINOR_VERSION 1

/**
 * Macros to control visibility.
 */
#define SO_PUBLIC __attribute__((visibility("default")))
/**
 * Macros to control visibility.
 */
#define SO_PRIVATE __attribute__((visibility("hidden")))

/** Basic types */
#include "ar_defs.h"

/** Error codes */
#include "ar_error_codes.h"

/** Subset of APIs from posal */
#include "posal.h"

/** CAPI */
#include "capi.h"
#include "capi_fwk_extns_dm.h"
#include "capi_fwk_extns_ecns.h"
#include "capi_fwk_extns_multi_port_buffering.h"
#include "capi_fwk_extns_signal_triggered_module.h"
#include "capi_fwk_extns_soft_timer.h"
#include "capi_fwk_extns_sync.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "capi_fwk_extns_voice_delivery.h"
#include "capi_fwk_extns_bt_codec.h"
#include "capi_fwk_extns_pcm.h"
#include "capi_fwk_extns_container_proc_duration.h"
#include "capi_fwk_extns_container_frame_duration.h"
#include "capi_fwk_extns_thresh_cfg.h"
#include "capi_fwk_extns_island.h"
#include "capi_fwk_extns_async_signal_trigger.h"
#include "capi_fwk_extns_global_shmem_msg.h"

#include "capi_mm_error_code_converter.h"

#include "capi_intf_extn_data_port_operation.h"
#include "capi_intf_extn_imcl.h"
#include "capi_intf_extn_metadata.h"
#include "capi_intf_extn_path_delay.h"
#include "capi_intf_extn_prop_port_ds_state.h"
#include "capi_intf_extn_prop_is_rt_port_property.h"
#include "capi_intf_extn_mimo_module_process_state.h"
#include "capi_intf_extn_duty_cycling_island_mode.h"
#include "capi_intf_extn_metadata.h"
#include "capi_intf_extn_period.h"
#include "capi_intf_extn_calibration_ops.h"
#include "capi_intf_extn_stm_ts.h"
#include "capi_lib_capi_process_thread.h"
#include "capi_lib_get_capi_module.h"
#include "capi_lib_get_imc.h"
#include "capi_lib_mem_mapper.h"

#include "module_cmn_api.h"

#include <stringl.h>

/** Audio common Ops, math utils */
#include "shared_aud_cmn_lib.h"

#define SHARED_LIB_API__BUILD_PROPERTY_VERSION 0x1
/** Structure of build property of ADSP builds for version 1. */
typedef struct spf_build_property_t
{
   uint16_t build_property_version;
   /**< version of this struct. Supported values: any. */
   uint16_t api_major_version;
   /**< Major version of the shared library API. Supported values: any.*/
   uint16_t api_minor_version;
   /**< Minor version of the shared library API. Supported values: any. */
   uint32_t build_tools_version;
   /**< Build tools version xx.yy.zz is represented as uint32_t 00xxyyzz, where xx, yy, and zz are decimals.
    *    Supported values: any. */
} spf_build_property_t;

#define SHARED_LIB_API__SHARED_LIB_BUILD_PROPERTY_VERSION 0x1

/** Structure of build property of shared library builds. Lacks build_id compared to version 1 */
typedef struct shared_lib_build_property_t
{
   uint16_t build_property_version;
   /**< version of this struct. Supported values: any. */
   uint16_t api_major_version;
   /**< Major version of the shared library API. Supported values: any.*/
   uint16_t api_minor_version;
   /**< Minor version of the shared library API. Supported values: any. */
   uint16_t lib_major_version;
   /**< Major version of the shared library. Supported values: any. */
   uint16_t lib_minor_version;
   /**< Minor version of the shared library. Supported values: any. */
   uint16_t reserved;
   /**< reserved field must be set to zero. used for 32bit alignment */
   uint32_t build_tools_version;
   /**< Build tools version : xx.yy.zz is represented as uint32_t 00xxyyzz, where xx, yy, and zz are decimals.*/

} shared_lib_build_property_t;

/**
 * Build property string for symbol lookup
 */
#define SHARED_LIB_API__SHARED_LIB_BUILD_PROPERTY_STRING "shared_lib_build_property"
/**
 * Build property string for symbol lookup
 */
#define SHARED_LIB_API__SPF_BUILD_PROPERTY_STRING "spf_build_property"

#ifdef SPF_BUILD_SOS // for shared libraries

/** shared library build property */
SO_PUBLIC extern const shared_lib_build_property_t shared_lib_build_property;

/** build-id of engineering builds */
SO_PUBLIC extern const char shared_lib_engineering_build_id[];
/** build-id of qualcomm CRM builds */
SO_PUBLIC extern const char shared_lib_qualcomm_build_id[];

#else // SPF_BUILD_SOS, for main ADSP build

/** ADSP Base image library build property */
SO_PUBLIC extern const spf_build_property_t spf_build_property;

#endif // SPF_BUILD_SOS

#endif // SHARED_LIB_API_H
