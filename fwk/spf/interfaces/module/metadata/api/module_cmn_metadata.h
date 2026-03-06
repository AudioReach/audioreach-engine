#ifndef MODULE_CMN_METADATA_H
#define MODULE_CMN_METADATA_H

/**
 *   \file module_cmn_metadata.h
 *   \brief
 *     defines Metadata ID and payloads for all
 *    primitive metadata. Custom metadata defined and used only be specific modules
 *    can be defined privately by those modules.
 *
 *    metadata_api.h defines the IDs that require to be public
 *    (so that graph designer can select which metadata needs to be propagated by splitter etc)
 *
 *    These definitions are common between capi metadata (transferred through
 *    modules within a container) and gk metadata (transferred between containers).
 *
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
#include "ar_guids.h"
#include "metadata_api.h"

/** @addtogroup capi_if_ext_metadata
@{ */

typedef struct module_cmn_md_tracking_payload_t module_cmn_md_tracking_payload_t;

typedef struct module_cmn_md_flags_t module_cmn_md_flags_t;

/** Specifies the control flags for the various metadata features. */
struct module_cmn_md_flags_t
{
   union
   {
      struct
      {
         uint32_t version : 3;
         /**< Specifies the version of the internal metadata API.

              @valuesbul
              - #MODULE_CMN_MD_VERSION @tablebulletend */

         uint32_t is_out_of_band : 1;
         /**< Indicates whether the metadata is out-of-band.

              @valuesbul
              - #MODULE_CMN_MD_OUT_OF_BAND
              - #MODULE_CMN_MD_IN_BAND @tablebulletend */

         uint32_t is_client_metadata : 1;
         /**< Specifies if the metadata is from/to the external client.

              @valuesbul
              - #MODULE_CMN_MD_IS_EXTERNAL_CLIENT_MD
              - #MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD @tablebulletend */

         uint32_t tracking_mode : 2;
         /**< Specifies if the metadata needs to be tracked and the event is
              raised for drop or consumption of the metadata.

              @valuesbul
              - #MODULE_CMN_MD_TRACKING_CONFIG_DISABLE
              - #MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY
              - #MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME @tablebulletend */

         uint32_t tracking_policy : 1;
         /**< Specifies the policy for raising the event.

               For every split in the path, the metadata will be cloned to both
               the paths. This increases the internal references for the
               specified metadata and can lead to multiple drops/consumptions.

              @valuesbul
              - #MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST
              - #MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH @tablebulletend */

         uint32_t buf_sample_association : 1;
         /**< Indicates whether the metadata is sample- or buffer-associated.

              Sample-associated metadata is applicable at the offset at which it is
              inserted. This metadata suffers both algorithmic and buffering delays, for
              example, EOS.

              Buffer-associated metadata is applicable at the offset for the buffer.
              This metadata suffers buffering delays only, for example, Data Flow Gap.

              @valuesbul
              - #MODULE_CMN_MD_BUFFER_ASSOCIATED
              - #MODULE_CMN_MD_SAMPLE_ASSOCIATED @tablebulletend */

         uint32_t is_begin_associated_md : 1;
         /**< Indicates whether the metadata is begin- or end-associated.

              This is applicable when metadata is present when the data buffer is empty.
              If a module produces output data when there is no input data, this field
              indicates whether the metadata offset at the output should be at the start
              or end of the buffer. The default behavior is end-associated.

              For example, consider TTR metadata on voice Rx path in case of erasure. The Mailbox
              Rx module will set the erasure flag, create TTR metadata, and provide an empty data
              buffer. Then, the voice decoder will insert the correct amount of zeros. During
              metadata propagation through the voice decoder, TTR must remain at the beginning
              of the buffer. However, other metadata like EOS must remain at the end of the
              buffer.

              @valuesbul
              - #MODULE_CMN_MD_END_ASSOCIATED_MD
              - #MODULE_CMN_MD_BEGIN_ASSOCIATED_MD @tablebulletend */

         uint32_t needs_propagation_to_client_buffer : 1;
         /**< Specifies if the metadata needs to be propagated to client in the client buffer as well

              This flag would be used to write the metadata to the Read Buffer if enabled

              @valuesbul
              - #MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_DISABLE
              - #MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE @tablebulletend */

      };

      uint32_t word;
      /**< Entire 32-bit word for easy access to read or write an entire word
           in one shot. */
   };
};

/** Metadata is out-of-band.

    The metadata-specific memory is elsewhere and #module_cmn_md_t has a
    pointer to it.
*/
#define MODULE_CMN_MD_OUT_OF_BAND 1

/** Metadata is in-band.

    The #module_cmn_md_t structure and the metadata-specific payload are in one
    contiguous memory buffer.
*/
#define MODULE_CMN_MD_IN_BAND 0

/** Metadata is applicable at the offset at which it is inserted.

    This metadata suffers both algorithmic and buffering delays. For example,
    EOS.
 */
