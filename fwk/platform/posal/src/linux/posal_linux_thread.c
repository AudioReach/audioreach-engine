/**
 * \file posal_win_thread.c
 * \brief
 *      This file contains utilities for Linux threads
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#define _GNU_SOURCE
#include "posal.h"
#include "posal_thread_profiling.h"
#include "posal_internal.h"
#include <ar_osal_thread.h>
#include <pthread.h>

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
#if defined(SIM)
#define POSAL_ENABLE_STACK_FILLING
#endif

//#define DEBUG_POSAL_THREAD

/* -------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */
// this stub is needed to seed the new thread with info not available through qurt_hread_create().
// typedef void (*posal_thread_handler_func_t) (void *);

static void posal_thread_stub(void *pThreadNode);

/* Local utility to destroy thread object */
static void thread_util_free_obj(posal_thread_t obj);

/*
 Local function for creaating thread.
 pTid           : Thread ID.
 threadname   : Thread name.
 stack_ptr         : Location of a preallocated stack (NULL causes a new allocation).
              stack_ptr must point to the lowest address in the stack.
 stack_size     : Size of the thread stack.
 nPriority      : Thread priority, where 0 is the lowest and 255 is the highest.
 pfStartRoutine : Entry function of the thread.
 arg            : This parameter is passed to the entry
                  function and can be a case to any pointer type.
 heap_id        : Thread stack will be allocated in the heap
                  specified by this parameter.
 return
 An indication of success or failure.
 */
ar_result_t posal_thread_launch(posal_thread_t     *posal_obj_ptr,
                                char               *threadname,
                                size_t              stack_size,
                                posal_thread_prio_t nPriority,
                                ar_result_t         (*pfStartRoutine)(void *),
                                void               *arg,
                                POSAL_HEAP_ID       heap_id)
{
   return posal_thread_launch2(posal_obj_ptr, threadname, stack_size, 0, nPriority, pfStartRoutine, arg, heap_id);
}

ar_result_t posal_thread_launch2(posal_thread_t     *posal_obj_ptr,
                                 char               *threadname,
                                 size_t              stack_size,
                                 size_t              root_stack_size,
                                 posal_thread_prio_t nPriority,
                                 ar_result_t         (*pfStartRoutine)(void *),
                                 void               *arg,
                                 POSAL_HEAP_ID       heap_id)
{
   return posal_thread_launch3(posal_obj_ptr, threadname, stack_size, root_stack_size, nPriority, pfStartRoutine, arg, heap_id, 0xFFFFFFFF, 0xFFFFFFFF);
}

/*
Creates and launches a island thread with nrootStackSize stack size used for guest mode.
 pTid           : Thread ID.
 threadname     : Thread name.
 stack_ptr      : Location of a preallocated stack (NULL causes a new allocation).
                  stack_ptr must point to the lowest address in the stack.
 stack_size     : Size of the thread stack.
 root_stack_size: Size of the island thread stack in guestmode.
 nPriority      : Thread priority, where 0 is the lowest and 255 is the highest.
 pfStartRoutine : Entry function of the thread.
 arg            : This parameter is passed to the entry
                  function and can be a case to any pointer type.
 heap_id        : Thread stack will be allocated in the heap
                  specified by this parameter.
 sched_policy   : depending on the platform. SCHED_FIFO, SCHED_RR, and
                    SCHED_OTHER. Posix: https://man7.org/linux/man-pages/man3/pthread_attr_setschedpolicy.3.html (
 affinity       : CPU set bit mask to set the affinity.
                   https://man7.org/linux/man-pages/man3/CPU_SET.3.html
                   https://man7.org/linux/man-pages/man3/pthread_attr_setaffinity_np.3.html

 return
 An indication of success or failure.
 */
