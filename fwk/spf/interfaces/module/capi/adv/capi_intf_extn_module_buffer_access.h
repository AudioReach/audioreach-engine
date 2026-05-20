#ifndef CAPI_INTF_EXTN_MODULE_BUFFER_ACCESS_H
#define CAPI_INTF_EXTN_MODULE_BUFFER_ACCESS_H

/**
 *  \file capi_intf_extn_module_buffer_access.h
 *  \brief
 *        Interface extensions related to reusing module buffers for the framework data processing.
 *        This allows use of the modules buffer directly in the framework without intermediate copy from
 *        module internal buffer to framework owned buffer, or vice versa.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------------
 * Include Files
 *----------------------------------------------------------------------------*/
#include "capi_types.h"

/** @addtogroup capi_if_ext_module_buffer_access
@{ */

/** Unique identifier to check if a given module support this extension. */
#define INTF_EXTN_MODULE_BUFFER_ACCESS 0x0A001061

/**
   Event ID used by module to inform the framework that it can access module's buffer for processing data
   directly into the module's buffer and avoid a memory copy.

   This extension can be used by framework to access the module's internal buffer directly for processing.
   For example, HW end point source module can directly provide the buffer pointer of the current interrupt
   for the framework to process. This avoids a fwk from module buffer to the fwk topo buffer. Similary if
   fwk has access to hardware sink module buffer for the current interrupt process, fwk can assign the buffer
   to upstream modules to directly process the data in the module's buffer and avoid a copy from topo to module
   buffer.

   The extension is defined such a way that fwk can query the module, for a specific input or output port. Module needs
   to raise the event for each of the output port using the capi api capi_event_info_t. And the frame work is expected
   to return the buffer back to module. The event payload has a module buffer manager buffer callback that can be used
   to get/return the module buffer.

   The way framework can gets and returns the buffer is slightly different for input and output.
   Descrbied the behavior below:


   OUTPUT PORT CONTEXT:
    1. Module can raise enable event once it knows output media format.
    2. Typically source hardware endpoint modules outputting interleaved or 1ch data are good examples to use this
       extension.
    3. When the extension is enabled and framework call capi process,
        i)  If framework doesn't provide a capi output buffer, module must populate the output sdata.
        ii) If framework provides its own output buffer, module has copy data into it. And it should not update the
            output buffer.  In ext output cases, module might pass the external buffer directly to the module to
            process.
    4. Framework must return the output buffer pointers using the callback function provided by the module.
    5. Framework might can call the return with just the first buffer in the capi_buf_t array populated. Its assumed
       that rest of the buffers in the capi_buf_t are also returned.
    6. Once module raises disable event, framework must return the buffers immediately to the module.
    7. **Framework can hold the buffer for the time duration equal to the PCM time duration of the data in the output
       buffer. If its held any longer, module might need the buffer to process the next frame, and lead to overrun in
       the module due to output unavailability. The module must follow the below guidelines to handle the overrun
   issues. Guidelines - a) When module shares a buffer with framework it should track that this buffer is shared with
   the framework. c) If the framework call capi process without returning earlier buffer, its an error scenario. Module
   can return a capi error. b) TODO: If the framework returns the buffer later than expected or doesnt return by the
   time module needs it, module need to raise an error event. c) Framework will try to handle any recover like resetting
   the module to handle the recovery.

   ### INPUT PORT CONTEXT:

    1. Once the module raises the enable event, Framework can query only once to get the buffer for the next process
   frame, write data into it.
    2. Frame work can return the buffer module in two ways,
         a) Framework can call module prcoess with the same sdata buffers to mark the return of those buffers.
         b) Framework can call the return function to return the buffers early, this will happen only if there is
            reset/graph stop received before the module process could be called.
    3. Framework can provide a topo buffer instead of module buffer in the following cases -
         a) Framework can skip getting a buffer and call the process with a topo buffer. Module must handle by copying
   from topo to internal buffer. b) Framework can return the buffer before capi process, and call process with a topo
   buffer. Module must handle by copying from topo to internal buffer. 4) If framework queries the input buffer but
   provides a different buffer in capi process its a error. Module must return capi process error.
    4. Tyically this extension is applicable for the sink end point modules when its handling interleaved data or 1 ch.
    5. Framework can hold the buffer for the time duration equal to the PCM frame time duration of the module. If not
   returned well within expected time, module may underrun due to the input unavailability.
    6. The module must follow the below guidelines to handle the underrun issues due to the framework holding buffer
   beyong its expected time i.e frame length. Guidelines - a) When module shares a buffer with framework it should mark
   that this buffer is shared with the framework. b) If the framework returns the buffer later than expected or doesnt
   return by the time module needs it, module need to raise an error event. c) If the framework queries for a buffer
   before returning earlier queuried buffer its an error sceanrio and module will return an error.

    Payload format:
            <struct  capi_event_info_t >
            <struct  capi_event_data_to_dsp_service_t >
            <struct  intf_extn_event_id_module_buffer_access_enable_t  >
            <struct intf_extn_input_buffer_manager_cb_info_t (or) intf_extn_output_buffer_manager_cb_info_t>

    @msgpayload{intf_extn_event_id_module_buffer_access_enable_t}
    @tablens{weak__intf__extn__event__id__module__buffer__access__enable__t}
*/
#define INTF_EXTN_EVENT_ID_MODULE_BUFFER_ACCESS_ENABLE 0x0A001062

typedef struct intf_extn_event_id_module_buffer_access_enable_t intf_extn_event_id_module_buffer_access_enable_t;

typedef capi_err_t (*intf_extn_get_module_input_buf_func_t)(uint32_t    buffer_mgr_cb_handle,
                                                            uint32_t    port_index,
                                                            uint32_t   *num_bufs,
                                                            capi_buf_t *buffer_ptr);