#define MODULE_CMN_MD_SAMPLE_ASSOCIATED 0

/** Metadata is applicable at the offset for the buffer.

    This metadata suffers buffering delays only. For example, Data Flow Gap
    (DFG).
 */
#define MODULE_CMN_MD_BUFFER_ASSOCIATED 1

/** Metadata is from an external SPF client. */
#define MODULE_CMN_MD_IS_EXTERNAL_CLIENT_MD 1

/** Metadata is from an internal SPF client. */
#define MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD 0

/** Definition of a metadata tracking configuration disable. */
#define MODULE_CMN_MD_TRACKING_CONFIG_DISABLE 0

/** Definition of a metadata tracking configuration to enable for MD drops only. */
#define MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY 1

/** Definition of a metadata tracking configuration to enable for MD drop or consume. */
#define MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME 2

/** Definition of a metadata tracking event policy last. */
#define MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST 0

/** Definition of a metadata tracking event policy each. */
#define MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH 1

/** Specifies the version of the metadata */
#define MODULE_CMN_MD_VERSION 0

/** Definition of a metadata to enable propagation to CLIENT BUFFER */
#define MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE 1

/** Definition of a metadata to disable propagation to CLIENT BUFFER */
#define MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_DISABLE 0

typedef struct module_cmn_md_t module_cmn_md_t;

/** Contains the CAPI metadata information.

  In the SPF, metadata is passed in a capi_vtbl_t::process() call through
  #capi_stream_data_v2_t.

  Modules can consume some metadata from the list and produce metadata that is
  attached to the list. Metadata can be transferred between containers via the
  metadata_list_ptr field of #capi_stream_data_v2_t.

  The metadata payload cannot contain references; that is, both metadata_buf
  and metadata_ptr cannot contain a pointer to another memory buffer.

  If the structure of the metadata payload is similar to {element 1, element 2,
  pointer_to_mem}, we recommend you split this payload into two metadata
  payloads with elements 1 and 2 in the first payload, and pointer_to_mem in
  the second (out of band) payload.

  All metadata is applicable for all channels. In other words, you cannot
  attach different metadata to different channels.
 */
struct module_cmn_md_t
{
   uint32_t metadata_id;
   /**< Globally Unique ID (GUID) of the metadata flag. */

   module_cmn_md_flags_t metadata_flag;
   /**< Indicates the metadata flag. */

   uint32_t actual_size;
   /**< Number of valid bytes in the payload.

        This value starts from metadata_ptr or metadata_buf and excludes this
        module_cmn_md_t struct. */

   uint32_t max_size;
   /**< Total size (in bytes) of the buffer in the payload.

        This value starts from metadata_ptr or metadata_buf and excludes this
        module_cmn_md_t struct. */

   uint32_t offset;
   /**< Starting offset from which metadata is applicable.

        When the offset is a valid value, the following criteria apply for
        propagation: @vtspstrbul
        - For raw compressed data (#CAPI_RAW_COMPRESSED), the offset is in
          bytes.
        - For PCM and packetized data, the offset is in samples (per channel).
        - For de-interleaved data, metadata is applicable from the sample at
          (buffer + (sample_offset * bits_per_sample)).
        - For interleaved data (most packetized formats are also interleaved),
          metadata is applicable from the sample at
          (buffer + (sample_offset * bits_per_sample * num channels)).
        - For de-interleaved packed data, metadata is applicable from the
          sample at (buffer + offset + bits_per_sample) in each channel.

        The sample_offset must be less than buffer end. */

   module_cmn_md_tracking_payload_t *tracking_ptr;
   /**< Metadata tracking payload, this pointer is valid only if tracking is enabled. */

   union
   {
      uint64_t metadata_buf[1];
      /**< First byte of the metadata starts here (inline). */

      void *metadata_ptr;
      /**< Pointer to the metadata buffer based on the metadata ID. It can be
           cast according to the structure that corresponds to the metadata
           ID. */
   };
};

typedef struct module_cmn_md_list_t module_cmn_md_list_t;

/** Doubly linked list of metadata objects.
 */
struct module_cmn_md_list_t
{
   module_cmn_md_t *     obj_ptr;  /**< Pointer to the metadata object. */
   module_cmn_md_list_t *next_ptr; /**< Pointer to the next list item. */
   module_cmn_md_list_t *prev_ptr; /**< Pointer to the previous list item. */
};

/** Macro for getting the required size for in-band metadata.
    @hideinitializer
 */
#define MODULE_CMN_MD_INBAND_GET_REQ_SIZE(inband_size) (inband_size + sizeof(module_cmn_md_t) - sizeof(uint64_t))

/**************************************** EOS - Begin *************************************/

/** Basic EOS command from the client. */
#define MODULE_CMN_MD_EOS_BASIC_CLIENT_CMD 0

