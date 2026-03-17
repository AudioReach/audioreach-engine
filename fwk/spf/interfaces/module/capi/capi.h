#ifndef CAPI_H
#define CAPI_H

/**
 *  \file capi.h
 *  \brief
 *     Common Audio Processing Interface v2 header file
 *
 *     This file defines a generalized C interface that can wrap a wide variety of audio processing modules, so that
 * they
 *  can be treated the same way by control code.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_types.h"
#include "capi_events.h"
#include "capi_properties.h"
#include "capi_intf_extn_metadata.h"
#include "capi_intf_extn_period.h"
#include "capi_intf_extn_calibration_ops.h"

typedef struct capi_t capi_t;

typedef struct capi_vtbl_t capi_vtbl_t;

/** @ingroup capi_virt_func_table
    Function table for plain C implementations of CAPI-compliant objects.

    Objects must have a pointer to a function table as the first element in
    their instance structure. This structure is the function table type for all
    such objects.
*/
struct capi_vtbl_t
{
   /** @ingroup capi_func_process
   Generic function that processes input data on all input ports and provides
   output on all output ports.

   @datatypes
   capi_t \n
   capi_stream_data_t

   @param[in,out] _pif   Pointer to the module object.
   @param[in,out] input  Array of pointers to the input data for each input
                         port. \n @vertspace{2}
                         The length of the array is the number of input ports.
                         The client sets the number of input ports using the
                         #CAPI_PORT_NUM_INFO property. \n @vertspace{2}
                         The function must modify the actual_data_len field
                         to indicate how many bytes were consumed. \n
                         @vertspace{2}
                         Depending on stream_data_version (in
                         #capi_stream_flags_t), the actual structure can be a
                         version of #capi_stream_data_t (like
                         #capi_stream_data_t or #capi_stream_data_v2_t). \n
                         @vertspace{2}
                         Some elements of input[] can be NULL. This occurs when
                         there is mismatch between #CAPI_PORT_NUM_INFO and the
                         currently active ports. NULL elements must be ignored.
   @param[out] output    Array of pointers to the output data for each output
                         port. \n @vertspace{2}
                         The client sets the number of output ports using the
                         #CAPI_PORT_NUM_INFO property. \n @vertspace{2}
                         The function sets the actual_data_len field to
                         indicate how many bytes were generated. \n
                         @vertspace{2}
                         Depending on stream_data_version (in
                         #capi_stream_flags_t), the actual structure can be a
                         version of #capi_stream_data_t (like
                         #capi_stream_data_t or #capi_stream_data_v2_t). \n
                         @vertspace{2}
                         For single input/single output modules, the framework
                         typically assigns the output flags, timestamp, and
                         metadata with input flags, timestamp, and metadata
                         before calling process. Metadata is only available in
                         capi_stream_data_v2_t and later. \n @vertspace{2}
                         If the module has delay, it must reset the output
                         capi_stream_data_t (or capi_stream_data_v2_t)
                         and set it back after the delay is over. \n
                         @vertspace{2}
                         Some elements of output[] can be NULL. This occurs
                         when there is mismatch between #CAPI_PORT_NUM_INFO and
                         the currently active ports. NULL elements must be
                         ignored.

   @detdesc
   On each call to capi_vtbl_t::process(), the behavior of the module depends
   on the value it returned for the #CAPI_REQUIRES_DATA_BUFFERING property. For
   a description of the behavior, see the comments for
   CAPI_REQUIRES_DATA_BUFFERING.
   @par
   No debug messages are allowed in this function.
   @par
   Modules must make a NULL check for the following and use them only if they
   are not NULL:
   - input
   - output
   - capi_buf_t in #capi_stream_data_t
   - data buffer in #capi_buf_t
   @par
   For some events that result from a capi_vtbl_t::process() call, the output buffer must
   not be filled. Check the event definition for this restriction.

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   @dependencies
   A valid input media type must have been set on each input port using the
   #CAPI_INPUT_MEDIA_FORMAT property.
   */
   capi_err_t (*process)(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

   /** @ingroup capi_func_end
   Frees any memory allocated by the module.

   @datatypes
   capi_t

   @param[in,out] _pif  Pointer to the module object.

   @note1hang After calling this function, _pif is no longer a valid CAPI
              object. Do not call any CAPI functions after using it.

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   @dependencies
   None. @newpage
   */
   capi_err_t (*end)(capi_t *_pif);

   /** @ingroup capi_func_set_param
   Sets a parameter value based on a unique parameter ID.

   @datatypes
   capi_t \n
   capi_port_info_t \n
   capi_buf_t

   @param[in,out] _pif      Pointer to the module object.
   @param[in] param_id      ID of the parameter whose value is to be set.
   @param[in] port_info_ptr Pointer to the information about the port on
                            which this function must operate. \n @vertspace{3}
                            If a valid port index is not provided, the port
                            index does not matter for the param_id, the
                            param_id is applicable to all ports, or the port
                            index might be part of the parameter payload.
   @param[in] params_ptr    Pointer to the buffer containing the value of the
                            parameter. \n @vertspace{3}
                            The format of the data in the buffer depends on
                            the implementation.

   @detdesc
   The actual_data_len field of the parameter pointer must be at least the
   size of the parameter structure. Therefore, the following check must be
   performed for each tuning parameter ID:
   @par
   @code
   if (params_ptr->actual_data_len >= sizeof(gain_struct_t))
   {
   :
   :
   }
   else
   {
   MSG_1(MSG_SSID_QDSP6, DBG_ERROR_PRIO,"CAPI Libname Set, Bad param size
   %lu",params_ptr->actual_data_len);
   return AR_ENEEDMORE;
   }

   @endcode
   @par
   Optionally, some parameter values can be printed for tuning verification.
   @par
   @note1 In this code sample, gain_struct is an example only.
          Use the correct structure based on the parameter ID. @newpage

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   @dependencies
   None. @newpage
   */
   capi_err_t (*set_param)(capi_t *                _pif,
                           uint32_t                param_id,
                           const capi_port_info_t *port_info_ptr,
                           capi_buf_t *            params_ptr);

   /** @ingroup capi_func_get_param
   Gets a parameter value based on a unique parameter ID.

   @datatypes
   capi_t \n
   capi_port_info_t \n
   capi_buf_t

   @param[in,out] _pif      Pointer to the module object.
   @param[in]     param_id  Parameter ID of the parameter whose value is
                            being passed in this function. For example:
                            @vertspace{2}
                            - CAPI_LIBNAME_ENABLE
                            - CAPI_LIBNAME_FILTER_COEFF @vertspace{-14}
   @param[in] port_info_ptr Pointer to the information about the port on which
                            this function must operate. \n @vertspace{3}
                            If the port index is invalid, either the port
                            index does not matter for the param_id, the
                            param_id is applicable to all ports, or the port
                            information might be part of the parameter payload.
   @param[out] params_ptr   Pointer to the buffer to be filled with the value
                            of the parameter. The format depends on the
                            implementation.

   @detdesc
   The max_data_len field of the parameter pointer must be at least the size
   of the parameter structure. Therefore, the following check must be
   performed for each tuning parameter ID.
   @par
   @code
   if (params_ptr->max_data_len >= sizeof(gain_struct_t))
   {
   :
   :
   }
   else
   {
   MSG_1(MSG_SSID_QDSP6, DBG_ERROR_PRIO,"CAPI Libname Get, Bad param size
   %lu",params_ptr->max_data_len);
   return AR_ENEEDMORE;
   }

   @endcode
   @par
   Before returning, the actual_data_len field must be filled with the number
   of bytes written into the buffer.
   @par
   Optionally, some parameter values can be printed for tuning verification.
   @par
   @note1 In this code sample, gain_struct is an example only.
          Use the correct structure based on the parameter ID.

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   @dependencies
   None.
   */
   capi_err_t (*get_param)(capi_t *                _pif,
                           uint32_t                param_id,
                           const capi_port_info_t *port_info_ptr,
                           capi_buf_t *            params_ptr);

   /** @ingroup capi_func_set_properties
   Sets a list of property values. Optionally, some property values can be
   printed for debugging.

   @datatypes
   capi_t \n
   capi_proplist_t

   @param[in,out] _pif      Pointer to the module object.
   @param[in] proplist_ptr  Pointer to the list of property values.

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   Errors that occur when setting or getting a property must be handled in
   the following way:
    - If the property is not supported by the module, the CAPI_EUNSUPPORTED flag must be
       set in the error code and the actual_data_len field for that property must be set to zero.
    - The rest of the properties must still be processed (rather than exiting when
       an unsupported property is encountered).

   @dependencies
   None. @newpage
   */
   capi_err_t (*set_properties)(capi_t *_pif, capi_proplist_t *proplist_ptr);

   /** @ingroup capi_func_get_properties
   Gets a list of property values.

   @datatypes
   capi_t \n
   capi_proplist_t

   @param[in,out] _pif       Pointer to the module object.
   @param[out] proplist_ptr  Pointer to the list of empty structures that
                             must be filled with the appropriate property
                             values, which are based on the property IDs
                             provided. \n @vertspace{3}
                             The client must fill some elements
                             of the structures as input to the module. These
                             elements must be explicitly indicated in the
                             structure definition.

   @return
   #CAPI_EOK -- Success
   @par
   Error code -- Failure (see Section @xref{hdr:errorCodes})

   Errors that occur when setting or getting a property must be handled in
   the following way:
    - If the property is not supported by the module, the CAPI_EUNSUPPORTED flag must be
       set in the error code and the actual_data_len field for that property must be set to zero.
    - The rest of the properties must still be processed (rather than exiting when
       an unsupported property is encountered).

   @dependencies
   None.
   */
   capi_err_t (*get_properties)(capi_t *_pif, capi_proplist_t *proplist_ptr);
};

/** @ingroup capi_virt_func_table
  Plain C interface wrapper for the virtual function table, capi_vtbl_t.

  This capi_t structure appears to the caller as a virtual function table.
  The virtual function table in the instance structure is followed by other
  structure elements, but those are invisible to the users of the CAPI
  object. This capi_t structure is all that is publicly visible.
 */
struct capi_t
{
   const capi_vtbl_t *vtbl_ptr; /**< Pointer to the virtual function table. */
};

/** @ingroup capi_func_get_static_prop
  Queries for properties as follows:
  - Static properties of the module that are independent of the instance
  - Any property that is part of the set of properties that can be statically
    queried

  @datatypes
  capi_proplist_t

  @param[in] init_set_proplist Pointer to the same properties that are sent in
                               the call to capi_init_f().
  @param[out] static_proplist  Pointer to the property list structure. \n
                               @vertspace{3}
                               The client fills in the property IDs for which
                               it needs property values. The client also
                               allocates the memory for the payloads.
                               The module must fill in the information in this
                               memory.

  @detdesc
  This function is used to query the memory requirements of the module to
  create an instance. The function must fill in the data for the properties in
  the static_proplist.
  @par
  As an input to this function, the client must pass in the property list that
  it passes to capi_init_f(). The module can use the property
  values in init_set_proplist to calculate its memory requirements.
  @par
  The same properties that are sent to the module in the call to
  %capi_init_f() are also sent to this function to enable the module to
  calculate the memory requirement.

  @return
  #CAPI_EOK -- Success
  @par
  Error code -- Failure (see Section @xref{hdr:errorCodes})

   Errors that occur when setting or getting a property must be handled in
   the following way:
    - If the property is not supported by the module, the CAPI_EUNSUPPORTED flag must be
       set in the error code and the actual_data_len field for that property must be set to zero.
    - The rest of the properties must still be processed (rather than exiting when
       an unsupported property is encountered).

  @dependencies
  None.
 */
typedef capi_err_t (*capi_get_static_properties_f)(capi_proplist_t *init_set_proplist,
                                                   capi_proplist_t *static_proplist);

/** @ingroup capi_func_init
  Instantiates the module to set up the virtual function table, and also
  allocates any memory required by the module.

  @datatypes
  capi_t \n
  capi_proplist_t

  @param[in,out] _pif          Pointer to the module object. \n @vertspace{3}
                               The memory has been allocated by the client
                               based on the size returned in the
                               #CAPI_INIT_MEMORY_REQUIREMENT property.
  @param[in] init_set_proplist Pointer to the properties set by the service
                               to be used while initializing.

  @detdesc
  States within the module must be initialized at the same time.
  @par
  For any unsupported property ID passed in the init_set_proplist parameter,
  the function prints a message and continues processing other property IDs.
  @par
  All return codes returned by this function, except #CAPI_EOK, are
  considered to be FATAL.

  @return
  #CAPI_EOK -- Success
  @par
  Error code -- Failure (see Section @xref{hdr:errorCodes})

  @dependencies
  None.
  */
typedef capi_err_t (*capi_init_f)(capi_t *_pif, capi_proplist_t *init_set_proplist);

#endif /* #ifndef CAPI_H */
