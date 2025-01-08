/**==============================================================================
  @file voice_timer.h
  @brief This file contains declarations of voice timer API

Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

#ifndef VOICE_TIMER_H
#define VOICE_TIMER_H

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "posal.h"
#include "spf_cmn_if.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#if USES_AUDIO_IN_ISLAND
#define VOICE_TIMER_HEAP_ID POSAL_HEAP_DEFAULT
#else
#define VOICE_TIMER_HEAP_ID POSAL_HEAP_DEFAULT
#endif

/* -----------------------------------------------------------------------
** Global definitions/forward declarations
** ----------------------------------------------------------------------- */
/* Macro for maximum number of timers/clients allowed per VFR source*/
#define VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE       8

/* Macro for total number of timer/clients allowed across all VFR sources
   Limiting to 32 until new requirements come in. Upto 32 is easy to
   manage due to availability of bit manipulation intrinsics for 32-bit.
   If the requirement goes beyond 32 then some other logic needs to be used.
   Common pool method for one-shot timers is used to avoid malloc and free
   during run-time.*/
#define VOICE_TIMER_MAX_TIMERS_TOTAL                32

#define NUM_VFR_SOURCES                              4

#define MAX_NUM_OF_VOICE_SESSIONS                    2

#define VOICE_TIMER_MSG_PREFIX "voice_timer: "

/* VFR ID of source 1 */
#define VFR_ID_1                                    0

/* VFR ID of source 2 */
#define VFR_ID_2                                    7

/* Default VFR ID */
#define VFR_ID_DEFAULT                              0xF

/** Gets the first high bit starting from LSB */
#define voice_timer_tst_bit(mask, bitnum)      Q6_p_tstbit_RR(mask, bitnum)

/** Sets a particular bit */
#define voice_timer_set_bit(mask, bitnum)      Q6_R_setbit_RR(mask, bitnum)

/** Toggles a particular bit */
#define voice_timer_toggle_bit(mask, bitnum)   Q6_R_togglebit_RR(mask, bitnum)

/** Gets the most significant bit signal position */
#define voice_timer_get_signal_pos(mask)  Q6_R_cl0_R(mask)

/* Global handle to voice timer */
extern spf_handle_t *voice_timer_handle;

/* Valid VFR mode values */
typedef enum
{
   VFR_SOFT = 0,       /**< VFR mode is Soft. (PS calls) */
   VFR_HARD,           /**< VFR mode is Hard. (CS calls)*/
   VFR_MODE_END        /**< Maximum value; indicates the end of the valid modes. */
} voice_vfr_mode_t;

/* Valid VFR sources */
enum
{
   VFR_SRC_1 = 0,
   VFR_SRC_2,
   VFR_SRC_3,
   VFR_SRC_4,
   VFR_SRC_END
};

/* Directions of VTM clients*/
typedef enum
{
  TX_DIR = 1,
  RX_DIR,
  TX_RX_DIR
}vtimer_client_dir_t;

/* Valid voice session indices */
enum
{
   VOICE_SESSION_1 = 0, /**< Index corresponding to VSID: 11C05000 */
   VOICE_SESSION_2      /**< Index corresponding to VSID: 11DC5000 */
};

/**< payload for SPF_MSG_VTIMER_SUBSCRIBE. as always spf_msg_header_t precedes this. */
typedef struct spf_msg_sub_vtimer_t
{
   posal_signal_t      *signal_ptr;
   /**< This signal is created by the client and is used for signaling the client
        when time offset expires */

   uint32_t             offset_us;
   /**< Time offset in usec from hard VFR tick. Client initializes it */

   uint32_t             vfr_cycle_us;
   /**< Indicates the time duration between two VFRs. */

   uint32_t             client_id;
   /**< Indicates client ID. */

   uint32_t             direction;
   /**< Indicates the direction of client. */

   uint32_t             vsid;
   /**< Indicates Voice System ID used to device hard VFR timing. */

   posal_signal_t      resync_signal_ptr;
   /**< VFR resync signal, client has to set this null if resync signaling is not required */

   volatile uint32_t           **resync_status_pptr;
   /**< VFR resync status */

   uint32_t             vfr_mode;
   /**< Indicates the mode of VFR as defined in voice_timer api enum voice_timer_vfr_mode_t.*/

   volatile uint64_t           **avtimer_timestamp_us_pptr;
   /**< reference of timstamp parameter which would be updated with VFR time stamp for every VFR cycle */

   volatile bool_t     **first_vfr_occurred_pptr;
   /**< Indicates if first VFR has occurred after VFR subscription. Voice timer does not update this if set to NULL.*/

   volatile uint32_t   *intr_counter_ptr;
   /**< Client owns this memory. Count of how many vfr cycles have elapsed since voice timer started. This value begins at 0 and gets incremented for each
        VFR that occurs after the client's subscription. */

   uint64_t abs_vfr_timestamp;
   /**< Indicates the Qtimer TS at which the periodic soft VFR should start. */

   posal_timer_callback_func_t client_cb_fun_ptr;
   void *cb_context_ptr;

}spf_msg_sub_vtimer_t;

