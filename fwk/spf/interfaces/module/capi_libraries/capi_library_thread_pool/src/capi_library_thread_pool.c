/**
 * \file capi_library_thread_pool.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_library_thread_pool.h"
#include "amdb_queue.h"
//#include "elite_thrd_prio.h"
#include "posal.h"
#include <stringl/stringl.h>

#define TH_POOL_TASK_Q_LENGTH (8)
#define TH_POOL_MAX_THREADS (4)
/** thread pool handle */
typedef struct th_pool_t
{
   uint32_t       num_threads;
   posal_thread_t threads[TH_POOL_MAX_THREADS];
   amdb_queue_t * task_queue_ptr;
} th_pool_t;

/** default callback info */
typedef struct th_pool_default_cb_t
{
   uint32_t        num_tasks; // num of tasks to wait
   posal_condvar_t condition_var;
   posal_nmutex_t  mutex;
} th_pool_default_cb_t;

/** function definition */
th_pool_t *th_pool_create(uint32_t   th_prio,
                          uint32_t   num_threads,
                          uint32_t   th_stack,
                          const char th_name[TH_POOL_TH_NAME_SIZE])
{
   posal_thread_prio_t thread_prio = (posal_thread_prio_t)th_prio;

   if (TH_POOL_MAX_THREADS < num_threads)
   {
      MSG_1(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: invalid number of threads %ld", num_threads);
      return NULL;
   }

   th_pool_t *obj_ptr = (th_pool_t *)posal_memory_malloc(sizeof(th_pool_t), POSAL_HEAP_DEFAULT);
   if (NULL == obj_ptr)
   {
      MSG(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: Failed to allocate the th pool memory.");
      return NULL;
   }

   memset(obj_ptr, 0, sizeof(th_pool_t));

   char task_q_name[] = "THPLQ";
   obj_ptr->task_queue_ptr =
      amdb_queue_create(TH_POOL_TASK_Q_LENGTH, sizeof(th_pool_task_t), task_q_name, POSAL_HEAP_DEFAULT);
   if (NULL == obj_ptr->task_queue_ptr)
   {
      MSG(MSG_SSID_QDSP6,
          DBG_ERROR_PRIO,
          "th_pool: Failed to allocate the task queue for the th pool parallel loader.");
      th_pool_destroy(obj_ptr);
      return NULL;
   }

   if (0 == thread_prio)
   {
      thread_prio = posal_thread_prio_get();
      // th_prio = posal_thread_prio_get();
      // We cannot use posal_thread_prio_get function as the thread pool function
      // can be called from ADM context as capiv2_init function is called from ADM context
   }

   // Threads should be launched only after everything else is initialized, since they may access the object.
   char thread_name[16];
   strlcpy(thread_name, th_name, sizeof(thread_name));
   strlcat(thread_name, "THPL0", sizeof(thread_name));

   for (uint32_t i = 0; i < num_threads; i++)
   {
      ar_result_t res;
      res = posal_thread_launch(&obj_ptr->threads[i],
                                thread_name,
                                th_stack,
                                thread_prio,
                                th_pool_work_loop,
                                obj_ptr,
                                POSAL_HEAP_DEFAULT);
      if (AR_DID_FAIL(res))
      {
         MSG_1(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: Failed to launch thread %lu.", i);
         th_pool_destroy(obj_ptr);
         return NULL;
      }

      obj_ptr->num_threads++;
      int32_t name_len = (int32_t)strlen(thread_name);
      if (name_len > 0)
      {
         thread_name[name_len - 1]++;
      }
   }

   // return object
   return obj_ptr;
}

void th_pool_destroy(th_pool_t *obj_ptr)
{
   if (NULL != obj_ptr)
   {
      if (obj_ptr->task_queue_ptr)
      {
         for (uint32_t i = 0; i < obj_ptr->num_threads; i++)
         {
            th_pool_task_t task;
            task.task_id = TH_POOL_EXIT_TASK_ID;
            amdb_queue_push(obj_ptr->task_queue_ptr, &task);
         }

         for (uint32_t i = 0; i < obj_ptr->num_threads; i++)
         {
            ar_result_t status = 0;
            posal_thread_join(obj_ptr->threads[i], &status);
         }

         if (NULL != obj_ptr->task_queue_ptr)
         {
            amdb_queue_destroy(obj_ptr->task_queue_ptr);
            obj_ptr->task_queue_ptr = NULL;
         }
      }
      posal_memory_free(obj_ptr);
   }
}

ar_result_t th_pool_work_loop(void *context)
{
   th_pool_t *obj_ptr = (th_pool_t *)(context);

   while (1)
   {
      th_pool_task_t task;
      amdb_queue_pop(obj_ptr->task_queue_ptr, &task);

      switch (task.task_id)
      {
         case TH_POOL_EXIT_TASK_ID:
            return 0;
         default:
         {
            // MSG_1(MSG_SSID_QDSP6, DBG_ERROR_PRIO,"th_pool: executing function %p",task.fn);
            task.fn(task.io_args);
            task.cb_fn(task.cb_context, &task);
            break;
         }
      }
   }
   return 0;
}

ar_result_t th_pool_push_task(th_pool_t *th_pool_handle, th_pool_task_t *task_ptr)
{
   if (th_pool_handle && task_ptr)
   {
      amdb_queue_push(th_pool_handle->task_queue_ptr, task_ptr);
      return AR_EOK;
   }
   else
   {
      MSG(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: NULL pointer error");
      return AR_EFAILED;
   }
}

/** default callback functions */
th_pool_default_cb_t *th_pool_default_cb_create(void)
{
   th_pool_default_cb_t *cb_ptr =
      (th_pool_default_cb_t *)posal_memory_malloc(sizeof(th_pool_default_cb_t), POSAL_HEAP_DEFAULT);
   if (NULL == cb_ptr)
   {
      MSG(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: failed to allocate memory for default cb");
      return NULL;
   }
   posal_nmutex_create(&cb_ptr->mutex, POSAL_HEAP_DEFAULT);
   posal_condvar_create(&cb_ptr->condition_var, POSAL_HEAP_DEFAULT);
   cb_ptr->num_tasks = 0;

   return cb_ptr;
}
void th_pool_set_num_tasks_to_wait(th_pool_default_cb_t *cb_info_ptr, uint32_t num_tasks)
{
   posal_nmutex_lock(cb_info_ptr->mutex);
   cb_info_ptr->num_tasks = num_tasks;
   posal_nmutex_unlock(cb_info_ptr->mutex);
}

void th_pool_wait_for_task_complete(th_pool_default_cb_t *cb_info_ptr)
{
   posal_nmutex_lock(cb_info_ptr->mutex);
   while (cb_info_ptr->num_tasks)
   {
      posal_condvar_wait(cb_info_ptr->condition_var, cb_info_ptr->mutex);
   }
   posal_nmutex_unlock(cb_info_ptr->mutex);
}

void th_pool_default_cb_destroy(th_pool_default_cb_t *cb_info_ptr)
{
   if (cb_info_ptr)
   {
      posal_nmutex_destroy(&cb_info_ptr->mutex);
      posal_condvar_destroy(&cb_info_ptr->condition_var);
      posal_memory_free(cb_info_ptr);
   }
}

void th_pool_default_cb_fn(void *cb_contex, th_pool_task_t *task_info)
{
   th_pool_default_cb_t *obj_ptr = (th_pool_default_cb_t *)(cb_contex);
   posal_nmutex_lock(obj_ptr->mutex);
   if (0 == obj_ptr->num_tasks)
   {
      MSG(MSG_SSID_QDSP6, DBG_ERROR_PRIO, "th_pool: invalid num_tasks");
   }
   else
   {
      obj_ptr->num_tasks--;
   }
   posal_condvar_signal(obj_ptr->condition_var);
   posal_nmutex_unlock(obj_ptr->mutex);
}