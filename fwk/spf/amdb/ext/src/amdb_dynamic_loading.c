/**
 * \file amdb_dynamic_loading.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "amdb_internal.h"

#include "ar_msg.h"
extern amdb_t *g_amdb_ptr;

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None
  Default call back mechanism - for synchronous response when get_modules_request is called
  This is called when all the loading is finished
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_default_callback(void *context)
{
   amdb_default_callback_t *obj_ptr = (amdb_default_callback_t *)(context);
   posal_nmutex_lock(obj_ptr->nmutex);
   obj_ptr->signal_set = TRUE;
   posal_condvar_signal(obj_ptr->condition_var);
   posal_nmutex_unlock(obj_ptr->nmutex);
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None
  Initializes the mutex and conditional variable needed to synchronous default callback
  mechanism implementations
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_init_callback_mechanism(amdb_default_callback_t *obj_ptr)
{
   amdb_t *amdb_ptr = g_amdb_ptr;
   posal_nmutex_create(&obj_ptr->nmutex, amdb_ptr->heap_id);
   posal_condvar_create(&obj_ptr->condition_var, amdb_ptr->heap_id);
   obj_ptr->signal_set = FALSE;
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None
  Waits until the default callback is called
  This happens when all the dynamic loading requested by the client is finished
  It deints mutex and cond var and destroys them
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_deinit_callback_mechanism(amdb_default_callback_t *obj_ptr)
{
   posal_nmutex_destroy(&obj_ptr->nmutex);
   posal_condvar_destroy(&obj_ptr->condition_var);
}
/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None
  Waits until the default callback is called
  This happens when all the dynamic loading requested by the client is finished
  Once it receives the cond variable signal, it deints mutex and cond var and destroys them
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_wait_deinit_callback_mechanism(amdb_default_callback_t *obj_ptr)
{
   posal_nmutex_lock(obj_ptr->nmutex);
   while (!obj_ptr->signal_set)
   {
      posal_condvar_wait(obj_ptr->condition_var, obj_ptr->nmutex);
   }
   posal_nmutex_unlock(obj_ptr->nmutex);
   amdb_deinit_callback_mechanism(obj_ptr);
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : result

  Creates dynamic parallel loader, threads, queue etc
  --------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_init_dynamic(POSAL_HEAP_ID heap_id)
{
   amdb_t *amdb_ptr     = g_amdb_ptr;
   amdb_ptr->loader_ptr = amdb_loader_create(heap_id);
   if (NULL == amdb_ptr->loader_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "amdb: amdb loader init failed");
      return AR_EFAILED;
   }
   return AR_EOK;
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : result

  Destroys dynamic parallel loader, threads, queue etc
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_deinit_dynamic()
{
   amdb_t *amdb_ptr = g_amdb_ptr;
   amdb_loader_destroy(amdb_ptr->loader_ptr);
   amdb_ptr->loader_ptr = NULL;
}
/*---------------------------------------------------------------------------------------------------------------------
  IN  : amdb node pointer
  OUT : None

  Atomically increaments the dl ref variable
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_node_inc_dl_ref(amdb_node_t *me)
{
   amdb_dynamic_t *dyn = amdb_node_get_dyn(me);

   ++(dyn->dl_refs);
   return;
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : amdb node pointer
  OUT : None

  Atomically decreaments the mem ref variable
  Closes the opened dynamic module using posal_dlclose if dl reference is 0
  --------------------------------------------------------------------------------------------------------------------*/