/**< payload for SPF_MSG_VTIMER_UNSUBSCRIBE. as always spf_msg_header_t precedes this. */
typedef struct spf_msg_unsub_vtimer_t
{
   uint32_t                 client_id;
   /**< Indicates client ID. */

   uint32_t             direction;
   /**< Indicates the direction of client. */

   uint32_t                 vsid;
   /**< Indicates Voice System ID used to device hard VFR timing. */

   uint32_t                 vfr_mode;
   /**< Indicates the mode of VFR as defined in voice_timer api enum voice_timer_vfr_mode_t.*/
}spf_msg_unsub_vtimer_t;

/**< payload for SPF_MSG_VTIMER_RESYNC.*/

typedef struct spf_msg_resync_vtimer_t
{
  uint64_t         resync_abs_vfr_timestamp_us;
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
}spf_msg_resync_vtimer_t;


/* Payload struct used for all voice timer commands */
typedef struct spf_msg_vtimer_payload_t
{
   posal_signal_t signal_ptr; /* Signal used for blocking wait */
   union
   {
      spf_msg_sub_vtimer_t   *sub_info_ptr;    /* subscribe info */
      spf_msg_unsub_vtimer_t *unsub_info_ptr;  /* unsubscribe info */
      spf_msg_resync_vtimer_t  *resync_info_ptr; /* resync info */
   };
} spf_msg_vtimer_payload_t;

typedef struct vfr_jitter_info_t
{
   uint64_t            vfr_drv_av_timestamp_us;
   /**< AV timer timestamp when VFR is received(read from hw registers)*/

   uint64_t            vfr_vt_av_timestamp_us;
   /**< AV timer timestamp when voice timer received signal from vfr driver*/

   uint32_t            vfr_curr_jitter;
   /**< Current VFR jitter = vfr_vtm_av_timestamp_us - vfr_afe_av_timestamp_us*/

   uint32_t            vfr_max_jitter;
   /**< Max VFR jitter detected since start of the call*/
}vfr_jitter_info_t;

typedef struct vfr_source_context_t
{
   uint32_t              sub_mask;
   /**< Mask to manage subscriptions to timer based on a VFR source*/

   uint32_t              client_cnt;
   /**< Number of active clients*/

   spf_msg_sub_vtimer_t  *client_subscription_info_ptr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of pointers to store client info structure address*/

   uint64_t             *client_vfr_timestamp_ptr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of pointers to store client timestamp pointer*/

   uint32_t             *client_resync_status_ptr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of pointers to store client resync status pointer*/

   posal_timer_t        oneshot_timer_ptr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of one-shot timer pointers, needed one for each client*/

   uint64_t              vfr_time_stamp_us;
   /**< Timestamp(in microseconds) of the most recent VFR*/

   uint32_t              vfr_timer_duration;
   /**< Periodic timer duration(in microseconds)*/

   uint32_t              vfr_mode;
   /**< Mask to manage subscriptions to timer based on a VFR source*/

   posal_signal_t        vfr_signal_ptr;
   /**< Signal used for soft/hard VFR */

   posal_timer_t         soft_vfr_timer;
   /**< Timer used to for soft VFR, used for ps calls*/

   vfr_jitter_info_t     vfr_jitter_info;
   /**< Jitter info for the VFR source*/

   uint32_t              vsid;
   /**< VSID corresponding to the VFR source*/

   bool_t             *client_first_vfr_occurred_ptr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of pointers to store client resync status pointer*/

   bool_t             is_client_active[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array to indicate if offsets of clients were updated and client is active to get VFR interrupts */

   bool_t                is_first_vfr;
   /**< Is it first VFR */

   vtimer_client_dir_t    vfr_clients_dir;
   /**< Direction of clients belonging to this VFR source */

   volatile uint32_t *intr_cntr_ptr_arr[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE];
   /**< Array of pointers to the client's interrupt count pointer. */
   uint32_t    mask_index[VOICE_TIMER_MAX_CLIENTS_PER_VFR_SOURCE]; //created this hash map to cache the bits corresponding to rx clients being moved to new vfr source.

}vfr_source_context_t;

/* Structure of Voice Timer instance */
typedef struct voice_timer_t
{
   spf_handle_t           spf_handle;
   /**< voice timer thread handle */

   spf_cmd_handle_t       cmd_handle;
   /**< voice timer thread command handle */

   posal_channel_t       timer_channel_ptr;
   /**< Channel for listening to msgs and signals*/

   uint32_t              wait_mask;
   /**< Mask to wait on the channel*/

   vfr_source_context_t  *vfr_source_context_ptr[NUM_VFR_SOURCES];
   /**< Array of context structures for book keeping data per VFR source */

   uint32_t              vfr_src_index_to_vfr_id_mapping[NUM_VFR_SOURCES];
   /**< VFR source index to VFR ID mapping, would be read from devcfg file */

   bool_t                should_exit_workloop;
   /**< Flag to indicate that the workloop has to be exited, set during voice timer destroy */

   bool_t                multiple_vfr_srcs_per_vsid[MAX_NUM_OF_VOICE_SESSIONS];
   /**< Flag to indicate that multiple VFR sources are being used for single voice session.
        In case of VFR resync, this will be true. */

}voice_timer_t;

/* Initializes and launches voice timer thread */
ar_result_t voice_timer_create();

/* Destroys voice timer thread */
ar_result_t voice_timer_destroy();


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef VOICE_TIMER_H

