#ifndef METADATA_API_H
#define METADATA_API_H
/**
 *   \file metadata_api.h
 * \brief 
 *  	 This file contains metadata IDs. Definitions are not supposed to be here.
 *  	 definitions will be in module_cmn_metadata.
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_defs.h"
#include "codec_metadata_api.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/** @h2xml_title1           {Metadata IDs}
    @h2xml_title_agile_rev  {Metadata IDs}
    @h2xml_title_date       {Apr 30 2019} */
/**
   @h2xmlx_xmlNumberFormat {int}
*/


/**
 * Metadata API to support the requirement of exchanging the metadata between the client and SPF
 *
 *   Metadata in general can be defined as additional information associated with the stream data.
 *
 *   Application or usage of the metadata information would be defined based on the type of
 *   Metadata which would be specified by the metadata ID
 *
 *   The metadata is expected to be exchanged in the specified format for writing and reading both
 *   by the HLOS client and the SPF during the data command read/write operations.
 *
 *   In general all the metadata is applicable to all the channels in the stream data
 *
 *  Defining metadata association
 *     Sample associated: Metadata is applicable at the offset at which it's inserted.
 *                        This metadata suffers both algorithmic and buffering delays.
 *     Buffer associated: metadata is applicable at the offset for the buffer.
 *                        This metadata suffers buffering delays only.
 *
 *  Defining metadata flag : The flags in the metadata payload specifies various control information.
 *  More details of individual controls are defined in the payload.
 *
 */
 
 
/** Definition of a metadata client information flag bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_CLIENT_INFO                AR_NON_GUID(0x00000001)

/** Definition of a metadata client information flag shift value. */
#define MD_HEADER_FLAGS_SHIFT_CLIENT_INFO                   0


/** Definition of a metadata tracking configuration bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG             AR_NON_GUID(0x00000006)

/** Definition of a metadata tracking configuration shift value. */
#define MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG            1
 
 
/** Definition of a metadata tracking event policy flag bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_TRACKING_EVENT_POLICY        AR_NON_GUID(0x00000008)

/** Definition of a metadata tracking event policy flag shift value. */
#define MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG      3

/** Definition of a metadata association with data bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_ASSOCIATION                  AR_NON_GUID(0x00000010)

/** Definition of a metadata association with data flag shift value. */
#define MD_HEADER_FLAGS_SHIFT_ASSOCIATION_FLAG                4

/** Definition of a metadata propagation to client buffer with data bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER AR_NON_GUID(0x00000020)

/** Definition of a metadata propagation to client buffer flag shift value. */
#define MD_HEADER_FLAGS_SHIFT_NEEDS_MD_PROPAGATION_TO_CLIENT_BUFFER_FLAG 5

/** Definition of a  metadata header extension present bitmask. */
#define MD_HEADER_FLAGS_BIT_MASK_NEEDS_HEADER_EXTN_PRESENT AR_NON_GUID(0x00000040)

/** Definition of a metadata header extension present flag shift value. */
#define MD_HEADER_FLAGS_SHIFT_HEADER_EXTN_PRESENT 6

/** Definition of a external client of SPF */
#define MD_HEADER_FLAGS_CLIENT_EXTERNAL                 0

/** Definition of a internal client of SPF */
#define MD_HEADER_FLAGS_CLIENT_INERTNAL                 1

/** Definition of a metadata tracking configuration disable. */
#define MD_HEADER_FLAGS_TRACKING_CONFIG_DISABLE          0

/** Definition of a metadata tracking configuration to enable for md drops only. */
#define MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY          1

/** Definition of a metadata tracking configuration to enable for md drop or consume. */
#define MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME     2

/** Definition of a metadata tracking event policy last. */
#define MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_LAST       0

/** Definition of a metadata tracking event policy last. */
#define MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_EACH       1

/** Definition of a metadata sample association . */
#define MD_HEADER_FLAGS_SAMPLE_ASSOCIATED                0

/** Definition of a metadata sample association . */
#define MD_HEADER_FLAGS_BUFFER_ASSOCIATED                1

/** Definition to enable the metadata propagation to the client buffer . */
#define MD_HEADER_FLAGS_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE 0

/** Definition to disable the metadata propagation to the client buffer . */
#define MD_HEADER_FLAGS_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_DISABLE 1

/** Definition to specify there is no metadata header extension */
#define MD_HEADER_FLAGS_HEADER_EXTN_NOT_PRESENT  0

/** Definition to specify there is metadata header extension */
#define MD_HEADER_FLAGS_HEADER_EXTN_PRESENT  1

/**
 * Payload for Metadata
 *
 * struct complete_metadata_api_header_t
 * {
 *    metadata_header_t base; //aligned to 4 bytes
 *    uint8_t payload[payload_size];
 *    uint8_t alignment[payload_size % 4]; // alignment bytes for 4 byte alignment.
 *    metadata_header_extn_t base_extn[0]; // optional extension header payload aligned to 4 bytes, used in MDF only for now
 *                                         // BIT 6 should be set to indicate the presence of this payload.
 *                                         // The present definition of extension is published for internal usage within and
 *                                         // is locally defined for MDF offload interfaces. (\offload_metadata_api.h)
 * }
 */