void amdb_node_dec_dl_ref(amdb_node_t *me)
{
   if (!me->flags.is_static)
   {
      amdb_dynamic_t *dyn = amdb_node_get_dyn(me);
      // Unload the so file if no longer needed.
      posal_mutex_lock_inline(&dyn->dl_mutex);
      dyn->dl_refs--;
      if (0 == dyn->dl_refs)
      {
         if (0 != dyn->h_dlopen)
         {
            posal_dlclose(dyn->h_dlopen);
            dyn->h_dlopen = 0;
         }
      }
      posal_mutex_unlock_inline(&dyn->dl_mutex);
   }
   return;
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : amdb node
  OUT : result

  Allocates memory for the dynamic part of the amdb node and initializes it
  --------------------------------------------------------------------------------------------------------------------*/
ar_result_t node_init_dynamic(amdb_node_t  *me,
                              bool_t        is_built_in,
                              uint32_t      tag_len,
                              const char   *tag_str,
                              uint32_t      filename_len,
                              const char   *filename_str,
                              POSAL_HEAP_ID heap_id)
{

   amdb_dynamic_t *dyn = amdb_node_get_dyn(me);

   if (NULL == filename_str)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Null filename encountered for dynamic module");
      return AR_EFAILED;
   }

   if (NULL == tag_str)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Null tag encountered for dynamic module");
      return AR_EFAILED;
   }

   if (!is_built_in)
   {
      dyn->tag_str      = (char *)(dyn + 1);
      dyn->filename_str = dyn->tag_str + tag_len + 1;

      memscpy(dyn->tag_str, tag_len, tag_str, tag_len);
      dyn->tag_str[tag_len] = '\0';

      memscpy(dyn->filename_str, filename_len, filename_str, filename_len);
      dyn->filename_str[filename_len] = '\0';
   }
   else
   {
      dyn->tag_str      = (char *)tag_str;
      dyn->filename_str = (char *)filename_str;
   }

   posal_inline_mutex_init(&dyn->dl_mutex);
   return AR_EOK;
}

/*---------------------------------------------------------------------------------------------------------------------
 *
  --------------------------------------------------------------------------------------------------------------------*/
ar_result_t check_dynamic_loading(amdb_node_t *me, bool_t *to_be_loaded)
{
   amdb_dynamic_t *dyn = amdb_node_get_dyn(me);

   posal_mutex_lock_inline(&dyn->dl_mutex);
   if (NULL == dyn->h_dlopen)
   {
      *to_be_loaded = TRUE;
   }
   else
   {
      amdb_node_inc_dl_ref(me);
   }
   posal_mutex_unlock_inline(&dyn->dl_mutex);
   return AR_EOK;
}

/*---------------------------------------------------------------------------------------------------------------------

  Push the dynamic loading tasks for loading
  --------------------------------------------------------------------------------------------------------------------*/
void handle_get_modules_request_list(spf_list_node_t            *module_handle_info_list_ptr,
                                     amdb_get_modules_callback_f callback_function,
                                     void                       *callback_context)
{
   amdb_t                     *amdb_ptr           = g_amdb_ptr;
   void                       *dynamic_loader_ptr = NULL;
   amdb_default_callback_t     default_cb_obj;
   amdb_dynamic_load_task_t    dynamic_load_task = { 0 };
   amdb_get_modules_callback_f cb_function;
   void                       *cb_context = NULL;

   // AR_MSG(DBG_HIGH_PRIO, "AMDB_GET: Get modules called");

   if (NULL == callback_function)
   {
      cb_function = amdb_default_callback;
      amdb_init_callback_mechanism(&default_cb_obj);
      cb_context = (void *)&default_cb_obj;
   }
   else
   {
      cb_function = callback_function;
      cb_context  = callback_context;
   }

   while (module_handle_info_list_ptr)
   {
      amdb_module_handle_info_t *h_info_ptr = (amdb_module_handle_info_t *)module_handle_info_list_ptr->obj_ptr;

      bool_t to_be_loaded = FALSE;
      h_info_ptr->result  = amdb_get_node(h_info_ptr, &to_be_loaded);
      if (AR_SUCCEEDED(h_info_ptr->result) && to_be_loaded)
      {
         if (NULL == dynamic_loader_ptr)
         {
            dynamic_loader_ptr =
               amdb_loader_get_handle(amdb_ptr->loader_ptr, cb_function, cb_context, amdb_dynamic_loader);
            if (NULL == dynamic_loader_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "amdb: Null parallel loader handle obtained");
               return;
            }
         }
         dynamic_load_task.handle_info_ptr = h_info_ptr;
         amdb_loader_push_task(dynamic_loader_ptr, dynamic_load_task.data);
      }

      module_handle_info_list_ptr = module_handle_info_list_ptr->next_ptr;
   }

   if (NULL == dynamic_loader_ptr)
   {
      if (NULL != callback_function)
      {
         cb_function(cb_context);
      }
      else
      {
         amdb_deinit_callback_mechanism((amdb_default_callback_t *)cb_context);
      }
   }
   else
   {
      if (NULL != dynamic_loader_ptr)
      {
         amdb_loader_release_handle(dynamic_loader_ptr);
      }

      if (NULL == callback_function)
      {
         amdb_wait_deinit_callback_mechanism((amdb_default_callback_t *)cb_context);
      }
   }
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None

  --------------------------------------------------------------------------------------------------------------------*/
