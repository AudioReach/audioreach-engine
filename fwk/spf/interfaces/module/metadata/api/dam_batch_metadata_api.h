#ifndef DAM_BATCH_METADATA_API_H
#define DAM_BATCH_METADATA_API_H
/**
 *   \file dam_batch_metadata_api.h
 * \brief
 *  	 This file contains metadata IDs used by DAM to send batch tracking
 *       information downstream.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/**
    Metadata ID for indicating the end of DAM Batch.

    module_cmn_md_t structure has to set the metadata_id field to this id
    when the metadata is DAM_BATCH_END_MD_ID_MARKER.

    DAM_BATCH_END_MD_ID_MARKER payload for internal propagation within SPF is defined by dam_batch_end_md_gen_t
*/
#define DAM_BATCH_END_MD_ID_MARKER         0x0A00106A


typedef struct dam_batch_end_md_gen_t dam_batch_end_md_gen_t;

/** Payload sent along with DAM_BATCH_END_MD_ID_MARKER metadata
*/
struct dam_batch_end_md_gen_t
{
   uint32_t param_id;
   /*Parameter ID need to be handled on recieving tracking event*/

   uint32_t output_port_idx;
   /*DAM output port index where Duty Cycling is enabled*/
};


#ifdef __cplusplus
}
#endif /*__cplusplus */

// clang-format on

#endif /* #ifndef DAM_BATCH_METADATA_API_H */
