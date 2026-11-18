#ifndef AMDB_INTERNAL_H
#define AMDB_INTERNAL_H
/**
 * \file amdb_internal.h
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi.h"
#include "stringl.h"
#include "amdb_static.h"
#include "amdb_autogen_def.h"
#include "shared_lib_api.h"
#include "posal.h"
#include "amdb_thread.h"
#include "spf_hashtable.h"
#include "amdb_parallel_loader.h"
#include "posal_inline_mutex.h"

#include "posal_rtld.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*----------------------------------------------------------------------------------------------------------------------
   Utility Macros
  --------------------------------------------------------------------------------------------------------------------*/

#ifdef STD_OFFSETOF
#undef STD_OFFSETOF
#endif

#ifdef STD_RECOVER_REC
#undef STD_RECOVER_REC
#endif

#define STD_OFFSETOF(type, member) (((char *)(&((type *)1)->member)) - ((char *)1))

#define STD_RECOVER_REC(type, member, p)                                                                               \
   ((void)((p) - &(((type *)1)->member)), (type *)(void *)(((char *)(void *)(p)) - STD_OFFSETOF(type, member)))

#define TRY(exception, func)                                                                                           \
   if (AR_EOK != (exception = func))                                                                                   \
   {                                                                                                                   \
      goto exception##bail;                                                                                            \
   }

#define THROW(exception, errno)                                                                                        \
   exception = errno;                                                                                                  \
   goto exception##bail;

#define CATCH(exception) exception##bail : if (exception != AR_EOK)

static inline uint32_t align_to_8_byte(const uint32_t num)
{
   return ((num + 7) & (0xFFFFFFF8));
}

/*----------------------------------------------------------------------------------------------------------------------
   Constants
  --------------------------------------------------------------------------------------------------------------------*/

/** ideally dlfcn.h must define these macros. but in some targets they are not defined. this is a work-around.*/
#ifndef RTLD_DI_LOAD_ADDR
#define RTLD_DI_LOAD_ADDR 3
#endif

#ifndef RTLD_DI_LOAD_SIZE
#define RTLD_DI_LOAD_SIZE 4
#endif

static const uint32_t AMDB_NODES_HASH_TABLE_SIZE    = 16;
static const uint32_t AMDB_CLIENT_TABLE_SIZE        = 4;
static const uint32_t AMDB_HASH_TABLE_RESIZE_FACTOR = 2;

/*----------------------------------------------------------------------------------------------------------------------
   Main AMDB structure
  --------------------------------------------------------------------------------------------------------------------*/
typedef struct amdb_t
{
   spf_hashtable_t ht;
   spf_hashtable_t load_ht;
   posal_mutex_t   mutex_node;
   void *          loader_ptr;
   POSAL_HEAP_ID   heap_id;
} amdb_t;

/*< Single structure to hold all the flags needed by amdb */
typedef struct amdb_flags_t
{
   uint32_t is_static : 1;      /*< Flag to if the module is static or dynamic, stubs are consider static as well*/
   uint32_t is_built_in : 1;    /*< Flag to indicate if the module is a built in module */
   uint32_t interface_type : 2; /*< Indicates if the module is of type stub, capiv2 etc*/
   uint32_t module_type : 4;    /*< Indicates if the module is of type encode/decode/generic etc*/
} amdb_flags_t;

/*< Structure holds all the dynamic loading related variables. Must follow an amdb_capi_t in the same allocation */
typedef struct amdb_dynamic_t
{
   // add attibute aligned here
   posal_inline_mutex_t dl_mutex __attribute__((aligned (8)));     /*< To prevent multiple dlopen calls for this module */
   uint32_t      dl_refs;      /*< Tracks number of load requests on a particular so file */

   char *        tag_str;      /*< Null terminated, tag string from which function strings can be generated */
   char *        filename_str; /*< Null terminated so filename string */
   void *        h_dlopen;     /*< Handle returned by dlopen */
} amdb_dynamic_t;

