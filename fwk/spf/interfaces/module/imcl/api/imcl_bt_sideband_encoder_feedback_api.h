/**========================================================================
 @file imcl_bt_sideband_encoder_feedback_api.h

 @brief This file contains API's to send the sideband encoder feedback

 Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 SPDX-License-Identifier: BSD-3-Clause-Clear

 ====================================================================== */

#ifndef _IMCL_BT_SIDEBAND_ENCODER_FEEDBACK_H_
#define _IMCL_BT_SIDEBAND_ENCODER_FEEDBACK_H_

/*------------------------------------------------------------------------------
 *  Header Includes
 *----------------------------------------------------------------------------*/

#include "imcl_fwk_intent_api.h"

#ifdef INTENT_ID_BT_ENCODER_FEEDBACK

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/** -----------------------------------------------------------------------
 ** Type Declarations
 ** ----------------------------------------------------------------------- */

/* clang-format off */

/* Param id to send all sideband data through imcl to encoder*/
#define IMCL_PARAM_ID_BT_SIDEBAND_ENCODER_FEEDBACK   0x0800116D

/** Maximum size of the sideband data structure. */
#define SIDEBAND_DATA_MAX_LENGTH                     256

/* Payload of IMCL_PARAM_ID_BT_SIDEBAND_ENCODER_FEEDBACK*/
typedef struct imcl_param_id_bt_sideband_encoder_feedback_t
{
    uint8_t sideband_id;
    /**< ID of the sideband payload. */

    uint8_t sideband_length;
    /**< Length of the sideband payload. */

    uint8_t sideband_data[SIDEBAND_DATA_MAX_LENGTH];
    /**< Maximum size of the sideband data payload */

}imcl_param_id_bt_sideband_encoder_feedback_t;

/* Param id to send all sideband data through imcl to encoder*/
#define IMCL_PARAM_ID_BT_SIDEBAND_ENCODER_FEEDBACK_V2   0x08001249

/* Payload of IMCL_PARAM_ID_BT_SIDEBAND_ENCODER_FEEDBACK_V2*/
typedef struct imcl_param_id_bt_sideband_encoder_feedback_v2_t
{
   uint32_t  num_sidebands;
   /** number of sideband payloads */
#ifdef __H2XML__
   imcl_param_id_bt_sideband_encoder_feedback_v2_payload_t payload[0];
   /** for each sideband a payload given by this struct */
#endif
} imcl_param_id_bt_sideband_encoder_feedback_v2_t;

/* Payload for each sideband to be sent over IMCL_PARAM_ID_BT_SIDEBAND_ENCODER_FEEDBACK_V2*/
typedef struct imcl_param_id_bt_sideband_encoder_feedback_v2_payload_t
{
    uint8_t sideband_id;
    /**< ID of the sideband payload. */

    bool_t channel_mask_valid;
    /**< is channel mask below valid */

    uint16_t sideband_length;
    /**< Length of the sideband payload. */

    uint32_t channel_mask_lsw;
    uint32_t channel_mask_msw;
    /**< channel mask indicating for which all channels sideband is applicable
     * Channel mask is obtained by shifting with channel map names
     * defined in media_fmt_api_basic.h::pcm_channel_map. E.g. channel map = PCM_CHANNEL_L, then mask = 1<<PCM_CHANNEL_L.
     * channel mask if zero implies all channels. For sidebands which don't rely on stream-id, channel mask is zero.
     * For sidebands which rely on stream-id, if channel mask is zero, then it sideband is applicable to all channels.*/
#ifdef __H2XML__
    uint8_t sideband_data[0];
    /**< variable length sideband payload. Note that for sideband ids which have stream-id, this payload also contains stream-id,
     *   but receiver may ignore it and use channel_mask. */
#endif

}imcl_param_id_bt_sideband_encoder_feedback_v2_payload_t;

/* Param id to request triggerable imcl out from depack
 * Currently used for Conn proxy source and cop depack usecases only*/
#define IMCL_PARAM_ID_BT_SIDEBAND_TRIGGER_ENABLE   0x08001320

/* Payload of IMCL_PARAM_ID_BT_SIDEBAND_TRIGGER_ENABLE*/
typedef struct imcl_param_id_bt_sideband_trigger_enable_t
{
   bool_t is_trigger;
   /**< Indicates whether sideband message is triggered or polled.
    *   - 0 -- FALSE; polled message 
    *   - 1 -- TRUE; triggered message */
}imcl_param_id_bt_sideband_trigger_enable_t; 


/* clang-format on */

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif //INTENT_ID_BT_ENCODER_FEEDBACK

#endif /* _IMCL_BT_SIDEBAND_ENCODER_FEEDBACK_H_ */