typedef capi_err_t (*intf_extn_return_module_output_buf_func_t)(uint32_t    buffer_mgr_cb_handle,
                                                                uint32_t    port_index,
                                                                uint32_t   *num_bufs,
                                                                capi_buf_t *buffer_ptr);

/** @weakgroup weak_intf_extn_event_id_module_buffer_access_enable_t
@{ */
struct intf_extn_event_id_module_buffer_access_enable_t
{
   uint32_t enable;
   /** 1. Indicates if the extension is supported or not.
       2. At any given point, if the module disablse the extn, fwk is expected to immediately return the buffers shared
      by the modules. If process gets called without returning buffers module's can return error. */

   /** Following this structure, there is a variable payload depending upon whether event is raised in input/output
    *  context. It can be either:
    *      <struct intf_extn_input_buffer_manager_cb_info_t>
    *             or
    *      <struct intf_extn_output_buffer_manager_cb_info_t>
    */
};
/** @} */ /* end_weakgroup weak_intf_extn_event_id_module_buffer_access_enable_t */

typedef struct intf_extn_input_buffer_manager_cb_info_t intf_extn_input_buffer_manager_cb_info_t;

/** @weakgroup intf_extn_input_buffer_manager_cb_info_t
@{ */
struct intf_extn_input_buffer_manager_cb_info_t
{
   uint32_t buffer_mgr_cb_handle;
   /** indicates if the extension is supported or not. At any given point, if the module disable the extn, fwk is
    * expected to immediately return the buffers shared by the modules.*/

   intf_extn_get_module_input_buf_func_t get_input_buf_fn;
   /* Framework uses this to request the module to share input buffer to process. Framework calls the module process
    * with the same as the capi input buffer, this marks return of the buffer to module. If a module gets algo
    * reset/port stop, it can that fwk is no longer using the buffer, and consider that buffer is returned to module. */

   /*
   TODO: fwk needs to be carefully when propagating the sink module buffer to upstream to a different SG. Because if
   SINK SG gets a stop, Upstream SG might continue to use the buffer. need to propagate buffer only till the sink SG
   boundary. Same applies in the source module SG as well.
   */
};
/** @} */ /* end_weakgroup intf_extn_input_buffer_manager_cb_info_t */

typedef struct intf_extn_output_buffer_manager_cb_info_t intf_extn_output_buffer_manager_cb_info_t;

/** @weakgroup intf_extn_output_buffer_manager_cb_info_t
@{ */
struct intf_extn_output_buffer_manager_cb_info_t
{
   uint32_t buffer_mgr_cb_handle;
   /** indicates if the extension is supported or not. At any given point, if the module disable the extn, fwk is
    * expected to immediately return the buffers shared by the modules.*/

   intf_extn_return_module_output_buf_func_t return_output_buf_fn;
   /** For source mdole framework uses this function only to return buffer back once processing or buffer is not needed
    * anymore due to graph commands. If module returns error for free and need to handle recovery if possible.
    */
};
/** @} */ /* end_weakgroup intf_extn_output_buffer_manager_cb_info_t */

#define INTF_EXTN_EVENT_ID_MODULE_BUFFER_ACCESS_ENABLE_V2 0x0A001060

typedef struct intf_extn_event_id_module_buffer_access_enable_v2_t intf_extn_event_id_module_buffer_access_enable_v2_t;

typedef capi_err_t (*intf_extn_get_module_port_buf_func_v2_t)(uint32_t    buffer_mgr_cb_handle,
                                                            uint32_t    port_index,
                                                            uint32_t   *num_bufs,
                                                            capi_buf_t *buffer_ptr);

typedef capi_err_t (*intf_extn_return_module_port_buf_func_v2_t)(uint32_t    buffer_mgr_cb_handle,
                                                                uint32_t    port_index,
                                                                uint32_t   *num_bufs,
                                                                capi_buf_t *buffer_ptr);

/** @weakgroup intf_extn_event_id_module_buffer_access_enable_v2_t
@{ */
struct intf_extn_event_id_module_buffer_access_enable_v2_t
{
   uint32_t enable;
   /** 1. Indicates if the extension is supported or not.
       2. At any given point, if the module disablse the extn, fwk is expected to immediately return the buffers shared
      by the modules. If process gets called without returning buffers module's can return error. */

   uint32_t buffer_mgr_cb_handle;
   /** indicates if the extension is supported or not. At any given point, if the module disable the extn, fwk is
    * expected to immediately return the buffers shared by the modules.*/

   intf_extn_get_module_port_buf_func_v2_t get_port_buf_fn;
   /* Framework uses this to request the module to share port buffer to process. This function can be used to query buffer
    * for input/output ports. If the module provides the buffer for a given port, fwk assigns the same data buffer to the corresponding
    * ports capi_stream_data_v2_t when it calls capi process().

    * If module doesnt provide the buffer or returns an CAPI error framework assigns a topo buffer
    * for that port before calling the process.
    *
    * Framework calls the module process with the same as the capi stream buffer, this marks return of the buffer to module.
    *
    * If the fwk wants to return the buffer before it calls the process for any reason, it can use return_port_buf_fn() for
    * example if graph stops or there is an algorithmic reset.*/

   intf_extn_return_module_port_buf_func_v2_t return_port_buf_fn;
   /** For module framework uses this function only to return buffer back once processing or buffer is not needed
    * anymore due to graph commands. If module returns error for free and need to handle recovery if possible.
    */
};
/** @} */ /* end_weakgroup intf_extn_event_id_module_buffer_access_enable_v2_t */

/** @} */ /* end_addtogroup capi_if_ext_module_buffer_access */
#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* CAPI_INTF_EXTN_ENDPOINT_BUFFERING_H */