#include "spf_begin_pack.h"
struct metadata_header_t
{
   uint32_t metadata_id;
   /**< Globally Unique ID of the metadata. */

   uint32_t	flags;
   /**<
    *  Specifies the control flags for the metadata.
    *
    *
    *   BIT_0 : specifies if the metadata was from the external client or meant to be rendered to external client.
    *
    *   @values{for bit 0}
    *      - 0 -- MD_HEADER_FLAGS_CLIENT_EXTERNAL (from/to external client (HLOS))
    *      - 1 -- MD_HEADER_FLAGS_CLIENT_INERTNAL (internal propagation in SPF, used for MDF)
    *
    *    This bit is specifically used for MDF to classify if the metadata is relevant
    *    to external client versus internally generated and propagated within SPF.
    *    Should be set to 0 always  for HLOS
    *
    *
    *   BIT_2_to_1 : Specifies if the Metadata is trackable.
    *   If the metadata is dropped or consumed in SPF then the client would get an event
    *   with relevant information for the metadata. There is no requirement of registering for an event.
    *
    *   @values{for bit 2_to_1}
    *      - 0 -- MD_HEADER_FLAGS_TRACKING_CONFIG_DISABLE
    *             (tracking is disabled)
    *      - 1 -- MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY
    *             (tracking enabled for drops only)
    *      - 2 -- MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME
    *             (tracking enabled for all drops and consumption)
    *      - 3 -- reserved and ignored.
    *
    *     For the configuration value set to 2, the event would be raised for metadata
    *     consumption from all the sink modules including hardware endpoints.
    *
    *   BIT_3 : Specifies the policy for raising the event.
    *   For every split in the path, the metadata would be cloned to both the paths.
    *   This will increase the internal references for the specified metadata and can
    *   lead to multiple drops/consumptions.
    *
    *   @values{for bit 3}
    *      - 0 -- MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_LAST ( raise event for only the last drop/consumption of md in SPF)
    *      - 1 -- MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_EACH ( raise event for every drop/consumption of md in SPF )
    *
    *    BIT_4: Specifies the metadata association
    *
    *    @values{for bit 4}
    *      - 0 -- MD_HEADER_FLAGS_SAMPLE_ASSOCIATED
    *      - 1 -- MD_HEADER_FLAGS_BUFFER_ASSOCIATED
    *
    *    BIT_5: Specifies if the Metadata needs to be propagated to Client in Client Buffer.
    *    For all the metadata sent by HLOS client to SPF, this is enabled by default and the
    *    Metadata would be propagated back to the client in the Read Client Buffer.
    *
    *    This bit would provide configuration capability to disable sending the metadata back to the
    *    Client.
    *
    *    The metadata propagation to client is only valid for external metadata and is associated with
    *    BIT_0 configuration
    *
    *    @values{for bit 5}
    *      - 0 -- MD_HEADER_FLAGS_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE
    *      - 1 -- MD_HEADER_FLAGS_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_DISABLE
    *
    *
    *     BIT_6 : Specifies if there is metadata header extension payload after the actual metadata payload.
    *             At present, this is used only for special cases for internal MD propagation in MDF.
    *
    *    @values{for bit 6}
    *      - 0 -- MD_HEADER_FLAGS_HEADER_EXTN_NOT_PRESENT
    *      - 1 -- MD_HEADER_FLAGS_HEADER_EXTN_PRESENT
    *
    *      Other bit fields are reserved and should be set to zero while writing the payload.
    *
    */

   uint32_t offset;
   /**< Start offset in the data buffer from which metadata is applicable.
    *
    *   For cases where offset is a valid value, the following criteria apply for specifying the offset.
    *
    *       For raw compressed, offset is in bytes.
    *       For PCM & packetized, offset in samples (per channel).
    *           For interleaved data (most packetized format are also interleaved),
    *               metadata is applicable from sample at (data buffer +  (sample_offset * bytes_per_sample * num channels )
    *           For deinterleaved packed data,
    *               metadata is applicable from sample at (data buffer + offset * bytes_per_sample) in each channel
    *           For deinterleaved data unpacked,
    *               metadata is applicable from sample at (channel data buffer + (sample_offset * bytes_per_sample))
    *   Effective sample_offset in above cases must be less than buffer end
    */

   uint32_t token_lsw;
   /* Unique token applicable for tracking Metadata.
    * The same token will be used while raising the Metadata tracking event
    *
    * Lower 32 bits of the token field 
    */
    
   uint32_t token_msw;
   /* 
    * Higher 32 bits of the token field 
    */

