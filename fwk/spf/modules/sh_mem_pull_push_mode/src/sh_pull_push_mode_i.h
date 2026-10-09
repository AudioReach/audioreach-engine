/* ======================================================================== */
/**
  @file sh_pull_push_mode_i.h
  @brief This file contains function declarations internal to CAPI
         Pull and Push mode module

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
#ifndef SH_PULL_PUSH_MODE_I_H
#define SH_PULL_PUSH_MODE_I_H

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "ar_defs.h"
#include "apm_container_api.h"
#include "module_cmn_api.h"
#include "media_fmt_api.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

#define EVENT_ID_SH_MEM_PULL_PUSH_MODE_BUFFER_LEVEL 0x08001BF5

 /** @ingroup ar_spf_mod_ep_shmempp_mods
     Payload for configuring the #EVENT_ID_SH_MEM_PULL_PUSH_MODE_BUFFER_LEVEL event.
     Event configuration structure sent by the client to the module to set the buffer level.
     Bytes set as the buffer level will be used by the module to decide intervals for raising event to the client.
     Module will raise event to the client whenever shared buffer is accessed by the number of bytes specified here.
 */
 #include "spf_begin_pack.h"
 struct event_cfg_sh_mem_pull_push_mode_buffer_level_t
 {
    uint32_t buffer_level_bytes;
    /**< Buffer level interval in the circular buffer, in bytes.

         The module will raise buffer level event at regular intervals based on this value. */

    /*#< @h2xmle_description {Buffer level interval in bytes.
                              The module will raise events at regular intervals based on this value.}
         @h2xmle_policy      {Basic}
         @h2xmle_default     {0}
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_policy      {Basic} */
 }
 #include "spf_end_pack.h"
 ;
 typedef struct event_cfg_sh_mem_pull_push_mode_buffer_level_t event_cfg_sh_mem_pull_push_mode_buffer_level_t;


 /** @ingroup ar_spf_mod_ep_shmempp_mods
     Payload of the #EVENT_ID_SH_MEM_PULL_PUSH_MODE_BUFFER_LEVEL event.
     This structure is sent by the module to the client when amount of data read/written
     into the buffer crosses the buffer level mark set by the client.
 */
 #include "spf_begin_pack.h"
 struct event_sh_mem_pull_push_mode_buffer_level_t
 {
    uint32_t buffer_level_bytes;
    /**< The number of bytes read/written from/to the buffer since the last event.

         When data in the buffer reaches this level, the module informs the
         client by raising a buffer level event. @newpagetable */

    /*#< h2xmle_description {Buffer level in the circular buffer in bytes.
                             When data in the buffer reaches this level, the
                             module informs the client by raising a buffer level
                             event.}
         h2xmle_policy      {Basic}
         h2xmle_default     {0}
         h2xmle_range       {0..0xFFFFFFFF}
         h2xmle_policy      {Basic} */
 }
 #include "spf_end_pack.h"
 ;
 typedef struct event_sh_mem_pull_push_mode_buffer_level_t event_sh_mem_pull_push_mode_buffer_level_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* SH_PULL_PUSH_MODE_I_H */
