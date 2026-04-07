/*==============================================================================
  @file alsa_device_driver.h
  @brief This file contains interface for alsa device driver

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

#ifndef _ALSA_DEVICE_DRIVER_H_
#define _ALSA_DEVICE_DRIVER_H_

/* =======================================================================
                     INCLUDE FILES FOR MODULE
========================================================================== */
#include "alsa_device_api.h"
#include "alsa_device_driver_i.h"
#include "posal.h"

/*=====================================================================
  Macros
 ======================================================================*/
enum
{
   ALSA_DEVICE_SINK = 1,
   ALSA_DEVICE_SOURCE
};

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// Perform ALSA device Driver initialization
ar_result_t alsa_device_driver_init(alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver set configuration.
ar_result_t alsa_device_driver_set_cfg(alsa_device_driver_t *alsa_device_driver_ptr,
                                       param_id_hw_ep_mf_t *alsa_device_cfg_ptr);

// Perform ALSA device Driver open
ar_result_t alsa_device_driver_open(alsa_device_driver_t *alsa_device_driver_ptr,
                                    uint32_t direction);

// Perform ALSA device Driver start
ar_result_t alsa_device_driver_start(alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver prepare
ar_result_t alsa_device_driver_prepare(alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver read
ar_result_t alsa_device_driver_read(alsa_device_driver_t *alsa_device_driver_ptr,
                                    int8_t *buffer_ptr,
                                    uint32_t num_bytes);

// Perform ALSA device Driver write
ar_result_t alsa_device_driver_write(alsa_device_driver_t *alsa_device_driver_ptr,
                                     int8_t *buffer_ptr,
                                     uint32_t num_bytes);

// Perform ALSA device Driver stop
ar_result_t alsa_device_driver_stop(alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver de-initialization
ar_result_t alsa_device_driver_close(alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver set interface configuration.
ar_result_t alsa_device_driver_set_intf_cfg(param_id_alsa_device_intf_cfg_t *alsa_device_cfg_ptr,
                                            alsa_device_driver_t *alsa_device_driver_ptr);

// Perform ALSA device Driver operating frame size configuration
ar_result_t alsa_device_driver_set_frame_size_cfg(param_id_frame_size_factor_t *alsa_device_cfg_ptr,
                                                  alsa_device_driver_t *alsa_device_driver_ptr);

// Wait for DMA interrupt/buffer availability
ar_result_t alsa_device_driver_wait(alsa_device_driver_t *alsa_device_driver_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // (_ALSA_DEVICE_DRIVER_H_)