void amdb_dynamic_loader(uint64_t task_info)
{
   amdb_dynamic_load_task_t task;
   task.data                                     = task_info;
   amdb_module_handle_info_t *module_handle_info = task.handle_info_ptr;
   amdb_node_t               *node_ptr           = amdb_handle_to_node(module_handle_info->handle_ptr);
   // AR_MSG(DBG_MED_PRIO, "AMDB:inside dynamic module loader");
   amdb_dynamic_t *dyn = amdb_node_get_dyn(node_ptr);

   posal_mutex_lock_inline(&dyn->dl_mutex);
   if (0 == dyn->dl_refs)
   {
      ar_result_t res = node_wrapper_dlopen(node_ptr, dyn->filename_str);

      if(AR_EOK != res)
      {
         res = amdb_internal_handle_dl_open_failure(module_handle_info, node_ptr);
      }

      if (AR_EOK != res)
      {
         AR_MSG(DBG_ERROR_PRIO, "AMDB: Encountered error while loading dynamic module %s", dyn->filename_str);
         posal_mutex_unlock_inline(&dyn->dl_mutex);
         amdb_node_dec_mem_ref(node_ptr);
         module_handle_info->handle_ptr = NULL;
         module_handle_info->result     = res;
      }
      else
      {
         amdb_node_inc_dl_ref(node_ptr);
         posal_mutex_unlock_inline(&dyn->dl_mutex);
      }
   }
   else
   {
      amdb_node_inc_dl_ref(node_ptr);
      posal_mutex_unlock_inline(&dyn->dl_mutex);
   }
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None

  --------------------------------------------------------------------------------------------------------------------*/
ar_result_t node_wrapper_dlopen(amdb_node_t *me, const char *filename_str)
{
   ar_result_t     result = AR_EOK;
   amdb_dynamic_t *dyn    = amdb_node_get_dyn(me);

   dyn->h_dlopen = posal_dlopen(filename_str, POSAL_RTLD_NOW);
   if (0 == dyn->h_dlopen)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: failed to open %s, %s", filename_str, posal_dlerror());
      goto err_cleanup_1;
   }

   result = amdb_node_vtbl[me->flags.interface_type - 1].resolve_symbols(me);
   if (AR_DID_FAIL(result))
   {
      goto err_cleanup_2;
   }

   return result;

err_cleanup_2:
   posal_dlclose(dyn->h_dlopen);
   dyn->h_dlopen = NULL;
err_cleanup_1:
   return AR_EFAILED;
}

/*---------------------------------------------------------------------------------------------------------------------
  IN  : None
  OUT : None

  --------------------------------------------------------------------------------------------------------------------*/
void amdb_get_dl_info(amdb_module_handle_info_t *module_handle_info, // handle to the amdb.
                      bool_t                    *is_dl,              // is module dynamically loaded.
                      uint32_t                 **start_addr,         // start address (virtual) where the lib is loaded.
                      uint32_t                  *so_size)                             // size of the loaded library.
{
   *is_dl      = FALSE;
   *start_addr = NULL;
   *so_size    = 0;
   if (AR_DID_FAIL(module_handle_info->result))
   {
      return;
   }
   amdb_node_t *node = amdb_handle_to_node(module_handle_info->handle_ptr);

   if (NULL == node)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: null node during dl_info");
      return;
   }
   if (node->flags.is_static)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: static node during dl_info");
      return;
   }
   *is_dl = TRUE;

#ifdef DL_INFO_DEFINED
   amdb_dynamic_t *dyn = amdb_node_get_dyn(node);
   int             p   = 0xABCD;

   int rc = posal_dlinfo(dyn->h_dlopen, RTLD_DI_LOAD_ADDR, &p);
   if (rc)
      return;
   *start_addr = (uint32_t *)p;

   rc = posal_dlinfo(dyn->h_dlopen, RTLD_DI_LOAD_SIZE, &p);
   if (rc)
   {
      *start_addr = NULL;
      return;
   }
   *so_size = p;
#endif
}