ar_result_t posal_thread_launch3(posal_thread_t     *posal_obj_ptr,
                                 char               *threadname,
                                 size_t              stack_size,
                                 size_t              root_stack_size,
                                 posal_thread_prio_t nPriority,
                                 ar_result_t         (*pfStartRoutine)(void *),
                                 void               *arg,
                                 POSAL_HEAP_ID       heap_id,
                                 uint32_t            sched_policy,
                                 uint32_t            affinity)
{
   ar_result_t        result      = AR_EOK;
   int                unix_result = 0;
   pthread_t          thread_handle;
   pthread_attr_t     attr;
   char              *stack_ptr;
   uint32_t           affinity_temp = affinity;
   struct sched_param sch_param = {0};

#ifdef DEBUG_POSAL_THREAD
   MSG_5(MSG_SSID_QDSP6,
         DBG_HIGH_PRIO,
         "THREAD CREATE: Name = %s, Stack Size=%d, sched_policy 0x%x, prio %u, affinity 0x%x",
         threadname,
         stack_size,
         sched_policy,
         nPriority,
         affinity_temp
         );
#endif /* DBG_BUFFER_ADDRESSES */

   int max_prio = 99, min_prio = 0;
   if (0xFFFFFFFF == sched_policy)
   {
	   AR_MSG(DBG_LOW_PRIO, "Incoming sched_policy overridden ");
	   sched_policy = SCHED_FIFO;
   }
      max_prio = sched_get_priority_max(sched_policy);
      min_prio = sched_get_priority_min(sched_policy);

   if (min_prio > nPriority || max_prio < nPriority)
   {
      int temp = (nPriority < min_prio) ? min_prio: ((nPriority > max_prio) ? max_prio : nPriority);
      AR_MSG(DBG_HIGH_PRIO, "Warning: given thread priority %d is beyond range (%d, %d). using %d",
            nPriority, min_prio, max_prio, temp);
      nPriority = temp;
   }

   /* Allocate memory for the object */
   _thread_args_t *thrd_obj_ptr = (_thread_args_t *)posal_memory_malloc(sizeof(_thread_args_t), heap_id);
   if (NULL == thrd_obj_ptr)
   {
      *posal_obj_ptr = NULL;
      return AR_ENOMEMORY;
   }
   POSAL_ZEROAT(thrd_obj_ptr);

   /* Assign return object */
   *posal_obj_ptr = (posal_thread_t)thrd_obj_ptr;

   /* Init attributes */
   unix_result = pthread_attr_init(&attr);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: failed to init attributes");
      *posal_obj_ptr = NULL;
      result         = AR_EBADPARAM;
      goto err_attr;
   }

#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 8192
#endif

   /* round up stack size */
   stack_size = (stack_size + 7) & (-8);
   if (stack_size < PTHREAD_STACK_MIN)
   {
      stack_size += PTHREAD_STACK_MIN;
      AR_MSG(DBG_HIGH_PRIO, "Warning: stack_size increased %u", stack_size);
   }

   unix_result = pthread_attr_setstacksize(&attr, stack_size);
   if (unix_result)
      AR_MSG(DBG_HIGH_PRIO, "Warning: set stack size %d failed with %d", stack_size, unix_result);

   memset(&sch_param, 0, sizeof(sch_param));

   unix_result = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_attr_setinheritsched failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_set;
   }

   unix_result = pthread_attr_setschedpolicy(&attr, sched_policy);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_attr_setschedpolicy failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_set;
   }

   sch_param.sched_priority = nPriority;
   unix_result = pthread_attr_setschedparam(&attr, &sch_param);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_attr_setschedparam failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_set;
   }
   unix_result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_attr_setdetachstate failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_set;
   }

// Thread Affinity is not supported via this api on QNX and Linux Android
#if defined (POSAL_THREAD_AFFINITY)
   if (0 != affinity)
   {
      cpu_set_t cs;
      CPU_ZERO(&cs);

      for (int i = 0; i < sizeof(affinity)*8; i++)
      {
         if (affinity & 1)
         {
            CPU_SET(i, &cs);
         }
         affinity = (affinity>>1);
      }

      unix_result = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), (cpu_set_t *)&cs);
      if (unix_result)
      {
         AR_MSG(DBG_ERROR_PRIO, "Error: pthread_attr_setdetachstate failed with status 0x%x", unix_result);
         result = AR_EFAILED;
         goto err_set;
      }
   }