/** Extended EOS message from the client for addressing multi-DSP offload. */
#define MODULE_CMN_MD_EOS_EXTENSION_CLIENT_CMD 1

/** Macro for flushing EOS.
*/
#define MODULE_CMN_MD_EOS_FLUSHING 1

/** Macro for non-flushing EOS. */
#define MODULE_CMN_MD_EOS_NON_FLUSHING 0

typedef struct module_cmn_md_eos_flags_t module_cmn_md_eos_flags_t;

/** Contains the metadata EOS flags.
 */
struct module_cmn_md_eos_flags_t
{
   union
   {
      struct
      {
         uint32_t is_flushing_eos : 1;
         /**< Flag that indicates whether all stream data is to be rendered.

              @valuesbul
              - #MODULE_CMN_MD_EOS_FLUSHING
              - #MODULE_CMN_MD_EOS_NON_FLUSHING @tablebulletend */

         uint32_t is_internal_eos : 1;
         /**< Flag that indicates whether an EOS was introduced internally.

              @valuesbul
              - TRUE (EOS was introduced internally due to an upstream gap
                     (stop or flush)
              - FALSE (EOS is due to some other reason) @tablebulletend */

         uint32_t skip_voting_on_dfs_change : 1;
         /**< Flag that indicates if KPPS and bandwidth voting needs to be skipped when data flow stops and restarts
              for the modules that propagated this particular EOS. Framework does best effort to not vote
              if this flag is set.

              This can help in the cases where data flow changes within island and the use case doesnt have to exit
              island.

              If any another EOS gets propagated with this flag=false, any previous skip votings may not be
              honored. The same applies if any control/events get triggered, skip vote will be ignored and
              framework will update the votes.

              @valuesbul
              - TRUE -- KPPS and bandwidth voting can be skipped in the propagated path
              - FALSE -- Default behavior; updates the votes based on the data flow state @tablebulletend */
      };

      uint32_t word;
      /**< Entire 32-bit word for easy access to read or write an entire word
           in one shot. */
   };
};

typedef struct module_cmn_md_eos_t module_cmn_md_eos_t;

/** Contains the stream's metadata
*/
struct module_cmn_md_eos_t
{
   module_cmn_md_eos_flags_t flags;
   /**< EOS flags */

   // module_cmn_md_eos_payload_t *core_ptr;
   /* EOS core pointer functionality is generalized and implemented by the tracking payload in the main metadata.
      EOS core is planned for deprecation and should not be referenced in the modules. */

   void *cntr_ref_ptr;
   /**< Pointer to the container reference structure, which only lives within a
        container. */
};

/**************************************** EOS - End *************************************/

/**************************************** DFG - Begin ***********************************/

/** Identifies the metadata ID for DFG.

    The #module_cmn_md_t structure must set the metadata_id field to this ID
    when the metadata is DFG. The module also must check this ID before
    operating on DFG structures.

    A data flow gap indicates that there will be a larger than steady-state
    gap in time between when the most recent data message was sent to this
    input port and when the next data message (if one exists) will be sent to
    this input port.
 */
#define MODULE_CMN_MD_ID_DFG 0x0A001025

/* No payload. */

/**************************************** DFG - End *************************************/

/*
 * The custom event is raised if the SPF internal metadata is set as custom tracking metadata.
 * The event will be sent to the GPR client from SPF in the following scenarios.
 *
 *  Metadata is dropped in the SPF.
 *  Metadata is consumed by the the module and not propagated further.
 *  All sink modules including the hardware end-points would consume the metadata.
*/
#define EVENT_ID_MODULE_CMN_METADATA_CUSTOM_TRACKING_EVENT 0x0300100F

typedef struct module_cmn_md_tracking_md_event_rsp_handle_t module_cmn_md_tracking_md_event_rsp_handle_t;

/*==============================================================================
   Type Definitions
==============================================================================*/
typedef enum
{
   TRACKING_MD_EVENT_RSP_INVALID = 0,
   /* Invalid option for the tracking cmd */

   TRACKING_MD_EVENT_RSP_SET_CFG = 1,
   /* Handle the tracking event as SET config after reciving it at the CU layer. */

} module_cmn_tracking_event_rsp_cfg_t;

/** Custom tracking event payload for metadata
*/
struct module_cmn_md_tracking_md_event_rsp_handle_t
{
   metadata_tracking_event_t tracking_payload;
   /* Tracking payload for the metadata used for generic events*/

   module_cmn_tracking_event_rsp_cfg_t tracking_rsp_cfg;
   /* GPR Tracking event command configuration */

   uint32_t tracking_event_payload_size;
   /* Size of the tracking metadata event payload */

};

/** @} */ /* end_addtogroup capi_if_ext_metadata */

#ifdef __cplusplus
}
#endif /*__cplusplus */

// clang-format on

#endif /* #ifndef MODULE_CMN_METADATA_H */
