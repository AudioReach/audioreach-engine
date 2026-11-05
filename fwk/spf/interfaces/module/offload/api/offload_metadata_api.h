/**
 *   \file offload_metadata_api.h
 *   \brief
 *        This file contains API's which are extended for metadata implementation
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef OFFLOAD_METADATA_API_H_
#define OFFLOAD_METADATA_API_H_

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "ar_defs.h"
#include "ar_guids.h"


#include "spf_begin_pack.h"
struct metadata_header_extn_t
{
   uint32_t metadata_extn_param_id;
   /**< unique param id to map the details for the metadata header extension. */

    uint32_t payload_size  ;
   /**< Total size of the payload in bytes for the specified metadata header extension */

#if defined(__H2XML__)
   uint8_t payload[0];
   /**
    * Specifes the payload size of the metadata extn for the specified param
    * This payload is specific to the param id
    *
    * Guideline for metadata payload :
    *  The metadata payload should not contain any further memory references.
    *
    */
#endif
}
#include "spf_end_pack.h"
;

/* Structure for metadata header. */
typedef struct metadata_header_extn_t metadata_header_extn_t;


/**
   Configuration of the metadata extension to communicate the Metadata Origination configuration
*/
#define PARAM_ID_MD_EXTN_MD_ORIGIN_CFG 0x08001A9C
/*==============================================================================
   Param structure definitions
==============================================================================*/

/** @h2xmlp_parameter   {"PARAM_ID_MD_EXTN_MD_ORIGIN_CFG", PARAM_ID_MD_EXTN_MD_ORIGIN_CFG}
    @h2xmlp_description {Parameter for metadata extn to communicate the metadata origination details.\n }
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct param_id_md_extn_md_origin_cfg_t
{
   uint32_t is_md_originated_in_src_domain;
   /**< @h2xmle_description {specifies if the MD first tracking reference is created
                             in source domain where the MD is being propagated from.
                             This flag should be set TRUE in the following cases.
                             If the Metadata client is external or
                             If the Metadata client is internal with tracking source/dest domain same.

                             If the Metadata client is internal with tracking source/dest domain different,
                             it would mean the MD is being propagated from one SPF instance to another SPF instance}

        @h2xmle_default     { 0 }
        @h2xmle_RangeList   { "FALSE" : 0
                              "TRUE"  : 1
                             }
        @h2xmle_policy      {advanced}
   */

   uint32_t domain_id;
   /**< @h2xmle_description {specifies the domain where Metadata is created first. This value is non-zero only if
                             is_md_originated_in_src_domain is set as TRUE }
        @h2xmle_default     { 0 }
        @h2xmle_policy      {advanced}
   */
}
#include "spf_end_pack.h"
;

/* Type definition for the above structure. */
typedef struct param_id_md_extn_md_origin_cfg_t param_id_md_extn_md_origin_cfg_t;

/* This EOS payload is only valid for SPF to SPF propagation in MDF
 * It should not be propagated to External Client */
typedef struct module_cmn_md_eos_ext_t module_cmn_md_eos_ext_t;

/** Contains the stream's metadata
 */
struct module_cmn_md_eos_ext_t
{
   module_cmn_md_eos_flags_t flags;
   /**< EOS flags */
};

#endif // OFFLOAD_METADATA_API_H_