/*< Base amdb structure which holds hash node and other things*/
typedef struct amdb_node_t
{
   spf_hash_node_t     hn;       /*< node use by hashtable or simple hashtable*/
   posal_atomic_word_internal_t mem_refs; /*< tracks the number of entities holding a pointer to this struct.*/
   int32_t             key;      /*< aka module is, used as a key for hashing*/
   amdb_flags_t        flags;    /*< Holds all the required flags*/
} amdb_node_t;

/*< Capiv2 handle structure */
typedef struct amdb_capi_t
{
   amdb_node_t                  node;                    /*< Must be the first element of the structure. */
   capi_get_static_properties_f get_static_properties_f; /*< function pointer*/
   capi_init_f                  init_f;                  /*< function pointer*/
} amdb_capi_t;

/*< stub handle structure */
typedef struct amdb_stub_t
{
   amdb_node_t node; /*< Must be the first element of the structure. */
} amdb_stub_t;

// capi_wrapper hijacks the objects vtbl adds a wrapper to the end function
// this method of tracking module lifetime is not the cleaneset due to the
// assumption that we can point the object to the wrappers vtbl without any
// ill side effect.  But, it does provide for the least overhead since the rest
// of the capi functions are direct calls
typedef struct capi_wrapper_t
{
   const capi_vtbl_t *pvtbl;
   capi_t *           icapi;
   amdb_capi_t *      capi_ptr;
} capi_wrapper_t;
/*----------------------------------------------------------------------------------------------------------------------
   Some utility amdb functions
  --------------------------------------------------------------------------------------------------------------------*/
/*<< Function to register all the built in modules, both static and dynamic*/
ar_result_t amdb_reg_built_in_modules();

ar_result_t amdb_node_init(amdb_node_t *me,
                           bool_t       is_static,
                           bool_t       is_built_in,
                           uint32_t     type,
                           uint32_t     id1,
                           void *       f1,
                           void *       f2,
                           uint32_t     tag_len,
                           const char * tag_str,
                           uint32_t     filename_len,
                           const char * filename_str);
amdb_node_t *amdb_node_hashtable_find(spf_hashtable_t *ht, const void *key_ptr, int key_size);
ar_result_t amdb_insert_node(amdb_node_t *node_ptr);
ar_result_t amdb_get_node(amdb_module_handle_info_t *module_info_ptr, bool_t *to_be_loaded);
bool_t amdb_get_details_from_mid(uint32_t module_id, amdb_node_t** node_pptr);
ar_result_t amdb_remove_all_capi(void);

/*----------------------------------------------------------------------------------------------------------------------
   Vtable definition
  --------------------------------------------------------------------------------------------------------------------*/
typedef struct amdb_node_vtbl_t
{
   ar_result_t (*resolve_symbols)(amdb_node_t *me);
   void (*store_static_functions)(amdb_node_t *me, void *f1_ptr, void *f2_ptr);
   void (*clear_symbols)(amdb_node_t *me);
   void (*end)(amdb_node_t *me);
} amdb_node_vtbl_t;

/*< Capiv2 vtable functions */
ar_result_t capi_resolve_symbols(amdb_node_t *node_ptr);
void capi_store_static_functions(amdb_node_t *node_ptr, void *f1_ptr, void *f2_ptr);
void capi_clear_symbols(amdb_node_t *node_ptr);
void capi_end(amdb_node_t *node_ptr);

/*< stub vtable functions */
ar_result_t stub_resolve_symbols(amdb_node_t *node_ptr);
ar_result_t stub_store_function_names(amdb_node_t *node_ptr, const char *f1_name_ptr, const char *f2_name_ptr);
void stub_store_static_functions(amdb_node_t *node_ptr, void *f1_ptr, void *f2_ptr);
void stub_clear_symbols(amdb_node_t *node_ptr);
void stub_end(amdb_node_t *node_ptr);

/*----------------------------------------------------------------------------------------------------------------------
   Default Callback mechanism definitions
  --------------------------------------------------------------------------------------------------------------------*/
typedef struct amdb_default_callback_t
{
   posal_condvar_t condition_var;
   posal_nmutex_t  nmutex;
   bool_t          signal_set;
} amdb_default_callback_t;

