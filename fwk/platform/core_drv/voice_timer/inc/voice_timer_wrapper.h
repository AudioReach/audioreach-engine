/*========================================================================
  @file voice_timer_wrapper.h
  @brief This file contains voice timer wrapper API

Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
========================================================================= */

#ifndef VOICE_TIMER_WRAPPER_H
#define VOICE_TIMER_WRAPPER_H

#include "voice_timer.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

/* -----------------------------------------------------------------------
 ** Global definitions/forward declarations
 ** ----------------------------------------------------------------------- */

/* voice timer subscribe info */
typedef struct voice_timer_subscribe_info_t
{
   posal_signal_t      signal_ptr;
   /**< This signal is created by the client and is used for signalling the client
        when time offset expires */

   uint32_t             offset_us;
   /**< Time offset in usec from hard VFR tick. Client initializes it */

   uint32_t             vfr_cycle_us;
   /**< Indicates the time duration between two VFRs. */

   uint32_t             client_id;
   /**< Indicates client ID. */

   uint32_t              direction;
   /**< Indicates direction(Tx/Rx) corresponding to the client */

   uint32_t             vsid;
   /**< Indicates Voice System ID used to device hard VFR timing. */

   posal_signal_t       resync_signal_ptr;
   /**< VFR resync signal, client has to set this null if resync signaling is not required */

   volatile uint32_t    **resync_status_pptr;
   /**< VFR resync status */

   uint32_t             vfr_mode;
   /**< Indicates the mode of VFR as defined in voice_timer api enum voice_timer_vfr_mode_t.*/

   volatile uint64_t    **avtimer_timestamp_us_pptr;
   /**< reference of timstamp parameter which would be updated with VFR timestamp for every VFR cycle */

   volatile bool_t     **first_vfr_occurred_pptr;
   /**< Indicates if first VFR has occurred after VFR subscription. Voice timer does not update this if set to NULL.*/

   volatile uint32_t   *intr_counter_ptr;
   /**< Client owns this memory. Count of how many vfr cycles have elapsed since voice timer started. This value begins at 0 and gets incremented for each
        VFR that occurs after the client's subscription. */

   uint64_t abs_vfr_timestamp;
   /**< Indicates the Qtimer TS at which the periodic soft VFR should start. */

   posal_timer_callback_func_t client_cb_fun_ptr;
   void *cb_context_ptr;
} voice_timer_subscribe_info_t;

/* voice timer unsubscribe info */
typedef struct voice_timer_unsubscribe_info_t
{
   uint32_t                 client_id;
   /**< Indicates client ID. */

   uint32_t                direction;
   /**< Indicates client direction. */

   uint32_t                 vsid;
   /**< Indicates Voice System ID used to device hard VFR timing. */

   uint32_t                 vfr_mode;
   /**< Indicates the mode of VFR as defined in voice_timer api enum voice_timer_vfr_mode_t.*/
} voice_timer_unsubscribe_info_t;

/* voice timer resync info.
   In the scenarios of CDRx start/Tx Ref/Rx Ref shift,
   this structure will be used by VCPM to indicate resync info to VTM.
 */
typedef struct voice_timer_resync_info_t
{
  uint64_t         resync_absolute_vfr_timestamp;
  /**< Indicates the timestamp to which new VFR needs to be re-aligned. */

  uint32_t           is_offset_changed;
  /**< Flag to indicate if Tx or Rx offsets are changed or not. */

  uint32_t         direction;
  /**< Indicates the direction of resync.
        values:
                1 ---- Tx
                2 ---- Rx
                3 ---- Tx_Rx
    */

  uint32_t vsid;
  /**< VSID corresponding to the voice session. */

  uint32_t vfr_mode;
  /**< VFR mode corresponding to the voice session. */

  uint32_t vfr_cycle_us;
  /** < VFR cycle duration in microseconds. */
}voice_timer_resync_info_t;

/**
  Subscribes to voice timer

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t voice_timer_wrapper_subscribe(voice_timer_subscribe_info_t *sub_info_ptr);

/**
  Unsubscribes to voice timer

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t voice_timer_wrapper_unsubscribe(voice_timer_unsubscribe_info_t *unsub_info_ptr);

/**
  Re-synchronizes the voice timer based on the shift in timing references.

  @return
  returns error code.

  @dependencies
  None.
 */
ar_result_t voice_timer_wrapper_resync(voice_timer_resync_info_t *resync_info_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef VOICE_TIMER_WRAPPER_H
