#ifndef _CONTEXTS_API_H_
#define _CONTEXTS_API_H_

/*==============================================================================
  @file contexts_api.h
  @brief This file contains Public Context IDs that can be used by clients to configure the ACD module

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

/** Environment*/
#define MODULE_CMN_CONTEXT_ID_ENV_HOME              0x08001324
#define MODULE_CMN_CONTEXT_ID_ENV_OFFICE            0x08001325
#define MODULE_CMN_CONTEXT_ID_ENV_RESTAURANT        0x08001326
#define MODULE_CMN_CONTEXT_ID_ENV_INDOOR            0x08001327
#define MODULE_CMN_CONTEXT_ID_ENV_INSTREET          0x08001328
#define MODULE_CMN_CONTEXT_ID_ENV_OUTDOOR           0x08001329
#define MODULE_CMN_CONTEXT_ID_ENV_INCAR             0x0800132A
#define MODULE_CMN_CONTEXT_ID_ENV_INTRAIN           0x0800132B
#define MODULE_CMN_CONTEXT_ID_ENV_UNKNOWN           0x0800132C


/** Event*/
#define MODULE_CMN_CONTEXT_ID_EVENT_ALARM           0x0800132D
#define MODULE_CMN_CONTEXT_ID_EVENT_BABYCRYING      0x0800132E
#define MODULE_CMN_CONTEXT_ID_EVENT_DOGBARKING      0x0800132F
#define MODULE_CMN_CONTEXT_ID_EVENT_DOORBELL        0x08001330
#define MODULE_CMN_CONTEXT_ID_EVENT_DOOROPENCLOSE   0x08001331
#define MODULE_CMN_CONTEXT_ID_EVENT_CRASH           0x08001332
#define MODULE_CMN_CONTEXT_ID_EVENT_GLASSBREAKING   0x08001333
#define MODULE_CMN_CONTEXT_ID_EVENT_SIREN           0x08001334
#define MODULE_CMN_CONTEXT_ID_EVENT_CARHONK       	0x080013CC
#define MODULE_CMN_CONTEXT_ID_EVENT_GUNSHOTS      	0x080013CD
#define MODULE_CMN_CONTEXT_ID_EVENT_PHONERING     	0x080013CE
#define MODULE_CMN_CONTEXT_ID_EVENT_SLEEPAPNEA    	0x080013CF
#define MODULE_CMN_CONTEXT_ID_EVENT_SNORNING      	0x080013D0



/** Ambience*/
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_SPEECH       0x08001335
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_MUSIC        0x08001336
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_SILENT_SPL   0x08001338
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_NOISY_SFLUX  0x08001339
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_SILENT_SFLUX 0x0800133A

//unit of threshold and confidence score used for noise SPL is dB.
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_NOISY_SPL    0x08001337
//unit of threshold and confidence score used for Sound Pressure Meter is dB and range is 0db to 150db.
#define MODULE_CMN_CONTEXT_ID_AMBIENCE_SPL_METER    0x08001AE2
/** Conversation Detection */
#define MODULE_CMN_CONTEXT_ID_CONVERSATION_DETECTION 0x08001AF2

#endif //_CONTEXTS_API_H_