void amdb_default_callback(void *context);
void amdb_init_callback_mechanism(amdb_default_callback_t *obj_ptr);
void amdb_deinit_callback_mechanism(amdb_default_callback_t *obj_ptr);
void amdb_wait_deinit_callback_mechanism(amdb_default_callback_t *obj_ptr);

/*----------------------------------------------------------------------------------------------------------------------
   Dynamic loading structure and function definitions
  --------------------------------------------------------------------------------------------------------------------*/
typedef union amdb_dynamic_load_task_t
{
   uint64_t                   data;
   amdb_module_handle_info_t *handle_info_ptr;
} amdb_dynamic_load_task_t;

extern const amdb_node_vtbl_t amdb_node_vtbl[];

ar_result_t node_init_dynamic(amdb_node_t * me,
                              bool_t        is_built_in,
                              uint32_t      tag_len,
                              const char *  tag_str,
                              uint32_t      filename_len,
                              const char *  filename_str,
                              POSAL_HEAP_ID heap_id);
ar_result_t check_dynamic_loading(amdb_node_t *me, bool_t *to_be_loaded);
void handle_get_modules_request_list(spf_list_node_t *           module_handle_info_list_ptr,
                                     amdb_get_modules_callback_f callback_function,
                                     void *                      callback_context);
void amdb_dynamic_loader(uint64_t task_info);

/*<< Function to free the load modules handle after unload */
void amdb_load_handle_holder_free(void *void_ptr, spf_hash_node_t *node);
ar_result_t node_wrapper_dlopen(amdb_node_t *me, const char *filename_str);
void *amdb_node_to_handle(amdb_node_t *node_ptr);
amdb_node_t *amdb_handle_to_node(void *handle_ptr);
ar_result_t amdb_init_dynamic(POSAL_HEAP_ID heap_id);
void        amdb_deinit_dynamic();
ar_result_t amdb_wait_for_dynamic_module_additions(uint32_t client_id);
void amdb_node_inc_mem_ref(amdb_node_t *me);
void amdb_node_inc_dl_ref(amdb_node_t *me);
void amdb_node_dec_mem_ref(amdb_node_t *me);
void amdb_node_dec_dl_ref(amdb_node_t *me);
void amdb_node_free(void *void_ptr, spf_hash_node_t *node);

static inline amdb_dynamic_t* amdb_node_get_dyn(amdb_node_t *node_ptr)
{
   return (amdb_dynamic_t*) ((uint8_t*)node_ptr + align_to_8_byte(sizeof(amdb_capi_t)));
}

ar_result_t  amdb_resolve_symbols(amdb_node_t *node_ptr);
ar_result_t  amdb_dyn_module_version(amdb_node_t *node_ptr, amdb_module_version_info_payload_t *module_version_ptr);
ar_result_t amdb_get_capi_module_version(amdb_node_t *node_ptr, capi_module_version_info_t *version_payload_ptr);

capi_err_t capi_wrapper_process(capi_t *icapi, capi_stream_data_t *input[], capi_stream_data_t *output[]);
capi_err_t capi_wrapper_set_param(capi_t *                icapi,
                                  uint32_t                param_id,
                                  const capi_port_info_t *port_info_ptr,
                                  capi_buf_t *            params_ptr);
capi_err_t capi_wrapper_get_param(capi_t *                icapi,
                                  uint32_t                param_id,
                                  const capi_port_info_t *port_info_ptr,
                                  capi_buf_t *            params_ptr);
capi_err_t capi_wrapper_set_properties(capi_t *icapi, capi_proplist_t *props_ptr);

capi_err_t capi_wrapper_get_properties(capi_t *icapi, capi_proplist_t *props_ptr);
capi_err_t capi_wrapper_end(capi_t *icapi);
ar_result_t capi_wrapper_init(capi_wrapper_t *me, capi_t *icapi, amdb_capi_t *capi_ptr);

ar_result_t amdb_internal_handle_dl_open_failure(amdb_module_handle_info_t *module_handle_info,
                                                 amdb_node_t *              node_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus
#endif // AMDB_INTERNAL_H