   uint32_t payload_size  ;
   /**< Total size of the payload in bytes for the specified metadata ID */

#if defined(__H2XML__)
   uint8_t payload[0];
   /**
    * Specifes the metadata payload of size defined in this API.
    * This payload is specific to the metadata ID.
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
typedef struct metadata_header_t metadata_header_t;

/** Definition of a metadata last instance flag bitmask. */
#define MD_TRACKING_FLAGS_BIT_MASK_LAST_INSTANCE       AR_NON_GUID(0x00000001)

/** Definition of a metadata last instance flag shift value. */
#define MD_TRACKING_FLAGS_IS_LAST_INSTANCE_FLAG         0

/** tracking status of the metadata when the meta-data is consumed. */
#define MD_TRACKING_STATUS_IS_CONSUMED                 AR_NON_GUID(0x00000000)

/** tracking status of the metadata when the meta-data is dropped. */
#define MD_TRACKING_STATUS_IS_DROPPED                  AR_NON_GUID(0x00000001)

/**
 * The metadata event is raised if the metadata is set as tracking metadata.
 * The event will be sent to the client from SPF in the following scenarios.
 *
 *  Metadata is dropped in the SPF and cannot be rendered to client through the data commands.
 *  Metadata is consumed by the the module and not propagated further.
 *  Note that all sink modules including the hardware end-points would consume the metadata.
 */

#define EVENT_ID_MODULE_CMN_METADATA_TRACKING_EVENT 0x0300100C

/**
 * Payload for Metadata tracking event
 */

#include "spf_begin_pack.h"
struct metadata_tracking_event_t
{
   uint32_t metadata_id;
   /**< Globally Unique ID of the metadata. */

   uint32_t source_module_instance;
   /**< Specifies the source module of the metadata.
    *
    * Client Metadata:
    *      For metadata from the client, this is basically derived from the source port ID in the data command from
    * client.
    * Module metadata to client:
    *      If the metadata is generated by a module in SPF, then this would be the module instance ID of the module.
    */
   uint32_t  module_instance_id;
   /**<  Specifies the module instance ID of the module which is raising the event when MD is consumed/dropped.
         Invalid (0) when the status is set as dropped.
   */
   uint32_t  flags;
   /**<
   *  Specifies the control flags for the metadata event.
   *
   *
   *   BIT_0 : specifies if this event corresponds to the last instance of metadata in the in SPF.
   *   @values{for bit 0}
   *      - 0 -- FALSE (not last instance)
   *      - 1 -- TRUE ( yes, its last instance)
   *
   *      1. For every split in the path, the metadata would be cloned to both the paths. This will increase
   *         the internal reference count for the specified MD in the SPF.
   *      2. If the event is generated in one path, while the MD in the other path is still active,
   *         this bit_field would be set as FALSE.
   *         This indicates that there is a possibility for more events corresponding to this MD
   *      3. If the metadata tracking is set as last drop/consumption, then there will be only one event and
   *         this bit would be set always.
   *
   *   Other bits are reserved for future use and should be set to zero.
   */

   uint32_t token_lsw;
   /** <
    *  Specifies the LSW of the unique token.
    *
    *  Client Metadata:
    *         For Metadata from the client, this is specified in the medata API header and the same value would be used here.
    *  Module metadata to client:
    *         If the metadata is generated by a module in SPF, the module would need to specify the token.
    *         It can be configured to the module by the client, if the MD needs to be trackable.    *
    */

   uint32_t token_msw;
   /** <
    *  Specifies the MSW of the unique token.
    */

   uint32_t	status;
   /**<
    * Specifies the status information for the metadata.
    *
   *   BIT_0 : Specifies if the metadata is consumed or dropped.
   *   @values{for bit 0}
    *      - 0 -- MD_TRACKING_STATUS_IS_CONSUMED; MD is consumed
    *      - 1 -- MD_TRACKING_STATUS_IS_DROPPED; MD is dropped
    *
    *      Other values are reserved for future use.
    */

}
#include "spf_end_pack.h"
;

/* Structure for metadata tracking  event. */
typedef struct metadata_tracking_event_t metadata_tracking_event_t;


/** Metadata ID to specify the Media format
 * Media format as metadata is propagated by the endpoint module in SPF to the client.
 * Client should configure the end-point module with appropriate configuration
 * to enable the propagation from SPF to Client in the data response.
 *
 *  Payload media_format_t
 */
#define MODULE_CMN_MD_ID_MEDIA_FORMAT                      0x0A001056


/**
    Metadata ID for EOS.

    module_cmn_md_t structure has to set the metadata_id field to this id
    when the metadata is EOS. The module also should check this ID before
    operating on EOS structures.

    Additional note on payload details
    EOS does not have any Payload definition for HLOS client.
    EOS payload for internal propagation within SPF is defined by module_cmn_md_eos_t in \module_cmn_metadata.h
    EOS payload for internal propagation between SPF instances (MDF) is defined by module_cmn_md_eos_ext_t in \offload_metadata_api.h
*/
#define MODULE_CMN_MD_ID_EOS 				0x0A00100C


#ifdef __cplusplus
}
#endif /*__cplusplus*/


#endif /* METADATA_API_H */