ar_result_t amdb_resolve_symbols(amdb_node_t *node_ptr)
{
   ar_result_t     result    = AR_EOK;
   amdb_capi_t    *me        = (amdb_capi_t *)(node_ptr);
   const char     *stat_prop = "_get_static_properties";
   const char     *init      = "_init";
   char           *ptr       = NULL;
   amdb_dynamic_t *dyn       = amdb_node_get_dyn(node_ptr);

   if (!node_ptr->flags.is_static)
   {
      // if DL handle is not valid throw error
      if(NULL == dyn->h_dlopen)
      {
         THROW(result, AR_EFAILED);
      }
      uint32_t len1      = strlen(stat_prop);
      uint32_t len2      = strlen(init);
      uint32_t len3      = strlen((const char *)dyn->tag_str);
      uint32_t total_len = (len1 + len2 + ((len3 + 1) * 2));

      ptr = (char *)posal_memory_malloc((total_len), g_amdb_ptr->heap_id);
      if (NULL == ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi: cannot allocate memory while resolving symbols");
         THROW(result, AR_ENOMEMORY);
      }

      char *get_static_properties_str = ptr;
      memscpy(get_static_properties_str, len3, dyn->tag_str, len3);
      memscpy(get_static_properties_str + len3, len1 + 1, stat_prop, len1 + 1);

      char *init_str = ptr + len1 + len3 + 1;
      memscpy(init_str, len3, dyn->tag_str, len3);
      memscpy(init_str + len3, len2 + 1, init, len2 + 1);

      me->get_static_properties_f = (capi_get_static_properties_f)posal_dlsym(dyn->h_dlopen, get_static_properties_str);
      if (!me->get_static_properties_f)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi: dlsym failed %s, %s", get_static_properties_str, posal_dlerror());
         THROW(result, AR_EFAILED);
      }

      me->init_f = (capi_init_f)posal_dlsym(dyn->h_dlopen, init_str);
      if (!me->init_f)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi: dlsym failed %s , %s", init_str, posal_dlerror());
         THROW(result, AR_EFAILED);
      }
   }

   CATCH(result)
   {
   }

   if (NULL != ptr)
   {
      posal_memory_free(ptr);
   }
   return result;
}

static ar_result_t amdb_dyn_fill_out_version_info(amdb_node_t                        *node_ptr,
                                                  amdb_module_version_info_payload_t *module_version_ptr,
                                                  amdb_dynamic_t                     *dyn)
{
   ar_result_t result = AR_EOK;
   // set up the module version static property and get it
   result = amdb_resolve_symbols(node_ptr);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: failed to resolve symbols");
      return result;
   }

   capi_module_version_info_t module_version_info;
   result                                   = amdb_get_capi_module_version(node_ptr, &module_version_info);
   module_version_ptr->module_version_major = module_version_info.version_major;
   module_version_ptr->module_version_minor = module_version_info.version_minor;

   // Now, dlsym for the build ts
   uint8_t *build_ts_ptr = posal_dlsym(dyn->h_dlopen, "spf_build_property_ts_info");
   if (NULL == build_ts_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: failed to resolve symbols");
      return AR_EFAILED;
   }
   memscpy(&module_version_ptr->build_ts,
           sizeof(module_version_ptr->build_ts),
           build_ts_ptr,
           sizeof(module_version_ptr->build_ts));

   return result;
}

ar_result_t amdb_dyn_module_version(amdb_node_t *node_ptr, amdb_module_version_info_payload_t *module_version_ptr)
{
   ar_result_t result = AR_EOK;

   amdb_dynamic_t *dyn = amdb_node_get_dyn(node_ptr);

   posal_mutex_lock_inline(&dyn->dl_mutex);
   if (0 == dyn->dl_refs)
   {
      result = node_wrapper_dlopen(node_ptr, dyn->filename_str);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "AMDB: Encountered error while loading dynamic module %s", dyn->filename_str);
         posal_mutex_unlock_inline(&dyn->dl_mutex);
         module_version_ptr->error_code = AMDB_MODULE_INFO_ERR_GENERAL;
      }
      else
      {
         result = amdb_dyn_fill_out_version_info(node_ptr, module_version_ptr, dyn);
         result = posal_dlclose(dyn->h_dlopen);
         posal_mutex_unlock_inline(&dyn->dl_mutex);
      }
   }
   else
   {
      result = amdb_dyn_fill_out_version_info(node_ptr, module_version_ptr, dyn);
      posal_mutex_unlock_inline(&dyn->dl_mutex);
      if (AR_EOK != result)
      {
         module_version_ptr->error_code = AMDB_MODULE_INFO_ERR_GENERAL;
      }
   }

   return result;
}