#endif

#ifdef DEBUG_POSAL_THREAD
   MSG_5(MSG_SSID_QDSP6,
         DBG_HIGH_PRIO,
         "THRD: Name = %s, Stack Size=%d, sched_policy 0x%x, prio %u, affinity 0x%x",
         threadname,
         stack_size,
         sched_policy,
         nPriority,
         affinity_temp
         );
#endif /* DBG_BUFFER_ADDRESSES */

   unix_result = pthread_create(&thread_handle, &attr, (void*)pfStartRoutine, arg);

   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_create failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_set;
   }

   AR_MSG(DBG_MED_PRIO, "thread created(0x%x)", thread_handle);
   thrd_obj_ptr->tid = (int64_t) thread_handle;

   thrd_obj_ptr->pfStartRoutine = pfStartRoutine;
   thrd_obj_ptr->arg            = arg;
   thrd_obj_ptr->thread_handle  = thread_handle;
   size_t _tmp;                                  // unused, required to get the stack ptr
   unix_result = pthread_attr_getstack(&attr, &thrd_obj_ptr->stack_ptr, &_tmp);

   unix_result = pthread_setname_np(thread_handle, threadname);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_setname_np failed with status 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_destroy;
   }

   unix_result = pthread_attr_destroy(&attr);
   if (unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error:  Failed to destroy attributes, unix_result = 0x%x", unix_result);
      result = AR_EFAILED;
      goto err_destroy;
   }

/* low prio debug message in case of difficult issues */
#ifdef DEBUG_POSAL_THREAD
   AR_MSG(DBG_LOW_PRIO,
          "THRD CREATE: Thread=0x%x stacktop=0x%x stackbottom=0x%x prio=%d",
          thrd_obj_ptr->tid,
          thrd_obj_ptr->stack_ptr,
          thrd_obj_ptr->stack_ptr + stack_size,
          nPriority);
#endif /* DEBUG_POSAL_THREAD */

/* Med prio debug message to help associate thread name with thread ID
 * Due to Qshrink requirement of not supporting varaible string,
 * thread name has to be printed out in Hex. */
#if defined(ARSPF_PLATFORM_QNX) || defined(ARSPF_PLATFORM_LRH)
   AR_MSG(DBG_MED_PRIO, "THRD CREATE: Thread=0x%x Name = %s", thrd_obj_ptr->tid, threadname);
#else
   AR_MSG(DBG_HIGH_PRIO,
          "THRD CREATE: Thread=0x%x Name(Hex)= %x, %x, %x, %x, %x, %x, %x, %x",
          thrd_obj_ptr->tid,
          threadname[0],
          threadname[1],
          threadname[2],
          threadname[3],
          threadname[4],
          threadname[5],
          threadname[6],
          threadname[7]);
#endif

   goto done;

err_destroy:
   pthread_join(thread_handle, NULL);

err_set:
   pthread_attr_destroy(&attr);

err_attr:
   free(thrd_obj_ptr);

done:
   return result;
}

void posal_thread_join(posal_thread_t thread_obj, ar_result_t *pStatus)
{
   *pStatus               = AR_EOK;
   int32_t         status = AR_EOK;
   pthread_t       thread;
   int             unix_result;
   _thread_args_t *thrd_obj_ptr = (_thread_args_t *)thread_obj;

   if (NULL == thrd_obj_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: thread object pointer(NULL)");
      *pStatus = AR_EBADPARAM;
      return;
   }
   thread = thrd_obj_ptr->thread_handle;

   if (0 == thread)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: thread handle is 0");
      *pStatus = AR_EBADPARAM;
      return;
   }

   AR_MSG(DBG_MED_PRIO, "thread join (0x%x)", thread);
   // ignore return value of the thread to maintain compatibility with other targets
   unix_result = pthread_join(thread, NULL);

   if (0 != unix_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: pthread_join failed with 0x%x", unix_result);
      *pStatus = AR_EFAILED;
   }

   /* Free the stack pointer and thread object pointer */
   thread_util_free_obj(thread_obj);

   return;
}

posal_thread_prio_t posal_thread_prio_get(void)
{
   ar_result_t result      = AR_EOK;
   int32_t     thread_prio = 0;
   if (AR_DID_FAIL(ar_osal_thread_self_get_priority(&thread_prio)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: ar_osal_thread_self_get_priority failed to get thread prio");
   }

   return thread_prio;
}

posal_thread_prio_t posal_thread_prio_get2(posal_thread_t tObj)
{
   _thread_args_t    *thrd_obj_ptr = (_thread_args_t *)tObj;
   int32_t            thread_prio  = 0;
   struct sched_param sch_param;
   int                policy;
   if (thrd_obj_ptr)
   {
      memset(&sch_param, 0, sizeof(sch_param));
      if (AR_DID_FAIL(pthread_getschedparam(thrd_obj_ptr->thread_handle, &policy, &sch_param)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error: pthread_getschedparam failed to get thread prio");
      }
      thread_prio = sch_param.sched_priority;
   }
   return thread_prio;
}

void posal_thread_set_prio(posal_thread_prio_t priority_no)
{
   if (AR_DID_FAIL(ar_osal_thread_self_set_priority(priority_no)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: ar_osal_thread_self_set_priority failed to set thread prio");
   }
}

void posal_thread_set_prio2(posal_thread_t tObj, posal_thread_prio_t nPrio)
{
   _thread_args_t    *thrd_obj_ptr = (_thread_args_t *)tObj;
   struct sched_param sch_param;
   int                policy;
   if (thrd_obj_ptr)
   {
      memset(&sch_param, 0, sizeof(sch_param));
      if (AR_DID_FAIL(pthread_getschedparam(thrd_obj_ptr->thread_handle, &policy, &sch_param)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error: pthread_getschedparam failed to get thread prio");
         return;
      }
      sch_param.sched_priority = nPrio;
      if (AR_DID_FAIL(pthread_setschedparam(thrd_obj_ptr->thread_handle, policy, &sch_param)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error: pthread_setschedparam failed to set new thread prio");
         return;
      }
   }
}

static void thread_util_free_obj(posal_thread_t obj)
{
   _thread_args_t *thrd_obj_ptr = (_thread_args_t *)obj;

   if (thrd_obj_ptr)
   {
      /* Free stack pointer if non NULL */
      // TODO amnydesh how are we even freeing this
      /*if (thrd_obj_ptr->stack_ptr)
      {
         posal_memory_free(thrd_obj_ptr->stack_ptr);
      }*/

#ifdef POSAL_ENABLE_THREAD_PROFILING
      /* Free thread profile object if non NULL */
      if (thrd_obj_ptr->thread_profile_obj_ptr)
      {
         /*Free the thread object similar to join */
         int dummy = 0;
         posal_thread_profile_at_join(thrd_obj_ptr->thread_profile_obj_ptr, &dummy);
      }
#endif /* POSAL_ENABLE_THREAD_PROFILING */

      /* Free thread object if non NULL */
      posal_memory_free(thrd_obj_ptr);
   }
}

int32_t posal_thread_get_tid(posal_thread_t obj)
{
   _thread_args_t *thrd_obj_ptr = (_thread_args_t *)obj;

   if (NULL == thrd_obj_ptr)
   {
      return 0;
   }

   return (int32_t)(thrd_obj_ptr->tid);
}

int32_t posal_thread_get_curr_tid(void)
{
   return (int32_t)pthread_self();
}

int64_t posal_thread_get_tid_v2(posal_thread_t obj)
{
   _thread_args_t *thrd_obj_ptr = (_thread_args_t *)obj;

   if (NULL == thrd_obj_ptr)
   {
      return 0;
   }

   return (thrd_obj_ptr->tid);
}

int64_t posal_thread_get_curr_tid_v2(void)
{
   return (int64_t)pthread_self();
}
